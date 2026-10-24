#include "chat_fixture.h"

#define TIMEOUT_PAST_S 121  ///< Seconds back to push `connected_at`.
#define UNKNOWN_FD 9999     ///< A descriptor chat never saw.

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
static void test_message_from_unknown_fd_is_ignored(void** state) {
  fixture_t* f = *state;
  f->cb.on_message(f->cb.ctx, &f->mgr, UNKNOWN_FD, "FOO\n", 4);
  assert_false(f->chat.clients[0].active);
}

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

int main(void) {
  const struct CMUnitTest tests[] = {
      CHAT_TEST(test_connect_starts_in_connected_state),
      CHAT_TEST(test_connect_when_full_is_refused),
      CHAT_TEST(test_disconnect_frees_the_slot),
      CHAT_TEST(test_connected_rejects_msg),
      CHAT_TEST(test_bad_lines_get_matching_errors),
      CHAT_TEST(test_register_logs_in),
      CHAT_TEST(test_authenticated_rejects_register_and_login),
      CHAT_TEST(test_authenticating_is_busy_until_tick),
      CHAT_TEST(test_quit_replies_ok_and_closes),
      CHAT_TEST(test_message_from_unknown_fd_is_ignored),
      CHAT_TEST(test_unauthenticated_client_times_out),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
