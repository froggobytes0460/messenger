#include <server/chat.h>
#include <server/constants.h>
#include <server/poll_mgr.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

static void announce(poll_mgr_t *mgr, int except_fd, const char *name,
                     const char *text) {
  char msg[MAX_NAME_LEN + MAX_ANNOUNCE_TEXT_LEN];
  int len = snprintf(msg, sizeof(msg), "%s %s\n", name, text);
  if (len > 0) {
    poll_mgr_broadcast(mgr, except_fd, msg, (size_t)len);
  }
}

// Handles one complete line (without the newline) in client->inbuf.
static void handle_line(chat_client_t *client, poll_mgr_t *mgr) {
  if (client->inlen > 0 && client->inbuf[client->inlen - 1] == '\r') {
    client->inlen--;
  }
  client->inbuf[client->inlen] = '\0';
  if (client->inlen == 0) {
    return;
  }

  if (!client->named) {
    // Usernames longer than MAX_NAME_LEN are truncated.
    size_t name_len =
        client->inlen < MAX_NAME_LEN ? client->inlen : MAX_NAME_LEN;
    memcpy(client->name, client->inbuf, name_len);
    client->name[name_len] = '\0';
    client->named = true;
    printf("[SERVER]: Client %d is now known as %s.\n", client->fd,
           client->name);
    announce(mgr, client->fd, client->name, "joined the chat...");
    return;
  }

  char msg[MAX_NAME_LEN + 2 + BUFFER_SIZE + 2];
  int len = snprintf(msg, sizeof(msg), "%s: %s\n", client->name, client->inbuf);
  if (len > 0) {
    printf("[%s]: %s\n", client->name, client->inbuf);
    poll_mgr_broadcast(mgr, client->fd, msg, (size_t)len);
  }
}

static void on_message(void *ctx, poll_mgr_t *mgr, int sender_fd,
                       const char *buf, size_t len) {
  chat_client_t *client = find_client(ctx, sender_fd);
  if (client == NULL) {
    return;
  }

  for (size_t i = 0; i < len; i++) {
    if (buf[i] == '\n') {
      handle_line(client, mgr);
      client->inlen = 0;
    } else if (client->inlen < sizeof(client->inbuf) - 1) {
      // Overlong lines are truncated: extra bytes are dropped until newline.
      client->inbuf[client->inlen++] = buf[i];
    }
  }
}

static void on_disconnect(void *ctx, poll_mgr_t *mgr, int client_fd) {
  printf("[SERVER]: Client with socket descriptor %d left.\n", client_fd);
  chat_client_t *client = find_client(ctx, client_fd);
  if (client == NULL) {
    return;
  }

  if (client->named) {
    announce(mgr, client_fd, client->name, "left the chat...");
  }
  memset(client, 0, sizeof(*client));
}

static void on_connect(void *ctx, poll_mgr_t *mgr, int client_fd) {
  (void)mgr;
  printf("[SERVER]: Client with socket descriptor %d entered.\n", client_fd);
  chat_client_t *client = alloc_client(ctx);
  if (client == NULL) {
    return;
  }
  client->active = true;
  client->fd = client_fd;
}

void chat_init(chat_t *chat) { memset(chat, 0, sizeof(*chat)); }

poll_callback_t chat_callbacks(chat_t *chat) {
  return (poll_callback_t){
      .on_connection = on_connect,
      .on_disconnection = on_disconnect,
      .on_message = on_message,
      .ctx = chat,
  };
}
