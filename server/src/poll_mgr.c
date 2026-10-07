#include <errno.h>
#include <netdb.h>
#include <server/constants.h>
#include <server/poll_mgr.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sysexits.h>
#include <unistd.h>

#define HOST_STR_LEN 1025
#define SERV_STR_LEN 32

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
    (void)fprintf(stderr, "getnameinfo failed: %s\n", gai_strerror(rc));
  }

  printf(
      "[Server]: Client of %s with IP address %s:%s connected to socket %d\n",
      ip_type, hoststr, portstr, client_fd);
}

static void handle_new_connection(poll_mgr_t *mgr,
                                  const poll_callback_t *callbacks) {
  struct sockaddr_storage addr_storage;
  socklen_t addr_len = sizeof(addr_storage);

  int client_fd =
      accept(mgr->listen_fd, (struct sockaddr *)&addr_storage, &addr_len);

  if (client_fd == -1) {
    perror("accept");
    return;
  }

  if (mgr->nfds < MAX_CONNECTIONS + 1) {
    mgr->fds[mgr->nfds].fd = client_fd;
    mgr->fds[mgr->nfds].events = POLLIN;
    mgr->nfds++;

    log_new_connection(&addr_storage, addr_len, client_fd);
    if (callbacks->on_connection) {
      callbacks->on_connection(callbacks->ctx, mgr, client_fd);
    }
  } else {
    (void)fprintf(
        stderr,
        "Server Error: Maximum Connections Reached, Rejecting Client.\n");
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
  }

  if (callbacks->on_disconnection) {
    callbacks->on_disconnection(callbacks->ctx, mgr, client_fd);
  }
}

void poll_mgr_init(poll_mgr_t *mgr, int listen_fd) {
  memset(mgr, 0, sizeof(*mgr));
  mgr->listen_fd = listen_fd;
  mgr->fds[0].fd = listen_fd; // First socket fd should be the listening one.
  mgr->fds[0].events = POLLIN;
  mgr->nfds = 1;
}

int poll_mgr_run(poll_mgr_t *mgr, poll_callback_t callbacks) {
  char buf[BUFFER_SIZE];

  while (1) {
    if (poll(mgr->fds, mgr->nfds, -1) == -1) {
      perror("poll");
      return EX_OSERR;
    }

    size_t i = 0;
    while (i < mgr->nfds) {
      if (!(mgr->fds[i].revents & (POLLIN | POLLHUP | POLLERR))) {
        i++;
        continue;
      }

      if (mgr->fds[i].fd == mgr->listen_fd) {
        handle_new_connection(mgr, &callbacks);
        i++;
        continue;
      }

      int client_fd = mgr->fds[i].fd;
      ssize_t nbytes = recv(client_fd, buf, sizeof(buf) - 1, 0);

      if (nbytes <= 0) {
        // The last entry was swapped into slot i; revisit it without advancing.
        remove_client(mgr, i, &callbacks);
        continue;
      }

      buf[nbytes] = '\0';
      if (callbacks.on_message) {
        callbacks.on_message(callbacks.ctx, mgr, client_fd, buf,
                             (size_t)nbytes);
      }
      i++;
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
