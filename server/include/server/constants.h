#ifndef CONSTANTS_H
#define CONSTANTS_H

/**
 * @file constants.h
 * @brief Compile-time limits and defaults shared across the server.
 */

#define MAX_PORT_STR_LEN 7 ///< Size of the buffer holding a port as a string.
#define DECIMAL_INTEGER_BASE                                                   \
  10 ///< Base passed to strtol() when parsing numbers.
#define PORT_NUMBER_DEFAULT 8000 ///< Port listened on when none is given.
#define BACKLOG_DEFAULT 10       ///< Default listen() backlog.
#define BUFFER_SIZE 1024 ///< Size of per-client and receive buffers, in bytes.
#define MAX_NAME_LEN 32  ///< Maximum username length, excluding the terminator.
#define MAX_ANNOUNCE_TEXT_LEN                                                  \
  64 ///< Maximum length of a join/leave announcement text.
#define MAX_CONNECTIONS 100  ///< Maximum number of simultaneous clients.
#define LOG_TIMESTAMP_LEN 32 ///< Size of the log timestamp buffer, in bytes.
#define LOG_MSG_MAX_LEN 512  ///< Maximum length of one formatted log message.

#endif // !CONSTANTS_H
