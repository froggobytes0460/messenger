#include <client/cli_args.h>
#include <client/constants.h>
#include <compact/getopt.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>

#define DECIMAL_INTEGER_BASE 10

static const char optstring[] = "s:p:u:t:r:h";
static struct option long_options[] = {
    {"server", required_argument, NULL, 's'},
    {"port", required_argument, NULL, 'p'},
    {"username", required_argument, NULL, 'u'},
    {"timeout", required_argument, NULL, 't'},
    {"retry", required_argument, NULL, 'r'},
    {"help", no_argument, NULL, 'h'},
    {0, 0, 0, 0},
};

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

static void print_usage(const char* prog_name) {
  printf("NAME\n");
  printf("    %s - Client for a messenger server.\n\n", prog_name);
  printf("USAGE\n");
  printf("    %s -s <ip> -p <port> -u <name> [OPTIONS]\n\n", prog_name);
  printf("OPTIONS\n");
  printf("    -s, --server <ip>\n");
  printf("        Server IP address.\n");
  printf("    -p, --port <port>\n");
  printf("        Server port number.\n");
  printf("    -u, --username <name>\n");
  printf("        Username for client.\n");
  printf("    -t, --timeout <seconds>\n");
  printf("        Timeout for server connections in seconds.\n");
  printf("    -r, --retry <number>\n");
  printf("        Number of retries allowed for server connections.\n");
  printf("    -h, --help\n");
  printf("        Print this help menu and exit.\n");
}

int parse_args(struct parsed_args* out, int argc, const char* argv[]) {
  if (!out || argc < 1 || !argv) {
    return EX_USAGE;
  }

  memset(out, 0, sizeof(*out));
  long parsed_timeout = DEFAULT_TIMEOUT;
  long parsed_retry = DEFAULT_RETRY;
  long parsed_port;

  optind = 1;

  int opt;
  while ((opt = getopt_long(argc, (char* const*)argv, optstring, long_options,
                            NULL)) != -1) {
    switch (opt) {
      case 's':
        out->host_ip = optarg;
        break;
      case 'u':
        out->username = optarg;
        break;
      case 'p':
        if (parse_long(optarg, 1, USHRT_MAX, &parsed_port) != 0) {
          (void)fprintf(stderr, "Invalid port number '%s'. Must be 1-%d",
                        optarg, USHRT_MAX);
          return EX_USAGE;
        }
        out->port = (unsigned short)parsed_port;
        break;
      case 't':
        if (parse_long(optarg, 1, UINT_MAX, &parsed_timeout) != 0) {
          (void)fprintf(stderr, "Invalid timeout '%s'. Must be 1-%d", optarg,
                        UINT_MAX);
          return EX_USAGE;
        }
        out->timeout = (unsigned)parsed_timeout;
        break;
      case 'r':
        if (parse_long(optarg, 1, UINT_MAX, &parsed_retry) != 0) {
          (void)fprintf(stderr, "Invalid number of retries '%s'. Must be 1-%d",
                        optarg, UINT_MAX);
          return EX_USAGE;
        }
        out->retry = (unsigned)parsed_retry;
        break;
      case 'h':
        print_usage(argv[0]);
        exit(EXIT_SUCCESS);
      case '?':
      default:
        (void)fprintf(stderr,
                      "Usage: %s -s <ip> -p <port> -u <name> [OPTIONS]\n",
                      argv[0]);
        return EX_USAGE;
    }
  }
  if (!out->host_ip || !out->port || !out->username) {
    (void)fprintf(stderr, "Error: Missing required arguments.\n\n");
    print_usage(argv[0]);
    return EX_USAGE;
  }

  return 0;
}
