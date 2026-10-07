#ifndef CHAT_H
#define CHAT_H

#include <server/poll_mgr.h>
#include <stddef.h>

typedef struct {
  size_t messages_relayed;
} chat_t;

void chat_init(chat_t *chat);

poll_callback_t chat_callbacks(chat_t *chat);

#endif // !CHAT_H
