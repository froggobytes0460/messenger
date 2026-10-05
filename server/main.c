#include <server/cli_args.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
  struct ParsedArgs cli_args = {0};
  parse_args(&cli_args, argc, argv);

  printf("Server Initialized...\n");
  printf("Listening on port: %hu\nLength of backlog: %u\n", cli_args.portNumber,
         cli_args.backlog);

  return 0;
}
