#include <server/auth.h>
#include <server/constants.h>
#include <server/user_store.h>
#include <stdbool.h>
#include <string.h>

// clang-format off
#include <cmocka.h>
#include <setjmp.h> // IWYU pragma: keep
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
// clang-format on

#define RECOGNIZABLE_UNINIT_VAL 0xAB
#define CAROL_ID 42  ///< Client id used for the successful registration
#define GHOST_ID 43  ///< Client id used for the failed login

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

static void test_no_store_is_internal_error(void** state) {
  (void)state;
  auth_init(NULL);  // teardown still closes the real store
  assert_int_equal(run(AUTH_REGISTER, "alice", 1).outcome, AUTH_INTERNAL);
  assert_int_equal(run(AUTH_LOGIN, "alice", 1).outcome, AUTH_INTERNAL);
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
      TEST(test_no_store_is_internal_error),
      TEST(test_result_echoes_request),
      TEST(test_collect_empty_leaves_out_untouched),
      TEST(test_queue_full_and_order),
  };

  return cmocka_run_group_tests(unit_tests, NULL, NULL);
}
