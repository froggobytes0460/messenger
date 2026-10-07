#ifndef CLI_ARGS_H
#define CLI_ARGS_H

struct ParsedArgs {
  unsigned short portNumber;
  unsigned backlog;
};

// Returns 0 on success, or a non-zero exit code on invalid arguments.
int parse_args(struct ParsedArgs *args_out, int argc, char *argv[]);

#endif // !CLI_ARGS_H
