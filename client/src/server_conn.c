#include <client/cli_args.h>
#include <client/log.h>
#include <client/server_conn.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#define LOG_TAG "server_conn"

#define SEC_TO_MS_CONV_RATIO 1e3

static int wait_and_verify_connection(int sockfd, unsigned timeout_sec) {
  struct pollfd pfd;
  pfd.fd = sockfd;
  pfd.events = POLLOUT;

  int timeout_ms = (int)(timeout_sec * SEC_TO_MS_CONV_RATIO);
  int poll_rc = poll(&pfd, 1, timeout_ms);

  if (poll_rc < 0) {
    LOG_PERROR("poll failed during connection sequence");
    return -1;
  }
  if (poll_rc == 0) {
    LOG_ERROR("connection attempt timed out after %u seconds.", timeout_sec);
    return -1;
  }

  if (!((unsigned short)pfd.revents & POLLOUT)) {
    return -1;
  }

  int sock_err = 0;
  // NOLINTNEXTLINE(misc-include-cleaner)
  if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &sock_err,
                 &(socklen_t){sizeof(sock_err)}) < 0) {
    LOG_PERROR("getsockopt failed");
    return -1;
  }
  if (sock_err != 0) {
    LOG_ERROR("connection rejected: %s", strerror(sock_err));
    return -1;
  }

  return 0;
}

static int attempt_connection(const char* host_ip, unsigned short port,
                              unsigned timeout_sec) {
  struct addrinfo hints;
  struct addrinfo* res = NULL;

  char port_str[NI_MAXSERV];
  (void)snprintf(port_str, sizeof(port_str), "%hu", port);

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_NUMERICHOST;

  int s = getaddrinfo(host_ip, port_str, &hints, &res);
  if (s != 0) {
    LOG_ERROR("getaddrinfo failed for '%s': '%s'", host_ip, gai_strerror(s));
    return -1;
  }

  int sockfd = -1;
  struct addrinfo* p = NULL;

  for (p = res; p != NULL; p = p->ai_next) {
    sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
    if (sockfd < 0) {
      continue;
    }

    unsigned flags = fcntl(sockfd, F_GETFL, 0);
    if ((int)flags < 0 || fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) < 0) {
      LOG_PERROR("failed to set non-blocking mode");
      close(sockfd);
      sockfd = -1;
      continue;
    }

    int conn_res = connect(sockfd, p->ai_addr, p->ai_addrlen);
    if (conn_res < 0) {
      //  NOLINTNEXTLINE(misc-include-cleaner)
      if (errno != EINPROGRESS) {
        close(sockfd);
        sockfd = -1;
        continue;
      }

      if (wait_and_verify_connection(sockfd, timeout_sec) != 0) {
        close(sockfd);
        sockfd = -1;
        break;
      }
    }

    if (fcntl(sockfd, F_SETFL, flags) < 0) {
      LOG_PERROR("failed to restore blocking mode to socket file descriptor");
      close(sockfd);
      sockfd = -1;
      break;
    }

    break;
  }

  freeaddrinfo(res);
  return sockfd;
}

int connect_to_server(const struct parsed_args* args) {
  int connected_fd = -1;

  for (unsigned attempts = 0; attempts <= args->retry; attempts++) {
    if (attempts > 0) {
      LOG_DEBUG("retrying connection... (attempt %u of %u)", attempts,
                args->retry);
      sleep(1);
    }
    connected_fd = attempt_connection(args->host_ip, args->port, args->timeout);
    if (connected_fd >= 0) {
      LOG_INFO("connected to server successfully");
      return connected_fd;
    }
  }

  LOG_ERROR("all retry limits exhausted");
  return -1;
}
