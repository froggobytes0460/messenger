#include <server/log.h>
#include <server/user_store.h>
#include <sqlite3.h>
#include <stdlib.h>

#define LOG_TAG "user_store"

/// The schema for database.
static const char SCHEMA[] =
    "CREATE TABLE IF NOT EXISTS users ("
    "  id         INTEGER PRIMARY KEY,"
    "  username   TEXT NOT NULL UNIQUE,"
    "  pass_hash  TEXT NOT NULL,"
    "  created_at INTEGER NOT NULL DEFAULT (unixepoch())"
    ");";

/**
 * @brief The internal user_store definition.
 * @note This is **forbidden to be used directly** by anything but
 * `user_store_*` API functions.
 */
struct user_store {
  sqlite3 *db;
};

user_store_t *user_store_open(const char *path) {
  user_store_t *s = calloc(1, sizeof(user_store_t));
  if (!s) {
    return NULL;
  }
  if (sqlite3_open(path, &s->db) != SQLITE_OK) {
    log_error("%s", sqlite3_errmsg(s->db));
    goto failure_cleanup;
  }

  char *sql_err = NULL;
  if (sqlite3_exec(s->db, SCHEMA, NULL, NULL, &sql_err) != SQLITE_OK) {
    log_error("schema: %s", sql_err ? sql_err : "unknown error");
    sqlite3_free(sql_err);
    goto failure_cleanup;
  }
  return s;

failure_cleanup:
  sqlite3_close(s->db);
  free(s);
  return NULL;
}

void user_store_close(user_store_t *store) {
  if (!store) {
    return;
  }
  sqlite3_close(store->db);
  free(store);
}
