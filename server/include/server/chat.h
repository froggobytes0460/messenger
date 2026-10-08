#ifndef CHAT_H
#define CHAT_H

#include <server/constants.h>
#include <server/poll_mgr.h>
#include <stdbool.h>
#include <stddef.h>

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
  // The client is authenticated, and can send/receive msgs.
  CLIENT_AUTHENTICATED,
} StateEnum;

/**
 * @brief State containing client data.
 *
 * One slot per connection; a slot is reusable once @c active is false.
 */
typedef struct {
  bool active;                 ///< Flag that determines if client is active.
  int fd;                      ///< The socket file descriptor of the client.
  StateEnum state;             ///< The state of the client.
  char name[MAX_NAME_LEN + 1]; ///< The name string.
  char inbuf[BUFFER_SIZE];     ///< Bytes received so far, not yet
                               ///< newline-terminated line.
  size_t inlen; ///< Number of valid bytes currently held in @c inbuf.
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
 * Protocol: the first line a client sends is its username; every following
 * line is a chat message broadcast to everyone else.
 *
 * @param[in] chat Chat state, passed back to the handlers as @c ctx. Must be
 *             initialized with chat_init() and outlive the run loop.
 * @return Callbacks to pass to poll_mgr_run().
 */
poll_callback_t chat_callbacks(chat_t *chat);

#endif // !CHAT_H
