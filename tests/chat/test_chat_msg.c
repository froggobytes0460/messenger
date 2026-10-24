#include "chat_fixture.h"

#define FLOOD_CHUNK 4096       ///< Bytes per send when filling an output queue.
#define FLOOD_MAX_ROUNDS 1000  ///< Safety bound for the flooding loop.

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

int main(void) {
  const struct CMUnitTest tests[] = {
      CHAT_TEST(test_msg_is_delivered),
      CHAT_TEST(test_msg_recipient_name_is_case_insensitive),
      CHAT_TEST(test_msg_to_unknown_user),
      CHAT_TEST(test_msg_to_unauthenticated_user),
      CHAT_TEST(test_msg_to_self_not_allowed),
      CHAT_TEST(test_msg_after_recipient_left),
      CHAT_TEST(test_msg_text_limit),
      CHAT_TEST(test_msg_to_slow_recipient_is_busy),
      CHAT_TEST(test_msg_to_dead_recipient_closes_it),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
