#include <server/cli_args.h>
#include <stdio.h>

int main(int argc, char *argv[]) {
  struct ParsedArgs cli_args = {0};
  parse_args(&cli_args, argc, argv);

  printf("Server Initialized...\n");

  return 0;
}
