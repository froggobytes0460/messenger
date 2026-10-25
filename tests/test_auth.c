#include <errno.h>
#include <server/auth.h>
#include <server/constants.h>
#include <server/user_store.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

// clang-format off
#include <cmocka.h>
#include <setjmp.h> // IWYU pragma: keep
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
// clang-format on

#define RECOGNIZABLE_UNINIT_VAL 0xAB
#define CAROL_ID 42   ///< Client id used where the test checks the echoed id
#define GHOST_ID 43   ///< Client id used for the failed login
#define HASH_BUF 256  ///< Room for any stored hash (argon2id strings are 97).

/// Builds a distinct, deterministic UUID from an integer.
static void uuid_from_int(client_uuid_t out, uint32_t n) {
  memset(out, 0, sizeof(client_uuid_t));
  memcpy(out, &n, sizeof(n));
}

static auth_req_t make_req(auth_kind_t kind, const char* user, uint32_t id) {
  auth_req_t req;
  memset(&req, 0, sizeof(req));
  req.kind = kind;
  uuid_from_int(req.client_id, id);
  strncpy(req.username, user, MAX_USERNAME);
  strncpy(req.password, "pw", MAX_PASSWORD);
  return req;
}

/// Submits a request and collects its single result.
static auth_result_t run(auth_kind_t kind, const char* user, uint32_t id) {
  auth_req_t req = make_req(kind, user, id);
  assert_true(auth_submit(&req));
  auth_result_t res;
  assert_true(auth_collect(&res));
  return res;
}

/// Like @ref run, but with an explicit password instead of the default "pw".
static auth_result_t run_pw(auth_kind_t kind, const char* user,
                            const char* pass, uint32_t id) {
  auth_req_t req = make_req(kind, user, id);
  memset(req.password, 0, sizeof(req.password));
  strncpy(req.password, pass, MAX_PASSWORD);
  assert_true(auth_submit(&req));
  auth_result_t res;
  assert_true(auth_collect(&res));
  return res;
}

static void test_register_then_login(void** state) {
  (void)state;
  assert_int_equal(run(AUTH_REGISTER, "alice", 1).outcome, AUTH_OK);
  assert_int_equal(run(AUTH_LOGIN, "alice", 1).outcome, AUTH_OK);
}

static void test_register_twice(void** state) {
  (void)state;
  assert_int_equal(run(AUTH_REGISTER, "bob", 1).outcome, AUTH_OK);
  assert_int_equal(run(AUTH_REGISTER, "bob", 2).outcome, AUTH_NAME_TAKEN);
}

static void test_login_unknown_user(void** state) {
  (void)state;
  assert_int_equal(run(AUTH_LOGIN, "nobody", 1).outcome, AUTH_BAD_CREDENTIALS);
}

static void test_login_other_user_than_registered(void** state) {
  (void)state;
  assert_int_equal(run(AUTH_REGISTER, "alice", 1).outcome, AUTH_OK);
  assert_int_equal(run(AUTH_LOGIN, "bob", 2).outcome, AUTH_BAD_CREDENTIALS);
}

static void test_login_correct_password(void** state) {
  (void)state;
  assert_int_equal(run_pw(AUTH_REGISTER, "alice", "hunter2", 1).outcome,
                   AUTH_OK);
  assert_int_equal(run_pw(AUTH_LOGIN, "alice", "hunter2", 2).outcome, AUTH_OK);
}

static void test_login_wrong_password(void** state) {
  (void)state;
  assert_int_equal(run_pw(AUTH_REGISTER, "alice", "hunter2", 1).outcome,
                   AUTH_OK);
  const char* wrong[] = {"hunter3", "Hunter2", "hunter", "hunter22", "x"};
  for (size_t i = 0; i < sizeof(wrong) / sizeof(wrong[0]); i++) {
    print_message("wrong password: %s\n", wrong[i]);
    assert_int_equal(run_pw(AUTH_LOGIN, "alice", wrong[i], 2).outcome,
                     AUTH_BAD_CREDENTIALS);
  }
  // The failed attempts must not have changed the account.
  assert_int_equal(run_pw(AUTH_LOGIN, "alice", "hunter2", 3).outcome, AUTH_OK);
}

