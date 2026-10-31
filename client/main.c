#include <client/cli_args.h>
#include <stdio.h>

int main(int argc, const char* argv[]) {
  struct parsed_args args = {0};
  int rc = parse_args(&args, argc, argv);
  if (rc != 0) {
    return rc;
  }

  printf("Client Initialized...\n");
  return 0;
}
