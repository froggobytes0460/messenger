#include <server/cli_args.h>
#include <server/main_socket.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
  struct ParsedArgs cli_args = {0};
  parse_args(&cli_args, argc, argv);

  printf("Server Initializing...\n");
  printf("Listening on port: %hu\nLength of backlog: %u\n", cli_args.portNumber,
         cli_args.backlog);

  int listen_fd = get_main_socket(cli_args.portNumber);
  close(listen_fd);

  return 0;
}
