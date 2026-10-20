#include <server/constants.h>
#include <server/user_store.h>

// clang-format off
#include <cmocka.h>
#include <setjmp.h> // IWYU pragma: keep
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
// clang-format on

static int setup_init_memory_db(void **state) {
  *state = user_store_open(":memory:");
  return *state ? 0 : 1;
}

static int teardown_free_memory_db(void **state) {
  user_store_close(*state);
  return 0;
}

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
}

static void test_double_db_open(void **state) {
  (void)state;
  // Store opened once.
  user_store_close(user_store_open(":memory:"));
  user_store_t *second_store_instace = user_store_open(":memory:");
  assert_non_null(second_store_instace);
  user_store_close(second_store_instace);
}

static void test_create_user_round_trip(void **state) {
  user_store_t *store = *state;
  assert_int_equal(user_store_create(store, "alice", "hash1"), DB_OK);
  char hash[USER_HASH_MAX_SIZE];
  assert_int_equal(user_store_get_hash(store, "alice", hash, sizeof(hash)),
                   DB_OK);
  assert_string_equal(hash, "hash1");
}

static void test_duplicate_create_user(void **state) {
  user_store_t *store = *state;
  assert_int_equal(user_store_create(store, "alice", "hash1"), DB_OK);
  assert_int_equal(user_store_create(store, "alice", "hash2"), DB_EXISTS);
  char hash[USER_HASH_MAX_SIZE];
  assert_int_equal(user_store_get_hash(store, "alice", hash, sizeof(hash)),
                   DB_OK);
  assert_string_equal(hash, "hash1");
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_open_user_store_memory),
      cmocka_unit_test(test_close_user_store_null_safe),
      cmocka_unit_test(test_unknown_db_returns_null),
      cmocka_unit_test(test_double_db_open),
      cmocka_unit_test_setup_teardown(test_create_user_round_trip,
                                      setup_init_memory_db,
                                      teardown_free_memory_db),
      cmocka_unit_test_setup_teardown(test_duplicate_create_user,
                                      setup_init_memory_db,
                                      teardown_free_memory_db),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
