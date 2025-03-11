#pragma once

#include <stdint.h>
#include "position.h"
#include "types.h"

#define bb_count(bb) __builtin_popcountll(bb)
#define FILE_MASK(file) (0x8080808080808080ULL >> (file))
#define WHITE_MASK 0xAAAAAAAAAAAAAAAAULL
#define BLACK_MASK (~WHITE_MASK)

void set_bbs(struct position *pos);
