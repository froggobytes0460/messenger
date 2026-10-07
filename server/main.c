#include <errno.h>
#include <server/cli_args.h>
#include <server/constants.h>
#include <server/main_socket.h>
#include <server/poll_mgr.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sysexits.h>
#include <unistd.h>

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

static void broadcast_message(poll_mgr_t *mgr, int sender_fd, const char *buf,
                              size_t buf_len) {
  for (size_t i = 0; i < mgr->nfds; i++) {
    int recipient_fd = mgr->fds[i].fd;

    if (recipient_fd != sender_fd && recipient_fd != mgr->listen_fd) {
      send_all(recipient_fd, buf, buf_len);
    }
  }
}

static void handle_message(void *ctx, poll_mgr_t *mgr, int sender_fd,
                           const char *buf, size_t len) {
  (void)ctx;
  printf("[Client %d]: %.*s", sender_fd, (int)len, buf);
  broadcast_message(mgr, sender_fd, buf, len);
}

static void on_disconnect(void *ctx, poll_mgr_t *mgr, int client_fd) {
  (void)ctx;
  printf("[SERVER]: Client with socket descriptor %d left.\n", client_fd);
  char msg[BROADCAST_MESSAGE_LENGTH];
  int l_msg = snprintf(msg, sizeof(msg), "User left the chat...\n");
  if (l_msg > 0) {
    broadcast_message(mgr, client_fd, msg, (size_t)l_msg);
  }
}

static void on_connect(void *ctx, poll_mgr_t *mgr, int client_fd) {
  (void)ctx;
  printf("[SERVER]: Client with socket descriptor %d entered.\n", client_fd);
  char msg[BROADCAST_MESSAGE_LENGTH];
  int l_msg = snprintf(msg, sizeof(msg), "User joined the chat...\n");
  if (l_msg > 0) {
    broadcast_message(mgr, client_fd, msg, (size_t)l_msg);
  }
}

int main(int argc, char *argv[]) {
  struct ParsedArgs cli_args = {0};
  parse_args(&cli_args, argc, argv);

  printf("Server Initializing...\n");
  printf("Listening on port: %hu\nLength of backlog: %u\n", cli_args.portNumber,
         cli_args.backlog);

  int listen_fd = get_main_socket(cli_args.portNumber);

  if (listen(listen_fd, (int)cli_args.backlog) != 0) {
    (void)fprintf(stderr, "Error: %s\n", strerror(errno));
    return EX_OSERR;
  }

  poll_mgr_t mgr;
  poll_mgr_init(&mgr, listen_fd);

  poll_callback_t callbacks = {
      .on_connection = on_connect,
      .on_disconnection = on_disconnect,
      .on_message = handle_message,
      .ctx = NULL,
  };

  poll_mgr_run(&mgr, callbacks);

  close(listen_fd);
  return 0;
}
