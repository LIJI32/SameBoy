#ifndef GB_MACBOOK_MOTION_H
#define GB_MACBOOK_MOTION_H

#include <stdbool.h>

bool GB_macbook_motion_start(void);
void GB_macbook_motion_stop(void);

// Returns true and fills x, y, z (in g-units) if data is available.
bool GB_macbook_motion_poll(double *x, double *y, double *z);

#endif
