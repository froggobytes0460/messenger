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
        "password over limit",
        "LOGIN bob "
        "pppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppppp"
        "ppppp"
        "pppppppppppppppppppppppppppppppppppppppppppppppppppppp", // 129 'p'
        PROTO_ERR_TOO_LONG,
    },
    {
        "msg text over limit",
        "MSG alice "
        "tttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttt"
        "tttt"
        "tttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttt"
        "tttt"
        "ttttttttttttttttttttttttttttttttttttttttttttttttttttt", // 201 't'
        PROTO_ERR_TOO_LONG,
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

int main(void) {
  const struct CMUnitTest unit_tests[] = {
      cmocka_unit_test(test_rejected_lines),
      cmocka_unit_test(test_expected_lines),
  };

  return cmocka_run_group_tests(unit_tests, NULL, NULL);
}
