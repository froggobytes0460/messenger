#include <server/log.h>
#include <server/user_store.h>
#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "user_store"

#define BUSY_TIMEOUT_MS 5000  ///< How long a locked database is retried.

/// The schema for database.
static const char SCHEMA[] =
    "CREATE TABLE IF NOT EXISTS users ("
    "  id         INTEGER PRIMARY KEY,"
    "  username   TEXT NOT NULL UNIQUE COLLATE NOCASE,"
    "  pass_hash  TEXT NOT NULL,"
    "  created_at INTEGER NOT NULL DEFAULT (unixepoch())"
    ");";

/// SQL query for user insertion.
static const char INSERT_SQL[] =
    "INSERT INTO users (username, pass_hash) VALUES (?, ?);";

/// SQL querying password hash for a user.
static const char QUERY_SQL[] =
    "SELECT pass_hash FROM users WHERE users.username = ?";

/**
 * @brief The internal @p user_store_t definition.
 * @note This is **forbidden to be used directly** by anything but
 * `user_store_*` API functions.
 */
struct user_store {
  sqlite3* db;
};

user_store_t* user_store_open(const char* path) {
  user_store_t* s = calloc(1, sizeof(user_store_t));
  if (!s) {
    return NULL;
  }
  if (sqlite3_open(path, &s->db) != SQLITE_OK) {
    log_error("%s", sqlite3_errmsg(s->db));
    goto failure_cleanup;
  }

  char* sql_err = NULL;
  if (sqlite3_exec(s->db, SCHEMA, NULL, NULL, &sql_err) != SQLITE_OK) {
    log_error("schema: %s", sql_err ? sql_err : "unknown error");
    sqlite3_free(sql_err);
    goto failure_cleanup;
  }
  // Wait for a competing writer instead of failing at once, and let readers
  // proceed during writes. (WAL is ignored by in-memory databases.)
  sqlite3_busy_timeout(s->db, BUSY_TIMEOUT_MS);
  if (sqlite3_exec(s->db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL) !=
      SQLITE_OK) {
    log_warn("could not enable WAL: %s", sqlite3_errmsg(s->db));
  }
  return s;

failure_cleanup:
  if (s->db) {
    sqlite3_close_v2(s->db);
  }
  free(s);
  return NULL;
}

void user_store_close(user_store_t* store) {
  if (!store) {
    return;
  }
  sqlite3_close_v2(store->db);
  free(store);
}

database_result_t user_store_create(user_store_t* store, const char* username,
                                    const char* pass_hash) {
  if (!store || !username || !pass_hash) {
    return DB_ERROR;
  }

  database_result_t result = DB_ERROR;
  sqlite3_stmt* stmt = NULL;
  if (sqlite3_prepare_v2(store->db, INSERT_SQL, -1, &stmt, NULL) != SQLITE_OK ||
      sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC) != SQLITE_OK ||
      sqlite3_bind_text(stmt, 2, pass_hash, -1, SQLITE_STATIC) != SQLITE_OK) {
    goto error;
  }

  int rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE) {
    result = DB_OK;
    goto cleanup;
  }
  if (sqlite3_extended_errcode(store->db) == SQLITE_CONSTRAINT_UNIQUE) {
    result = DB_EXISTS;
    goto cleanup;
  }

error:
  log_error("%s", sqlite3_errmsg(store->db));
cleanup:
  sqlite3_finalize(stmt);
  return result;
}

database_result_t user_store_get_hash(user_store_t* store, const char* username,
                                      char* pass_hash_out,
                                      size_t pass_hash_len) {
  if (!store || !username || !pass_hash_out || pass_hash_len == 0) {
    return DB_ERROR;
  }

  database_result_t result = DB_ERROR;
  sqlite3_stmt* stmt = NULL;
  if (sqlite3_prepare_v2(store->db, QUERY_SQL, -1, &stmt, NULL) != SQLITE_OK ||
      sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC) != SQLITE_OK) {
    goto error;
  }

  int rc = sqlite3_step(stmt);
  if (rc == SQLITE_DONE) {
    result = DB_NOT_FOUND;
    goto cleanup;
  }
  if (rc == SQLITE_ROW) {
    const unsigned char* db_hash = sqlite3_column_text(stmt, 0);
    size_t len = (size_t)sqlite3_column_bytes(stmt, 0);
    if (!db_hash || len >= pass_hash_len) {
      log_error("stored hash missing or does not fit in the buffer");
      goto cleanup;
    }
    memcpy(pass_hash_out, db_hash, len + 1);  // includes the NUL
    result = DB_OK;
    goto cleanup;
  }

error:
  log_error("%s", sqlite3_errmsg(store->db));
cleanup:
  sqlite3_finalize(stmt);
  return result;
}
