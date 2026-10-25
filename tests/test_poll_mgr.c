#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <server/constants.h>
#include <server/poll_mgr.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <unistd.h>

// clang-format off
#include <cmocka.h>
#include <setjmp.h> // IWYU pragma: keep
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
// clang-format on

/// A poll manager plus a client socket pair: `peer` is the far end.
typedef struct {
  poll_mgr_t mgr;
  int listen_fd;
  int client_fd;  ///< Registered as the first client slot.
  int peer_fd;    ///< The test reads and writes here.
} fixture_t;

static int make_listener(void) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (fd == -1 || bind(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0 ||
      listen(fd, 1) != 0) {
    return -1;
  }
  return fd;
}

static int setup(void** state) {
  fixture_t* f = calloc(1, sizeof(*f));
  if (!f) {
    return 1;
  }
  int sv[2];
  f->listen_fd = make_listener();
  if (f->listen_fd == -1 || poll_mgr_init(&f->mgr, f->listen_fd) != 0 ||
      socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
    return 1;
  }
  f->client_fd = sv[0];
  f->peer_fd = sv[1];
  fcntl(f->client_fd, F_SETFL, fcntl(f->client_fd, F_GETFL) | O_NONBLOCK);
  f->mgr.fds[POLL_MGR_FIRST_CLIENT_SLOT].fd = f->client_fd;
  f->mgr.fds[POLL_MGR_FIRST_CLIENT_SLOT].events = POLLIN;
  f->mgr.nfds = POLL_MGR_FIRST_CLIENT_SLOT + 1;
  *state = f;
  return 0;
}

static int teardown(void** state) {
  fixture_t* f = *state;
  close(f->listen_fd);
  close(f->client_fd);
  close(f->peer_fd);
  free(f);
  return 0;
}

static void test_send_reaches_peer(void** state) {
  fixture_t* f = *state;
  assert_int_equal(poll_mgr_send(&f->mgr, f->client_fd, "OK\n", 3), 0);
  char buf[8] = {0};
  assert_int_equal(recv(f->peer_fd, buf, sizeof(buf), 0), 3);
  assert_string_equal(buf, "OK\n");
}

static void test_send_unknown_fd_fails(void** state) {
  fixture_t* f = *state;
  assert_int_equal(poll_mgr_send(&f->mgr, 9999, "x", 1), -1);
  assert_true(poll_mgr_is_closing(&f->mgr, 9999));
}

/// A reader that never reads fills the socket, then the queue; the client is
/// reported as full but not flagged for closing.
static void test_full_queue_reports_full_without_closing(void** state) {
  fixture_t* f = *state;
  char chunk[4096];
  memset(chunk, 'x', sizeof(chunk));

  int rc = 0;
  for (int i = 0; i < 100000 && rc == 0; i++) {
    rc = poll_mgr_send(&f->mgr, f->client_fd, chunk, sizeof(chunk));
  }
  assert_int_equal(rc, POLL_MGR_SEND_FULL);
  assert_false(poll_mgr_is_closing(&f->mgr, f->client_fd));
  assert_true(f->mgr.out[POLL_MGR_FIRST_CLIENT_SLOT].len <= MAX_OUTBUF);
}

static void test_close_flags_client_idempotently(void** state) {
  fixture_t* f = *state;
  assert_false(poll_mgr_is_closing(&f->mgr, f->client_fd));
  poll_mgr_close(&f->mgr, f->client_fd);
  poll_mgr_close(&f->mgr, f->client_fd);
  assert_true(poll_mgr_is_closing(&f->mgr, f->client_fd));
}

/// The wake fd sits in its reserved slot, so it is never found as a client.
static void test_wake_fd_is_not_a_client(void** state) {
  fixture_t* f = *state;
  int wake = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  assert_true(wake != -1);
  poll_mgr_set_wake_fd(&f->mgr, wake);
  assert_int_equal(f->mgr.fds[POLL_MGR_WAKE_SLOT].fd, wake);
  assert_int_equal(poll_mgr_send(&f->mgr, wake, "x", 1), -1);
  assert_true(poll_mgr_is_closing(&f->mgr, wake));
  poll_mgr_close(&f->mgr, wake);  // unknown fd: must not flag anything
  assert_false(f->mgr.closing[POLL_MGR_WAKE_SLOT]);
  close(wake);
}

static int disconnections;
static int ticks;

static void count_disconnection(void* ctx, poll_mgr_t* mgr, int client_fd) {
  (void)ctx;
  (void)mgr;
  (void)client_fd;
  disconnections++;
}

static void stop_on_tick(void* ctx, poll_mgr_t* mgr) {
  (void)ctx;
  (void)mgr;
  ticks++;
  poll_mgr_request_stop();
}

/// Also covers POLLNVAL: the client fd is closed behind the manager's back, so
/// poll() reports it invalid; the manager must drop it instead of spinning.
/// And the wake fd: a pending wake makes poll() return, is drained with one
/// read (a second read finds nothing) and is not treated as a client.
/// Runs last because the stop request is process-wide and never cleared.
static void test_run_drops_invalid_fd_and_stops(void** state) {
  fixture_t* f = *state;
  int wake = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  assert_true(wake != -1);
  poll_mgr_set_wake_fd(&f->mgr, wake);
  uint64_t one = 1;
  assert_int_equal(write(wake, &one, sizeof(one)), sizeof(one));
  close(f->client_fd);
  poll_callback_t callbacks = {.on_disconnection = count_disconnection,
                               .on_tick = stop_on_tick};
  assert_int_equal(poll_mgr_run(&f->mgr, callbacks), 0);
  assert_int_equal(ticks, 1);
  assert_int_equal(disconnections, 1);
  assert_int_equal((int)f->mgr.nfds, POLL_MGR_FIRST_CLIENT_SLOT);
  uint64_t left = 0;
  assert_int_equal(read(wake, &left, sizeof(left)), -1);
  assert_int_equal(errno, EAGAIN);
  close(wake);
  poll_mgr_set_wake_fd(&f->mgr, -1);
  f->client_fd = dup(f->peer_fd);  // so teardown can close something valid
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test_setup_teardown(test_send_reaches_peer, setup, teardown),
      cmocka_unit_test_setup_teardown(test_send_unknown_fd_fails, setup,
                                      teardown),
      cmocka_unit_test_setup_teardown(
          test_full_queue_reports_full_without_closing, setup, teardown),
      cmocka_unit_test_setup_teardown(test_close_flags_client_idempotently,
                                      setup, teardown),
      cmocka_unit_test_setup_teardown(test_wake_fd_is_not_a_client, setup,
                                      teardown),
      cmocka_unit_test_setup_teardown(test_run_drops_invalid_fd_and_stops,
                                      setup, teardown),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
