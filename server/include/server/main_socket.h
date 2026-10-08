#ifndef MAIN_SOCKET_H
#define MAIN_SOCKET_H

/**
 * @file main_socket.h
 * @brief Creation of the server's listening socket.
 */

/**
 * @brief Creates a listening socket bound to a port.
 * @param[in] port The port the socket is bound to.
 * @param[out] fd_out Receives the socket's file descriptor on success.
 * @return 0 on success, otherwise a non-zero sysexits.h code suitable as the
 * exit code of @c main .
 */
int get_main_socket(unsigned short port, int *fd_out);

#endif // !MAIN_SOCKET_H
