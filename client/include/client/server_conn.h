#ifndef SERVER_CONN_H
#define SERVER_CONN_H

#include <client/cli_args.h>

/**
 * @file server_conn.h
 * @brief Handles connnection to server.
 */

/**
 * Handles connection to server with retry orchestration.
 * @param[in] args The parsed CLI arguments to acertain server port, IP address,
 * retry and timeout.
 * @return The socket file descriptor (socket fd) on success, otherwise -1 on
 * failure.
 */
int connect_to_server(const struct parsed_args* args);

#endif  // !SERVER_CONN_H
