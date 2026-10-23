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
  char* argv[] = {"server", "-p", "9000", "-l", "out.log", NULL};
  struct ParsedArgs args;
  assert_int_equal(parse(&args, dynamic_argc, argv), 0);
  assert_int_equal(args.portNumber, 9000);
  assert_string_equal(args.logFile, "out.log");
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
  char* argv[] = {"server", "-p", "99999", NULL};
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
  static char* const rows[][4] = {
      {"server", "-p", "0", NULL},       {"server", "-p", "-1", NULL},
      {"server", "-p", "80x", NULL},     {"server", "-p", "abc", NULL},
      {"server", "-p", "", NULL},        {"server", "-b", "-1", NULL},
      {"server", "-b", "x", NULL},       {"server", "-p", NULL, NULL},
      {"server", "-z", NULL, NULL},      {"server", "extra", NULL, NULL},
      {"server", "-p", "9000", "extra"},
  };
  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
    char* argv[4];
    memcpy(argv, rows[i], sizeof(argv));
    int argc = 0;
    while (argc < 4 && argv[argc] != NULL) {
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
      cmocka_unit_test(test_database_path),
      cmocka_unit_test(test_invalid_port),
      cmocka_unit_test(test_backlog),
      cmocka_unit_test(test_rejected_arguments),
      cmocka_unit_test(test_null_output),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
