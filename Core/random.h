#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "defs.h"

#ifdef GB_INTERNAL
internal uint8_t GB_random(void);
internal uint32_t GB_random32(void);
#endif
void GB_random_seed(uint64_t seed);
void GB_random_set_enabled(bool enable);
