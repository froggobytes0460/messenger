#ifndef CLI_ARGS_H
#define CLI_ARGS_H

struct ParsedArgs {
  unsigned short portNumber;
  unsigned backlog;
};

void parse_args(struct ParsedArgs *args_out, int argc, char *argv[]);

#endif // !CLI_ARGS_H
