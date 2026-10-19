#include <server/user_store.h>

// clang-format off
#include <cmocka.h>
#include <setjmp.h> // IWYU pragma: keep
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
// clang-format on

static void test_open_user_store_memory(void **state) {
  (void)state;
  user_store_t *store = user_store_open(":memory:");
  assert_non_null(store);
  user_store_close(store);
}

static void test_close_user_store_null_safe(void **state) {
  (void)state;
  user_store_close(NULL);
}

static void test_unknown_db_returns_null(void **state) {
  (void)state;
  user_store_t *store = user_store_open("/nonexistent/dir/unknown.db");
  assert_null(store);
  user_store_close(store);
}

// Setup/Teardown functions for later:
//
// static int setup_init_memory_db(void **state) {
//   *state = user_store_open(":memory:");
//   return *state ? 0 : 1;
// }

// static int teardown_free_memory_db(void **state) {
//   user_store_close(*state);
//   return 0;
// }

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_open_user_store_memory),
      cmocka_unit_test(test_close_user_store_null_safe),
      cmocka_unit_test(test_unknown_db_returns_null),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
