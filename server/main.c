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

static void handle_message(void *ctx, poll_mgr_t *mgr, int sender_fd,
                           const char *buf, size_t len) {
  (void)ctx;
  printf("[Client %d]: %.*s", sender_fd, (int)len, buf);
  poll_mgr_broadcast(mgr, sender_fd, buf, len);
}

static void on_disconnect(void *ctx, poll_mgr_t *mgr, int client_fd) {
  (void)ctx;
  printf("[SERVER]: Client with socket descriptor %d left.\n", client_fd);
  char msg[BROADCAST_MESSAGE_LENGTH];
  int l_msg = snprintf(msg, sizeof(msg), "User left the chat...\n");
  if (l_msg > 0) {
    poll_mgr_broadcast(mgr, client_fd, msg, (size_t)l_msg);
  }
}

static void on_connect(void *ctx, poll_mgr_t *mgr, int client_fd) {
  (void)ctx;
  printf("[SERVER]: Client with socket descriptor %d entered.\n", client_fd);
  char msg[BROADCAST_MESSAGE_LENGTH];
  int l_msg = snprintf(msg, sizeof(msg), "User joined the chat...\n");
  if (l_msg > 0) {
    poll_mgr_broadcast(mgr, client_fd, msg, (size_t)l_msg);
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
