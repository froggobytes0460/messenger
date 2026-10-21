#include <server/auth.h>
#include <server/constants.h>
#include <server/log.h>
#include <server/user_store.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define LOG_TAG "auth"

static user_store_t* g_user_store;

/// The global-list of results queued for authentication.
static auth_result_t results[AUTH_QUEUE_CAP];

static size_t results_head = 0;   ///< Oldest slot; next slot to pop
static size_t results_tail = 0;   ///< Next slot to write
static size_t results_count = 0;  ///< results currently stored

static void push_result(const auth_result_t* result) {
  memcpy(&results[results_tail], result, sizeof(*result));
  results_count++;
  results_tail = (results_tail + 1) % AUTH_QUEUE_CAP;
}

void auth_init(user_store_t* store) { g_user_store = store; }

bool auth_collect(auth_result_t* out) {
  if (results_count == 0) {
    return false;
  }
  memcpy(out, &results[results_head], sizeof(results[results_head]));
  results_head = (results_head + 1) % AUTH_QUEUE_CAP;
  results_count--;
  return true;
}

bool auth_submit(const auth_req_t* req) {
  if (results_count == AUTH_QUEUE_CAP) {
    return false;
  }

  auth_result_t result;
  memset(&result, 0, sizeof(result));
  memcpy(result.client_id, req->client_id, sizeof(result.client_id));

  int len =
      snprintf(result.username, sizeof(result.username), "%s", req->username);
  if (len < 0 || len >= (int)sizeof(result.username)) {
    // Rejected by design: the parser caps names, so this should never fire.
    return false;
  }

  switch (req->kind) {
    case AUTH_REGISTER: {
      switch (user_store_create(g_user_store, req->username, req->password)) {
        case DB_EXISTS:
          result.outcome = AUTH_NAME_TAKEN;
          break;
        case DB_OK:
          result.outcome = AUTH_OK;
          break;
        case DB_ERROR:
          result.outcome = AUTH_INTERNAL;
          break;
        case DB_NOT_FOUND:
          result.outcome = AUTH_INTERNAL;
          // Cannot reach but clang-tidy forces all branches on switch-case
          // statements
          log_debug(
              "reaching `DB_NOT_FOUND` branch in `user_store_create` is "
              "logically impossible");
          break;
      }
      break;
    }
    case AUTH_LOGIN: {
      char pass_hash[USER_HASH_MAX_SIZE];
      switch (user_store_get_hash(g_user_store, req->username, pass_hash,
                                  sizeof(pass_hash))) {
        case DB_NOT_FOUND:
          result.outcome = AUTH_BAD_CREDENTIALS;
          break;
        case DB_OK:
          result.outcome = AUTH_OK;
          break;
        case DB_ERROR:
          result.outcome = AUTH_INTERNAL;
          break;
        case DB_EXISTS:
          result.outcome = AUTH_INTERNAL;
          // Cannot reach but clang-tidy forces all branches on switch-case
          // statements
          log_debug(
              "reaching `DB_EXISTS` branch in `user_store_get_hash` is "
              "logically impossible");
          break;
      }
      break;
    }
    default:
      result.outcome = AUTH_INTERNAL;
      break;
  }

  push_result(&result);
  return true;
}
