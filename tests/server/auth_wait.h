#ifndef AUTH_WAIT_H
#define AUTH_WAIT_H

/**
 * @file auth_wait.h
 * @brief Test helpers for the asynchronous auth worker: wait on its wake fd
 * instead of assuming a result is ready right after `auth_submit`.
 */

#include <poll.h>
#include <server/auth.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#define AUTH_WAIT_MS \
  5000  ///< Upper bound for one wait (a failure shows as a timeout).
#define MS_PER_S 1000L
#define NS_PER_MS 1000000L

/// Waits up to @p timeout_ms for the worker to ring the wake fd, then drains
/// it. Returns false on timeout.
static inline bool wait_for_auth(int timeout_ms) {
  struct pollfd pfd = {.fd = auth_wake_fd(), .events = POLLIN, .revents = 0};
  if (poll(&pfd, 1, timeout_ms) <= 0 || !(pfd.revents & POLLIN)) {
    return false;
  }
  uint64_t counter = 0;
  (void)!read(pfd.fd, &counter, sizeof(counter));
  return true;
}

/// Like `auth_collect`, but sleeps until the worker has produced a result (or
/// @p timeout_ms passes). Returns false on timeout.
static inline bool collect_wait(auth_result_t* out, int timeout_ms) {
  struct timespec start;
  struct timespec now;
  (void)clock_gettime(CLOCK_MONOTONIC, &start);
  for (;;) {
    if (auth_collect(out)) {
      return true;
    }
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed = ((now.tv_sec - start.tv_sec) * MS_PER_S) +
                   ((now.tv_nsec - start.tv_nsec) / NS_PER_MS);
    if (elapsed >= timeout_ms || !wait_for_auth(timeout_ms - (int)elapsed)) {
      return auth_collect(out);
    }
  }
}

#endif  // !AUTH_WAIT_H
