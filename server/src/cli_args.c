#include <errno.h>
#include <getopt.h> // NOLINT(misc-include-cleaner)
#include <limits.h>
#include <server/cli_args.h>
#include <server/constants.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int parse_long(const char *str, long min, long max, long *out) {
  char *end = NULL;
  errno = 0;
  long value = strtol(str, &end, DECIMAL_INTEGER_BASE);
  if (errno != 0 || end == str || *end != '\0' || value < min || value > max) {
    return -1;
  }
  *out = value;
  return 0;
}

int parse_args(struct ParsedArgs *args_out, int argc, char *argv[]) {
  if (!args_out) {
    return EXIT_FAILURE;
  }

  // Sensible default values
  args_out->portNumber = PORT_NUMBER_DEFAULT;
  args_out->backlog = BACKLOG_DEFAULT;

  // "p:" means -p expects an argument. "b:" means -b expects an argument.
  const char *optstring = "p:b:";
  int opt;
  long value;

  // NOLINTBEGIN(misc-include-cleaner)
  while ((opt = getopt(argc, argv, optstring)) != -1) {
    switch (opt) {
    case 'p':
      if (parse_long(optarg, 1, USHRT_MAX, &value) != 0) {
        (void)fprintf(stderr, "Error: invalid port '%s' (expected 1-%d)\n",
                      optarg, USHRT_MAX);
        return EXIT_FAILURE;
      }
      args_out->portNumber = (unsigned short)value;
      break;
    case 'b':
      if (parse_long(optarg, 0, INT_MAX, &value) != 0) {
        (void)fprintf(stderr, "Error: invalid backlog '%s' (expected 0-%d)\n",
                      optarg, INT_MAX);
        return EXIT_FAILURE;
      }
      args_out->backlog = (unsigned int)value;
      break;
    default:
      (void)fprintf(stderr, "Usage: %s [-p port] [-b backlog]\n", argv[0]);
      return EXIT_FAILURE;
    }
  }
  // NOLINTEND(misc-include-cleaner)
  return 0;
}
