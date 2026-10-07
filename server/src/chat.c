#include <server/chat.h>
#include <server/constants.h>
#include <server/poll_mgr.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static void handle_message(void *ctx, poll_mgr_t *mgr, int sender_fd,
                           const char *buf, size_t len) {
  chat_t *chat = ctx;
  chat->messages_relayed++;
  printf("[Client %d]: %.*s", sender_fd, (int)len, buf);
  poll_mgr_broadcast(mgr, sender_fd, buf, len);
}

static void on_disconnect(void *ctx, poll_mgr_t *mgr, int client_fd) {
  (void)ctx;
  printf("[SERVER]: Client with socket descriptor %d left.\n", client_fd);
  char msg[BROADCAST_MESSAGE_LENGTH];
  int l_msg = snprintf(msg, sizeof(msg), "User left the chat...\n");
  if (l_msg > 0) {
    poll_mgr_broadcast(mgr, client_fd, msg, (size_t)l_msg);
  }
}

static void on_connect(void *ctx, poll_mgr_t *mgr, int client_fd) {
  (void)ctx;
  printf("[SERVER]: Client with socket descriptor %d entered.\n", client_fd);
  char msg[BROADCAST_MESSAGE_LENGTH];
  int l_msg = snprintf(msg, sizeof(msg), "User joined the chat...\n");
  if (l_msg > 0) {
    poll_mgr_broadcast(mgr, client_fd, msg, (size_t)l_msg);
  }
}

void chat_init(chat_t *chat) { memset(chat, 0, sizeof(*chat)); }

poll_callback_t chat_callbacks(chat_t *chat) {
  return (poll_callback_t){
      .on_connection = on_connect,
      .on_disconnection = on_disconnect,
      .on_message = handle_message,
      .ctx = chat,
  };
}
