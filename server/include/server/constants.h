#ifndef CONSTANTS_H
#define CONSTANTS_H

/**
 * @file constants.h
 * @brief Compile-time limits and defaults shared across the server.
 */

#include <stdbool.h>

#define MAX_PORT_STR_LEN 7 ///< Size of the buffer holding a port as a string.
#define DECIMAL_INTEGER_BASE                                                   \
  10 ///< Base passed to strtol() when parsing numbers.
#define PORT_NUMBER_DEFAULT 8000 ///< Port listened on when none is given.
#define BACKLOG_DEFAULT 10       ///< Default listen() backlog.
#define BUFFER_SIZE 1024 ///< Size of per-client and receive buffers, in bytes.
#define MAX_CONNECTIONS 100  ///< Maximum number of simultaneous clients.
#define LOG_TIMESTAMP_LEN 32 ///< Size of the log timestamp buffer, in bytes.
#define LOG_MSG_MAX_LEN 512  ///< Maximum length of one formatted log message.
#define POLL_TICK_MS                                                           \
  1000 ///< The time in milliseconds for waiting with each poll iteration.
#define UNAUTH_TIMEOUT_MS                                                      \
  120000 ///< The timeout in ms for unauthenticated clients before rejection.
#define MAX_OUTBUF                                                             \
  16384 ///< Maximum unsent bytes queued for each client before dropping.
#define DEFAULT_VERBOSITY false ///< Default verbosity for program.
#define MAX_USERNAME 32  ///< Maximum username length allowed for clients.
#define MAX_PASSWORD 128 ///< Maximum password length allowed for clients.
#define MAX_TEXT 200     ///< Maximum text in client requests.
#define MAX_REPLY                                                              \
  256 ///< Buffer size that fits any reply line (longest is a FROM line).
_Static_assert(MAX_REPLY >=
                   sizeof("FROM ") - 1 + MAX_USERNAME + 1 + MAX_TEXT + 1 + 1,
               "MAX_REPLY must fit \"FROM <user> <text>\\n\" and its NUL");
#define MAX_NUM_USERS 10000ULL ///< Maximum number of users supported by server.
#define AUTH_QUEUE_CAP 300     ///< Limit on length of authentication queue.
#define MAX_AUTH_ATTEMPTS 3    ///< Maximum authentication attempts allowed.

#endif // !CONSTANTS_H
