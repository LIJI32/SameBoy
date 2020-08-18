#include "coverage.h"

#include <assert.h>
#include <stdio.h>

#define GB_coverage_INITIAL_CAPACITY 0x400

void GB_coverage_initialize(GB_coverage* pCov)
{
    assert(NULL != pCov);
    // Should succeed unless out of memory or programmer doble init
    pCov->enabled = false;
    assert(HT_SUCCESS == ht_setup(&pCov->covered_functions, sizeof(uint32_t), sizeof(uint32_t), GB_coverage_INITIAL_CAPACITY));
}

void GB_coverage_reset(GB_coverage* pCov)
{
    assert(NULL != pCov);

    pCov->enabled = false;
    assert(HT_SUCCESS == ht_clear(&pCov->covered_functions));
}

void GB_coverage_start(GB_coverage* pCov)
{
    assert(NULL != pCov);

    pCov->enabled = true;
}

bool GB_coverage_write_result(GB_coverage* pCov, char* pPath)
{
    assert(NULL != pCov);
    assert(NULL != pPath);

    HTNode* node = NULL;
	HTNode* next = NULL;

    pCov->enabled = false;

    for (uint32_t chain = 0; chain < pCov->covered_functions.capacity; chain++) 
    {
		node = pCov->covered_functions.nodes[chain];
		while (node) 
        {
            if (NULL !=  node->key && NULL !=  node->value)
            {
                uint32_t key = *(uint32_t*)node->key;
                uint32_t value = *(uint32_t*)node->value;
                printf("Node: 0x%08x, %u\n", key, value);
            }
            next = node->next;
			node = next;
		}
	}
    return true;
}

void GB_coverage_add_data_point(GB_coverage* pCov, uint16_t bank, uint16_t address)
{
    assert(NULL != pCov);

    if (pCov->enabled)
    {
        uint32_t key = ((uint32_t)bank) << 16 | address;

        //printf("Insert: 0x%08x\n", key);

        if (NULL == ht_lookup(&pCov->covered_functions,&key))
        {
            uint32_t value = 1; // Hit count
            assert(HT_ERROR != ht_insert(&pCov->covered_functions, &key, &value));
        }
        else
        {
            uint32_t value = *(uint32_t*)ht_lookup(&pCov->covered_functions,&key);
            value++;
            assert(HT_ERROR != ht_insert(&pCov->covered_functions, &key, &value));
        }
    }
}

void GB_coverage_deinitialize(GB_coverage* pCov)
{
    assert(NULL != pCov);

    pCov->enabled = false;
    assert(HT_SUCCESS == ht_destroy(&pCov->covered_functions));
}