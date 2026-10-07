#include <errno.h>
#include <server/chat.h>
#include <server/cli_args.h>
#include <server/main_socket.h>
#include <server/poll_mgr.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sysexits.h>
#include <unistd.h>

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

  chat_t chat;
  chat_init(&chat);

  poll_mgr_run(&mgr, chat_callbacks(&chat));

  close(listen_fd);
  return 0;
}
