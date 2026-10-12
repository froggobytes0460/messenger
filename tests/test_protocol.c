#include <server/constants.h>
#include <server/protocol.h>
#include <stdbool.h>
#include <string.h>

// clang-format off
#include <cmocka.h>
#include <setjmp.h> // IWYU pragma: keep
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
// clang-format on

#define LINE_SIZE 512

// Helpers:
static void assert_cmd_equal(const protocol_cmd_t *got,
                             const protocol_cmd_t *want) {
  assert_int_equal(got->type, want->type);
  assert_string_equal(got->username, want->username);
  assert_string_equal(got->password, want->password);
  assert_string_equal(got->text, want->text);
}

// Parameterized testing case structures:
typedef struct {
  const char *case_name;
  const char *line;
  const proto_status_t rc;
} bad_case_t;

typedef struct {
  const char *case_name;
  const char *line;
  const protocol_cmd_t *expected;
} expected_case_t;

// Parameters for tests.
static const bad_case_t bad_cases[] = {
    {"unknown command", "HELLO bob", PROTO_ERR_UNKNOWN},
    {"missing password", "LOGIN bob", PROTO_ERR_MISSING},
    {"empty field", "LOGIN  pw", PROTO_ERR_MISSING},
    {"stray CR", "QUIT\r", PROTO_ERR_BAD_INPUT},
    {"empty line", "", PROTO_ERR_MISSING},
    {"leading space", " LOGIN bob pw", PROTO_ERR_MISSING},
    {"quit trailing space", "QUIT ", PROTO_ERR_BAD_INPUT},
    {"quit extra word", "QUIT extra", PROTO_ERR_BAD_INPUT},
    {"login extra word", "LOGIN bob pw extra", PROTO_ERR_BAD_INPUT},
    {"login trailing space", "LOGIN bob pw ", PROTO_ERR_BAD_INPUT},
    {"byte >= 0x80", "LOGIN bob p\xc3\xa9", PROTO_ERR_BAD_INPUT},
    {"missing msg text", "MSG alice", PROTO_ERR_MISSING},
    {
        "username over limit",
        "LOGIN aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa pw", // 33 'a'
        PROTO_ERR_TOO_LONG,
    },
    {
        "msg recipient over limit",
        "MSG aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa hi", // 33 'a'
        PROTO_ERR_TOO_LONG,
    },
    {
        "invalid username in login",
        "LOGIN bob! pass",
        PROTO_ERR_BAD_INPUT,
    },
    {
        "invalid username in msg",
        "MSG bob! HI!",
        PROTO_ERR_BAD_INPUT,
    },
};

static const expected_case_t expected_cases[] = {
    {
        "register",
        "REGISTER bob hunter2",
        &(const protocol_cmd_t){
            .type = CMD_REGISTER,
            .username = "bob",
            .password = "hunter2",
        },
    },
    {
        "login",
        "LOGIN bob hunter2",
        &(const protocol_cmd_t){
            .type = CMD_LOGIN,
            .username = "bob",
            .password = "hunter2",
        },
    },
    {
        "msg single word",
        "MSG alice hi",
        &(const protocol_cmd_t){
            .type = CMD_MSG,
            .username = "alice",
            .text = "hi",
        },
    },
    {
        "msg keeps spaces in text",
        "MSG alice hello there, how are you?",
        &(const protocol_cmd_t){
            .type = CMD_MSG,
            .username = "alice",
            .text = "hello there, how are you?",
        },
    },
    {"quit", "QUIT", &(const protocol_cmd_t){.type = CMD_QUIT}},
    {
        "username at max length",
        "LOGIN aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa pw", // 32 'a'
        &(const protocol_cmd_t){
            .type = CMD_LOGIN,
            .username = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            .password = "pw",
        },
    },
};

// Tests:
static void test_rejected_lines(void **state) {
  (void)state;
  for (size_t i = 0; i < sizeof(bad_cases) / sizeof(bad_case_t); i++) {
    print_message("case: %s\n", bad_cases[i].case_name);
    protocol_cmd_t cmd;
    int rc =
        parse_protocol_str(bad_cases[i].line, strlen(bad_cases[i].line), &cmd);
    assert_int_equal(rc, bad_cases[i].rc);
  }
}

static void test_expected_lines(void **state) {
  (void)state;
  for (size_t i = 0; i < sizeof(expected_cases) / sizeof(expected_case_t);
       i++) {
    print_message("case: %s\n", expected_cases[i].case_name);
    protocol_cmd_t cmd;
    int rc = parse_protocol_str(expected_cases[i].line,
                                strlen(expected_cases[i].line), &cmd);
    assert_int_equal(rc, PROTO_OK);
    assert_cmd_equal(&cmd, expected_cases[i].expected);
  }
}

static void test_len_bounds_parsing(void **state) {
  (void)state;
  protocol_cmd_t cmd;
  // Bytes past len must never be read.
  assert_int_equal(parse_protocol_str("QUIT extra", 4, &cmd), PROTO_OK);
  assert_int_equal(cmd.type, CMD_QUIT);
  assert_int_equal(parse_protocol_str("LOGIN bob pw extra", 12, &cmd),
                   PROTO_OK);
  assert_string_equal(cmd.password, "pw");
  assert_int_equal(parse_protocol_str("anything", 0, &cmd), PROTO_ERR_MISSING);
}

// Builds "<prefix><n times ch>" into buf.
static void build_line(char *buf, const char *prefix, char ch, size_t n) {
  size_t p = strlen(prefix);
  memcpy(buf, prefix, p);
  memset(buf + p, ch, n);
  buf[p + n] = '\0';
}

static void test_field_limit_boundaries(void **state) {
  (void)state;
  char line[LINE_SIZE];
  protocol_cmd_t cmd;

  build_line(line, "LOGIN bob ", 'p', MAX_PASSWORD);
  assert_int_equal(parse_protocol_str(line, strlen(line), &cmd), PROTO_OK);
  assert_int_equal(strlen(cmd.password), MAX_PASSWORD);
  build_line(line, "LOGIN bob ", 'p', MAX_PASSWORD + 1);
  assert_int_equal(parse_protocol_str(line, strlen(line), &cmd),
                   PROTO_ERR_TOO_LONG);

  build_line(line, "MSG alice ", 't', MAX_TEXT);
  assert_int_equal(parse_protocol_str(line, strlen(line), &cmd), PROTO_OK);
  assert_int_equal(strlen(cmd.text), MAX_TEXT);
  build_line(line, "MSG alice ", 't', MAX_TEXT + 1);
  assert_int_equal(parse_protocol_str(line, strlen(line), &cmd),
                   PROTO_ERR_TOO_LONG);
}

int main(void) {
  const struct CMUnitTest unit_tests[] = {
      cmocka_unit_test(test_rejected_lines),
      cmocka_unit_test(test_expected_lines),
      cmocka_unit_test(test_len_bounds_parsing),
      cmocka_unit_test(test_field_limit_boundaries),
  };

  return cmocka_run_group_tests(unit_tests, NULL, NULL);
}
