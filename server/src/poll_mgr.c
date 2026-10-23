#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <server/constants.h>
#include <server/log.h>
#include <server/poll_mgr.h>
#include <signal.h>
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

/// Set from a signal handler by poll_mgr_request_stop().
static volatile sig_atomic_t stop_requested = 0;

#define HOST_STR_LEN 1025
#define SERV_STR_LEN 32

static int set_fd_nonblocking(int fd) {
  int fd_flags = fcntl(fd, F_GETFL, 0);
  if (fd_flags == -1) {
    log_perror("fcntl F_GETFL");
    return -1;
  }
  // NOLINTNEXTLINE(bugprone-signed-bitwise)
  fd_flags |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, fd_flags) == -1) {
    log_perror("fcntl F_SETFL");
    return -1;
  }
  return 0;
}

static ssize_t try_send(int fd, const char* buf, size_t len) {
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(fd, buf + sent, len - sent, MSG_NOSIGNAL);
    if (n == -1) {
      if (errno == EINTR) {  // NOLINT(misc-include-cleaner)
        continue;
      }
      // NOLINTNEXTLINE(misc-include-cleaner)
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        break;
      }
      return -1;
    }
    if (n == 0) {
      break;
    }
    sent += (size_t)n;
  }
  return (ssize_t)sent;
}

static int queue_bytes(outbuf_t* out, const char* buf, size_t len) {
  if (len > MAX_OUTBUF - out->len) {
    return -1;
  }
  memcpy(out->data + out->len, buf, len);
  out->len += len;
  return 0;
}

static ssize_t find_index(const poll_mgr_t* mgr, int sockfd) {
  for (size_t i = 1; i < mgr->nfds; i++) {
    if (mgr->fds[i].fd == sockfd) {
      return (ssize_t)i;
    }
  }
  return -1;
}

static void log_new_connection(struct sockaddr_storage* addr_storage,
                               socklen_t len, int client_fd) {
  const char* ip_type;
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

  int rc =
      getnameinfo((struct sockaddr*)addr_storage, len, hoststr, sizeof(hoststr),
                  portstr, sizeof(portstr), NI_NUMERICHOST | NI_NUMERICSERV);
  if (rc != 0) {
    log_error("getnameinfo failed: %s", gai_strerror(rc));
  }

  log_info("%s client %s:%s connected on socket %d", ip_type, hoststr, portstr,
           client_fd);
}

static void handle_new_connection(poll_mgr_t* mgr,
                                  const poll_callback_t* callbacks) {
  struct sockaddr_storage addr_storage;
  socklen_t addr_len = sizeof(addr_storage);

  int client_fd =
      accept(mgr->listen_fd, (struct sockaddr*)&addr_storage, &addr_len);

  if (client_fd == -1) {
    // NOLINTNEXTLINE(misc-include-cleaner)
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNABORTED) {
      return;
    }
    log_perror("accept");
    // NOLINTBEGIN(misc-include-cleaner)
    bool out_of_resources = errno == EMFILE || errno == ENFILE ||
                            errno == ENOBUFS || errno == ENOMEM;
    // NOLINTEND(misc-include-cleaner)
    if (out_of_resources) {
      // The pending connection stays queued, so the listening socket would
      // stay readable and spin the loop. Stop polling it until the next wake.
      mgr->fds[0].events = 0;
    }
    return;
  }

  if (set_fd_nonblocking(client_fd) == -1) {
    close(client_fd);
    return;
  }

  if (mgr->nfds < MAX_CONNECTIONS + 1) {
    mgr->fds[mgr->nfds].fd = client_fd;
    mgr->fds[mgr->nfds].events = POLLIN;
    mgr->fds[mgr->nfds].revents = 0;
    mgr->out[mgr->nfds].len = 0;
    mgr->closing[mgr->nfds] = false;
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

static void remove_client(poll_mgr_t* mgr, size_t index,
                          const poll_callback_t* callbacks) {
  int client_fd = mgr->fds[index].fd;
  close(client_fd);

  size_t nfds = --mgr->nfds;
  if (index < nfds) {
    mgr->fds[index] = mgr->fds[nfds];
    mgr->closing[index] = mgr->closing[nfds];
    memcpy(mgr->out[index].data, mgr->out[nfds].data, mgr->out[nfds].len);
    mgr->out[index].len = mgr->out[nfds].len;
  }

  if (callbacks->on_disconnection) {
    callbacks->on_disconnection(callbacks->ctx, mgr, client_fd);
  }
}

/// Reads from a readable client. Flags it for closing on EOF or error.
static void handle_client_readable(poll_mgr_t* mgr, size_t index, char* buf,
                                   size_t buf_size,
                                   const poll_callback_t* callbacks) {
  int client_fd = mgr->fds[index].fd;
  ssize_t nbytes = recv(client_fd, buf, buf_size - 1, 0);

  if (nbytes == -1) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return;
    }
    log_perror("recv");
  }
  if (nbytes <= 0) {
    mgr->closing[index] = true;
    return;
  }

  buf[nbytes] = '\0';
  if (callbacks->on_message) {
    callbacks->on_message(callbacks->ctx, mgr, client_fd, buf, (size_t)nbytes);
  }
}

