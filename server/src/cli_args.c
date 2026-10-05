#include <getopt.h> // NOLINT(misc-include-cleaner)
#include <limits.h>
#include <server/cli_args.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define DECIMAL_INTEGER_BASE 10

void parse_args(struct ParsedArgs *args_out, int argc, char *argv[]) {
  if (!args_out) {
    return;
  }

  // Sensible default values
  args_out->portNumber = PORT_NUMBER_DEFAULT;
  args_out->backlog = BACKLOG_DEAULT;

  // "p:" means -p expects an argument. "b:" means -b expects an argument.
  const char *optstring = "p:b:";
  int opt;
  long p;
  long b;

  // NOLINTBEGIN(misc-include-cleaner)
  while ((opt = getopt(argc, argv, optstring)) != -1) {
    switch (opt) {
    case 'p':
      p = strtol(optarg, NULL, DECIMAL_INTEGER_BASE);
      if (p < 0 || p > USHRT_MAX) {
        (void)fprintf(stderr, "Error: Port number %ld out of bounds\n", p);
        exit(EXIT_FAILURE);
      }
      args_out->portNumber = (unsigned short)p;
      break;
    case 'b':
      b = strtol(optarg, NULL, DECIMAL_INTEGER_BASE);
      if (b < 0 || b > INT_MAX) {
        (void)fprintf(stderr, "Error: backlog number %ld out of bounds\n", b);
        exit(EXIT_FAILURE);
      }
      args_out->backlog = (unsigned int)b;
      break;
    default:
      (void)fprintf(stderr, "Usage: %s [-p port] [-b backlog]\n", argv[0]);
      exit(EXIT_FAILURE);
    }
  }
  // NOLINTEND(misc-include-cleaner)
}
