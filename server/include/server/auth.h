#ifndef AUTH_H
#define AUTH_H

/**
 * @file auth.h
 * @brief Asynchronous authentication: submit a request, collect the result.
 *
 * The caller never learns the outcome from @ref auth_submit. It calls
 * @ref auth_collect later (for example on every tick) and gets one
 * @ref auth_result_t per accepted request.
 *
 * The check (password hashing and the user store) runs on one worker thread, so
 * the calling thread never blocks on it. The worker rings @ref auth_wake_fd
 * when a result is ready, which lets an event loop sleep in poll() until then.
 *
 * Typical use: @ref auth_init once, @ref auth_submit per request, wait for
 * @ref auth_wake_fd, drain with @ref auth_collect, and @ref auth_shutdown
 * before closing the user store.
 *
 * All functions except @ref auth_init, @ref auth_shutdown and
 * @ref auth_set_hash_cost may be called from any thread; those three must be
 * called from one thread (the one that owns the event loop).
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
 * @brief Stops the worker thread and waits for it to exit.
 *
 * A request the worker is already processing is finished first; requests still
 * queued are discarded and get no result. Does nothing if no worker is running,
 * so it is safe to call twice.
 *
 * Results already produced stay available to @ref auth_collect. The wake fd
 * stays open.
 *
 * @note Call it before the user store is closed: the worker uses the store.
 */
void auth_shutdown(void);

/**
 * @brief Sets the user store and starts the worker thread.
 *
 * Stops any previous worker first (see @ref auth_shutdown), then discards all
 * queued requests and results, creates the wake fd on the first call, builds
 * the dummy hash and starts a new worker. Calling it again is allowed: it
 * restarts everything with the new store.
 *
 * The store is borrowed, not owned: the caller keeps it open while it is set,
 * calls @ref auth_shutdown, and only then closes it.
 *
 * @param[in] store An already opened user store, or `NULL` to detach the
 * current one (requests then end as @ref AUTH_INTERNAL). A worker is started
 * either way.
 *
 * @note Call @ref auth_set_hash_cost before this, not after.
 * @note If the worker cannot be started an error is logged and submitted
 * requests will never get a result.
 */
void auth_init(user_store_t* store);

/**
 * @brief Descriptor that becomes readable when a result may be waiting.
 *
 * Create-once: @ref auth_init makes it (a non-blocking eventfd) and keeps it
 * for the life of the process, so the number stays valid across re-inits. The
 * worker writes to it after every finished request. Add it to the event loop
 * with @c poll_mgr_set_wake_fd so the loop wakes up; the loop drains it, then
 * calls @ref auth_collect until it returns false. The caller must not close it.
 *
 * A readable fd means "a result may be waiting", not "exactly one": several
 * results can share one wake-up, and a wake-up can find none.
 *
 * @return The descriptor, or -1 before @ref auth_init or if it could not be
 * created.
 */
int auth_wake_fd(void);

/**
 * @brief Submits an authentication request.
 *
 * Only validates and queues the request, then wakes the worker; it returns
 * immediately and never hashes. The outcome arrives later through
 * @ref auth_collect, usually after @ref auth_wake_fd becomes readable. Every
 * request accepted here produces exactly one result, unless
 * @ref auth_shutdown or @ref auth_init discards it first.
 *
 * Results stay queued until collected. Once @ref AUTH_QUEUE_CAP requests are
 * pending, in progress or uncollected, further submits return false, so call
 * @ref auth_collect regularly (for example on every tick).
 *
 * @param[in] req The request. Copied; not kept after the call returns.
 * @return true if the request was accepted (a result will follow), false if it
 * was rejected (null, unterminated fields, or the queue is full) and no result
 * will follow.
 *
 * @pre @ref auth_init must have been called.
 */
bool auth_submit(const auth_req_t* req);

/**
 * @brief Takes the oldest finished result, if any.
 *
 * Never blocks: it returns false at once when nothing is ready. Results come
 * back in the order the requests were finished, which is the order they were
 * submitted (there is one worker). Call it in a loop until it returns false.
 *
 * @param[out] out Filled when a result is returned, untouched otherwise.
 * @return true if @p out was filled, false if no result is ready.
 */
bool auth_collect(auth_result_t* out);

/**
 * @brief Securely zeroes a buffer; the compiler cannot optimize it away.
 * @param[in,out] buf The buffer to wipe.
 * @param[in] len The length of the buffer in bytes.
 */
void auth_wipe(void* buf, size_t len);

/**
 * @brief How expensive it is to hash a new password.
 */
typedef enum {
  AUTH_HASH_COST_NORMAL,  ///< Production setting (the default)
  AUTH_HASH_COST_FAST,    ///< Cheapest possible; for tests only, NOT secure
} auth_hash_cost_t;

/**
 * @brief Chooses the cost of hashing new passwords.
 *
 * Meant for tests: with @ref AUTH_HASH_COST_FAST a hash takes microseconds
 * instead of tens of milliseconds, so suites that submit hundreds of requests
 * stay fast. Existing hashes keep verifying, because their cost is stored in
 * the hash string.
 *
 * @param[in] cost The cost to use from now on.
 *
 * @note Call it before @ref auth_init: the dummy hash used for unknown users is
 * built there with the cost in effect at that moment.
 *
 * @warning Never call it with @ref AUTH_HASH_COST_FAST in the server.
 */
void auth_set_hash_cost(auth_hash_cost_t cost);

#endif  // !AUTH_H
