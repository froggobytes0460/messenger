#ifndef UUID_H
#define UUID_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @file uuid.h
 * @brief Handles UUIDs (UUIDv4)
 */

#define UUID_LEN_BYTES 16

typedef uint8_t client_uuid_t[UUID_LEN_BYTES];

/**
 * @brief Generates UUIDv4 randomly.
 * @param[out] out The UUID in which to generate.
 * @note ignore @p out if -1 is returned.
 * @return 0 if successful, otherwise -1
 */
int generate_uuid4(client_uuid_t out);

/**
 * @brief Compares two UUIDs.
 * @param[in] a First UUID to compare.
 * @param[in] b Second UUID to compare.
 */
bool uuid_equals(const client_uuid_t a, const client_uuid_t b);

#endif // !UUID_H
