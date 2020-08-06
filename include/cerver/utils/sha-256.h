#ifndef _CERVER_UTILS_SHA_256_H_
#define _CERVER_UTILS_SHA_256_H_

#include <stdint.h>

#include "cerver/config.h"

CERVER_PUBLIC void sha_256_calc (uint8_t hash[32], const void *input, size_t len);

CERVER_PUBLIC void sha_256_hash_to_string (char string[65], const uint8_t hash[32]);

#endif