#ifndef POLL_MGR_H
#define POLL_MGR_H

#include <server/constants.h>
#include <stdbool.h>
#include <stddef.h>
#include <sys/poll.h>

/**
 * @file poll_mgr.h
 * @brief poll()-based event loop that dispatches connection events to
 * callbacks.
 */

/**
 * @brief Message buffer area to store data to ensure non-blocking send.
 */
typedef struct {
  size_t len;             ///< The length of data in current buffer.
  char data[MAX_OUTBUF];  ///< The buffer storing queued messages.
} outbuf_t;

/**
 * @brief State containing the listening socket and all client sockets.
 */
typedef struct {
  int listen_fd;  ///< The socket descriptor of the listening socket.
  size_t nfds;    ///< The number of socket descriptors in @c fds .
  /// Polled descriptors (+1 for the listening socket).
  struct pollfd fds[MAX_CONNECTIONS + 1];
  outbuf_t out[MAX_CONNECTIONS + 1];  ///< Buffer queue for each client.
  /// Flag array to indicate if client needs to be closed.
  bool closing[MAX_CONNECTIONS + 1];
} poll_mgr_t;

/**
 * @brief Event handlers invoked by poll_mgr_run().
 *
 * Each handler receives @c ctx as its first argument.
 */
typedef struct {
  /// Called after a client disconnected; @p client_fd is already closed.
  void (*on_disconnection)(void* ctx, poll_mgr_t* mgr, int client_fd);
  /// Called after a new client was accepted as @p client_fd.
  void (*on_connection)(void* ctx, poll_mgr_t* mgr, int client_fd);
  /// Called with @p len bytes in @p buf from @p sender_fd. The bytes are
  /// followed by a NUL for convenience, but may contain NULs themselves; the
  /// buffer is shared and only valid during the call.
  void (*on_message)(void* ctx, poll_mgr_t* mgr, int sender_fd, const char* buf,
                     size_t len);
  /// Called after every `poll()` return, including timeouts with no events.
  void (*on_tick)(void* ctx, poll_mgr_t* mgr);
  void* ctx;  ///< User data passed as the first argument to every handler.
} poll_callback_t;

/// Returned by @ref poll_mgr_send when the client's output queue is full.
#define POLL_MGR_SEND_FULL (-2)

/**
 * @brief Initializes a poll manager.
 * @param[out] mgr Poll manager to initialize.
 * @param[in] listen_fd The socket descriptor of the listening socket.
 * @return 0 if successful, otherwise log error and return -1.
 */
int poll_mgr_init(poll_mgr_t* mgr, int listen_fd);

/**
 * @brief Runs the event loop.
 * @param[in,out] mgr Initialized poll manager.
 * @param[in] callbacks Handlers invoked on connection, disconnection, message
 * and tick, plus the context pointer passed to them.
 * @return 0 after @ref poll_mgr_request_stop was called, otherwise a non-zero
 * exit code on failure.
 */
int poll_mgr_run(poll_mgr_t* mgr, poll_callback_t callbacks);

/**
 * @brief Asks @ref poll_mgr_run to return after its current iteration.
 *
 * Async-signal-safe: call it from a SIGINT/SIGTERM handler installed without
 * SA_RESTART so the blocked poll() wakes up with EINTR.
 */
void poll_mgr_request_stop(void);

/**
 * @brief Sends a message to client, adhering to buffer queues.
 * @param[in] mgr Poll manager holding connected clients.
 * @param[in] fd The socket file descriptor of the client to send.
 * @param[in] buf The bytes to send to client. (not NUL-terminated)
 * @param[in] len The length of the @p buf .
 * @return 0 on success, -1 on error (unknown fd or socket failure), or
 * @ref POLL_MGR_SEND_FULL if the queue is full and nothing was queued. The
 * client is not flagged for closing in either failure case.
 */
int poll_mgr_send(poll_mgr_t* mgr, int fd, const char* buf, size_t len);

/**
 * @brief Idempotent function that flags a client for closing.
 * @param[in] mgr Poll manager holding connected clients.
 * @param[in] client_fd The client socket fd to be closed.
 */
void poll_mgr_close(poll_mgr_t* mgr, int client_fd);

/**
 * @brief Tells whether a client is already flagged for closing.
 * @param[in] mgr Poll manager holding connected clients.
 * @param[in] client_fd The client socket fd to check.
 * @return true if the client is flagged for closing or is unknown.
 */
bool poll_mgr_is_closing(const poll_mgr_t* mgr, int client_fd);

#endif  // !POLL_MGR_H
