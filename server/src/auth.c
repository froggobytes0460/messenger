#include <server/auth.h>
#include <server/constants.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief User-record stored by server
 */
typedef struct {
  bool used; ///< Whether the struct holds an actual user or not
  char username[MAX_USERNAME + 1]; ///< Username
} user_rec_t;

/// Global-list of users.
static user_rec_t users[MAX_NUM_USERS];

/// The global-list of results queued for authentication.
static auth_result_t results[AUTH_QUEUE_CAP];

static size_t results_head = 0;  ///< Oldest slot; next slot to pop
static size_t results_tail = 0;  ///< Next slot to write
static size_t results_count = 0; ///< results currently stored

/// Finds user from list of users.
static user_rec_t *find_user(const char *username) {
  for (size_t i = 0; i < MAX_NUM_USERS; i++) {
    if (!users[i].used) {
      continue;
    }
    if (strcmp(username, users[i].username) == 0) {
      return &users[i];
    }
  }
  return NULL;
}

static user_rec_t *find_free_slot(void) {
  for (size_t i = 0; i < MAX_NUM_USERS; i++) {
    if (!users[i].used) {
      return &users[i];
    }
  }
  return NULL;
}

static void push_result(const auth_result_t *result) {
  memcpy(&results[results_tail], result, sizeof(*result));
  results_count++;
  results_tail = (results_tail + 1) % AUTH_QUEUE_CAP;
}

bool auth_collect(auth_result_t *out) {
  if (results_count == 0) {
    return false;
  }
  memcpy(out, &results[results_head], sizeof(results[results_head]));
  results_head = (results_head + 1) % AUTH_QUEUE_CAP;
  results_count--;
  return true;
}

bool auth_submit(const auth_req_t *req) {
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
    if (find_user(req->username) != NULL) {
      result.outcome = AUTH_NAME_TAKEN;
      break;
    }
    user_rec_t *slot = find_free_slot();
    if (slot == NULL) {
      result.outcome = AUTH_INTERNAL;
      break;
    }
    len = snprintf(slot->username, sizeof(slot->username), "%s", req->username);
    if (len < 0 || len >= (int)sizeof(slot->username)) {
      result.outcome = AUTH_INTERNAL;
      break;
    }
    slot->used = true;
    result.outcome = AUTH_OK;
    break;
  }
  case AUTH_LOGIN: {
    user_rec_t *user = find_user(req->username);
    if (user == NULL) {
      result.outcome = AUTH_BAD_CREDENTIALS;
      break;
    }
    result.outcome = AUTH_OK;
    break;
  }
  default:
    result.outcome = AUTH_INTERNAL;
    break;
  }

  push_result(&result);
  return true;
}
