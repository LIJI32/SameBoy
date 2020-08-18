#ifndef coverage_h
#define coverage_h

#include <stdint.h>
#include "HashTable/hashtable.h"

typedef struct GB_coverage
{
    bool enabled;
    HashTable covered_functions;
}GB_coverage;

void GB_coverage_initialize(GB_coverage* pCov);
void GB_coverage_start(GB_coverage* pCov);
void GB_coverage_reset(GB_coverage* pCov);
bool GB_coverage_write_result(GB_coverage* pCov, char* pPath);
void GB_coverage_add_data_point(GB_coverage* pCov, uint16_t bank, uint16_t address);
void GB_coverage_deinitialize(GB_coverage* pCov);

#endif
