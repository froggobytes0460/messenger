#include <server/auth.h>
#include <server/chat.h>
#include <server/constants.h>
#include <server/log.h>
#include <server/poll_mgr.h>
#include <server/protocol.h>
#include <server/uuid.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#define LOG_TAG "chat"

static chat_client_t* find_client(chat_t* chat, int fd) {
  for (size_t i = 0; i < MAX_CONNECTIONS; i++) {
    if (chat->clients[i].active && chat->clients[i].fd == fd) {
      return &chat->clients[i];
    }
  }
  return NULL;
}

static chat_client_t* find_client_by_id(chat_t* chat, client_uuid_t id) {
  for (size_t i = 0; i < MAX_CONNECTIONS; i++) {
    if (chat->clients[i].active &&
        memcmp(chat->clients[i].id, id, sizeof(client_uuid_t)) == 0) {
      return &chat->clients[i];
    }
  }
  return NULL;
}

static chat_client_t* find_client_by_name(chat_t* chat, const char* name) {
  for (size_t i = 0; i < MAX_CONNECTIONS; i++) {
    if (chat->clients[i].active &&
        chat->clients[i].state == CLIENT_AUTHENTICATED &&
        strcmp(name, chat->clients[i].name) == 0) {
      return &chat->clients[i];
    }
  }
  return NULL;
}

static chat_client_t* alloc_client(chat_t* chat) {
  for (size_t i = 0; i < MAX_CONNECTIONS; i++) {
    if (!chat->clients[i].active) {
      return &chat->clients[i];
    }
  }
  return NULL;
}

