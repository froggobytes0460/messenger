#include "server/protocol.h"
#include <server/chat.h>
#include <server/constants.h>
#include <server/log.h>
#include <server/poll_mgr.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#define LOG_TAG "chat"

static chat_client_t *find_client(chat_t *chat, int fd) {
  for (size_t i = 0; i < MAX_CONNECTIONS; i++) {
    if (chat->clients[i].active && chat->clients[i].fd == fd) {
      return &chat->clients[i];
    }
  }
  return NULL;
}

static chat_client_t *alloc_client(chat_t *chat) {
  for (size_t i = 0; i < MAX_CONNECTIONS; i++) {
    if (!chat->clients[i].active) {
      return &chat->clients[i];
    }
  }
  return NULL;
}

// NOLINTBEGIN(readability-magic-numbers)
static unsigned long long get_time_diff(const struct timespec *start,
                                        const struct timespec *end) {
  if (end->tv_sec < start->tv_sec ||
      (end->tv_sec == start->tv_sec && end->tv_nsec < start->tv_nsec)) {
    return 0ULL;
  }

  long long diff_sec = end->tv_sec - start->tv_sec;
  long long diff_nsec = end->tv_nsec - start->tv_nsec;

  if (diff_nsec < 0) {
    diff_sec -= 1;
    diff_nsec += 1000000000LL;
  }

  return ((unsigned long long)diff_sec * 1000ULL) +
         ((unsigned long long)diff_nsec / 1000000ULL);
}
// NOLINTEND(readability-magic-numbers)

static void send_err(poll_mgr_t *mgr, int client_fd, const char *reason) {
  char reply[MAX_REPLY];
  size_t len_reply = sizeof(reply);
  if (format_err_reason(reply, &len_reply, reason) == PROTO_OK) {
    if (poll_mgr_send(mgr, client_fd, reply, len_reply) < 0) {
      poll_mgr_close(mgr, client_fd);
    }
  }
}

// Handles one complete line (without the newline) in client->inbuf.
static void handle_line(chat_client_t *client, poll_mgr_t *mgr) {
  protocol_cmd_t cmd;
  proto_status_t pt =
      client->overflow ? PROTO_ERR_TOO_LONG
                       : parse_protocol_str(client->inbuf, client->inlen, &cmd);
  char reply[MAX_REPLY];
  size_t len_reply = sizeof(reply);

  proto_status_t fmt = (pt == PROTO_OK) ? format_ok(reply, &len_reply, NULL)
                                        : format_err(reply, &len_reply, pt);

  if (fmt == PROTO_OK && poll_mgr_send(mgr, client->fd, reply, len_reply) < 0) {
    poll_mgr_close(mgr, client->fd);
  }

  memset(&cmd, 0, sizeof(cmd));
  memset(&client->inbuf, 0, sizeof(client->inbuf));
}

static void on_message(void *ctx, poll_mgr_t *mgr, int sender_fd,
                       const char *buf, size_t len) {
  chat_t *chat = ctx;
  chat_client_t *client = find_client(chat, sender_fd);
  if (client == NULL) {
    return;
  }

  for (size_t i = 0; i < len; i++) {
    if (buf[i] == '\n') {
      handle_line(client, mgr);
      client->inlen = 0;
      client->overflow = false;
    } else if (client->inlen < sizeof(client->inbuf) - 1) {
      client->inbuf[client->inlen++] = buf[i];
    } else {
      // Line does not fit: drop the rest of it and reject it at the newline.
      client->overflow = true;
    }
  }
}

static void on_disconnect(void *ctx, poll_mgr_t *mgr, int client_fd) {
  (void)mgr;
  chat_t *chat = ctx;
  log_info("client %d left", client_fd);
  chat_client_t *client = find_client(chat, client_fd);
  if (client == NULL) {
    return;
  }
  memset(client, 0, sizeof(*client));
}

static void on_connect(void *ctx, poll_mgr_t *mgr, int client_fd) {
  chat_t *chat = ctx;
  (void)mgr;
  struct timespec now;

  // NOLINTNEXTLINE(misc-include-cleaner)
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    log_perror("clock_gettime");
    return;
  }
  log_info("client %d entered", client_fd);
  chat_client_t *client = alloc_client(chat);
  if (client == NULL) {
    return;
  }
  client->active = true;
  client->fd = client_fd;
  memcpy(&client->connected_at, &now, sizeof(now));
}

static void on_tick(void *ctx, poll_mgr_t *mgr) {
  chat_t *chat = ctx;
  struct timespec now;
  // NOLINTNEXTLINE(misc-include-cleaner)
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    log_perror("clock_gettime");
    return;
  }

  for (size_t i = 0; i < MAX_CONNECTIONS; i++) {
    if (chat->clients[i].state == CLIENT_AUTHENTICATED ||
        !chat->clients[i].active) {
      continue;
    }
    if (get_time_diff(&chat->clients[i].connected_at, &now) >
        UNAUTH_TIMEOUT_MS) {
      log_info("client %d timed out", chat->clients[i].fd);
      send_err(mgr, chat->clients[i].fd, "timeout");
      poll_mgr_close(mgr, chat->clients[i].fd);
    }
  }
}

void chat_init(chat_t *chat) { memset(chat, 0, sizeof(*chat)); }

poll_callback_t chat_callbacks(chat_t *chat) {
  return (poll_callback_t){
      .on_connection = on_connect,
      .on_disconnection = on_disconnect,
      .on_message = on_message,
      .on_tick = on_tick,
      .ctx = chat,
  };
}
