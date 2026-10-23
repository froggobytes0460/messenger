#include <server/auth.h>
#include <server/chat.h>
#include <server/cli_args.h>
#include <server/log.h>
#include <server/main_socket.h>
#include <server/poll_mgr.h>
#include <server/user_store.h>
#include <signal.h>
#include <sodium/core.h>
#include <string.h>
#include <sys/socket.h>
#include <sysexits.h>
#include <unistd.h>

#define LOG_TAG "main"

/// Large (about 1.7 MB each), so kept off the stack.
static poll_mgr_t mgr;
static chat_t chat;

static void handle_stop_signal(int signo) {
  (void)signo;
  poll_mgr_request_stop();
}

/// Installs SIGINT/SIGTERM handlers without SA_RESTART so poll() wakes up.
static int install_stop_handlers(void) {
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_handler = handle_stop_signal;
  sigemptyset(&action.sa_mask);
  return sigaction(SIGINT, &action, NULL) == 0 &&
                 sigaction(SIGTERM, &action, NULL) == 0
             ? 0
             : -1;
}

int main(int argc, char* argv[]) {
  // NOLINTNEXTLINE(readability-implicit-bool-conversion)
  struct ParsedArgs cli_args = {0};

  int rc = parse_args(&cli_args, argc, argv);
  if (rc != 0) {
    return rc;
  }

  if (cli_args.logFile != NULL && log_open_file(cli_args.logFile) != 0) {
    log_perror(cli_args.logFile);
    return EX_CANTCREAT;
  }

  if (cli_args.verbose) {
    log_set_level(LOG_LEVEL_DEBUG);
    log_info("Verbosity set to true.");
  }

  if (sodium_init() == -1) {
    log_error("error in initializing libsodium");
    log_close();
    return EX_SOFTWARE;
  }

  user_store_t* user_store = user_store_open(cli_args.databasePath);
  if (!user_store) {
    log_error("error in opening database %s", cli_args.databasePath);
    log_close();
    return EX_IOERR;
  }
  log_info("Using database in path \"%s\"", cli_args.databasePath);
  auth_init(user_store);

  log_info("initializing, port %hu, backlog %u", cli_args.portNumber,
           cli_args.backlog);

  int listen_fd = -1;
  rc = get_main_socket(cli_args.portNumber, &listen_fd);
  if (rc != 0) {
    user_store_close(user_store);
    log_close();
    return rc;
  }

  if (listen(listen_fd, (int)cli_args.backlog) != 0) {
    log_perror("listen");
    rc = EX_OSERR;
    goto cleanup_and_exit;
  }

  if (install_stop_handlers() != 0) {
    log_perror("sigaction");
    rc = EX_OSERR;
    goto cleanup_and_exit;
  }

  rc = poll_mgr_init(&mgr, listen_fd);
  if (rc != 0) {
    rc = EX_OSERR;
    goto cleanup_and_exit;
  }

  chat_init(&chat);

  rc = poll_mgr_run(&mgr, chat_callbacks(&chat));
  log_info("shutting down");

cleanup_and_exit:
  close(listen_fd);
  user_store_close(user_store);
  log_close();
  return rc;
}
