#include <server/constants.h>
#include <server/protocol.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define MIN_PRINTABLE_CHAR 0x20
#define ASCII_DEL 0x7f
#define DELIM ' '

/// Read position inside the line. Every scan stops at @c end.
typedef struct {
  const char* cur;
  const char* end;
} cursor_t;

typedef struct {
  commands_t type;
  const char* name;
} cmd_entry_t;

static const cmd_entry_t commands[] = {
    {CMD_REGISTER, "REGISTER"},
    {CMD_LOGIN, "LOGIN"},
    {CMD_MSG, "MSG"},
    {CMD_QUIT, "QUIT"},
};

/// ASCII only: rejects control characters, DEL and every byte >= 0x80.
static bool validate_line(const char* str, size_t len) {
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)str[i];
    if (c < MIN_PRINTABLE_CHAR || c >= ASCII_DEL) {
      return false;
    }
  }
  return true;
}

/// Validates if all bytes in username falls under allowed range (A-Z, a-z,
/// 0-9), with exceptions of '-' and '_'.
static bool validate_username(const char* username, size_t len) {
  for (size_t i = 0; i < len; i++) {
    unsigned char c = username[i];
    if (c == '-' || c == '_') {
      continue;
    }
    // Ranges for valid character
    bool is_digit = ('0' <= c && c <= '9') != false;
    bool is_alphaupper = ('A' <= c && c <= 'Z') != false;
    bool is_alphalower = ('a' <= c && c <= 'z') != false;

    if (!is_digit && !is_alphalower && !is_alphaupper) {
      return false;
    }
  }
  return true;
}

/// Returns the next token and leaves the cursor on the separator (or the end).
static size_t take_token(cursor_t* c, const char* tok[]) {
  *tok = c->cur;
  size_t remaining = (size_t)(c->end - c->cur);
  const char* sep = remaining > 0 ? memchr(c->cur, DELIM, remaining) : NULL;
  size_t n = sep != NULL ? (size_t)(sep - c->cur) : remaining;
  c->cur += n;
  return n;
}

/// Steps over exactly one separator. False if the line ends here.
static bool skip_sep(cursor_t* c) {
  if (c->cur == c->end) {
    return false;
  }
  c->cur++;
  return true;
}

static proto_status_t copy_field(char* dst, size_t max, const char* src,
                                 size_t n) {
  if (n == 0) {
    return PROTO_ERR_MISSING;
  }
  if (n > max) {
    return PROTO_ERR_TOO_LONG;
  }
  memcpy(dst, src, n);
  dst[n] = '\0';
  return PROTO_OK;
}

static proto_status_t parse_credentials(cursor_t* c, protocol_cmd_t* out) {
  const char* tok = NULL;
  if (!skip_sep(c)) {
    return PROTO_ERR_MISSING;
  }

  size_t n = take_token(c, &tok);
  proto_status_t rc = copy_field(out->username, MAX_USERNAME, tok, n);
  if (rc != PROTO_OK) {
    return rc;
  }
  if (!validate_username(out->username, n)) {
    return PROTO_ERR_BAD_INPUT;
  }

  if (!skip_sep(c)) {
    return PROTO_ERR_MISSING;
  }
  n = take_token(c, &tok);
  rc = copy_field(out->password, MAX_PASSWORD, tok, n);
  if (rc != PROTO_OK) {
    return rc;
  }

  // Anything left (extra word, trailing space) is not allowed.
  return c->cur == c->end ? PROTO_OK : PROTO_ERR_BAD_INPUT;
}

/// The text is everything after the recipient, spaces included.
static proto_status_t parse_msg(cursor_t* c, protocol_cmd_t* out) {
  const char* tok = NULL;
  if (!skip_sep(c)) {
    return PROTO_ERR_MISSING;
  }

  size_t n = take_token(c, &tok);
  proto_status_t rc = copy_field(out->username, MAX_USERNAME, tok, n);
  if (rc != PROTO_OK) {
    return rc;
  }
  if (!validate_username(out->username, n)) {
    return PROTO_ERR_BAD_INPUT;
  }

  if (!skip_sep(c)) {
    return PROTO_ERR_MISSING;
  }
  size_t text_len = (size_t)(c->end - c->cur);
  size_t first_char = 0;
  while (first_char < text_len && c->cur[first_char] == ' ') {
    first_char++;
  }
  if (first_char == text_len) {
    return PROTO_ERR_MISSING;  // Nothing but spaces is not a message.
  }
  return copy_field(out->text, MAX_TEXT, c->cur, text_len);
}

