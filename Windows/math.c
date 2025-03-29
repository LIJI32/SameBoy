#include <math.h>

#ifdef USE_MSVCRT_DLL

/* "Old" (Pre-2015) Windows headers/libc don't have round and exp2. */

__attribute__((no_builtin)) double round(double f)
{
    return f >= 0? (int)(f + 0.5) : (int)(f - 0.5);
}


__attribute__((no_builtin)) double exp2(double f)
{
    return pow(2, f);
}

#endif
