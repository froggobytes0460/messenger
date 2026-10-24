#include <pthread.h>
#include <server/auth.h>
#include <server/constants.h>
#include <server/log.h>
#include <server/user_store.h>
#include <sodium/crypto_pwhash.h>
#include <sodium/utils.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define LOG_TAG "auth"

/// Valid hash verified against for unknown users, so a failed login takes as
/// long whether or not the name exists. Rebuilt by @ref auth_init.
static char dummy_hash[crypto_pwhash_STRBYTES];

/// Argon2 cost for new hashes; see @ref auth_set_hash_cost.
static unsigned long long hash_ops = crypto_pwhash_OPSLIMIT_INTERACTIVE;
static size_t hash_mem = crypto_pwhash_MEMLIMIT_INTERACTIVE;

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

/// Guards the rings and counters below.
// NOLINTNEXTLINE(misc-include-cleaner)
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static size_t in_flight = 0;  ///< Count of requests popped but not pushed yet.

/// @note Caller must hold lock.
static void push_request(const auth_req_t* req) {
  memcpy(&requests[requests_tail], req, sizeof(*req));
  requests_count++;
  requests_tail = (requests_tail + 1) % AUTH_QUEUE_CAP;
}

/// Moves the oldest request into @p out and wipes its slot (it holds a
/// password). The caller must check that the queue is not empty.
/// @note Caller must hold lock.
static void pop_request(auth_req_t* out) {
  memcpy(out, &requests[requests_head], sizeof(*out));
  auth_wipe(&requests[requests_head], sizeof(requests[requests_head]));
  requests_head = (requests_head + 1) % AUTH_QUEUE_CAP;
  requests_count--;
  in_flight++;
}

/// @note Caller must hold lock.
static void push_result(const auth_result_t* result) {
  memcpy(&results[results_tail], result, sizeof(*result));
  results_count++;
  in_flight--;
  results_tail = (results_tail + 1) % AUTH_QUEUE_CAP;
}

static auth_outcome_t register_user(const auth_req_t* req) {
  char hash[crypto_pwhash_STRBYTES];
  if (crypto_pwhash_str(hash, req->password, strlen(req->password), hash_ops,
                        hash_mem) != 0) {
    log_error("password hash generation failed (out of memory).");
    return AUTH_INTERNAL;
  }

  switch (user_store_create(g_user_store, req->username, hash)) {
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
  char pass_hash[crypto_pwhash_STRBYTES];
  auth_outcome_t outcome = AUTH_INTERNAL;
  switch (user_store_get_hash(g_user_store, req->username, pass_hash,
                              sizeof(pass_hash))) {
    case DB_OK:
      outcome = crypto_pwhash_str_verify(pass_hash, req->password,
                                         strlen(req->password)) == 0
                    ? AUTH_OK
                    : AUTH_BAD_CREDENTIALS;
      break;
    case DB_NOT_FOUND: {
      // Verify against a dummy hash even for unknown users, so the response
      // time does not reveal which usernames exist.
      const int ignored = crypto_pwhash_str_verify(dummy_hash, req->password,
                                                   strlen(req->password));
      (void)ignored;
      outcome = AUTH_BAD_CREDENTIALS;
    } break;
    case DB_ERROR:
      break;
    case DB_EXISTS:
      // Cannot reach, but clang-tidy requires every enum value in a switch.
      log_debug("DB_EXISTS from `user_store_get_hash` is impossible");
      break;
  }
  auth_wipe(pass_hash, sizeof(pass_hash));
  return outcome;
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
  for (;;) {
    auth_req_t req;
    auth_result_t result;

    pthread_mutex_lock(&lock);
    if (requests_count == 0) {
      pthread_mutex_unlock(&lock);
      return;
    }
    pop_request(&req);
    pthread_mutex_unlock(&lock);

    process_request(&req, &result);
    auth_wipe(&req, sizeof(req));  // holds a password

    pthread_mutex_lock(&lock);
    push_result(&result);
    pthread_mutex_unlock(&lock);
  }
}

void auth_wipe(void* buf, size_t len) { sodium_memzero(buf, len); }

void auth_set_hash_cost(auth_hash_cost_t cost) {
  switch (cost) {
    case AUTH_HASH_COST_FAST:
      hash_ops = crypto_pwhash_OPSLIMIT_MIN;
      hash_mem = crypto_pwhash_MEMLIMIT_MIN;
      return;
    case AUTH_HASH_COST_NORMAL:
      break;
  }
  hash_ops = crypto_pwhash_OPSLIMIT_INTERACTIVE;
  hash_mem = crypto_pwhash_MEMLIMIT_INTERACTIVE;
}

void auth_init(user_store_t* store) {
  g_user_store = store;
  pthread_mutex_lock(&lock);
  auth_wipe(requests, sizeof(requests));  // may still hold passwords
  auth_wipe(results, sizeof(results));
  requests_head = requests_tail = requests_count = 0;
  results_head = results_tail = results_count = 0;
  in_flight = 0;
  pthread_mutex_unlock(&lock);
  if (store &&
      crypto_pwhash_str(dummy_hash, "DUMMY-PASSWORD", strlen("DUMMY-PASSWORD"),
                        hash_ops, hash_mem) != 0) {
    log_error("dummy password hash generation failed");
  }
}

bool auth_submit(const auth_req_t* req) {
  if (!req) {
    return false;
  }

  // Rejected by design: the parser caps fields, so this should never fire.
  if (memchr(req->username, '\0', sizeof(req->username)) == NULL ||
      memchr(req->password, '\0', sizeof(req->password)) == NULL) {
    return false;
  }

  // A request keeps its place until its result is collected, so every
  // accepted request is guaranteed room for its result.
  pthread_mutex_lock(&lock);
  if (requests_count + in_flight + results_count >= AUTH_QUEUE_CAP) {
    pthread_mutex_unlock(&lock);
    return false;
  }

  push_request(req);
  pthread_mutex_unlock(&lock);

  run_pending();
  return true;
}

bool auth_collect(auth_result_t* out) {
  pthread_mutex_lock(&lock);
  if (results_count == 0) {
    pthread_mutex_unlock(&lock);
    return false;
  }
  memcpy(out, &results[results_head], sizeof(results[results_head]));
  results_head = (results_head + 1) % AUTH_QUEUE_CAP;
  results_count--;
  pthread_mutex_unlock(&lock);
  return true;
}