static void test_wrong_password_matches_unknown_user(void** state) {
  (void)state;
  assert_int_equal(run_pw(AUTH_REGISTER, "alice", "hunter2", 1).outcome,
                   AUTH_OK);
  // The client must not be able to tell the two failures apart.
  assert_int_equal(run_pw(AUTH_LOGIN, "alice", "nope", 2).outcome,
                   run_pw(AUTH_LOGIN, "ghost", "nope", 3).outcome);
}

static void test_stored_value_is_a_hash(void** state) {
  user_store_t* store = *state;
  const char* password = "hunter2";
  assert_int_equal(run_pw(AUTH_REGISTER, "alice", password, 1).outcome,
                   AUTH_OK);
  char hash[HASH_BUF];
  assert_int_equal(user_store_get_hash(store, "alice", hash, sizeof(hash)),
                   DB_OK);
  assert_int_equal(strncmp(hash, "$argon2", strlen("$argon2")), 0);
  assert_null(strstr(hash, password));
}

/// The one test that pays for the production cost (two hashes).
static void test_default_cost_is_the_production_setting(void** state) {
  user_store_t* store = *state;
  auth_set_hash_cost(AUTH_HASH_COST_NORMAL);
  auth_init(store);  // rebuild the dummy hash with the production cost
  assert_int_equal(run_pw(AUTH_REGISTER, "alice", "hunter2", 1).outcome,
                   AUTH_OK);
  char hash[HASH_BUF];
  assert_int_equal(user_store_get_hash(store, "alice", hash, sizeof(hash)),
                   DB_OK);
  assert_int_equal(strncmp(hash, "$argon2id$", strlen("$argon2id$")), 0);
  assert_non_null(strstr(hash, "m=65536,"));  // 64 MiB, the INTERACTIVE limit
  assert_int_equal(run_pw(AUTH_LOGIN, "alice", "hunter2", 2).outcome, AUTH_OK);
}

/// Hashes made at the fast cost still verify after the cost changes back.
static void test_hash_verifies_after_cost_change(void** state) {
  (void)state;
  assert_int_equal(run_pw(AUTH_REGISTER, "alice", "hunter2", 1).outcome,
                   AUTH_OK);  // fast hash
  auth_set_hash_cost(AUTH_HASH_COST_NORMAL);
  assert_int_equal(run_pw(AUTH_LOGIN, "alice", "hunter2", 2).outcome, AUTH_OK);
  assert_int_equal(run_pw(AUTH_LOGIN, "alice", "wrong", 3).outcome,
                   AUTH_BAD_CREDENTIALS);
}

static void test_same_password_gets_different_hashes(void** state) {
  user_store_t* store = *state;
  assert_int_equal(run_pw(AUTH_REGISTER, "alice", "samepw", 1).outcome,
                   AUTH_OK);
  assert_int_equal(run_pw(AUTH_REGISTER, "bob", "samepw", 2).outcome, AUTH_OK);
  char hash_a[HASH_BUF];
  char hash_b[HASH_BUF];
  assert_int_equal(user_store_get_hash(store, "alice", hash_a, sizeof(hash_a)),
                   DB_OK);
  assert_int_equal(user_store_get_hash(store, "bob", hash_b, sizeof(hash_b)),
                   DB_OK);
  assert_string_not_equal(hash_a, hash_b);  // each hash has its own salt
}

static void test_max_length_password(void** state) {
  (void)state;
  char pass[MAX_PASSWORD + 1];
  memset(pass, 'p', MAX_PASSWORD);
  pass[MAX_PASSWORD] = '\0';
  assert_int_equal(run_pw(AUTH_REGISTER, "alice", pass, 1).outcome, AUTH_OK);
  assert_int_equal(run_pw(AUTH_LOGIN, "alice", pass, 2).outcome, AUTH_OK);
  // Every byte counts, including the last one.
  pass[MAX_PASSWORD - 1] = 'q';
  assert_int_equal(run_pw(AUTH_LOGIN, "alice", pass, 3).outcome,
                   AUTH_BAD_CREDENTIALS);
}

