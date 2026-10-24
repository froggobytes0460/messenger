#include "chat_fixture.h"

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

int main(void) {
  const struct CMUnitTest tests[] = {
      CHAT_TEST(test_register_taken_name_can_retry),
      CHAT_TEST(test_bad_credentials_same_reply),
      CHAT_TEST(test_third_failure_disconnects),
      CHAT_TEST(test_duplicate_login_refused_and_counted),
      CHAT_TEST(test_login_possible_after_other_session_left),
      CHAT_TEST(test_internal_failure_closes_client),
      CHAT_TEST(test_stale_result_is_ignored),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
