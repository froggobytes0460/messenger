#include <errno.h>
#include <netdb.h>
#include <server/constants.h>
#include <server/log.h>
#include <server/main_socket.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sysexits.h>
#include <unistd.h>

#define LOG_TAG "main_socket"

int get_main_socket(const char* host, unsigned short port, int* fd_out) {
  int rc = 0;
  struct addrinfo hints;
  struct addrinfo* res;

  // Convert port to string.
  char port_str[MAX_PORT_STR_LEN];
  (void)snprintf(port_str, sizeof(port_str), "%hu", port);

  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  rc = getaddrinfo(host, port_str, &hints, &res);
  if (rc != 0) {
    log_error("getaddrinfo: %s", gai_strerror(rc));
    return EX_NOHOST;
  }

  struct addrinfo* p;
  int sockfd = -1;

  for (p = res; p != NULL; p = p->ai_next) {
    sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sockfd == -1) {
      continue;
    }

    // Allow reconnection shortly after disconnection.
    // NOLINTNEXTLINE(misc-include-cleaner)
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int))) {
      log_perror("setsockopt");
      rc = EX_OSERR;
      goto cleanup;
    }

    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      int bind_errno = errno;
      log_perror("bind");
      // NOLINTNEXTLINE(misc-include-cleaner)
      if (bind_errno != EADDRINUSE) {
        close(sockfd);
        continue;
      }
      rc = EX_UNAVAILABLE;
      goto cleanup;
    }

    break;
  }

  if (!p) {
    log_error("failed to bind any address");
    rc = EX_UNAVAILABLE;
    goto cleanup;
  }

  *fd_out = sockfd;
  sockfd = -1;  // Ensure socket is not closed.

cleanup:
  if (sockfd != -1) {
    close(sockfd);
  }
  freeaddrinfo(res);
  return rc;
}
