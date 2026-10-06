#include <netdb.h>
#include <server/constants.h>
#include <server/poll_mgr.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sysexits.h>
#include <unistd.h>

#define NI_MAXHOST 1025
#define NI_MAXSERVE 32

void poll_mgr_init(poll_mgr_t *mgr, int listen_fd) {
  memset(mgr, 0, sizeof(*mgr));
  mgr->fds[0].fd = listen_fd; // First socket fd should be the listening one.
  mgr->fds[0].events = POLLIN;
  mgr->listen_fd = listen_fd;
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
    ip_type = "Unkown";
    break;
  }

  char hoststr[NI_MAXHOST];
  char portstr[NI_MAXSERVE];

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

  if (mgr->nfds < MAX_CONNECTIONS) {
    mgr->fds[mgr->nfds].fd = client_fd;
    mgr->fds[mgr->nfds].events = POLLIN;
    mgr->nfds++;

    if (on_connection) {
      on_connection(mgr, client_fd);
    }
  } else {
    (void)fprintf(
        stderr,
        "Server Error: Maximum Connections Reached, Rejecting Client.\n");
    close(client_fd);
  }

  log_new_connection(&addr_storage, addr_len, client_fd);
}

static void remove_client(poll_mgr_t *mgr, int index,
                          void (*on_disconnection)(poll_mgr_t *, int)) {
  int client_id = mgr->fds[index].fd;
  close(client_id);

  int last_index = (int)--mgr->nfds;
  if (*on_disconnection) {
    on_disconnection(mgr, client_id);
  }

  if (index < last_index) {
    mgr->fds[index] = mgr->fds[last_index];
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

    for (size_t i = 0; i < mgr->nfds; i++) {
      if (!(mgr->fds[i].revents & POLLIN)) {
        continue;
      }

      if (mgr->fds[i].fd == mgr->listen_fd) {
        handle_new_connection(mgr, callbacks.on_connection);
      } else {
        int client_fd = mgr->fds[i].fd;
        size_t nbytes = recv(client_fd, buf, sizeof(buf) - 1, 0);

        if (nbytes <= 0) {
          remove_client(mgr, (int)i, callbacks.on_disconnection);
          i--;
        } else {
          buf[nbytes] = '\0';
          if (callbacks.on_message) {
            callbacks.on_message(mgr, client_fd, buf, nbytes);
          }
        }
      }
    }
  }
}
