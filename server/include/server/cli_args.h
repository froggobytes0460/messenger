#ifndef CLI_ARGS_H
#define CLI_ARGS_H

/**
 * @file cli_args.h
 * @brief Command line argument parsing.
 */

/**
 * @brief The CLI arguments that are parsed.
 */
struct ParsedArgs {
  unsigned short portNumber; ///< The port number to listen on.
  /// The number of connections that are allowed to be on queue/backlog.
  unsigned backlog;
  /// File to append log lines to (in addition to stderr), or NULL for none.
  /// Points into @c argv , so it lives as long as the program.
  const char *logFile;
};

/**
 * @brief Parses CLI arguments passed to program, to attain attributes listed in
 * @c ParsedArgs .
 * @param[out] args_out The arguments parsed from the CLI.
 * @param[in] argc The argc passed to @c main
 * @param[in] argv The argv passed to @c main
 * @return 0 on success, @c EXIT_FAILURE on invalid arguments.
 */
int parse_args(struct ParsedArgs *args_out, int argc, char *argv[]);

#endif // !CLI_ARGS_H
