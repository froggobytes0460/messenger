#ifndef AUTH_H
#define AUTH_H

/**
 * @file auth.h
 * @brief Asynchronous authentication: submit a request, collect the result.
 *
 * The caller never learns the outcome from @ref auth_submit. It calls
 * @ref auth_collect later (for example on every tick) and gets one
 * @ref auth_result_t per accepted request. How the check is done (inline in the
 * calling thread today, a worker thread later) is hidden behind this interface.
 *
 * @warning An @ref auth_req_t holds a plaintext password. Never log it.
 */

#include <server/constants.h>
#include <server/user_store.h>
#include <server/uuid.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief The type of authentication being done.
 */
typedef enum {
  AUTH_REGISTER,  ///< Create a new account
  AUTH_LOGIN,     ///< Log in to an existing account
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
  auth_kind_t kind;                 ///< Register or log in
  client_uuid_t client_id;          ///< UUID of client
  char username[MAX_USERNAME + 1];  ///< Username (NUL terminated)
  char password[MAX_PASSWORD + 1];  ///< Password (NUL terminated)
} auth_req_t;

/**
 * @brief How an authentication request ended.
 */
typedef enum {
  AUTH_OK,               ///< Registered or logged in
  AUTH_BAD_CREDENTIALS,  ///< Login failed
  AUTH_NAME_TAKEN,       ///< Registration failed: the username already exists
  AUTH_INTERNAL,         ///< Server-side failure (not the client's fault)
} auth_outcome_t;

/**
 * @brief The finished outcome of one request.
 *
 * Holds no password.
 */
typedef struct {
  auth_outcome_t outcome;           ///< How the request ended
  client_uuid_t client_id;          ///< Copied from @ref auth_req_t::client_id
  char username[MAX_USERNAME + 1];  ///< Username from the request
} auth_result_t;

/**
 * @brief Sets the user store used for registration and login.
 *
 * The store is borrowed, not owned: the caller keeps it open while it is set
 * and closes it afterwards. Any queued requests and results are discarded.
 *
 * @param[in] store An already opened user store, or `NULL` to detach the
 * current one (requests then end as @ref AUTH_INTERNAL).
 */
void auth_init(user_store_t* store);

/**
 * @brief Submits an authentication request.
 *
 * The outcome is delivered through @ref auth_collect, never by this call.
 * Today the check runs inside this call, so the result is already waiting; do
 * not rely on that, since a worker thread may take over later. Every request
 * accepted here produces exactly one result.
 *
 * Results stay queued until collected. Once @ref AUTH_QUEUE_CAP requests are
 * pending or uncollected, further submits return false, so call
 * @ref auth_collect regularly (for example on every tick).
 *
 * @param[in] req The request. Copied; not kept after the call returns.
 * @return true if the request was accepted (a result will follow), false if it
 * was rejected (for example the queue is full) and no result will follow.
 *
 * @pre @ref auth_init must be used beforehand to set the internal user store.
 */
bool auth_submit(const auth_req_t* req);

/**
 * @brief Takes the oldest finished result, if any.
 * @param[out] out Filled when a result is returned, untouched otherwise.
 * @return true if @p out was filled, false if no result is ready.
 */
bool auth_collect(auth_result_t* out);

/**
 * @brief Mandates a wipe of the buffer provide.
 * @param[out] buf The buffer to wipe.
 * @param[in] len The length of buffer.
 */
void auth_wipe(void* buf, size_t len);

#endif  // !AUTH_H
