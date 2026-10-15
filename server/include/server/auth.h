#ifndef AUTH_H
#define AUTH_H

/**
 * @file auth.h
 * @brief Asynchronous authentication: submit a request, collect the result.
 *
 * The caller never learns the outcome from @ref auth_submit. It calls
 * @ref auth_collect later (for example on every tick) and gets one
 * @ref auth_result_t per accepted request. How the check is done (in memory,
 * SQLite, a worker thread) is hidden behind this interface.
 *
 * @warning An @ref auth_req_t holds a plaintext password. Never log it.
 */

#include <server/constants.h>
#include <stdbool.h>

/**
 * @brief The type of authentication being done.
 */
typedef enum {
  AUTH_REGISTER, ///< Create a new account
  AUTH_LOGIN,    ///< Log in to an existing account
} auth_kind_t;

/**
 * @brief One authentication request.
 *
 * The caller fills every field. The module copies what it needs, so the caller
 * may wipe its own copy as soon as @ref auth_submit returns.
 *
 * @warning Holds plaintext password; do not log.
 */
typedef struct {
  auth_kind_t kind; ///< Register or log in

  /// Opaque to this module: copied unchanged into the matching result.
  int client_id;

  /// Opaque to this module: lets the caller detect that @ref client_id was
  /// reused by another client while the request was in flight.
  unsigned client_generation;

  char username[MAX_USERNAME + 1]; ///< Username (NUL terminated)
  char password[MAX_PASSWORD + 1]; ///< Password (NUL terminated)
} auth_req_t;

/**
 * @brief How an authentication request ended.
 */
typedef enum {
  AUTH_OK,              ///< Registered or logged in
  AUTH_BAD_CREDENTIALS, ///< Login failed
  AUTH_NAME_TAKEN,      ///< Registration failed: the username already exists
  AUTH_INTERNAL,        ///< Server-side failure (not the client's fault)
} auth_outcome_t;

/**
 * @brief The finished outcome of one request.
 *
 * Holds no password.
 */
typedef struct {
  auth_outcome_t outcome; ///< How the request ended

  int client_id;                   ///< Copied from @ref auth_req_t::client_id
  unsigned client_generation;      ///< Copied from
                                   ///< @ref auth_req_t::client_generation
  char username[MAX_USERNAME + 1]; ///< Username from the request
} auth_result_t;

/**
 * @brief Submits an authentication request.
 *
 * Returns immediately; the outcome is delivered later through
 * @ref auth_collect. Every request accepted here produces exactly one result.
 *
 * @param[in] req The request. Copied; not kept after the call returns.
 * @return true if the request was accepted (a result will follow), false if it
 * was rejected (for example the queue is full) and no result will follow.
 */
bool auth_submit(const auth_req_t *req);

/**
 * @brief Takes the oldest finished result, if any.
 * @param[out] out Filled when a result is returned, untouched otherwise.
 * @return true if @p out was filled, false if no result is ready.
 */
bool auth_collect(auth_result_t *out);

#endif // !AUTH_H
