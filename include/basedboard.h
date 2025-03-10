#pragma once

#include <stdint.h>
#include "position.h"
#include "types.h"

#define bb_count(bb) __builtin_popcountll(bb)

void set_bbs(struct position *pos);
