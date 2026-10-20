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
  user_store_t *second_store_instance = user_store_open(":memory:");
  assert_non_null(second_store_instance);
  user_store_close(second_store_instance);
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

static void test_get_hash_unknown_user(void **state) {
  user_store_t *store = *state;
  char hash[USER_HASH_MAX_SIZE];
  assert_int_equal(user_store_get_hash(store, "nobody", hash, sizeof(hash)),
                   DB_NOT_FOUND);
}

static void test_get_hash_buffer_too_small(void **state) {
  user_store_t *store = *state;
  assert_int_equal(user_store_create(store, "alice", "hash1"), DB_OK);
  // "hash1" needs 6 bytes with the NUL; 5 must fail rather than truncate.
  char exact[5];
  assert_int_equal(user_store_get_hash(store, "alice", exact, sizeof(exact)),
                   DB_ERROR);
  char tiny[3];
  assert_int_equal(user_store_get_hash(store, "alice", tiny, sizeof(tiny)),
                   DB_ERROR);
  char fits[6];
  assert_int_equal(user_store_get_hash(store, "alice", fits, sizeof(fits)),
                   DB_OK);
  assert_string_equal(fits, "hash1");
}

static void test_null_arguments(void **state) {
  user_store_t *store = *state;
  char hash[USER_HASH_MAX_SIZE];
  assert_int_equal(user_store_create(NULL, "alice", "h"), DB_ERROR);
  assert_int_equal(user_store_create(store, NULL, "h"), DB_ERROR);
  assert_int_equal(user_store_create(store, "alice", NULL), DB_ERROR);
  assert_int_equal(user_store_get_hash(NULL, "alice", hash, sizeof(hash)),
                   DB_ERROR);
  assert_int_equal(user_store_get_hash(store, NULL, hash, sizeof(hash)),
                   DB_ERROR);
  assert_int_equal(user_store_get_hash(store, "alice", NULL, sizeof(hash)),
                   DB_ERROR);
  assert_int_equal(user_store_get_hash(store, "alice", hash, 0), DB_ERROR);
  // Nothing above may have created a user.
  assert_int_equal(user_store_get_hash(store, "alice", hash, sizeof(hash)),
                   DB_NOT_FOUND);
}

static void test_users_keep_separate_hashes(void **state) {
  user_store_t *store = *state;
  assert_int_equal(user_store_create(store, "alice", "hashA"), DB_OK);
  assert_int_equal(user_store_create(store, "bob", "hashB"), DB_OK);
  char hash[USER_HASH_MAX_SIZE];
  assert_int_equal(user_store_get_hash(store, "alice", hash, sizeof(hash)),
                   DB_OK);
  assert_string_equal(hash, "hashA");
  assert_int_equal(user_store_get_hash(store, "bob", hash, sizeof(hash)),
                   DB_OK);
  assert_string_equal(hash, "hashB");
}

static void test_injection_like_username(void **state) {
  user_store_t *store = *state;
  const char *evil = "x'); DROP TABLE users;--";
  assert_int_equal(user_store_create(store, "alice", "hash1"), DB_OK);
  assert_int_equal(user_store_create(store, evil, "hashX"), DB_OK);
  char hash[USER_HASH_MAX_SIZE];
  assert_int_equal(user_store_get_hash(store, evil, hash, sizeof(hash)), DB_OK);
  assert_string_equal(hash, "hashX");
  // The table and the other user must be intact.
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
      cmocka_unit_test_setup_teardown(test_get_hash_unknown_user,
                                      setup_init_memory_db,
                                      teardown_free_memory_db),
      cmocka_unit_test_setup_teardown(test_get_hash_buffer_too_small,
                                      setup_init_memory_db,
                                      teardown_free_memory_db),
      cmocka_unit_test_setup_teardown(test_null_arguments, setup_init_memory_db,
                                      teardown_free_memory_db),
      cmocka_unit_test_setup_teardown(test_users_keep_separate_hashes,
                                      setup_init_memory_db,
                                      teardown_free_memory_db),
      cmocka_unit_test_setup_teardown(test_injection_like_username,
                                      setup_init_memory_db,
                                      teardown_free_memory_db),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
