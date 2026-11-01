#include <client/cli_args.h>
#include <client/log.h>

#define LOG_TAG "main"

int main(int argc, const char* argv[]) {
  struct parsed_args args;
  int rc = parse_args(&args, argc, argv);
  if (rc == PARSE_HELP_REQUESTED) {
    return 0;
  }
  if (rc != 0) {
    return rc;
  }

  if (args.log_file) {
    open_log_file(args.log_file);
  }

  if (args.verbosity) {
    set_min_level(LOG_LVL_DEBUG);
    LOG_INFO("set minimum log level to `debug`");
  }

  LOG_INFO("client initialized...");

  close_log_file();
  return 0;
}
