#ifndef POLL_MGR_H
#define POLL_MGR_H

#include <server/constants.h>
#include <stddef.h>
#include <sys/poll.h>

typedef struct {
  int listen_fd;
  size_t nfds;
  struct pollfd fds[MAX_CONNECTIONS + 1]; // +1 for the listening socket
} poll_mgr_t;

typedef struct {
  void (*on_disconnection)(void *ctx, poll_mgr_t *mgr, int client_fd);
  void (*on_connection)(void *ctx, poll_mgr_t *mgr, int client_fd);
  void (*on_message)(void *ctx, poll_mgr_t *mgr, int sender_fd, const char *buf,
                     size_t len);
  void *ctx;
} poll_callback_t;

void poll_mgr_init(poll_mgr_t *mgr, int listen_fd);
// Runs the event loop. Only returns, with a non-zero exit code, on failure.
int poll_mgr_run(poll_mgr_t *mgr, poll_callback_t callbacks);

void poll_mgr_broadcast(poll_mgr_t *mgr, int except_fd, const char *buf,
                        size_t buf_len);

#endif // !POLL_MGR_H
