#ifndef MAIN_SOCKET_H
#define MAIN_SOCKET_H

// Creates a socket bound to `port` and stores it in `fd_out`. Returns 0 on
// success, or a non-zero exit code on failure.
int get_main_socket(unsigned short port, int *fd_out);

#endif // !MAIN_SOCKET_H
