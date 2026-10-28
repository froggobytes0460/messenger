#include "chat_fixture.h"

#define LONGEST_LINE 1023   ///< Longest line that fits `chat_client_t::inbuf`.
#define OVERFLOW_EXTRA 100  ///< Bytes past `BUFFER_SIZE` in the long line.

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

int main(void) {
  const struct CMUnitTest tests[] = {
      CHAT_TEST(test_line_split_across_packets),
      CHAT_TEST(test_two_lines_in_one_packet),
      CHAT_TEST(test_longest_line_fits_one_more_does_not),
      CHAT_TEST(test_overflow_does_not_poison_next_line),
      CHAT_TEST(test_input_after_quit_is_ignored),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
