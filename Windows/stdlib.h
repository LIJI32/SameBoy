#pragma once
#include_next <stdlib.h>

#ifndef __MINGW32__
static inline long int random(void)
{
    return rand();
}
#endif
