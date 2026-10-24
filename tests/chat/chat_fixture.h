#ifndef CHAT_FIXTURE_H
#define CHAT_FIXTURE_H

/**
 * @file chat_fixture.h
 * @brief Shared fixture for the chat test suites: a chat room on a real poll
 * manager, with socket pairs as clients.
 */

#include <fcntl.h>
#include <netinet/in.h>
#include <server/auth.h>
#include <server/chat.h>
#include <server/constants.h>
#include <server/poll_mgr.h>
#include <server/user_store.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

// clang-format off
#include <cmocka.h>
#include <setjmp.h> // IWYU pragma: keep
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
// clang-format on

#define NCLIENTS 3
#define REPLY_BUF 4096  ///< Room for every reply a test reads at once.

/// A chat room on a real poll manager, with socket pairs as clients: `fd[i]` is
/// the server's end, `peer[i]` is what the test reads and writes.
typedef struct {
  poll_mgr_t mgr;
  chat_t chat;
  poll_callback_t cb;
  user_store_t* store;
  int listen_fd;
  int fd[NCLIENTS];
  int peer[NCLIENTS];
} fixture_t;

static inline int make_listener(void) {
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

static inline int setup(void** state) {
  fixture_t* f = calloc(1, sizeof(*f));
  if (!f) {
    return 1;
  }
  for (int i = 0; i < NCLIENTS; i++) {
    f->fd[i] = -1;
    f->peer[i] = -1;
  }
  f->store = user_store_open(":memory:");
  f->listen_fd = make_listener();
  if (!f->store || f->listen_fd == -1 ||
      poll_mgr_init(&f->mgr, f->listen_fd) != 0) {
    return 1;
  }
  // Cheap hashes: these tests check chat logic, not password strength.
  auth_set_hash_cost(AUTH_HASH_COST_FAST);
  auth_init(f->store);
  chat_init(&f->chat);
  f->cb = chat_callbacks(&f->chat);
  *state = f;
  return 0;
}

static inline int teardown(void** state) {
  fixture_t* f = *state;
  for (int i = 0; i < NCLIENTS; i++) {
    if (f->fd[i] != -1) {
      close(f->fd[i]);
    }
    if (f->peer[i] != -1) {
      close(f->peer[i]);
    }
  }
  close(f->listen_fd);
  auth_init(NULL);
  user_store_close(f->store);
  free(f);
  return 0;
}

/// Connects client @p i: a socket pair registered in the manager as slot i + 1.
static inline void connect_client(fixture_t* f, int i) {
  int sv[2];
  assert_int_equal(socketpair(AF_UNIX, SOCK_STREAM, 0, sv), 0);
  f->fd[i] = sv[0];
  f->peer[i] = sv[1];
  assert_int_equal(
      fcntl(f->fd[i], F_SETFL, fcntl(f->fd[i], F_GETFL) | O_NONBLOCK), 0);
  f->mgr.fds[i + 1].fd = f->fd[i];
  f->mgr.fds[i + 1].events = POLLIN;
  if (f->mgr.nfds < (size_t)i + 2) {
    f->mgr.nfds = (size_t)i + 2;
  }
  f->cb.on_connection(f->cb.ctx, &f->mgr, f->fd[i]);
}

/// Delivers @p text from client @p i, as if it had arrived on its socket.
static inline void say(fixture_t* f, int i, const char* text) {
  f->cb.on_message(f->cb.ctx, &f->mgr, f->fd[i], text, strlen(text));
}

static inline void tick(fixture_t* f) { f->cb.on_tick(f->cb.ctx, &f->mgr); }

/// Reads whatever the server has sent to client @p i (empty if nothing).
static inline void read_reply(fixture_t* f, int i, char* buf, size_t size) {
  ssize_t n = recv(f->peer[i], buf, size - 1, MSG_DONTWAIT);
  buf[n > 0 ? n : 0] = '\0';
}

static inline void expect_reply(fixture_t* f, int i, const char* want) {
  char buf[REPLY_BUF];
  read_reply(f, i, buf, sizeof(buf));
  assert_string_equal(buf, want);
}

static inline void expect_silence(fixture_t* f, int i) {
  expect_reply(f, i, "");
}

/// The chat slot of client @p i (slots are reused, so it need not be slot i).
static inline chat_client_t* slot(fixture_t* f, int i) {
  for (size_t n = 0; n < MAX_CONNECTIONS; n++) {
    if (f->chat.clients[n].active && f->chat.clients[n].fd == f->fd[i]) {
      return &f->chat.clients[n];
    }
  }
  fail_msg("client %d has no chat slot", i);
  return NULL;
}

static inline bool is_closing(fixture_t* f, int i) {
  return poll_mgr_is_closing(&f->mgr, f->fd[i]);
}

/// Connects client @p i and registers @p name (password "pw"), ending logged
/// in.
static inline void join(fixture_t* f, int i, const char* name) {
  char line[MAX_REPLY];
  connect_client(f, i);
  (void)snprintf(line, sizeof(line), "REGISTER %s pw\n", name);
  say(f, i, line);
  tick(f);
  expect_reply(f, i, "OK\n");
  assert_int_equal(slot(f, i)->state, CLIENT_AUTHENTICATED);
}

/// Connects client @p i and registers @p name, then drops the connection, so
/// the account exists but nobody is logged in.
static inline void register_account(fixture_t* f, int i, const char* name) {
  join(f, i, name);
  f->cb.on_disconnection(f->cb.ctx, &f->mgr, f->fd[i]);
  close(f->fd[i]);
  close(f->peer[i]);
  f->fd[i] = -1;
  f->peer[i] = -1;
}

/// Registers a test with the fixture's setup and teardown.
#define CHAT_TEST(fn) cmocka_unit_test_setup_teardown(fn, setup, teardown)

#endif  // !CHAT_FIXTURE_H