static void test_no_store_is_internal_error(void** state) {
  (void)state;
  auth_init(NULL);  // teardown still closes the real store
  assert_int_equal(run(AUTH_REGISTER, "alice", 1).outcome, AUTH_INTERNAL);
  auth_result_t res = run(AUTH_LOGIN, "alice", CAROL_ID);
  assert_int_equal(res.outcome, AUTH_INTERNAL);
  // Even a failed request is addressed to its client.
  client_uuid_t want;
  uuid_from_int(want, CAROL_ID);
  assert_memory_equal(res.client_id, want, sizeof(client_uuid_t));
  assert_string_equal(res.username, "alice");
}

static void test_max_length_username(void** state) {
  (void)state;
  char name[MAX_USERNAME + 1];
  memset(name, 'a', MAX_USERNAME);
  name[MAX_USERNAME] = '\0';
  assert_int_equal(run(AUTH_REGISTER, name, 1).outcome, AUTH_OK);
  auth_result_t res = run(AUTH_LOGIN, name, 2);
  assert_int_equal(res.outcome, AUTH_OK);
  assert_string_equal(res.username, name);
}

static void test_unterminated_fields_rejected(void** state) {
  (void)state;
  auth_result_t res;

  auth_req_t req = make_req(AUTH_REGISTER, "alice", 1);
  memset(req.username, 'a', sizeof(req.username));  // no NUL
  assert_false(auth_submit(&req));

  req = make_req(AUTH_REGISTER, "alice", 1);
  memset(req.password, 'p', sizeof(req.password));  // no NUL
  assert_false(auth_submit(&req));

  // Rejected requests leave nothing behind and register nobody.
  assert_false(auth_collect(&res));
  assert_int_equal(run(AUTH_LOGIN, "alice", 2).outcome, AUTH_BAD_CREDENTIALS);
}

static void test_null_request_rejected(void** state) {
  (void)state;
  assert_false(auth_submit(NULL));
  auth_result_t res;
  assert_false(auth_collect(&res));
}

/// The ring wraps while results are still queued and stays in order.
static void test_ring_wraps_with_queued_results(void** state) {
  (void)state;
  enum { TAKEN = 10 };
  auth_req_t req = make_req(AUTH_LOGIN, "nobody", 0);
  auth_result_t res;

  for (uint32_t i = 0; i < AUTH_QUEUE_CAP; i++) {
    uuid_from_int(req.client_id, i);
    assert_true(auth_submit(&req));
  }
  for (uint32_t i = 0; i < TAKEN; i++) {
    assert_true(auth_collect(&res));
  }
  for (uint32_t i = 0; i < TAKEN; i++) {  // refill the freed slots
    uuid_from_int(req.client_id, AUTH_QUEUE_CAP + i);
    assert_true(auth_submit(&req));
  }
  assert_false(auth_submit(&req));  // full again

  for (uint32_t i = TAKEN; i < AUTH_QUEUE_CAP + TAKEN; i++) {
    client_uuid_t want;
    uuid_from_int(want, i);
    assert_true(auth_collect(&res));
    assert_memory_equal(res.client_id, want, sizeof(client_uuid_t));
  }
  assert_false(auth_collect(&res));
}

static void test_result_echoes_request(void** state) {
  (void)state;
  client_uuid_t want;

  auth_result_t res = run(AUTH_REGISTER, "carol", CAROL_ID);
  uuid_from_int(want, CAROL_ID);
  assert_true(memcmp(res.client_id, want, sizeof(client_uuid_t)) == 0);
  assert_string_equal(res.username, "carol");

  // Failed results are addressed too.
  res = run(AUTH_LOGIN, "ghost", GHOST_ID);
  uuid_from_int(want, GHOST_ID);
  assert_int_equal(res.outcome, AUTH_BAD_CREDENTIALS);
  assert_true(memcmp(res.client_id, want, sizeof(client_uuid_t)) == 0);
  assert_string_equal(res.username, "ghost");
}

