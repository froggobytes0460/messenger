#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <server/constants.h>
#include <stddef.h>

/**
 * @file protocol.h
 * @brief Handles server/client protocol I/O.
 */

/**
 * @brief Types of commands supported by the server.
 */
typedef enum {
  CMD_REGISTER, ///< Register client as new user
  CMD_LOGIN,    ///< Login client as existing user
  CMD_MSG,      ///< Sends message to other client
  CMD_QUIT,     ///< Exits server
} commands_t;

/**
 * @brief Return codes that can be given by server.
 */
typedef enum {
  PROTO_OK,           ///< Successful operation
  PROTO_ERR_UNKNOWN,  ///< Unkown error
  PROTO_ERR_MISSING,  ///< Missing requirements error
  PROTO_ERR_TOO_LONG, ///< Input/Output text too long error
  PROTO_ERR_BAD_CHAR, ///< Bad input from client error
} proto_status_t;

/**
 * @brief Structure of commands that can be sent by client.
 */
typedef struct {
  commands_t type; ///< The type of command.

  /// The username (login/register require own, whereas message requires
  /// recipient).
  char username[MAX_USERNAME + 1];
  char password[MAX_PASSWORD + 1]; ///< The password (login/register only)
  char text[MAX_TEXT + 1];         ///< The text in the command (message only)
} protocol_cmd_t;

/**
 * @brief Parses protocol structure from string.
 * @param[in] line The string to parse.
 * @param[in] len The length of @p line .
 * @param[out] out Parses string into this structure.
 */
proto_status_t parse_protocol_str(const char *line, size_t len,
                                  protocol_cmd_t *out);
#endif // !PROTOCOL_H
