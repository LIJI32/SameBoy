#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#define CON_EOF "\x04"
bool CON_start(char *(*completer)(const char *substring, uintptr_t *context));
char *CON_readline(const char *prompt);
char *CON_readline_async(void);

typedef struct {
    enum {
        CON_COLOR_NONE = 0,
        CON_COLOR_BLACK,
        CON_COLOR_RED,
        CON_COLOR_GREEN,
        CON_COLOR_YELLOW,
        CON_COLOR_BLUE,
        CON_COLOR_MAGENTA,
        CON_COLOR_CYAN,
        CON_COLOR_LIGHT_GREY,
        
        CON_COLOR_DARK_GREY = CON_COLOR_BLACK + 0x10,
        CON_COLOR_BRIGHT_RED = CON_COLOR_RED + 0x10,
        CON_COLOR_BRIGHT_GREEN = CON_COLOR_GREEN + 0x10,
        CON_COLOR_BRIGHT_YELLOW = CON_COLOR_YELLOW + 0x10,
        CON_COLOR_BRIGHT_BLUE = CON_COLOR_BLUE + 0x10,
        CON_COLOR_BRIGHT_MAGENTA = CON_COLOR_MAGENTA + 0x10,
        CON_COLOR_BRIGHT_CYAN = CON_COLOR_CYAN + 0x10,
        CON_COLOR_WHITE = CON_COLOR_LIGHT_GREY + 0x10,
    } color:8, background:8;
    bool bold;
    bool italic;
    bool underline;
} CON_attributes_t;

#ifndef __printflike
/* Missing from Linux headers. */
#define __printflike(fmtarg, firstvararg) \
__attribute__((__format__ (__printf__, fmtarg, firstvararg)))
#endif

void CON_print(const char *string);
void CON_attributed_print(const char *string, CON_attributes_t *attributes);
void CON_vprintf(const char *fmt, va_list args);
void CON_attributed_vprintf(const char *fmt, CON_attributes_t *attributes, va_list args);
void CON_printf(const char *fmt, ...) __printflike(1, 2);
void CON_attributed_printf(const char *fmt, CON_attributes_t *attributes,...) __printflike(1, 3);
void CON_set_async_prompt(const char *string);
void CON_set_repeat_empty(bool repeat);
void CON_set_line_ready_callback(void (*callback)(void));
bool CON_no_csi_mode(void);
