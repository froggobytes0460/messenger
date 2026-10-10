#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <server/constants.h>
#include <server/log.h>
#include <server/poll_mgr.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sysexits.h>
#include <unistd.h>

#define LOG_TAG "poll_mgr"

#define HOST_STR_LEN 1025
#define SERV_STR_LEN 32

static int set_fd_nonblocking(int fd) {
  int fd_flags = fcntl(fd, F_GETFL, 0);
  if (fd_flags == -1) {
    log_perror("fcntl F_GETFL");
    return -1;
  }
  fd_flags |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, fd_flags) == -1) {
    log_perror("fcntl F_SETFL");
    return -1;
  }
  return 0;
}

static void send_all(int fd, const char *buf, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
    if (n == -1) {
      if (errno == EINTR) { // NOLINT(misc-include-cleaner)
        continue;
      }
      return;
    }
    sent += (size_t)n;
  }
}

static void log_new_connection(struct sockaddr_storage *addr_storage,
                               socklen_t len, int client_fd) {
  const char *ip_type;
  switch (addr_storage->ss_family) {
  case AF_INET:
    ip_type = "IPv4";
    break;
  case AF_INET6:
    ip_type = "IPv6";
    break;
  default:
    ip_type = "Unknown";
    break;
  }

  char hoststr[HOST_STR_LEN] = "?";
  char portstr[SERV_STR_LEN] = "?";

  int rc = getnameinfo((struct sockaddr *)addr_storage, len, hoststr,
                       sizeof(hoststr), portstr, sizeof(portstr),
                       NI_NUMERICHOST | NI_NUMERICSERV);
  if (rc != 0) {
    log_error("getnameinfo failed: %s", gai_strerror(rc));
  }

  log_info("%s client %s:%s connected on socket %d", ip_type, hoststr, portstr,
           client_fd);
}

static void handle_new_connection(poll_mgr_t *mgr,
                                  const poll_callback_t *callbacks) {
  struct sockaddr_storage addr_storage;
  socklen_t addr_len = sizeof(addr_storage);

  int client_fd =
      accept(mgr->listen_fd, (struct sockaddr *)&addr_storage, &addr_len);

  if (client_fd == -1) {
    // NOLINTNEXTLINE(misc-include-cleaner)
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNABORTED) {
      return;
    }
    log_perror("accept");
    return;
  }

  if (set_fd_nonblocking(client_fd) == -1) {
    close(client_fd);
    return;
  }

  if (mgr->nfds < MAX_CONNECTIONS + 1) {
    mgr->fds[mgr->nfds].fd = client_fd;
    mgr->fds[mgr->nfds].events = POLLIN;
    mgr->out[mgr->nfds].len = 0;
    mgr->nfds++;

    log_new_connection(&addr_storage, addr_len, client_fd);
    if (callbacks->on_connection) {
      callbacks->on_connection(callbacks->ctx, mgr, client_fd);
    }
  } else {
    log_warn("maximum connections reached, rejecting client");
    close(client_fd);
  }
}

static void remove_client(poll_mgr_t *mgr, size_t index,
                          const poll_callback_t *callbacks) {
  int client_fd = mgr->fds[index].fd;
  close(client_fd);

  mgr->nfds--;
  if (index < mgr->nfds) {
    mgr->fds[index] = mgr->fds[mgr->nfds];
    memcpy(mgr->out[index].data, mgr->out[mgr->nfds].data,
           mgr->out[mgr->nfds].len);
    mgr->out[index].len = mgr->out[mgr->nfds].len;
  }

  if (callbacks->on_disconnection) {
    callbacks->on_disconnection(callbacks->ctx, mgr, client_fd);
  }
}

int poll_mgr_init(poll_mgr_t *mgr, int listen_fd) {
  memset(mgr, 0, sizeof(*mgr));
  int rc = set_fd_nonblocking(listen_fd);
  if (rc != 0) {
    return rc;
  }
  mgr->listen_fd = listen_fd;
  mgr->fds[0].fd = listen_fd; // First socket fd should be the listening one.
  mgr->fds[0].events = POLLIN;
  mgr->nfds = 1;
  return 0;
}

/// Reads from a readable client. Returns true if the client was removed.
static bool handle_client_readable(poll_mgr_t *mgr, size_t index, char *buf,
                                   size_t buf_size,
                                   const poll_callback_t *callbacks) {
  int client_fd = mgr->fds[index].fd;
  ssize_t nbytes = recv(client_fd, buf, buf_size - 1, 0);

  if (nbytes == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return false;
    }
    log_perror("recv");
  }
  if (nbytes <= 0) {
    remove_client(mgr, index, callbacks);
    return true;
  }

  buf[nbytes] = '\0';
  if (callbacks->on_message) {
    callbacks->on_message(callbacks->ctx, mgr, client_fd, buf, (size_t)nbytes);
  }
  return false;
}

static void dispatch_events(poll_mgr_t *mgr, char *buf, size_t buf_size,
                            const poll_callback_t *callbacks) {
  size_t i = 0;
  while (i < mgr->nfds) {
    if (!(mgr->fds[i].revents & (POLLIN | POLLHUP | POLLERR))) {
      i++;
      continue;
    }

    if (mgr->fds[i].fd == mgr->listen_fd) {
      handle_new_connection(mgr, callbacks);
      i++;
      continue;
    }

    // If the client was removed, the last entry was swapped into slot i;
    // revisit it without advancing.
    if (!handle_client_readable(mgr, i, buf, buf_size, callbacks)) {
      i++;
    }
  }
}

int poll_mgr_run(poll_mgr_t *mgr, poll_callback_t callbacks) {
  char buf[BUFFER_SIZE];

  while (1) {
    int rc = poll(mgr->fds, mgr->nfds, POLL_TICK_MS);
    if (rc == -1) {
      if (errno == EINTR) {
        continue;
      }
      log_perror("poll");
      return EX_OSERR;
    }

    if (rc > 0) {
      dispatch_events(mgr, buf, sizeof(buf), &callbacks);
    }

    if (callbacks.on_tick) {
      callbacks.on_tick(callbacks.ctx, mgr);
    }
  }
}

void poll_mgr_broadcast(poll_mgr_t *mgr, int except_fd, const char *buf,
                        size_t buf_len) {
  for (size_t i = 0; i < mgr->nfds; i++) {
    int recipient_fd = mgr->fds[i].fd;

    if (recipient_fd != except_fd && recipient_fd != mgr->listen_fd) {
      send_all(recipient_fd, buf, buf_len);
    }
  }
}