/// Writes to a writable client. Returns true if it was flagged for closing.
static bool handle_client_writable(poll_mgr_t* mgr, size_t index) {
  outbuf_t* out = &mgr->out[index];
  ssize_t n = try_send(mgr->fds[index].fd, out->data, out->len);

  if (n == -1) {
    log_perror("send");
    mgr->closing[index] = true;
    return true;
  }

  // Shift unsent bytes to the front.
  size_t remaining = out->len - (size_t)n;
  memmove(out->data, out->data + n, remaining);
  out->len = remaining;

  if (out->len == 0) {
    // NOLINTNEXTLINE(bugprone-signed-bitwise)
    mgr->fds[index].events &= (short)~POLLOUT;
  }
  return false;
}

static void dispatch_events(poll_mgr_t* mgr, char* buf, size_t buf_size,
                            const poll_callback_t* callbacks) {
  size_t i = 0;
  while (i < mgr->nfds) {
    unsigned short revents = mgr->fds[i].revents;

    if (mgr->fds[i].fd == mgr->listen_fd) {
      if (revents & POLLIN) {
        handle_new_connection(mgr, callbacks);
      }
      i++;
      continue;
    }
    if (mgr->closing[i]) {
      i++;
      continue;
    }
    if (revents & POLLNVAL) {
      // The fd is not open (closed behind our back): poll() would report it
      // on every call, so drop the client.
      log_error("poll reported an invalid fd %d", mgr->fds[i].fd);
      mgr->closing[i] = true;
      i++;
      continue;
    }

    // Handlers only flag clients for closing; nothing is swapped until
    // sweep_closed(), so always advance. Skip reading if the write failed.
    if ((revents & POLLOUT) && handle_client_writable(mgr, i)) {
      i++;
      continue;
    }
    if (revents & (unsigned short)(POLLIN | POLLHUP | POLLERR)) {
      handle_client_readable(mgr, i, buf, buf_size, callbacks);
    }
    i++;
  }
}

/// One last non-blocking attempt to deliver replies queued before the close
/// (for example "ERR too many failed attempts"). Whatever does not fit is lost.
static void flush_before_close(poll_mgr_t* mgr, size_t index) {
  outbuf_t* out = &mgr->out[index];
  if (out->len > 0) {
    (void)try_send(mgr->fds[index].fd, out->data, out->len);
  }
}

static void sweep_closed(poll_mgr_t* mgr, const poll_callback_t* callbacks) {
  for (size_t i = mgr->nfds - 1; i >= 1; i--) {
    if (mgr->closing[i]) {
      flush_before_close(mgr, i);
      remove_client(mgr, i, callbacks);
    }
  }
}

int poll_mgr_init(poll_mgr_t* mgr, int listen_fd) {
  memset(mgr, 0, sizeof(*mgr));
  int rc = set_fd_nonblocking(listen_fd);
  if (rc != 0) {
    return rc;
  }
  mgr->listen_fd = listen_fd;
  mgr->fds[0].fd = listen_fd;  // First socket fd should be the listening one.
  mgr->fds[0].events = POLLIN;
  mgr->nfds = 1;
  return 0;
}

void poll_mgr_request_stop(void) { stop_requested = 1; }

int poll_mgr_run(poll_mgr_t* mgr, poll_callback_t callbacks) {
  char buf[BUFFER_SIZE];

  while (!stop_requested) {
    int rc = poll(mgr->fds, mgr->nfds, POLL_TICK_MS);
    if (rc == -1) {
      if (errno == EINTR) {
        continue;
      }
      log_perror("poll");
      return EX_OSERR;
    }
    mgr->fds[0].events = POLLIN;  // Resume accepting if accept() paused it.

    if (rc > 0) {
      dispatch_events(mgr, buf, sizeof(buf), &callbacks);
    }

    if (callbacks.on_tick) {
      callbacks.on_tick(callbacks.ctx, mgr);
    }

    sweep_closed(mgr, &callbacks);
  }
  return 0;
}

int poll_mgr_send(poll_mgr_t* mgr, int fd, const char* buf, size_t len) {
  ssize_t idx = find_index(mgr, fd);
  if (idx == -1) {
    return -1;
  }

  outbuf_t* out = &mgr->out[idx];
  if (out->len == 0) {
    ssize_t sent = try_send(fd, buf, len);
    if (sent == -1) {
      return -1;
    }
    if ((size_t)sent == len) {
      return 0;
    }
    buf += sent;
    len -= (size_t)sent;
  }
  if (queue_bytes(out, buf, len) == -1) {
    return POLL_MGR_SEND_FULL;
  }
  // NOLINTNEXTLINE(bugprone-signed-bitwise)
  mgr->fds[idx].events |= POLLOUT;
  return 0;
}

bool poll_mgr_is_closing(const poll_mgr_t* mgr, int client_fd) {
  ssize_t idx = find_index(mgr, client_fd);
  if (idx == -1) {
    return true;
  }
  return mgr->closing[idx];
}

void poll_mgr_close(poll_mgr_t* mgr, int client_fd) {
  ssize_t idx = find_index(mgr, client_fd);
  if (idx == -1) {
    return;
  }
  mgr->closing[idx] = true;
}
