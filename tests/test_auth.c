#include <server/auth.h>
#include <server/constants.h>
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

// The user table and result queue are file-level statics in auth.c, so state
// persists between tests in this binary. Every test uses its own usernames.

static auth_req_t make_req(auth_kind_t kind, const char *user, int id,
                           unsigned gen) {
  auth_req_t req;
  memset(&req, 0, sizeof(req));
  req.kind = kind;
  req.client_id = id;
  req.client_generation = gen;
  strncpy(req.username, user, MAX_USERNAME);
  strncpy(req.password, "pw", MAX_PASSWORD);
  return req;
}

/// Submits a request and collects its single result.
static auth_result_t run(auth_kind_t kind, const char *user, int id,
                         unsigned gen) {
  auth_req_t req = make_req(kind, user, id, gen);
  assert_true(auth_submit(&req));
  auth_result_t res;
  assert_true(auth_collect(&res));
  return res;
}

static void test_register_then_login(void **state) {
  (void)state;
  assert_int_equal(run(AUTH_REGISTER, "alice", 1, 1).outcome, AUTH_OK);
  assert_int_equal(run(AUTH_LOGIN, "alice", 1, 2).outcome, AUTH_OK);
}

static void test_register_twice(void **state) {
  (void)state;
  assert_int_equal(run(AUTH_REGISTER, "bob", 1, 1).outcome, AUTH_OK);
  assert_int_equal(run(AUTH_REGISTER, "bob", 2, 2).outcome, AUTH_NAME_TAKEN);
}

static void test_login_unknown_user(void **state) {
  (void)state;
  assert_int_equal(run(AUTH_LOGIN, "nobody", 1, 1).outcome,
                   AUTH_BAD_CREDENTIALS);
}

static void test_result_echoes_request(void **state) {
  (void)state;
  auth_result_t res = run(AUTH_REGISTER, "carol", 42, 7);
  assert_int_equal(res.client_id, 42);
  assert_int_equal(res.client_generation, 7);
  assert_string_equal(res.username, "carol");

  // Failed results are addressed too.
  res = run(AUTH_LOGIN, "ghost", 43, 8);
  assert_int_equal(res.outcome, AUTH_BAD_CREDENTIALS);
  assert_int_equal(res.client_id, 43);
  assert_int_equal(res.client_generation, 8);
  assert_string_equal(res.username, "ghost");
}

static void test_collect_empty_leaves_out_untouched(void **state) {
  (void)state;
  auth_result_t res;
  memset(&res, RECOGNIZABLE_UNINIT_VAL, sizeof(res));
  auth_result_t before = res;
  assert_false(auth_collect(&res));
  assert_memory_equal(&res, &before, sizeof(res));
}

static void test_queue_full_and_order(void **state) {
  (void)state;
  auth_req_t req = make_req(AUTH_LOGIN, "nobody", 0, 0);
  for (int i = 0; i < AUTH_QUEUE_CAP; i++) {
    req.client_id = i;
    assert_true(auth_submit(&req));
  }
  req.client_id = AUTH_QUEUE_CAP;
  assert_false(auth_submit(&req)); // full: rejected, no result will follow

  // Results come back oldest first.
  auth_result_t res;
  for (int i = 0; i < AUTH_QUEUE_CAP; i++) {
    assert_true(auth_collect(&res));
    assert_int_equal(res.client_id, i);
  }
  assert_false(auth_collect(&res));

  // The ring wraps and is usable again.
  assert_true(auth_submit(&req));
  assert_true(auth_collect(&res));
  assert_int_equal(res.client_id, AUTH_QUEUE_CAP);
}

int main(void) {
  const struct CMUnitTest unit_tests[] = {
      cmocka_unit_test(test_register_then_login),
      cmocka_unit_test(test_register_twice),
      cmocka_unit_test(test_login_unknown_user),
      cmocka_unit_test(test_result_echoes_request),
      cmocka_unit_test(test_collect_empty_leaves_out_untouched),
      cmocka_unit_test(test_queue_full_and_order),
  };

  return cmocka_run_group_tests(unit_tests, NULL, NULL);
}
