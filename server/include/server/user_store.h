#ifndef USER_STORE_H
#define USER_STORE_H

#include <stddef.h>

/**
 * @file user_store.h
 * @brief Database operations on user store.
 */

/// Results that can come from database operations.
typedef enum {
  DB_OK,        ///< Database operation successful.
  DB_EXISTS,    ///< Item exists in database already.
  DB_NOT_FOUND, ///< No result found for database query.
  DB_ERROR,     ///< Some internal error occurred during database operation.
} database_result_t;

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

/**
 * @brief Add an user to database.
 * @param[in] store The user store to add user to.
 * @param[in] username The username.
 * @param[in] pass_hash The hash of user password.
 */
database_result_t user_store_create(user_store_t *store, const char *username,
                                    const char *pass_hash);

/**
 * @brief Query user store for password hash of an user.
 * @param[in] store The user store to query for password hash.
 * @param[in] username The username.
 * @param[out] pass_hash_out The string array to store password hash in.
 * @param[in] pass_hash_len The length of password hash.
 */
database_result_t user_store_get_hash(user_store_t *store, const char *username,
                                      char *pass_hash_out,
                                      size_t pass_hash_len);

#endif // !USER_STORE_H
