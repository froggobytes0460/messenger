#include <server/chat.h>
#include <server/cli_args.h>
#include <server/log.h>
#include <server/main_socket.h>
#include <server/poll_mgr.h>
#include <sys/socket.h>
#include <sysexits.h>
#include <unistd.h>

#define LOG_TAG "main"

int main(int argc, char *argv[]) {
  struct ParsedArgs cli_args = {0};
  int rc = parse_args(&cli_args, argc, argv);
  if (rc != 0) {
    return rc;
  }

  if (cli_args.logFile != NULL && log_open_file(cli_args.logFile) != 0) {
    log_perror(cli_args.logFile);
    return EX_CANTCREAT;
  }

  log_info("initializing, port %hu, backlog %u", cli_args.portNumber,
           cli_args.backlog);

  int listen_fd = -1;
  rc = get_main_socket(cli_args.portNumber, &listen_fd);
  if (rc != 0) {
    return rc;
  }

  if (listen(listen_fd, (int)cli_args.backlog) != 0) {
    log_perror("listen");
    return EX_OSERR;
  }

  poll_mgr_t mgr;
  poll_mgr_init(&mgr, listen_fd);

  chat_t chat;
  chat_init(&chat);

  rc = poll_mgr_run(&mgr, chat_callbacks(&chat));

  close(listen_fd);
  log_close();
  return rc;
}