static bool lookup_command(const char* tok, size_t n, commands_t* type) {
  for (size_t i = 0; i < sizeof(commands) / sizeof(cmd_entry_t); i++) {
    if (strlen(commands[i].name) == n &&
        memcmp(commands[i].name, tok, n) == 0) {
      *type = commands[i].type;
      return true;
    }
  }
  return false;
}

static proto_status_t parse_line(const char* line, size_t len,
                                 protocol_cmd_t* out) {
  if (!validate_line(line, len)) {
    return PROTO_ERR_BAD_INPUT;
  }

  cursor_t c = {.cur = line, .end = line + len};
  const char* tok = NULL;
  size_t n = take_token(&c, &tok);
  if (n == 0) {
    return PROTO_ERR_MISSING;
  }
  if (!lookup_command(tok, n, &out->type)) {
    return PROTO_ERR_UNKNOWN;
  }

  switch (out->type) {
    case CMD_REGISTER:
    case CMD_LOGIN:
      return parse_credentials(&c, out);
    case CMD_MSG:
      return parse_msg(&c, out);
    case CMD_QUIT:
      return c.cur == c.end ? PROTO_OK : PROTO_ERR_BAD_INPUT;
  }
  return PROTO_ERR_UNKNOWN;
}

proto_status_t parse_protocol_str(const char* line, size_t len,
                                  protocol_cmd_t* out) {
  memset(out, 0, sizeof(*out));
  proto_status_t rc = parse_line(line, len, out);
  if (rc != PROTO_OK) {
    // A failed parse may hold a partial username or password.
    memset(out, 0, sizeof(*out));
  }
  return rc;
}

typedef struct {
  proto_status_t status;
  const char* reason;
} err_entry_t;

static const err_entry_t err_reasons[] = {
    {PROTO_ERR_UNKNOWN, "unknown command"},
    {PROTO_ERR_MISSING, "missing field"},
    {PROTO_ERR_TOO_LONG, "too long"},
    {PROTO_ERR_BAD_INPUT, "bad input"},
};

static bool is_empty(const char* s) { return (s == NULL || s[0] == '\0') != 0; }

/// Formats one line into a scratch buffer, then copies it to @p buf if it fits.
/// @p len is value-result: capacity in, bytes written out (0 on failure).
__attribute__((format(printf, 3, 4))) static proto_status_t emit_line(
    char* buf, size_t* len, const char* fmt, ...) {
  char line[MAX_REPLY];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(line, sizeof(line), fmt, ap);
  va_end(ap);

  if (n < 0 || (size_t)n >= sizeof(line) || (size_t)n > *len) {
    *len = 0;
    return PROTO_ERR_TOO_LONG;
  }
  memcpy(buf, line, (size_t)n);
  *len = (size_t)n;
  return PROTO_OK;
}

proto_status_t format_ok(char* buf, size_t* len, const char* text) {
  if (is_empty(text)) {
    return emit_line(buf, len, "OK\n");
  }
  return emit_line(buf, len, "OK %s\n", text);
}

proto_status_t format_err_reason(char* buf, size_t* len, const char* reason) {
  if (is_empty(reason)) {
    *len = 0;
    return PROTO_ERR_MISSING;
  }
  return emit_line(buf, len, "ERR %s\n", reason);
}

proto_status_t format_err(char* buf, size_t* len, proto_status_t why) {
  for (size_t i = 0; i < sizeof(err_reasons) / sizeof(err_entry_t); i++) {
    if (err_reasons[i].status == why) {
      return format_err_reason(buf, len, err_reasons[i].reason);
    }
  }
  *len = 0;
  return PROTO_ERR_BAD_INPUT;
}

proto_status_t format_from(char* buf, size_t* len, const char* user,
                           const char* text) {
  if (is_empty(user) || is_empty(text)) {
    *len = 0;
    return PROTO_ERR_MISSING;
  }
  return emit_line(buf, len, "FROM %s %s\n", user, text);
}
