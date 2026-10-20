#include <server/log.h>
#include <server/uuid.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define LOG_TAG "uuid"

#define UUID_VERSION_IDX 6U
#define UUID_VARIANT_IDX 8U

#define UUID_V4_MASK 0x0FU
#define UUID_V4_MARKER 0x40U

#define UUID_VAR_MASK 0x3FU
#define UUID_VAR_MARKER 0x80U

int generate_uuid4(client_uuid_t out) {
  if (getentropy(out, sizeof(client_uuid_t)) != 0) {
    log_perror("getentropy");
    return -1;
  }

  out[UUID_VERSION_IDX] =
      (out[UUID_VERSION_IDX] & UUID_V4_MASK) | UUID_V4_MARKER;
  out[UUID_VARIANT_IDX] =
      (out[UUID_VARIANT_IDX] & UUID_VAR_MASK) | UUID_VAR_MARKER;

  return 0;
}

bool uuid_equals(const client_uuid_t a, const client_uuid_t b) {
  return memcmp(a, b, sizeof(client_uuid_t)) == 0;
}
