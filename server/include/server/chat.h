#ifndef CHAT_H
#define CHAT_H

#include <server/constants.h>
#include <server/poll_mgr.h>
#include <stdbool.h>
#include <stddef.h>

// Per-connection chat state. A zeroed slot is an unused one.
typedef struct {
  bool active;
  int fd;
  bool named;
  char name[MAX_NAME_LEN + 1];

  // Bytes received so far for the current, not yet newline-terminated line.
  char inbuf[BUFFER_SIZE];
  size_t inlen;
} chat_client_t;

typedef struct {
  chat_client_t clients[MAX_CONNECTIONS];
} chat_t;

void chat_init(chat_t *chat);

// Callbacks that implement the chat room on top of the poll manager. `chat`
// is passed back to the handlers as ctx and must outlive the run loop.
//
// Protocol: the first line a client sends is its username; every following
// line is a chat message broadcast to everyone else.
poll_callback_t chat_callbacks(chat_t *chat);

#endif // !CHAT_H
