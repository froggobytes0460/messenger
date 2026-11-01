#ifndef CLIENT_LOG_H
#define CLIENT_LOG_H

/**
 * @file log.h
 * @brief Handles logging for program.
 */

/**
 * @brief The levels of logging supported by this logging module.
 */
typedef enum {
  LOG_LVL_DEBUG,    ///< Debug logging
  LOG_LVL_INFO,     ///< Info logging
  LOG_LVL_WARNING,  ///< Logging warnings
  LOG_LVL_ERROR,    ///< Error loggings
} log_level_t;

/**
 * @brief Sets the minimum log level to treat as messages; all below it is
 * dropped as noise.
 * @param[in] lvl The log level to be set to minimum.
 */
void set_min_level(log_level_t lvl);

/**
 * @brief Opens the log file.
 * @param[in] filename The name of the file to open as log file.
 * @return 0 on success, otherwise -1 on failure.
 */
int open_log_file(const char* filename);

/**
 * @brief Closes the log file.
 */
void close_log_file();

/**
 * @brief Base logging function; logs a line to file and `stdout`.
 * @param[in] lvl The level of logging for this line.
 * @param[in] tag A tag for this line; specific string indicator to tell where
 * this line comes from.
 * @param[in] fmt A `printf`-style format string.
 * @param[in] ... Optional additional arguments corresponding to the @c fmt
 * string.
 */
void log_write(log_level_t lvl, const char* tag, const char* fmt, ...)
    __attribute__((format(printf, 3, 4))) __attribute__((nonnull(2, 3)));

/**
 * @brief `perror` but for centralized logging.
 * @param[in] tag A tag for this line; specific string indicator to tell where
 * this line comes from.
 * @param[in] msg The message to be put with the `errno` value.
 */
void log_errno(const char* tag, const char* msg);

/*
 * Note: These macros depend on `LOG_TAG` being defined in the source code (not
 * as const var, but as macro)
 */
#define LOG_DEBUG(fmt, ...) \
  log_write(LOG_LVL_DEBUG, LOG_TAG, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) log_write(LOG_LVL_INFO, LOG_TAG, fmt, ##__VA_ARGS__)
#define LOG_WARNING(fmt, ...) \
  log_write(LOG_LVL_WARNING, LOG_TAG, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) \
  log_write(LOG_LVL_ERROR, LOG_TAG, fmt, ##__VA_ARGS__)
#define LOG_PERROR(msg) log_errno(LOG_TAG, msg)

#endif  // !CLIENT_LOG_H
