#ifndef gb_struct_def_h
#define gb_struct_def_h
#include <stdint.h>
struct GB_gameboy_s;
typedef struct GB_gameboy_s GB_gameboy_t;

#ifdef GB_16BIT_OUTPUT_COLOR
typedef uint16_t GB_output_color_t;
#else
typedef uint32_t GB_output_color_t;
#endif
#endif 
