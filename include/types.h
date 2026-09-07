#ifndef TYPES_H
#define TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef size_t usize;
typedef ptrdiff_t isize;

typedef bool b8;

_Static_assert(sizeof(u64) == 8, "u64 must be 8 bytes");
_Static_assert(sizeof(usize) == sizeof(void*), "usize must hold a pointer");

#endif
