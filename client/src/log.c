#include <client/constants.h>
#include <client/log.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static FILE* log_file = NULL;
static const char* const level_names[] = {"DEBUG", "INFO", "WARN", "ERROR"};
static log_level_t min_level = LOG_LVL_INFO;

void set_min_level(log_level_t lvl) { min_level = lvl; }

int open_log_file(const char* filename) {
  FILE* file = fopen(filename, "a");
  if (file == NULL) {
    perror("Error opening log file");
    return -1;
  }
  close_log_file();
  log_file = file;
  return 0;
}

void close_log_file() {
  if (log_file == NULL) {
    return;
  }
  int rc = fclose(log_file);
  if (rc != 0) {
    perror("Error closing log file");
  }
}

void log_write(log_level_t lvl, const char* tag, const char* fmt, ...) {
  if (min_level > lvl) {
    return;
  }

  char timestamp[LOG_TIMESTAMP_LEN] = "?";
  time_t now = time(NULL);
  struct tm tm_now = {0};
  if (localtime_r(&now, &tm_now) != NULL) {
    (void)strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_now);
  }

  char log_msg[MAX_LOG_MSG_LEN];
  va_list args;
  va_start(args, fmt);
  (void)vsnprintf(log_msg, sizeof(log_msg), fmt, args);
  va_end(args);

  (void)fprintf(stderr, "%s %-5s [%s] %s\n", timestamp, level_names[lvl], tag,
                log_msg);

  if (log_file != NULL) {
    (void)fprintf(log_file, "%s %-5s [%s] %s\n", timestamp, level_names[lvl],
                  tag, log_msg);
    (void)fflush(log_file);
  }
}

void log_errno(const char* tag, const char* msg) {
  int saved = errno;
  log_write(LOG_LVL_ERROR, tag, "%s: %s", msg, strerror(saved));
}
