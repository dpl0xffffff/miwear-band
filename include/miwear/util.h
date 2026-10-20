#ifndef MIWEAR_UTIL_H
#define MIWEAR_UTIL_H

#include <stdint.h>

static inline uint32_t miwear_read_u32_le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint16_t miwear_read_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

#endif
