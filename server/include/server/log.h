#ifndef LOG_H
#define LOG_H

/**
 * @file log.h
 * @brief Unified server logging.
 *
 * Every line is written to stderr as
 * @code
 * 2026-10-09 12:00:00 INFO  [poll_mgr] message
 * @endcode
 * With log_open_file() the same lines are also appended to a file.
 * Each source file defines @c LOG_TAG before using the macros below:
 * @code
 * #define LOG_TAG "poll_mgr"
 * @endcode
 * Messages below the current level (see log_set_level()) are discarded.
 * User-facing command line errors (usage text) are not logs and stay on
 * stderr via plain @c fprintf .
 */

/**
 * @brief Severity of a log message, in increasing order.
 */
typedef enum {
  LOG_LEVEL_DEBUG, ///< Verbose detail, such as message contents.
  LOG_LEVEL_INFO,  ///< Normal events: startup, connects, disconnects.
  LOG_LEVEL_WARN,  ///< Unexpected but recoverable.
  LOG_LEVEL_ERROR, ///< An operation failed.
} log_level_t;

/**
 * @brief Sets the minimum level that is written. The default is
 * @c LOG_LEVEL_INFO .
 * @param[in] level Lowest severity to keep.
 */
void log_set_level(log_level_t level);

/**
 * @brief Also appends every log line to a file, in addition to stderr.
 * @param[in] path File to append to; created if missing.
 * @return 0 on success, -1 on failure with @c errno set. On failure logging
 * continues on stderr only.
 */
int log_open_file(const char *path);

/**
 * @brief Closes the log file opened by log_open_file(), if any.
 */
void log_close(void);

/**
 * @brief Writes one log line. Prefer the @c log_* macros, which fill in the
 * tag.
 * @param[in] level Severity of the message.
 * @param[in] tag Name of the component logging, such as @c "chat" .
 * @param[in] fmt @c printf -style format string.
 */
void log_write(log_level_t level, const char *tag, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

/**
 * @brief Logs @p msg together with the description of the current @c errno ,
 * as an error. Replaces @c perror .
 * @param[in] tag Name of the component logging.
 * @param[in] msg The operation that failed, such as @c "accept" .
 */
void log_errno(const char *tag, const char *msg);

#define log_debug(...) log_write(LOG_LEVEL_DEBUG, LOG_TAG, __VA_ARGS__)
#define log_info(...) log_write(LOG_LEVEL_INFO, LOG_TAG, __VA_ARGS__)
#define log_warn(...) log_write(LOG_LEVEL_WARN, LOG_TAG, __VA_ARGS__)
#define log_error(...) log_write(LOG_LEVEL_ERROR, LOG_TAG, __VA_ARGS__)
/// Logs @c errno after the failed operation @p msg .
#define log_perror(msg) log_errno(LOG_TAG, msg)

#endif // !LOG_H
