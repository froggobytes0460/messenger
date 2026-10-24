#ifndef MAIN_SOCKET_H
#define MAIN_SOCKET_H

/**
 * @file main_socket.h
 * @brief Creation of the server's bound (not yet listening) socket.
 */

/**
 * @brief Creates a socket bound to a port on the address provided.
 *
 * Uses the first address family `getaddrinfo` offers that binds. The caller
 * must still call `listen()` on the returned descriptor.
 *
 * @param[in] host The IP string that the server runs on.
 * @param[in] port The port the socket is bound to.
 * @param[out] fd_out Receives the socket's file descriptor on success.
 * @return 0 on success, otherwise a non-zero sysexits.h code suitable as the
 * exit code of @c main .
 */
int get_main_socket(const char* host, unsigned short port, int* fd_out);

#endif  // !MAIN_SOCKET_H
