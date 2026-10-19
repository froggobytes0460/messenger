#include <server/cli_args.h>
#include <server/constants.h>
#include <stdlib.h>
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
static int parse(struct ParsedArgs *out, int argc, char *argv[]) {
  optind = 1; // NOLINT(misc-include-cleaner)
  return parse_args(out, argc, argv);
}

static void test_defaults(void **state) {
  (void)state;
  char *argv[] = {"server", NULL};
  struct ParsedArgs args;
  assert_int_equal(parse(&args, 1, argv), 0);
  assert_int_equal(args.portNumber, PORT_NUMBER_DEFAULT);
  assert_null(args.logFile);
  assert_string_equal(args.databasePath, DEFAULT_DB_PATH);
}

static void test_verbose_flag(void **state) {
  (void)state;
  char *argv[] = {"server", "-v", NULL};
  struct ParsedArgs args;
  assert_int_equal(parse(&args, dynamic_argc, argv), 0);
  assert_true(args.verbose);
}

static void test_port_and_log(void **state) {
  (void)state;
  char *argv[] = {"server", "-p", "9000", "-l", "out.log", NULL};
  struct ParsedArgs args;
  assert_int_equal(parse(&args, dynamic_argc, argv), 0);
  assert_int_equal(args.portNumber, 9000);
  assert_string_equal(args.logFile, "out.log");
}

static void test_database_path(void **state) {
  (void)state;
  char *argv[] = {"server", "-d", "new.db", NULL};
  struct ParsedArgs args;
  assert_int_equal(parse(&args, dynamic_argc, argv), 0);
  assert_string_equal(args.databasePath, "new.db");
}

static void test_invalid_port(void **state) {
  (void)state;
  char *argv[] = {"server", "-p", "99999", NULL};
  struct ParsedArgs args;
  assert_int_equal(parse(&args, dynamic_argc, argv), EXIT_FAILURE);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_defaults),     cmocka_unit_test(test_verbose_flag),
      cmocka_unit_test(test_port_and_log), cmocka_unit_test(test_database_path),
      cmocka_unit_test(test_invalid_port),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
