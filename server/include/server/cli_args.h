#ifndef CLI_ARGS_HEADER
#define CLI_ARGS_HEADER

#define PORT_NUMBER_DEFAULT 8000
#define BACKLOG_DEAULT 10

struct ParsedArgs {
  unsigned short portNumber;
  unsigned backlog;
};

void parse_args(struct ParsedArgs *args_out, int argc, char *argv[]);

#endif // !CLI_ARGS_HEADER
