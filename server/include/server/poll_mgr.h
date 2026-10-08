#ifndef POLL_MGR_H
#define POLL_MGR_H

#include <server/constants.h>
#include <stddef.h>
#include <sys/poll.h>

/**
 * @file poll_mgr.h
 * @brief poll()-based event loop that dispatches connection events to
 * callbacks.
 */

/**
 * @brief State containing the listening socket and all client sockets.
 */
typedef struct {
  int listen_fd; ///< The socket descriptor of the listening socket.
  size_t nfds;   ///< The number of socket descriptors in @c fds .
  /// Polled descriptors (+1 for the listening socket).
  struct pollfd fds[MAX_CONNECTIONS + 1];
} poll_mgr_t;

/**
 * @brief Event handlers invoked by poll_mgr_run().
 *
 * Each handler receives @c ctx as its first argument.
 */
typedef struct {
  /// Called after a client disconnected; @p client_fd is already closed.
  void (*on_disconnection)(void *ctx, poll_mgr_t *mgr, int client_fd);
  /// Called after a new client was accepted as @p client_fd.
  void (*on_connection)(void *ctx, poll_mgr_t *mgr, int client_fd);
  /// Called with @p len bytes in @p buf (not NUL-terminated) from @p sender_fd.
  void (*on_message)(void *ctx, poll_mgr_t *mgr, int sender_fd, const char *buf,
                     size_t len);
  void *ctx; ///< User data passed as the first argument to every handler.
} poll_callback_t;

/**
 * @brief Initializes a poll manager.
 * @param[out] mgr Poll manager to initialize.
 * @param[in] listen_fd The socket descriptor of the listening socket.
 */
void poll_mgr_init(poll_mgr_t *mgr, int listen_fd);

/**
 * @brief Runs the event loop.
 * @param[in,out] mgr Initialized poll manager.
 * @param[in] callbacks Handlers invoked on connection, disconnection and
 * message, plus the context pointer passed to them.
 * @return Only returns, with a non-zero exit code, on failure.
 */
int poll_mgr_run(poll_mgr_t *mgr, poll_callback_t callbacks);

/**
 * @brief Broadcasts a message to all connected clients other than `except_fd`.
 * @param[in] mgr Poll manager holding the connected clients.
 * @param[in] except_fd The socket descriptor of the client to not send this
 * message to (use -1 to broadcast to all clients).
 * @param[in] buf The bytes to broadcast.
 * @param[in] buf_len The number of bytes in @p buf .
 */
void poll_mgr_broadcast(poll_mgr_t *mgr, int except_fd, const char *buf,
                        size_t buf_len);

#endif // !POLL_MGR_H
