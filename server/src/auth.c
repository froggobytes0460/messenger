#include <server/auth.h>
#include <server/constants.h>
#include <server/log.h>
#include <server/user_store.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define LOG_TAG "auth"

static user_store_t* g_user_store;  ///< Borrowed; the caller closes it.

/// Requests accepted by @ref auth_submit and not yet processed (FIFO ring).
static auth_req_t requests[AUTH_QUEUE_CAP];
static size_t requests_head = 0;   ///< Oldest slot; next slot to process
static size_t requests_tail = 0;   ///< Next slot to write
static size_t requests_count = 0;  ///< Requests currently queued

/// Finished results not yet taken by @ref auth_collect (FIFO ring).
static auth_result_t results[AUTH_QUEUE_CAP];
static size_t results_head = 0;   ///< Oldest slot; next slot to pop
static size_t results_tail = 0;   ///< Next slot to write
static size_t results_count = 0;  ///< Results currently stored

static void push_request(const auth_req_t* req) {
  memcpy(&requests[requests_tail], req, sizeof(*req));
  requests_count++;
  requests_tail = (requests_tail + 1) % AUTH_QUEUE_CAP;
}

/// Moves the oldest request into @p out and wipes its slot (it holds a
/// password). The caller must check that the queue is not empty.
static void pop_request(auth_req_t* out) {
  memcpy(out, &requests[requests_head], sizeof(*out));
  memset(&requests[requests_head], 0, sizeof(requests[requests_head]));
  requests_head = (requests_head + 1) % AUTH_QUEUE_CAP;
  requests_count--;
}

static void push_result(const auth_result_t* result) {
  memcpy(&results[results_tail], result, sizeof(*result));
  results_count++;
  results_tail = (results_tail + 1) % AUTH_QUEUE_CAP;
}

static auth_outcome_t register_user(const auth_req_t* req) {
  switch (user_store_create(g_user_store, req->username, req->password)) {
    case DB_OK:
      return AUTH_OK;
    case DB_EXISTS:
      return AUTH_NAME_TAKEN;
    case DB_ERROR:
      return AUTH_INTERNAL;
    case DB_NOT_FOUND:
      // Cannot reach, but clang-tidy requires every enum value in a switch.
      log_debug("DB_NOT_FOUND from `user_store_create` is impossible");
      return AUTH_INTERNAL;
  }
  return AUTH_INTERNAL;
}

static auth_outcome_t login_user(const auth_req_t* req) {
  char pass_hash[USER_HASH_MAX_SIZE];
  switch (user_store_get_hash(g_user_store, req->username, pass_hash,
                              sizeof(pass_hash))) {
    case DB_OK:
      return AUTH_OK;
    case DB_NOT_FOUND:
      return AUTH_BAD_CREDENTIALS;
    case DB_ERROR:
      return AUTH_INTERNAL;
    case DB_EXISTS:
      // Cannot reach, but clang-tidy requires every enum value in a switch.
      log_debug("DB_EXISTS from `user_store_get_hash` is impossible");
      return AUTH_INTERNAL;
  }
  return AUTH_INTERNAL;
}

static void process_request(const auth_req_t* req, auth_result_t* out) {
  memset(out, 0, sizeof(*out));
  memcpy(out->client_id, req->client_id, sizeof(out->client_id));
  memcpy(out->username, req->username, sizeof(out->username));

  switch (req->kind) {
    case AUTH_REGISTER:
      out->outcome = register_user(req);
      break;
    case AUTH_LOGIN:
      out->outcome = login_user(req);
      break;
    default:
      out->outcome = AUTH_INTERNAL;
      break;
  }
}

static void run_pending(void) {
  while (requests_count > 0) {
    auth_req_t req;
    auth_result_t result;
    pop_request(&req);
    process_request(&req, &result);
    memset(&req, 0, sizeof(req));  // holds a password
    push_result(&result);
  }
}

void auth_init(user_store_t* store) {
  g_user_store = store;
  requests_head = requests_tail = requests_count = 0;
  results_head = results_tail = results_count = 0;
}

bool auth_submit(const auth_req_t* req) {
  // A request keeps its place until its result is collected, so every
  // accepted request is guaranteed room for its result.
  if (requests_count + results_count >= AUTH_QUEUE_CAP) {
    return false;
  }

  // Rejected by design: the parser caps fields, so this should never fire.
  if (memchr(req->username, '\0', sizeof(req->username)) == NULL ||
      memchr(req->password, '\0', sizeof(req->password)) == NULL) {
    return false;
  }

  push_request(req);
  run_pending();  // Runs inline and synchronous today; will be asynchronous
                  // later
  return true;
}

bool auth_collect(auth_result_t* out) {
  if (results_count == 0) {
    return false;
  }
  memcpy(out, &results[results_head], sizeof(results[results_head]));
  results_head = (results_head + 1) % AUTH_QUEUE_CAP;
  results_count--;
  return true;
}
