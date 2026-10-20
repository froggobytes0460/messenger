#include <server/uuid.h>
#include <string.h>

// clang-format off
#include <cmocka.h>
#include <setjmp.h> // IWYU pragma: keep
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
// clang-format on

#define UUID_ITERATIONS 500
#define UUID_VERSION_BYTE 6
#define UUID_VARIANT_BYTE 8

static void test_version_and_variant_bits(void** state) {
  (void)state;
  for (int i = 0; i < UUID_ITERATIONS; i++) {
    client_uuid_t id;
    assert_int_equal(generate_uuid4(id), 0);
    // Version 4: high nibble of byte 6 is 0100.
    assert_int_equal(id[UUID_VERSION_BYTE] >> 4, 4);
    // RFC 4122 variant: top two bits of byte 8 are 10.
    assert_int_equal(id[UUID_VARIANT_BYTE] >> 6, 2);
  }
}

static void test_calls_differ(void** state) {
  (void)state;
  client_uuid_t a;
  client_uuid_t b;
  assert_int_equal(generate_uuid4(a), 0);
  assert_int_equal(generate_uuid4(b), 0);
  assert_memory_not_equal(a, b, UUID_LEN_BYTES);
}

static void test_equals_same_value(void** state) {
  (void)state;
  client_uuid_t a;
  client_uuid_t copy;
  assert_int_equal(generate_uuid4(a), 0);
  memcpy(copy, a, UUID_LEN_BYTES);
  assert_true(uuid_equals(a, copy));
  assert_true(uuid_equals(a, a));
}

static void test_equals_differs_in_any_byte(void** state) {
  (void)state;
  client_uuid_t a;
  assert_int_equal(generate_uuid4(a), 0);
  // A difference in the first, middle or last byte must all be detected.
  for (int i = 0; i < UUID_LEN_BYTES; i++) {
    client_uuid_t b;
    memcpy(b, a, UUID_LEN_BYTES);
    b[i] ^= 0x01;
    print_message("differs at byte %d\n", i);
    assert_false(uuid_equals(a, b));
  }
}

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_version_and_variant_bits),
      cmocka_unit_test(test_calls_differ),
      cmocka_unit_test(test_equals_same_value),
      cmocka_unit_test(test_equals_differs_in_any_byte),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
