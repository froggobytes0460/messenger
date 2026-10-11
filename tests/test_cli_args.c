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
}

static void test_verbose_flag(void **state) {
  (void)state;
  char *argv[] = {"server", "-v", NULL};
  struct ParsedArgs args;
  assert_int_equal(parse(&args, 2, argv), 0);
  assert_true(args.verbose);
}

static void test_port_and_log(void **state) {
  (void)state;
  char *argv[] = {"server", "-p", "9000", "-l", "out.log", NULL};
  struct ParsedArgs args;
  assert_int_equal(parse(&args, 5, argv), 0);
  assert_int_equal(args.portNumber, 9000);
  assert_string_equal(args.logFile, "out.log");
}

static void test_invalid_port(void **state) {
  (void)state;
  char *argv[] = {"server", "-p", "99999", NULL};
  struct ParsedArgs args;
  assert_int_equal(parse(&args, 3, argv), EXIT_FAILURE);
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_defaults),
      cmocka_unit_test(test_verbose_flag),
      cmocka_unit_test(test_port_and_log),
      cmocka_unit_test(test_invalid_port),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
