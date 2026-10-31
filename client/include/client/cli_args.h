#ifndef CLI_ARGS_H
#define CLI_ARGS_H

/**
 * @file cli_args.h
 * @brief Handles CLI arguments for program.
 */

/**
 * @brief Parsed arguments from CLI
 */
struct parsed_args {
  unsigned short port;  ///< The port number to connect to.
  unsigned timeout;     ///< The time (in seconds) to wait while connecting.
  /// Number of times failure before connnection is handled before dropping.
  unsigned retry;
  const char* host_ip;   ///< The IP address of host server.
  const char* username;  ///< Username of client.
};

/**
 * @brief Parses CLI arguments passed to program, to attain attributes listed in
 * @c parsed_args .
 * @param[out] args_out The arguments parsed from the CLI.
 * @param[in] argc The argc passed to @c main
 * @param[in] argv The argv passed to @c main
 * @return 0 on success, the exit code to return with on failure.
 */
int parse_args(struct parsed_args* out, int argc, const char* argv[]);

#endif  // !CLI_ARGS_H
