#include <netdb.h>
#include <server/constants.h>
#include <server/poll_mgr.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sysexits.h>
#include <unistd.h>

#define HOST_STR_LEN 1025
#define SERV_STR_LEN 32

void poll_mgr_init(poll_mgr_t *mgr, int listen_fd) {
  memset(mgr, 0, sizeof(*mgr));
  mgr->listen_fd = listen_fd;
  mgr->fds[0].fd = listen_fd; // First socket fd should be the listening one.
  mgr->fds[0].events = POLLIN;
  mgr->nfds = 1;
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
                                  void (*on_connection)(poll_mgr_t *, int)) {
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
    if (on_connection) {
      on_connection(mgr, client_fd);
    }
  } else {
    (void)fprintf(
        stderr,
        "Server Error: Maximum Connections Reached, Rejecting Client.\n");
    close(client_fd);
  }
}

static void remove_client(poll_mgr_t *mgr, size_t index,
                          void (*on_disconnection)(poll_mgr_t *, int)) {
  int client_fd = mgr->fds[index].fd;
  close(client_fd);

  mgr->nfds--;
  if (index < mgr->nfds) {
    mgr->fds[index] = mgr->fds[mgr->nfds];
  }

  if (on_disconnection) {
    on_disconnection(mgr, client_fd);
  }
}

void poll_mgr_run(poll_mgr_t *mgr, poll_callback_t callbacks) {
  char buf[BUFFER_SIZE];

  while (1) {
    int poll_count = poll(mgr->fds, mgr->nfds, -1);
    if (poll_count == -1) {
      perror("poll");
      exit(EX_OSERR);
    }

    size_t i = 0;
    while (i < mgr->nfds) {
      if (!(mgr->fds[i].revents & (POLLIN | POLLHUP | POLLERR))) {
        i++;
        continue;
      }

      if (mgr->fds[i].fd == mgr->listen_fd) {
        handle_new_connection(mgr, callbacks.on_connection);
        i++;
        continue;
      }

      int client_fd = mgr->fds[i].fd;
      ssize_t nbytes = recv(client_fd, buf, sizeof(buf) - 1, 0);

      if (nbytes <= 0) {
        // The last entry was swapped into slot i; revisit it without advancing.
        remove_client(mgr, i, callbacks.on_disconnection);
        continue;
      }

      buf[nbytes] = '\0';
      if (callbacks.on_message) {
        callbacks.on_message(mgr, client_fd, buf, (size_t)nbytes);
      }
      i++;
    }
  }
}