// NOLINTBEGIN(readability-magic-numbers)
static unsigned long long get_time_diff(const struct timespec* start,
                                        const struct timespec* end) {
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

// Sends a formatted reply if formatting worked; closes the client if the send
// fails.
static void deliver(poll_mgr_t* mgr, int client_fd, proto_status_t fmt,
                    const char* reply, size_t len) {
  if (fmt == PROTO_OK && poll_mgr_send(mgr, client_fd, reply, len) < 0) {
    poll_mgr_close(mgr, client_fd);
  }
}

static void send_ok(poll_mgr_t* mgr, int client_fd, const char* text) {
  char reply[MAX_REPLY];
  size_t len = sizeof(reply);
  proto_status_t fmt = format_ok(reply, &len, text);
  deliver(mgr, client_fd, fmt, reply, len);
}

// Only pass string literals as @p reason, never client input.
static void send_err(poll_mgr_t* mgr, int client_fd, const char* reason) {
  char reply[MAX_REPLY];
  size_t len = sizeof(reply);
  proto_status_t fmt = format_err_reason(reply, &len, reason);
  deliver(mgr, client_fd, fmt, reply, len);
}

static void send_proto_err(poll_mgr_t* mgr, int client_fd, proto_status_t why) {
  char reply[MAX_REPLY];
  size_t len = sizeof(reply);
  proto_status_t fmt = format_err(reply, &len, why);
  deliver(mgr, client_fd, fmt, reply, len);
}

static void reroute_msg(chat_t* chat, const protocol_cmd_t* cmd,
                        poll_mgr_t* mgr, const chat_client_t* client) {
  chat_client_t* recipient = find_client_by_name(chat, cmd->username);
  if (!recipient) {
    send_err(mgr, client->fd, "no such user");
    return;
  }
  if (client == recipient) {
    send_err(mgr, client->fd, "not allowed");
    return;
  }
  char reply[MAX_REPLY];
  size_t len_reply = sizeof(reply);
  proto_status_t rc = format_from(reply, &len_reply, client->name, cmd->text);
  if (rc != PROTO_OK) {
    send_proto_err(mgr, client->fd, rc);
    return;
  }
  if (poll_mgr_send(mgr, recipient->fd, reply, len_reply) == -1) {
    send_err(mgr, client->fd, "no such user");
    poll_mgr_close(mgr, recipient->fd);
    return;
  }
  send_ok(mgr, client->fd, NULL);
}

// Best effort OK, then close: the client is gone either way.
static void quit_client(int client_fd, poll_mgr_t* mgr) {
  send_ok(mgr, client_fd, NULL);
  poll_mgr_close(mgr, client_fd);
}

static void drain_auth_results(chat_t* chat, poll_mgr_t* mgr) {
  auth_result_t res;
  while (auth_collect(&res)) {
    chat_client_t* client = find_client_by_id(chat, res.client_id);
    if (!client || client->state != CLIENT_AUTHENTICATING) {
      continue;
    }

    switch (res.outcome) {
      case AUTH_OK: {
        if (find_client_by_name(chat, res.username)) {
          send_err(mgr, client->fd, "already logged in");
          client->state = CLIENT_CONNECTED;
          continue;
        }
        client->state = CLIENT_AUTHENTICATED;
        memcpy(client->name, res.username, sizeof(client->name));
        send_ok(mgr, client->fd, NULL);
        log_info("client %d logged in as %s", client->fd, client->name);
        break;
      }
      case AUTH_BAD_CREDENTIALS:
        if (++client->failures >= MAX_AUTH_ATTEMPTS) {
          send_err(mgr, client->fd, "too many failed attempts");
          poll_mgr_close(mgr, client->fd);
          continue;
        }
        send_err(mgr, client->fd, "bad credentials");
        client->state = CLIENT_CONNECTED;
        break;
      case AUTH_NAME_TAKEN:
        send_err(mgr, client->fd, "name taken");
        client->state = CLIENT_CONNECTED;
        break;
      case AUTH_INTERNAL:
        send_err(mgr, client->fd, "internal issue");
        poll_mgr_close(mgr, client->fd);
        break;
    }
  }
}

static void remove_unauth_clients(chat_t* chat, poll_mgr_t* mgr) {
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

static void handle_login(const protocol_cmd_t* cmd, poll_mgr_t* mgr,
                         chat_client_t* client) {
  auth_req_t auth_req = {0};
  auth_req.kind = cmd->type == CMD_REGISTER ? AUTH_REGISTER : AUTH_LOGIN;
  memcpy(auth_req.client_id, client->id, sizeof(client->id));
  memcpy(auth_req.username, cmd->username, sizeof(cmd->username));
  memcpy(auth_req.password, cmd->password, sizeof(cmd->password));
  if (auth_submit(&auth_req)) {
    client->state = CLIENT_AUTHENTICATING;
  } else {
    send_err(mgr, client->fd, "busy");
  }
  memset(&auth_req, 0, sizeof(auth_req));
}

static void handle_connected(chat_client_t* client, poll_mgr_t* mgr,
                             const protocol_cmd_t* cmd) {
  switch (cmd->type) {
    case CMD_REGISTER:
    case CMD_LOGIN: {
      handle_login(cmd, mgr, client);
      break;
    }
    case CMD_QUIT:
      quit_client(client->fd, mgr);
      break;
    case CMD_MSG:
      send_err(mgr, client->fd, "not allowed");
      break;
  }
}

static void handle_authenticated(const chat_client_t* client, poll_mgr_t* mgr,
                                 const protocol_cmd_t* cmd, chat_t* chat) {
  switch (cmd->type) {
    case CMD_REGISTER:
    case CMD_LOGIN:
      send_err(mgr, client->fd, "not allowed");
      break;
    case CMD_MSG:
      reroute_msg(chat, cmd, mgr, client);
      break;
    case CMD_QUIT:
      quit_client(client->fd, mgr);
      break;
  }
}

// Handles one complete line (without the newline) in client->inbuf.
static void handle_line(chat_client_t* client, poll_mgr_t* mgr, chat_t* chat) {
  protocol_cmd_t cmd;
  proto_status_t pt =
      client->overflow ? PROTO_ERR_TOO_LONG
                       : parse_protocol_str(client->inbuf, client->inlen, &cmd);

  if (pt != PROTO_OK) {
    send_proto_err(mgr, client->fd, pt);
  } else {
    switch (client->state) {
      case CLIENT_CONNECTED:
        handle_connected(client, mgr, &cmd);
        break;
      case CLIENT_AUTHENTICATING:
        send_err(mgr, client->fd, "busy");
        break;
      case CLIENT_AUTHENTICATED:
        handle_authenticated(client, mgr, &cmd, chat);
        break;
    }
  }

  memset(&cmd, 0, sizeof(cmd));
  memset(&client->inbuf, 0, sizeof(client->inbuf));
}

static void on_message(void* ctx, poll_mgr_t* mgr, int sender_fd,
                       const char* buf, size_t len) {
  chat_t* chat = ctx;
  chat_client_t* client = find_client(chat, sender_fd);
  if (!client) {
    return;
  }

  for (size_t i = 0; i < len; i++) {
    if (buf[i] == '\n') {
      handle_line(client, mgr, chat);
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

static void on_disconnect(void* ctx, poll_mgr_t* mgr, int client_fd) {
  (void)mgr;
  chat_t* chat = ctx;
  log_info("client %d left", client_fd);
  chat_client_t* client = find_client(chat, client_fd);
  if (!client) {
    return;
  }
  memset(client, 0, sizeof(*client));
}

static void on_connect(void* ctx, poll_mgr_t* mgr, int client_fd) {
  chat_t* chat = ctx;
  struct timespec now;

  // NOLINTNEXTLINE(misc-include-cleaner)
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    log_perror("clock_gettime");
    poll_mgr_close(mgr, client_fd);
    return;
  }
  log_info("client %d entered", client_fd);
  chat_client_t* client = alloc_client(chat);
  if (!client) {
    log_error("server full, refusing client %d", client_fd);
    send_err(mgr, client_fd, "server full");
    poll_mgr_close(mgr, client_fd);
    return;
  }
  if (generate_uuid4(client->id) != 0) {
    log_error("UUID generation failed for client %d", client_fd);
    poll_mgr_close(mgr, client_fd);
    return;
  }
  client->active = true;
  client->fd = client_fd;
  memcpy(&client->connected_at, &now, sizeof(now));
}

static void on_tick(void* ctx, poll_mgr_t* mgr) {
  chat_t* chat = ctx;
  drain_auth_results(chat, mgr);
  remove_unauth_clients(chat, mgr);
}

void chat_init(chat_t* chat) { memset(chat, 0, sizeof(*chat)); }

poll_callback_t chat_callbacks(chat_t* chat) {
  return (poll_callback_t){
      .on_connection = on_connect,
      .on_disconnection = on_disconnect,
      .on_message = on_message,
      .on_tick = on_tick,
      .ctx = chat,
  };
}
