#include <server/cli_args.h>
#include <server/constants.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

// clang-format off
#include <cmocka.h>
#include <setjmp.h> // IWYU pragma: keep
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
// clang-format on

/// Simple macro to dynamically determine the `argc` for the `argv` string array
/// in context.
#define dynamic_argc ((int)(sizeof(argv) / sizeof(argv[0])) - 1)

// getopt keeps global state; reset it so each case parses from argv[1].
static int parse(struct ParsedArgs* out, int argc, char* argv[]) {
  optind = 1;  // NOLINT(misc-include-cleaner)
  return parse_args(out, argc, argv);
}

static void test_defaults(void** state) {
  (void)state;
  char* argv[] = {"server", NULL};
  struct ParsedArgs args;
  assert_int_equal(parse(&args, dynamic_argc, argv), 0);
  assert_int_equal(args.portNumber, PORT_NUMBER_DEFAULT);
  assert_string_equal(args.ipString, IP_HOST_DEFAULT);
  assert_int_equal(args.backlog, BACKLOG_DEFAULT);
  assert_false(args.verbose);
  assert_null(args.logFile);
  assert_string_equal(args.databasePath, DEFAULT_DB_PATH);
}

static void test_verbose_flag(void** state) {
  (void)state;
  char* argv[] = {"server", "-v", NULL};
  struct ParsedArgs args;
  assert_int_equal(parse(&args, dynamic_argc, argv), 0);
  assert_true(args.verbose);
}

static void test_port_and_log(void** state) {
  (void)state;
  char* argv[] = {"server", "-l", "out.log", "9000", NULL};
  struct ParsedArgs args;
  assert_int_equal(parse(&args, dynamic_argc, argv), 0);
  assert_int_equal(args.portNumber, 9000);
  assert_string_equal(args.ipString, IP_HOST_DEFAULT);
  assert_string_equal(args.logFile, "out.log");
}

static void test_host_and_port(void** state) {
  (void)state;
  char* argv[] = {"server", "127.0.0.1", "9000", NULL};
  struct ParsedArgs args;
  assert_int_equal(parse(&args, dynamic_argc, argv), 0);
  assert_string_equal(args.ipString, "127.0.0.1");
  assert_int_equal(args.portNumber, 9000);
}

static void test_options_with_host_and_port(void** state) {
  (void)state;
  char* argv[] = {"server", "-v", "-b", "5", "::1", "65535", NULL};
  struct ParsedArgs args;
  assert_int_equal(parse(&args, dynamic_argc, argv), 0);
  assert_true(args.verbose);
  assert_int_equal(args.backlog, 5);
  assert_string_equal(args.ipString, "::1");
  assert_int_equal(args.portNumber, 65535);
}

static void test_database_path(void** state) {
  (void)state;
  char* argv[] = {"server", "-d", "new.db", NULL};
  struct ParsedArgs args;
  assert_int_equal(parse(&args, dynamic_argc, argv), 0);
  assert_string_equal(args.databasePath, "new.db");
}

static void test_invalid_port(void** state) {
  (void)state;
  char* argv[] = {"server", "99999", NULL};
  struct ParsedArgs args;
  assert_int_equal(parse(&args, dynamic_argc, argv), EX_USAGE);
}

static void test_backlog(void** state) {
  (void)state;
  char* argv[] = {"server", "-b", "0", NULL};
  struct ParsedArgs args;
  assert_int_equal(parse(&args, dynamic_argc, argv), 0);
  assert_int_equal(args.backlog, 0);
}

/// Every row must be rejected with EX_USAGE.
static void test_rejected_arguments(void** state) {
  (void)state;
  enum { ROW_LEN = 5 };
  static char* const rows[][ROW_LEN] = {
      {"server", "0", NULL},
      {"server", "--", "-1", NULL},
      {"server", "80x", NULL},
      {"server", "abc", NULL},
      {"server", "", NULL},
      {"server", "localhost", NULL},
      {"server", "localhost", "99999", NULL},
      {"server", "localhost", "abc", NULL},
      {"server", "a", "b", "9000", NULL},
      {"server", "-b", "-1", NULL},
      {"server", "-b", "x", NULL},
      {"server", "-b", NULL},
      {"server", "-p", "9000", NULL},
      {"server", "-z", NULL},
  };
  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
    char* argv[ROW_LEN];
    memcpy((void*)argv, (const void*)rows[i], sizeof(argv));
    int argc = 0;
    while (argc < ROW_LEN && argv[argc] != NULL) {
      argc++;
    }
    print_message("row %zu: argc=%d first arg '%s'\n", i, argc, argv[1]);
    struct ParsedArgs args;
    assert_int_equal(parse(&args, argc, argv), EX_USAGE);
  }
}

static void test_null_output(void** state) {
  (void)state;
  char* argv[] = {"server", NULL};
  assert_int_equal(parse(NULL, dynamic_argc, argv), EX_USAGE);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_defaults),
      cmocka_unit_test(test_verbose_flag),
      cmocka_unit_test(test_port_and_log),
      cmocka_unit_test(test_host_and_port),
      cmocka_unit_test(test_options_with_host_and_port),
      cmocka_unit_test(test_database_path),
      cmocka_unit_test(test_invalid_port),
      cmocka_unit_test(test_backlog),
      cmocka_unit_test(test_rejected_arguments),
      cmocka_unit_test(test_null_output),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
