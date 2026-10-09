#include <errno.h>
#include <server/constants.h>
#include <server/log.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static log_level_t min_level = LOG_LEVEL_INFO;
static FILE *log_file = NULL; ///< NULL when no log file is open.

static const char *const level_names[] = {"DEBUG", "INFO", "WARN", "ERROR"};

void log_set_level(log_level_t level) { min_level = level; }

int log_open_file(const char *path) {
  FILE *file = fopen(path, "a");
  if (file == NULL) {
    return -1;
  }
  log_file = file;
  return 0;
}

void log_close(void) {
  if (log_file != NULL) {
    (void)fclose(log_file);
    log_file = NULL;
  }
}

void log_write(log_level_t level, const char *tag, const char *fmt, ...) {
  if (level < min_level) {
    return;
  }

  char stamp[LOG_TIMESTAMP_LEN] = "?";
  time_t now = time(NULL);
  struct tm tm_now;
  if (localtime_r(&now, &tm_now) != NULL) {
    (void)strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &tm_now);
  }

  // One stdio call per line keeps lines intact if threads are added later.
  char body[LOG_MSG_MAX_LEN];
  va_list args;
  va_start(args, fmt);
  (void)vsnprintf(body, sizeof(body), fmt, args);
  va_end(args);

  (void)fprintf(stderr, "%s %-5s [%s] %s\n", stamp, level_names[level], tag,
                body);

  if (log_file != NULL) {
    (void)fprintf(log_file, "%s %-5s [%s] %s\n", stamp, level_names[level], tag,
                  body);
    (void)fflush(log_file); // Keep the file current if the server is killed.
  }
}

void log_errno(const char *tag, const char *msg) {
  int saved = errno;
  log_write(LOG_LEVEL_ERROR, tag, "%s: %s", msg, strerror(saved));
}
