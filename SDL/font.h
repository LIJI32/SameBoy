#ifndef font_h
#define font_h

#include <stdint.h>
extern uint8_t font[];
extern const uint8_t font_max;
#define GLYPH_HEIGHT 8
#define GLYPH_WIDTH 6
#define LEFT_ARROW_STRING "\x86"
#define RIGHT_ARROW_STRING "\x7F"
#define SELECTION_STRING RIGHT_ARROW_STRING
#define CTRL_STRING "\x80\x81\x82"
#define SHIFT_STRING "\x83"
#define CMD_STRING "\x84\x85"
#define ELLIPSIS_STRING "\x87"
#define MOJIBAKE_STRING "\x88"
#define SLIDER_STRING "\x89\x8A\x8A\x8A\x8A\x8A\x8A\x8A\x8A\x8A\x8F\x8A\x8A\x8A\x8A\x8A\x8A\x8A\x8A\x8A\x8B"
#define SELECTED_SLIDER_STRING "\x8C\x8D\x8D\x8D\x8D\x8D\x8D\x8D\x8D\x8D\x8D\x8D\x8D\x8D\x8D\x8D\x8D\x8D\x8D\x8D\x8E"
#endif /* font_h */

