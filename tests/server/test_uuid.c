#include <server/uuid.h>

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

int main(void) {
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_version_and_variant_bits),
      cmocka_unit_test(test_calls_differ),
  };
  return cmocka_run_group_tests(tests, NULL, NULL);
}