static void test_collect_empty_leaves_out_untouched(void** state) {
  (void)state;
  auth_result_t res;
  memset(&res, RECOGNIZABLE_UNINIT_VAL, sizeof(res));
  auth_result_t before = res;
  assert_false(auth_collect(&res));
  assert_memory_equal(&res, &before, sizeof(res));
}

/// The wake fd exists, is non-blocking, and keeps the same number across
/// re-inits (the event loop holds on to it).
static void test_wake_fd_is_stable_and_non_blocking(void** state) {
  (void)state;
  int fd = auth_wake_fd();
  assert_true(fd >= 0);
  uint64_t counter = 0;
  assert_int_equal(read(fd, &counter, sizeof(counter)), -1);
  assert_int_equal(errno, EAGAIN);
  auth_init(NULL);
  assert_int_equal(auth_wake_fd(), fd);
}

static void test_queue_full_and_order(void** state) {
  (void)state;
  // One extra UUID for the request that overflows the queue.
  client_uuid_t uuids[AUTH_QUEUE_CAP + 1];
  for (int i = 0; i <= AUTH_QUEUE_CAP; i++) {
    uuid_from_int(uuids[i], (uint32_t)i);
  }

  auth_req_t req = make_req(AUTH_LOGIN, "nobody", 0);
  for (int i = 0; i < AUTH_QUEUE_CAP; i++) {
    memcpy(req.client_id, uuids[i], sizeof(req.client_id));
    assert_true(auth_submit(&req));
  }
  memcpy(req.client_id, uuids[AUTH_QUEUE_CAP], sizeof(req.client_id));
  assert_false(auth_submit(&req));  // full: rejected, no result will follow

  // Results come back oldest first.
  auth_result_t res;
  for (int i = 0; i < AUTH_QUEUE_CAP; i++) {
    assert_true(auth_collect(&res));
    assert_true(memcmp(res.client_id, uuids[i], sizeof(client_uuid_t)) == 0);
  }
  assert_false(auth_collect(&res));

  // The ring wraps and is usable again.
  assert_true(auth_submit(&req));
  assert_true(auth_collect(&res));
  assert_true(
      memcmp(res.client_id, uuids[AUTH_QUEUE_CAP], sizeof(client_uuid_t)) == 0);
}

static int setup_tests(void** state) {
  *state = user_store_open(":memory:");
  if (*state == NULL) {
    return 1;
  }
  // Cheap hashes: these tests check logic, not password strength. Must come
  // before auth_init, which builds the dummy hash.
  auth_set_hash_cost(AUTH_HASH_COST_FAST);
  auth_init(*state);
  return 0;
}

static int teardown_tests(void** state) {
  auth_init(NULL);  // Refresh the authentication user store
  user_store_close(*state);
  return 0;
}

#define TEST(fn) \
  cmocka_unit_test_setup_teardown(fn, setup_tests, teardown_tests)

int main(void) {
  const struct CMUnitTest unit_tests[] = {
      TEST(test_register_then_login),
      TEST(test_register_twice),
      TEST(test_login_unknown_user),
      TEST(test_login_other_user_than_registered),
      TEST(test_login_correct_password),
      TEST(test_login_wrong_password),
      TEST(test_wrong_password_matches_unknown_user),
      TEST(test_stored_value_is_a_hash),
      TEST(test_default_cost_is_the_production_setting),
      TEST(test_hash_verifies_after_cost_change),
      TEST(test_same_password_gets_different_hashes),
      TEST(test_max_length_password),
      TEST(test_no_store_is_internal_error),
      TEST(test_max_length_username),
      TEST(test_unterminated_fields_rejected),
      TEST(test_null_request_rejected),
      TEST(test_ring_wraps_with_queued_results),
      TEST(test_result_echoes_request),
      TEST(test_collect_empty_leaves_out_untouched),
      TEST(test_wake_fd_is_stable_and_non_blocking),
      TEST(test_queue_full_and_order),
  };

  return cmocka_run_group_tests(unit_tests, NULL, NULL);
}
