#include <errno.h>
#include <getopt.h>  // NOLINT(misc-include-cleaner)
#include <limits.h>
#include <server/cli_args.h>
#include <server/constants.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

static int parse_long(const char* str, long min, long max, long* out) {
  char* end = NULL;
  errno = 0;
  long value = strtol(str, &end, DECIMAL_INTEGER_BASE);
  if (errno != 0 || end == str || *end != '\0' || value < min || value > max) {
    return -1;
  }
  *out = value;
  return 0;
}

int parse_args(struct ParsedArgs* args_out, int argc, char* argv[]) {
  if (!args_out) {
    return EX_USAGE;
  }

  // Sensible default values
  args_out->verbose = DEFAULT_VERBOSITY;
  args_out->portNumber = PORT_NUMBER_DEFAULT;
  args_out->backlog = BACKLOG_DEFAULT;
  args_out->logFile = NULL;
  args_out->databasePath = DEFAULT_DB_PATH;

  const char* optstring = "p:b:l:d:v";
  int opt;
  long value;

  // NOLINTBEGIN(misc-include-cleaner)
  while ((opt = getopt(argc, argv, optstring)) != -1) {
    switch (opt) {
      case 'p':
        if (parse_long(optarg, 1, USHRT_MAX, &value) != 0) {
          (void)fprintf(stderr, "Error: invalid port '%s' (expected 1-%d)\n",
                        optarg, USHRT_MAX);
          return EX_USAGE;
        }
        args_out->portNumber = (unsigned short)value;
        break;
      case 'b':
        if (parse_long(optarg, 0, INT_MAX, &value) != 0) {
          (void)fprintf(stderr, "Error: invalid backlog '%s' (expected 0-%d)\n",
                        optarg, INT_MAX);
          return EX_USAGE;
        }
        args_out->backlog = (unsigned int)value;
        break;
      case 'l':
        args_out->logFile = optarg;
        break;
      case 'd':
        args_out->databasePath = optarg;
        break;
      case 'v':
        args_out->verbose = true;
        break;
      default:
        (void)fprintf(
            stderr,
            "Usage: %s [-p port] [-b backlog] [-l logfile] [-d dbpath] [-v]\n",
            argv[0]);
        return EX_USAGE;
    }
  }
  if (optind < argc) {
    (void)fprintf(stderr, "Error: unexpected argument '%s'\n", argv[optind]);
    return EX_USAGE;
  }
  // NOLINTEND(misc-include-cleaner)
  return 0;
}
