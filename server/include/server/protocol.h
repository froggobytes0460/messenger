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
  PROTO_OK,            ///< Successful operation
  PROTO_ERR_UNKNOWN,   ///< Unkown error
  PROTO_ERR_MISSING,   ///< Missing requirements error
  PROTO_ERR_TOO_LONG,  ///< Input/Output text too long error
  PROTO_ERR_BAD_INPUT, ///< Bad input from client error
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
 *
 * If the output is not @p PROTO_OK , the @p out is zeroed out, otherwise a
 * proper structure is put in @p out .
 * @param[in] line The string to parse.
 * @param[in] len The length of @p line .
 * @param[out] out Parses string into this structure.
 */
proto_status_t parse_protocol_str(const char *line, size_t len,
                                  protocol_cmd_t *out);

/**
 * @brief Formats an @c OK reply line, e.g. @c "OK\n" or @c "OK logged in\n".
 *
 * Reply formatters never truncate. @p len is value-result, like @c accept() :
 * on entry it is the capacity of @p buf , on return it is the number of bytes
 * written (the line, including the trailing newline, without a terminator). On
 * failure @p len is set to 0.
 * @param[out] buf Receives the reply line.
 * @param[in,out] len Capacity of @p buf in, bytes written out.
 * @param[in] text Optional text after @c OK , or @c NULL for none.
 * @return @c PROTO_OK , or @c PROTO_ERR_TOO_LONG if the line does not fit.
 */
proto_status_t format_ok(char *buf, size_t *len, const char *text);

/**
 * @brief Formats an @c ERR reply line from a free-form reason.
 *
 * For failures that are not parse errors, e.g. @c "busy" or @c "no such user".
 * @p len behaves as in format_ok().
 * @param[out] buf Receives the reply line.
 * @param[in,out] len Capacity of @p buf in, bytes written out.
 * @param[in] reason Non-empty reason text.
 * @return @c PROTO_OK , @c PROTO_ERR_MISSING if @p reason is NULL or empty, or
 * @c PROTO_ERR_TOO_LONG if the line does not fit.
 */
proto_status_t format_err_reason(char *buf, size_t *len, const char *reason);

/**
 * @brief Formats an @c ERR reply line for a failed parse.
 *
 * @p len behaves as in format_ok().
 * @param[out] buf Receives the reply line.
 * @param[in,out] len Capacity of @p buf in, bytes written out.
 * @param[in] why The failing status. @c PROTO_OK is not a failure.
 * @return @c PROTO_OK , @c PROTO_ERR_BAD_INPUT if @p why is @c PROTO_OK or
 * not a known status, or @c PROTO_ERR_TOO_LONG if the line does not fit.
 */
proto_status_t format_err(char *buf, size_t *len, proto_status_t why);

/**
 * @brief Formats a pushed @c FROM reply line: @c "FROM <user> <text>\n".
 *
 * @p len behaves as in format_ok().
 * @param[out] buf Receives the reply line.
 * @param[in,out] len Capacity of @p buf in, bytes written out.
 * @param[in] user Sender's username, non-empty.
 * @param[in] text Message text, non-empty.
 * @return @c PROTO_OK , @c PROTO_ERR_MISSING if @p user or @p text is NULL or
 * empty, or @c PROTO_ERR_TOO_LONG if the line does not fit.
 */
proto_status_t format_from(char *buf, size_t *len, const char *user,
                           const char *text);
#endif // !PROTOCOL_H
