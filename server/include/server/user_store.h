#ifndef USER_STORE_H
#define USER_STORE_H

#include <stddef.h>

/**
 * @file user_store.h
 * @brief Database operations on user store.
 */

/// Results that can come from database operations.
typedef enum {
  DB_OK,         ///< Database operation successful.
  DB_EXISTS,     ///< Item exists in database already.
  DB_NOT_FOUND,  ///< No result found for database query.
  DB_ERROR,      ///< Some internal error occurred during database operation.
} database_result_t;

/// Opaque handle to the user database. Meant for public use.
typedef struct user_store user_store_t;

/**
 * @brief Opens user store (database).
 *
 * Creates the file and the schema if they do not exist yet. Usernames are
 * unique and compared case-insensitively. Use `":memory:"` for a private,
 * temporary database.
 *
 * @param[in] path The path to the database.
 * @return The user store, `NULL` on failure.
 */
user_store_t* user_store_open(const char* path);

/**
 * @brief Closes user store (database). (Does nothing if `NULL` is provided)
 * @param[in] store The user store to close.
 */
void user_store_close(user_store_t* store);

/**
 * @brief Add an user to database.
 * @param[in] store The user store to add user to.
 * @param[in] username The username.
 * @param[in] pass_hash The hash of user password.
 * @return @ref DB_OK if the user was added, @ref DB_EXISTS if the username is
 * taken (case-insensitively), @ref DB_ERROR on a `NULL` argument or any other
 * failure.
 */
database_result_t user_store_create(user_store_t* store, const char* username,
                                    const char* pass_hash);

/**
 * @brief Query user store for password hash of an user.
 * @param[in] store The user store to query for password hash.
 * @param[in] username The username.
 * @param[out] pass_hash_out The string array to store password hash in. It is
 * left untouched unless the result is @ref DB_OK.
 * @param[in] pass_hash_len The size of @p pass_hash_out in bytes, including the
 * terminating NUL. A hash that does not fit is never truncated.
 * @return @ref DB_OK if the hash was copied, @ref DB_NOT_FOUND if there is no
 * such user, @ref DB_ERROR on a `NULL` argument, a too small buffer or any
 * other failure.
 */
database_result_t user_store_get_hash(user_store_t* store, const char* username,
                                      char* pass_hash_out,
                                      size_t pass_hash_len);

#endif  // !USER_STORE_H
