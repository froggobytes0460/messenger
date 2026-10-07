#include <netdb.h>
#include <server/constants.h>
#include <server/main_socket.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sysexits.h>
#include <unistd.h>

int get_main_socket(unsigned short port, int *fd_out) {
  struct addrinfo hints;
  struct addrinfo *res;

  // Convert port to string.
  char port_str[MAX_PORT_STR_LEN];
  (void)snprintf(port_str, sizeof(port_str), "%hu", port);

  memset(&hints, 0, sizeof(hints));
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  int rc = getaddrinfo(NULL, port_str, &hints, &res);
  if (rc != 0) {
    (void)fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(rc));
    return EX_NOHOST;
  }

  struct addrinfo *p;
  int sockfd;

  for (p = res; p != NULL; p = p->ai_next) {
    sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sockfd == -1) {
      continue;
    }

    // Allow reconnection shortly after disconnection.
    // NOLINTNEXTLINE(misc-include-cleaner)
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int))) {
      perror("setsockopt");
      close(sockfd);
      freeaddrinfo(res);
      return EX_OSERR;
    }

    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      close(sockfd);
      continue;
    }

    break;
  }

  if (p == NULL) {
    (void)fprintf(stderr, "Server Error: Failed to bind any address.\n");
    freeaddrinfo(res);
    return EX_UNAVAILABLE;
  }

  freeaddrinfo(res);
  *fd_out = sockfd;

  return 0;
}
