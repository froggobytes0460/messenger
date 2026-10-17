#ifndef CHAT_H
#define CHAT_H

#include <server/constants.h>
#include <server/poll_mgr.h>
#include <server/uuid.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/**
 * @file chat.h
 * @brief Chat room logic layered on top of the poll manager.
 */

/**
 * @brief The state of a client connected to server.
 */
typedef enum {
  CLIENT_CONNECTED,      ///< The client has just connected
  CLIENT_AUTHENTICATING, ///< The client is authenticating.
  /// The client is authenticated, and can send/receive msgs.
  CLIENT_AUTHENTICATED,
} StateEnum;

/**
 * @brief State containing client data.
 *
 * One slot per connection; a slot is reusable once @c active is false.
 */
typedef struct {
  /// The current line did not fit in @c inbuf; it is rejected, not parsed.
  bool overflow;
  bool active;      ///< Flag that determines if client is active.
  int fd;           ///< The socket file descriptor of the client.
  int failures;     ///< Number of failed authentication attempts.
  StateEnum state;  ///< The state of the client.
  size_t inlen;     ///< Number of valid bytes currently held in @c inbuf.
  client_uuid_t id; ///< ID of client.
  struct timespec connected_at; ///< The time client connected to server.
  char name[MAX_USERNAME + 1];  ///< The name string.
  /// Bytes received so far, not yet newline-terminated line.
  char inbuf[BUFFER_SIZE];
} chat_client_t;

/**
 * @brief Chat room state: all client slots.
 */
typedef struct {
  chat_client_t clients[MAX_CONNECTIONS]; ///< Fixed pool of client slots.
} chat_t;

/**
 * @brief Initializes a chat room, marking every client slot as unused.
 * @param[out] chat Chat state to initialize.
 */
void chat_init(chat_t *chat);

/**
 * @brief Builds the poll manager callbacks that implement the chat room.
 *
 * Clients speak a line protocol: REGISTER, LOGIN, MSG and QUIT, one command per
 * line, each line ending in '\n' (no '\r'). Malformed lines get the matching
 * "ERR <reason>". Well-formed ones depend on the client state: a connected
 * client may REGISTER, LOGIN or QUIT; while authenticating every command gets
 * "ERR busy"; an authenticated client may MSG or QUIT. Anything else gets
 * "ERR not allowed". Authentication itself is not implemented yet, so a client
 * that sends REGISTER or LOGIN stays in CLIENT_AUTHENTICATING until it times
 * out.
 *
 * @param[in] chat Chat state, passed back to the handlers as @c ctx. Must be
 *             initialized with chat_init() and outlive the run loop.
 * @return Callbacks to pass to poll_mgr_run().
 */
poll_callback_t chat_callbacks(chat_t *chat);

#endif // !CHAT_H
