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
#define REPLY_BUF 4096      ///< Room for every reply a test reads at once.
#define LONGEST_LINE 1023   ///< Longest line that fits `chat_client_t::inbuf`.
#define TIMEOUT_PAST_S 121  ///< Seconds back to push `connected_at`.
#define FLOOD_CHUNK 4096    ///< Bytes per send when filling an output queue.
#define FLOOD_MAX_ROUNDS 1000  ///< Safety bound for the flooding loop.
#define OVERFLOW_EXTRA 100     ///< Bytes past `BUFFER_SIZE` in the long line.
#define UNKNOWN_FD 9999        ///< A descriptor chat never saw.

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

static int teardown(void** state) {
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
static void connect_client(fixture_t* f, int i) {
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
static void say(fixture_t* f, int i, const char* text) {
  f->cb.on_message(f->cb.ctx, &f->mgr, f->fd[i], text, strlen(text));
}

static void tick(fixture_t* f) { f->cb.on_tick(f->cb.ctx, &f->mgr); }

/// Reads whatever the server has sent to client @p i (empty if nothing).
static void read_reply(fixture_t* f, int i, char* buf, size_t size) {
  ssize_t n = recv(f->peer[i], buf, size - 1, MSG_DONTWAIT);
  buf[n > 0 ? n : 0] = '\0';
}

static void expect_reply(fixture_t* f, int i, const char* want) {
  char buf[REPLY_BUF];
  read_reply(f, i, buf, sizeof(buf));
  assert_string_equal(buf, want);
}

static void expect_silence(fixture_t* f, int i) { expect_reply(f, i, ""); }

/// The chat slot of client @p i (slots are reused, so it need not be slot i).
static chat_client_t* slot(fixture_t* f, int i) {
  for (size_t n = 0; n < MAX_CONNECTIONS; n++) {
    if (f->chat.clients[n].active && f->chat.clients[n].fd == f->fd[i]) {
      return &f->chat.clients[n];
    }
  }
  fail_msg("client %d has no chat slot", i);
  return NULL;
}

static bool is_closing(fixture_t* f, int i) {
  return poll_mgr_is_closing(&f->mgr, f->fd[i]);
}

/// Connects client @p i and registers @p name (password "pw"), ending logged
/// in.
static void join(fixture_t* f, int i, const char* name) {
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
static void register_account(fixture_t* f, int i, const char* name) {
  join(f, i, name);
  f->cb.on_disconnection(f->cb.ctx, &f->mgr, f->fd[i]);
  close(f->fd[i]);
  close(f->peer[i]);
  f->fd[i] = -1;
  f->peer[i] = -1;
}

// --- connection -----------------------------------------------------------

static void test_connect_starts_in_connected_state(void** state) {
  fixture_t* f = *state;
  connect_client(f, 0);
  assert_true(f->chat.clients[0].active);
  assert_int_equal(slot(f, 0)->state, CLIENT_CONNECTED);
  assert_int_equal(slot(f, 0)->failures, 0);
  expect_silence(f, 0);
}

static void test_connect_when_full_is_refused(void** state) {
  fixture_t* f = *state;
  for (size_t i = 0; i < MAX_CONNECTIONS; i++) {
    f->chat.clients[i].active = true;
  }
  connect_client(f, 0);
  expect_reply(f, 0, "ERR server full\n");
  assert_true(is_closing(f, 0));
}

static void test_disconnect_frees_the_slot(void** state) {
  fixture_t* f = *state;
  join(f, 0, "alice");
  const chat_client_t* client = slot(f, 0);
  f->cb.on_disconnection(f->cb.ctx, &f->mgr, f->fd[0]);
  assert_false(client->active);
  assert_int_equal(client->state, CLIENT_CONNECTED);
  assert_int_equal((int)client->name[0], 0);
}

// --- state machine --------------------------------------------------------

static void test_connected_rejects_msg(void** state) {
  fixture_t* f = *state;
  connect_client(f, 0);
  say(f, 0, "MSG bob hi\n");
  expect_reply(f, 0, "ERR not allowed\n");
  assert_false(is_closing(f, 0));
}

static void test_bad_lines_get_matching_errors(void** state) {
  fixture_t* f = *state;
  connect_client(f, 0);
  say(f, 0, "HELLO\n");
  expect_reply(f, 0, "ERR unknown command\n");
  say(f, 0, "LOGIN alice\n");
  expect_reply(f, 0, "ERR missing field\n");
  say(f, 0, "LOGIN alice pw extra\n");
  expect_reply(f, 0, "ERR bad input\n");
  assert_int_equal(slot(f, 0)->state, CLIENT_CONNECTED);
}

static void test_register_logs_in(void** state) {
  fixture_t* f = *state;
  join(f, 0, "alice");
  assert_string_equal(slot(f, 0)->name, "alice");
  assert_false(is_closing(f, 0));
}

static void test_authenticated_rejects_register_and_login(void** state) {
  fixture_t* f = *state;
  join(f, 0, "alice");
  say(f, 0, "REGISTER bob pw\n");
  expect_reply(f, 0, "ERR not allowed\n");
  say(f, 0, "LOGIN alice pw\n");
  expect_reply(f, 0, "ERR not allowed\n");
}

/// The result is only collected on the tick, so the client stays busy until
/// then and every command, QUIT included, gets `ERR busy`.
static void test_authenticating_is_busy_until_tick(void** state) {
  fixture_t* f = *state;
  connect_client(f, 0);
  say(f, 0, "REGISTER alice pw\n");
  assert_int_equal(slot(f, 0)->state, CLIENT_AUTHENTICATING);
  expect_silence(f, 0);

  say(f, 0, "LOGIN alice pw\n");
  expect_reply(f, 0, "ERR busy\n");
  say(f, 0, "QUIT\n");
  expect_reply(f, 0, "ERR busy\n");
  assert_false(is_closing(f, 0));

  tick(f);
  expect_reply(f, 0, "OK\n");
  assert_int_equal(slot(f, 0)->state, CLIENT_AUTHENTICATED);
}

static void test_quit_replies_ok_and_closes(void** state) {
  fixture_t* f = *state;
  connect_client(f, 0);
  say(f, 0, "QUIT\n");
  expect_reply(f, 0, "OK\n");
  assert_true(is_closing(f, 0));

  join(f, 1, "alice");
  say(f, 1, "QUIT\n");
  expect_reply(f, 1, "OK\n");
  assert_true(is_closing(f, 1));
}

// --- auth outcomes --------------------------------------------------------

static void test_register_taken_name_can_retry(void** state) {
  fixture_t* f = *state;
  register_account(f, 0, "alice");
  connect_client(f, 1);
  say(f, 1, "REGISTER alice other\n");
  tick(f);
  expect_reply(f, 1, "ERR name taken\n");
  assert_int_equal(slot(f, 1)->state, CLIENT_CONNECTED);
  assert_false(is_closing(f, 1));

  say(f, 1, "LOGIN alice pw\n");
  tick(f);
  expect_reply(f, 1, "OK\n");
}

/// Unknown user and wrong password must be indistinguishable to the client.
static void test_bad_credentials_same_reply(void** state) {
  fixture_t* f = *state;
  register_account(f, 0, "alice");
  connect_client(f, 1);
  say(f, 1, "LOGIN alice wrong\n");
  tick(f);
  expect_reply(f, 1, "ERR bad credentials\n");
  say(f, 1, "LOGIN nobody wrong\n");
  tick(f);
  expect_reply(f, 1, "ERR bad credentials\n");
  assert_int_equal(slot(f, 1)->state, CLIENT_CONNECTED);
  assert_int_equal(slot(f, 1)->failures, 2);
}

static void test_third_failure_disconnects(void** state) {
  fixture_t* f = *state;
  connect_client(f, 0);
  for (int attempt = 1; attempt < MAX_AUTH_ATTEMPTS; attempt++) {
    say(f, 0, "LOGIN nobody pw\n");
    tick(f);
    expect_reply(f, 0, "ERR bad credentials\n");
    assert_false(is_closing(f, 0));
  }
  say(f, 0, "LOGIN nobody pw\n");
  tick(f);
  expect_reply(f, 0, "ERR too many failed attempts\n");
  assert_true(is_closing(f, 0));
}

static void test_duplicate_login_refused_and_counted(void** state) {
  fixture_t* f = *state;
  join(f, 0, "alice");
  connect_client(f, 1);
  // Names are case-insensitive, so ALICE is the same user.
  for (int attempt = 1; attempt < MAX_AUTH_ATTEMPTS; attempt++) {
    say(f, 1, "LOGIN ALICE pw\n");
    tick(f);
    expect_reply(f, 1, "ERR already logged in\n");
    assert_false(is_closing(f, 1));
  }
  say(f, 1, "LOGIN ALICE pw\n");
  tick(f);
  expect_reply(f, 1, "ERR too many failed attempts\n");
  assert_true(is_closing(f, 1));
  assert_false(is_closing(f, 0));
}

static void test_login_possible_after_other_session_left(void** state) {
  fixture_t* f = *state;
  join(f, 0, "alice");
  f->cb.on_disconnection(f->cb.ctx, &f->mgr, f->fd[0]);
  connect_client(f, 1);
  say(f, 1, "LOGIN alice pw\n");
  tick(f);
  expect_reply(f, 1, "OK\n");
}

static void test_internal_failure_closes_client(void** state) {
  fixture_t* f = *state;
  connect_client(f, 0);
  auth_init(NULL);  // no store: every request ends as AUTH_INTERNAL
  say(f, 0, "REGISTER alice pw\n");
  tick(f);
  expect_reply(f, 0, "ERR internal issue\n");
  assert_true(is_closing(f, 0));
}

/// A result for a client that left (or whose slot got a new owner) is dropped.
static void test_stale_result_is_ignored(void** state) {
  fixture_t* f = *state;
  connect_client(f, 0);
  say(f, 0, "REGISTER alice pw\n");
  f->cb.on_disconnection(f->cb.ctx, &f->mgr, f->fd[0]);
  assert_false(f->chat.clients[0].active);
  tick(f);
  expect_silence(f, 0);

  // Same slot, new connection, new UUID: the old result must not touch it.
  connect_client(f, 1);
  say(f, 1, "REGISTER bob pw\n");
  f->cb.on_disconnection(f->cb.ctx, &f->mgr, f->fd[1]);
  connect_client(f, 2);
  tick(f);
  expect_silence(f, 2);
  assert_int_equal(slot(f, 2)->state, CLIENT_CONNECTED);
}

// --- MSG routing ----------------------------------------------------------

static void test_msg_is_delivered(void** state) {
  fixture_t* f = *state;
  join(f, 0, "alice");
  join(f, 1, "bob");
  say(f, 0, "MSG bob hello  there\n");
  expect_reply(f, 1, "FROM alice hello  there\n");
  expect_reply(f, 0, "OK\n");
}

static void test_msg_recipient_name_is_case_insensitive(void** state) {
  fixture_t* f = *state;
  join(f, 0, "alice");
  join(f, 1, "Bob");
  say(f, 0, "MSG BOB hi\n");
  expect_reply(f, 1, "FROM alice hi\n");
  expect_reply(f, 0, "OK\n");
}

static void test_msg_to_unknown_user(void** state) {
  fixture_t* f = *state;
  join(f, 0, "alice");
  say(f, 0, "MSG ghost hi\n");
  expect_reply(f, 0, "ERR no such user\n");
}

/// Connected-but-not-logged-in clients are not recipients.
static void test_msg_to_unauthenticated_user(void** state) {
  fixture_t* f = *state;
  join(f, 0, "alice");
  connect_client(f, 1);
  slot(f, 1)->name[0] = 'b';  // a stray name must not make it routable
  say(f, 0, "MSG b hi\n");
  expect_reply(f, 0, "ERR no such user\n");
  expect_silence(f, 1);
}

static void test_msg_to_self_not_allowed(void** state) {
  fixture_t* f = *state;
  join(f, 0, "alice");
  say(f, 0, "MSG alice hi\n");
  expect_reply(f, 0, "ERR not allowed\n");
}

static void test_msg_after_recipient_left(void** state) {
  fixture_t* f = *state;
  join(f, 0, "alice");
  join(f, 1, "bob");
  f->cb.on_disconnection(f->cb.ctx, &f->mgr, f->fd[1]);
  say(f, 0, "MSG bob hi\n");
  expect_reply(f, 0, "ERR no such user\n");
}

static void test_msg_text_limit(void** state) {
  fixture_t* f = *state;
  join(f, 0, "alice");
  join(f, 1, "bob");
  char line[MAX_REPLY + MAX_TEXT];

  char text[MAX_TEXT + 2];
  memset(text, 'x', sizeof(text));
  text[MAX_TEXT] = '\0';
  (void)snprintf(line, sizeof(line), "MSG bob %s\n", text);
  say(f, 0, line);
  expect_reply(f, 0, "OK\n");
  char want[MAX_REPLY + MAX_TEXT];
  (void)snprintf(want, sizeof(want), "FROM alice %s\n", text);
  expect_reply(f, 1, want);

  text[MAX_TEXT] = 'x';
  text[MAX_TEXT + 1] = '\0';
  (void)snprintf(line, sizeof(line), "MSG bob %s\n", text);
  say(f, 0, line);
  expect_reply(f, 0, "ERR too long\n");
  expect_silence(f, 1);
}

/// A recipient that cannot keep up is told apart from a dead one: it is not
/// kicked, and the sender is asked to retry later.
static void test_msg_to_slow_recipient_is_busy(void** state) {
  fixture_t* f = *state;
  join(f, 0, "alice");
  join(f, 1, "bob");
  char chunk[FLOOD_CHUNK];
  memset(chunk, 'x', sizeof(chunk));
  int rc = 0;
  for (int i = 0; i < FLOOD_MAX_ROUNDS && rc == 0; i++) {
    rc = poll_mgr_send(&f->mgr, f->fd[1], chunk, sizeof(chunk));
  }
  assert_int_equal(rc, POLL_MGR_SEND_FULL);

  say(f, 0, "MSG bob hi\n");
  expect_reply(f, 0, "ERR recipient busy\n");
  assert_false(is_closing(f, 1));
  assert_false(is_closing(f, 0));
}

static void test_msg_to_dead_recipient_closes_it(void** state) {
  fixture_t* f = *state;
  join(f, 0, "alice");
  join(f, 1, "bob");
  close(f->peer[1]);  // the far end vanished: sending now fails with EPIPE
  f->peer[1] = -1;
  say(f, 0, "MSG bob hi\n");
  expect_reply(f, 0, "ERR no such user\n");
  assert_true(is_closing(f, 1));
  assert_false(is_closing(f, 0));
}

// --- line framing ---------------------------------------------------------

static void test_line_split_across_packets(void** state) {
  fixture_t* f = *state;
  connect_client(f, 0);
  say(f, 0, "REGIS");
  say(f, 0, "TER ali");
  expect_silence(f, 0);
  say(f, 0, "ce pw\n");
  tick(f);
  expect_reply(f, 0, "OK\n");
}

static void test_two_lines_in_one_packet(void** state) {
  fixture_t* f = *state;
  connect_client(f, 0);
  say(f, 0, "FOO\nBAR\n");
  expect_reply(f, 0, "ERR unknown command\nERR unknown command\n");
}

static void test_longest_line_fits_one_more_does_not(void** state) {
  fixture_t* f = *state;
  connect_client(f, 0);
  char line[LONGEST_LINE + 3];

  memset(line, 'a', LONGEST_LINE);
  line[LONGEST_LINE] = '\n';
  line[LONGEST_LINE + 1] = '\0';
  say(f, 0, line);
  expect_reply(f, 0, "ERR unknown command\n");

  memset(line, 'a', LONGEST_LINE + 1);
  line[LONGEST_LINE + 1] = '\n';
  line[LONGEST_LINE + 2] = '\0';
  say(f, 0, line);
  expect_reply(f, 0, "ERR too long\n");
}

/// The overflow only costs the one line: the next one is parsed normally.
static void test_overflow_does_not_poison_next_line(void** state) {
  fixture_t* f = *state;
  connect_client(f, 0);
  char line[BUFFER_SIZE * 2];
  memset(line, 'a', sizeof(line));
  size_t n = (size_t)BUFFER_SIZE + OVERFLOW_EXTRA;
  memcpy(line + n, "\nFOO\n", sizeof("\nFOO\n"));  // includes the NUL
  say(f, 0, line);
  expect_reply(f, 0, "ERR too long\nERR unknown command\n");
}

static void test_input_after_quit_is_ignored(void** state) {
  fixture_t* f = *state;
  connect_client(f, 0);
  say(f, 0, "QUIT\nFOO\nQUIT\n");
  expect_reply(f, 0, "OK\n");
  assert_true(is_closing(f, 0));
}

static void test_message_from_unknown_fd_is_ignored(void** state) {
  fixture_t* f = *state;
  f->cb.on_message(f->cb.ctx, &f->mgr, UNKNOWN_FD, "FOO\n", 4);
  assert_false(f->chat.clients[0].active);
}

// --- timeout --------------------------------------------------------------

static void test_unauthenticated_client_times_out(void** state) {
  fixture_t* f = *state;
  join(f, 0, "alice");
  connect_client(f, 1);
  tick(f);
  expect_silence(f, 1);
  assert_false(is_closing(f, 1));

  slot(f, 0)->connected_at.tv_sec -= TIMEOUT_PAST_S;
  slot(f, 1)->connected_at.tv_sec -= TIMEOUT_PAST_S;
  tick(f);
  expect_reply(f, 1, "ERR timeout\n");
  assert_true(is_closing(f, 1));
  // Logged-in clients never time out.
  expect_silence(f, 0);
  assert_false(is_closing(f, 0));
}

#define TEST(fn) cmocka_unit_test_setup_teardown(fn, setup, teardown)

int main(void) {
  const struct CMUnitTest tests[] = {
      TEST(test_connect_starts_in_connected_state),
      TEST(test_connect_when_full_is_refused),
      TEST(test_disconnect_frees_the_slot),
      TEST(test_connected_rejects_msg),
      TEST(test_bad_lines_get_matching_errors),
      TEST(test_register_logs_in),
      TEST(test_authenticated_rejects_register_and_login),
      TEST(test_authenticating_is_busy_until_tick),
      TEST(test_quit_replies_ok_and_closes),
      TEST(test_register_taken_name_can_retry),
      TEST(test_bad_credentials_same_reply),
      TEST(test_third_failure_disconnects),
      TEST(test_duplicate_login_refused_and_counted),
      TEST(test_login_possible_after_other_session_left),
      TEST(test_internal_failure_closes_client),
      TEST(test_stale_result_is_ignored),
      TEST(test_msg_is_delivered),
      TEST(test_msg_recipient_name_is_case_insensitive),
      TEST(test_msg_to_unknown_user),
      TEST(test_msg_to_unauthenticated_user),
      TEST(test_msg_to_self_not_allowed),
      TEST(test_msg_after_recipient_left),
      TEST(test_msg_text_limit),
      TEST(test_msg_to_slow_recipient_is_busy),
      TEST(test_msg_to_dead_recipient_closes_it),
      TEST(test_line_split_across_packets),
      TEST(test_two_lines_in_one_packet),
      TEST(test_longest_line_fits_one_more_does_not),
      TEST(test_overflow_does_not_poison_next_line),
      TEST(test_input_after_quit_is_ignored),
      TEST(test_message_from_unknown_fd_is_ignored),
      TEST(test_unauthenticated_client_times_out),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
