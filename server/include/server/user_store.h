#ifndef USER_STORE_H
#define USER_STORE_H

/**
 * @file user_store.h
 * @brief Database operations on user store.
 */

/// Opaque handle to the user database. Meant for public use.
typedef struct user_store user_store_t;

/**
 * @brief Opens user store (database).
 * @param[in] path The path to the database.
 * @return The user store, `NULL` on failure.
 */
user_store_t *user_store_open(const char *path);

/**
 * @brief Closes user store (database). (Does nothing if `NULL` is provided)
 * @param[in] store The user store to close.
 */
void user_store_close(user_store_t *store);

#endif // !USER_STORE_H
