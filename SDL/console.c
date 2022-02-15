#include "console.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <ctype.h>
#include <assert.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>

#define ESC(x) "\x1B" x
#define CSI(x) ESC("[" x)
#define SGR(x) CSI(x "m")

static bool initialized = false;
typedef struct listent_s listent_t;

struct listent_s {
    listent_t *prev;
    listent_t *next;
    char content[];
};

typedef struct {
    listent_t *first;
    listent_t *last;
} fifo_t;

static fifo_t lines;
static fifo_t history;

static void remove_entry(fifo_t *fifo, listent_t *entry)
{
    if (fifo->last == entry) {
        fifo->last = entry->prev;
    }
    if (fifo->first == entry) {
        fifo->first = entry->next;
    }
    if (entry->next) {
        entry->next->prev = entry->prev;
    }
    if (entry->prev) {
        entry->prev->next = entry->next;
    }
    free(entry);
}

static void add_entry(fifo_t *fifo, const char *content)
{
    size_t length = strlen(content);
    listent_t *entry = malloc(sizeof(*entry) + length + 1);
    entry->next = NULL;
    entry->prev = fifo->last;
    memcpy(entry->content, content, length);
    entry->content[length] = 0;
    if (fifo->last) {
        fifo->last->next = entry;
    }
    fifo->last = entry;
    if (!fifo->first) {
        fifo->first = entry;
    }
}

static listent_t *reverse_find(listent_t *entry, const char *string, bool exact)
{
    while (entry) {
        if (exact && strcmp(entry->content, string) == 0) {
            return entry;
        }
        if (!exact && strstr(entry->content, string)) {
            return entry;
        }
        entry = entry->prev;
    }
    return NULL;
}

static bool is_term(void)
{
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) return false;
#ifdef _WIN32
    if (AllocConsole()) {
        FreeConsole();
        return false;
    }
    
    unsigned long input_mode, output_mode;
    
    GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &input_mode);
    GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &output_mode);
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_VIRTUAL_TERMINAL_INPUT);
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), ENABLE_WRAP_AT_EOL_OUTPUT | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    
    CONSOLE_SCREEN_BUFFER_INFO before = {0,};
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &before);
    
    printf(SGR("0"));
    
    CONSOLE_SCREEN_BUFFER_INFO after = {0,};
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &after);
    
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), input_mode);
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), output_mode);


    if (before.dwCursorPosition.X != after.dwCursorPosition.X ||
        before.dwCursorPosition.Y != after.dwCursorPosition.Y) {
        printf("\r      \r");
        return false;
    }
    return true;
#else
    return getenv("TERM");
#endif
}

static unsigned width, height;

static char raw_getc(void)
{
#ifdef _WIN32
    char c;
    unsigned long ret;
    ReadConsole(GetStdHandle(STD_INPUT_HANDLE), &c, 1, &ret, NULL);
#else
    ssize_t ret;
    char c;
    
    do {
        ret = read(STDIN_FILENO, &c, 1);
    } while (ret == -1 && errno == EINTR);
#endif
    return ret == 1? c : EOF;
}

#ifdef _WIN32
#pragma clang diagnostic ignored "-Wmacro-redefined"
#include <Windows.h>

static void update_size(void)
{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
}

static unsigned long input_mode, output_mode;

static void cleanup(void)
{
    printf(CSI("!p")); // reset
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), input_mode);
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), output_mode);
    fflush(stdout);
}

static bool initialize(void)
{
    if (!is_term()) return false;
    update_size();
    if (width == 0 || height == 0) {
        return false;
    }
    
    static bool once = false;
    if (!once) {
        atexit(cleanup);
        GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &input_mode);
        GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &output_mode);
        once = true;
    }
    SetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), ENABLE_VIRTUAL_TERMINAL_INPUT);
    SetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), ENABLE_WRAP_AT_EOL_OUTPUT | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    printf(CSI("%dB") "\n" CSI("A") ESC("7") CSI("B"), height);
    
    fflush(stdout);
    initialized = true;
    return true;
}
#else
#include <sys/ioctl.h>

static void update_size(void)
{
    struct winsize winsize;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &winsize);
    width = winsize.ws_col;
    height = winsize.ws_row;
}

static void terminal_resized(int ignored)
{
    update_size();
}

#include <termios.h>
static struct termios terminal;


static void cleanup(void)
{
    printf(CSI("!p")); // reset
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &terminal);
    fflush(stdout);
}

static bool initialize(void)
{
    if (!is_term()) return false;
    update_size();
    if (width == 0 || height == 0) {
        return false;
    }
    
    static bool once = false;
    if (!once) {
        atexit(cleanup);
        signal(SIGWINCH, terminal_resized);
        tcgetattr(STDIN_FILENO, &terminal);
#ifdef _WIN32
        _setmode(STDIN_FILENO, _O_TEXT);
#endif
        once = true;
    }
    struct termios raw_terminal;
    raw_terminal = terminal;
    raw_terminal.c_lflag = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw_terminal);
    
    printf(CSI("%dB") "\n" CSI("A") ESC("7") CSI("B"), height);
    
    fflush(stdout);
    initialized = true;
    return true;
}
#endif

static struct {
    char *content;
    size_t allocation_size;
    size_t length;
    size_t position;
    size_t scroll;
    bool reverse_search;
    listent_t *search_line;
} line;

#define CTL(x) ((x) - 'A' + 1)

static const char *prompt = "";
static size_t prompt_length = 0;
static bool repeat_empty = false;

static bool redraw_prompt(bool force)
{
    if (line.reverse_search) {
        if (!force) return false;
        if (line.length == 0) {
            printf("\r" CSI("K") "%s" SGR("2") "Reverse Search..." SGR("0") CSI("%zuG"), prompt, prompt_length + 1);
            return true;
        }
        if (!line.search_line) {
            printf("\r" CSI("K") "%s" SGR("1") "%s" SGR("0"), prompt, line.content);
            return true;
        }
        const char *loc = strstr(line.search_line->content, line.content);
        printf("\r" CSI("K") "%s" "%.*s" SGR("1") "%s" SGR("0") "%s" CSI("%uG"),
               prompt,
               (int)(loc - line.search_line->content),
               line.search_line->content,
               line.content,
               loc + line.length,
               (unsigned)(loc - line.search_line->content + line.length + prompt_length + 1));
        return true;
    }
    
    size_t max = width - 1 - prompt_length;
    
    if (line.scroll && line.length <= max) {
        line.scroll = 0;
        force = true;
    }
    
    if (line.scroll > line.length - max) {
        line.scroll = line.length - max;
        force = true;
    }
    
    if (line.position < line.scroll + 1 && line.position) {
        line.scroll = line.position - 1;
        force = true;
    }
    
    if (line.position == 0 && line.scroll) {
        line.scroll = 0;
        force = true;
    }
    
    if (line.position > line.scroll + max) {
        line.scroll = line.position - max;
        force = true;
    }
    
    if (!force && line.length <= max) {
        return false;
    }
    
    if (line.length <= max) {
        printf("\r" CSI("K") "%s%s" CSI("%uG"), prompt, line.content, (unsigned)(line.position + prompt_length + 1));
        return true;
    }
    
    size_t left = max;
    const char *string = line.content + line.scroll;
    printf("\r" CSI("K") "%s", prompt);
    if (line.scroll) {
        printf(SGR("2") "%c" SGR("0"), *string);
        string++;
        left--;
    }
    if (line.scroll + max == line.length) {
        printf("%s", string);
    }
    else {
        printf("%.*s", (int)(left - 1), string);
        string += left;
        left = 1;
        printf(SGR("2") "%c" SGR("0"), *string);
    }
    printf(CSI("%uG"), (unsigned)(line.position - line.scroll + prompt_length + 1));
    
    return true;
}

static void set_position(size_t position)
{
    if (position > line.length) {
        printf("\a");
        return;
    }
    line.position = position;
    if (!redraw_prompt(false)) {
        printf(CSI("%uG"), (unsigned)(position + prompt_length + 1));
    }
}

static void set_line(const char *content)
{
    line.length = strlen(content);
    if (line.length + 1 > line.allocation_size) {
        line.content = realloc(line.content, line.length + 1);
        line.allocation_size = line.length + 1;
    }
    else if (line.allocation_size > 256 && line.length < 128) {
        line.content = realloc(line.content, line.length + 1);
        line.allocation_size = line.length + 1;
    }
    line.position = line.length;
    strcpy(line.content, content);
    redraw_prompt(true);
}

static void insert(const char *string)
{
    size_t insertion_length = strlen(string);
    size_t new_length = insertion_length + line.length;
    bool need_realloc = false;
    while (line.allocation_size < new_length + 1) {
        line.allocation_size *= 2;
        need_realloc = true;
    }
    if (need_realloc) {
        line.content = realloc(line.content, line.allocation_size);
    }
    memmove(line.content + line.position + insertion_length,
            line.content + line.position,
            line.length - line.position);
    memcpy(line.content + line.position, string, insertion_length);
    line.position += insertion_length;
    line.content[new_length] = 0;
    line.length = new_length;
    if (!redraw_prompt(line.position != line.length)) {
        printf("%s", string);
    }
}

static void delete(size_t size, bool forward)
{
    if (line.length < size) {
        printf("\a");
        return;
    }
    if (forward) {
        if (line.position > line.length - size) {
            printf("\a");
            return;
        }
        else {
            line.position += size;
        }
    }
    else if (line.position < size) {
        printf("\a");
        return;
    }
    memmove(line.content + line.position - size,
            line.content + line.position,
            line.length - line.position);
    line.length -= size;
    line.content[line.length] = 0;
    line.position -= size;
    
    if (!redraw_prompt(line.position != line.length)) {
        printf(CSI("%uG") CSI("K"),
               (unsigned)(line.position + prompt_length + 1));
    }
}

static void move_word(bool forward)
{
    signed offset = forward? 1 : -1;
    size_t end = forward? line.length : 0;
    signed check_offset = forward? 0 : -1;
    if (line.position == end) {
        printf("\a");
        return;
    }
    line.position += offset;
    while (line.position != end && isalnum(line.content[line.position + check_offset])) {
        line.position += offset;
    }
    if (!redraw_prompt(false)) {
        printf(CSI("%uG"), (unsigned)(line.position + prompt_length + 1));
    }
}

static void delete_word(bool forward)
{
    size_t original_pos = line.position;
    signed offset = forward? 1 : -1;
    size_t end = forward? line.length : 0;
    signed check_offset = forward? 0 : -1;
    if (line.position == end) {
        printf("\a");
        return;
    }
    line.position += offset;
    while (line.position != end && isalnum(line.content[line.position + check_offset])) {
        line.position += offset;
    }
    if (forward) {
        delete(line.position - original_pos, false);
    }
    else {
        delete(original_pos - line.position, true);
    }
}

#define MOD_ALT(x) (0x100 | x)
#define MOD_SHIFT(x) (0x200 | x)
#define MOD_CTRL(x) (0x400 | x)
#define MOD_SPECIAL(x) (0x800 | x)

static unsigned get_extended_key(void)
{
    unsigned modifiers = 0;
    char c = 0;
restart:
    c = raw_getc();
    if (c == 0x1B) {
        modifiers = MOD_SHIFT(MOD_ALT(0));
        goto restart;
    }
    else if (c != '[' && c != 'O') {
        return MOD_ALT(c);
    }
    unsigned ret = 0;
    while (true) {
        c = raw_getc();
        if (c >= '0' && c <= '9') {
            ret = ret * 10 + c - '0';
        }
        else if (c == ';') {
            if (ret == 1) {
                modifiers |= MOD_ALT(0);
            }
            else if (ret == 2) {
                modifiers |= MOD_SHIFT(0);
            }
            else if (ret == 5) {
                modifiers |= MOD_CTRL(0);
            }
            ret = 0;
        }
        else if (c == '~') {
            return MOD_SPECIAL(ret) | modifiers;
        }
        else {
            if (ret == 1) {
                modifiers |= MOD_ALT(0);
            }
            else if (ret == 2) {
                modifiers |= MOD_SHIFT(0);
            }
            else if (ret == 5) {
                modifiers |= MOD_CTRL(0);
            }
            return c | modifiers;
        }
    }
}

#define SWAP(x, y) do {typeof(*(x)) _tmp = *(x); *(x) = *(y);*(y) = _tmp;} while (0)
static pthread_mutex_t terminal_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t lines_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t lines_cond = PTHREAD_COND_INITIALIZER;

static char reverse_search_mainloop(void)
{
    while (true) {
        char c = raw_getc();
        pthread_mutex_lock(&terminal_lock);
        
        switch (c) {
            case CTL('C'):
                line.search_line = NULL;
                set_line("");
                pthread_mutex_unlock(&terminal_lock);
                return CTL('A');
            case CTL('R'):
                line.search_line = reverse_find(line.search_line? line.search_line->prev : history.last, line.content, false);
                if (!line.search_line) {
                    printf("\a");
                }
                redraw_prompt(true);
                break;
            case CTL('W'):
                delete_word(false);
                redraw_prompt(true);
                break;
#ifndef _WIN32
            case CTL('Z'):
                set_line("");
                raise(SIGSTOP);
                initialize(); // Reinitialize
                redraw_prompt(true);
                break;
#endif
            case CTL('H'):
            case 0x7F: // Backspace
                delete(1, false);
                redraw_prompt(true);
                break;
            default:
                if (c >= ' ') {
                    char string[2] = {c, 0};
                    insert(string);
                    line.search_line = reverse_find(line.search_line?: history.last, line.content, false);
                    if (!line.search_line) {
                        printf("\a");
                    }
                    redraw_prompt(true);
                }
                else {
                    pthread_mutex_unlock(&terminal_lock);
                    return c;
                }
                break;
        }
        pthread_mutex_unlock(&terminal_lock);
        fflush(stdout);
    }
    
}


static
#ifdef _WIN32
int __stdcall
#else
void *
#endif
mainloop(char *(*completer)(const char *substring, uintptr_t *context))
{
    listent_t *history_line = NULL;
    uintptr_t complete_context = 0;
    size_t completion_length = 0;
    while (true) {
        char c;
        if (line.reverse_search) {
            c = reverse_search_mainloop();
            line.reverse_search = false;
            if (line.search_line) {
                size_t pos = strstr(line.search_line->content, line.content) - line.search_line->content + line.length;
                set_line(line.search_line->content);
                line.search_line = NULL;
                set_position(pos);
            }
            else {
                redraw_prompt(true);
            }
        }
        else {
            c = raw_getc();
        }
        pthread_mutex_lock(&terminal_lock);
        
        switch (c) {
            case CTL('A'):
                set_position(0);
                complete_context = completion_length = 0;
                break;
            case CTL('B'):
                set_position(line.position - 1);
                complete_context = completion_length = 0;
                break;
            case CTL('C'):
                if (line.length) {
                    set_line("");
                    history_line = NULL;
                    complete_context = completion_length = 0;
                }
                else {
#ifdef _WIN32
                    raise(SIGINT);
#else
                    kill(getpid(), SIGINT);
#endif
                }
                break;
            case CTL('D'):
                if (line.length) {
                    delete(1, true);
                    complete_context = completion_length = 0;
                }
                else {
                    pthread_mutex_lock(&lines_lock);
                    add_entry(&lines, CON_EOF);
                    pthread_cond_signal(&lines_cond);
                    pthread_mutex_unlock(&lines_lock);
                }
                break;
            case CTL('E'):
                set_position(line.length);
                complete_context = completion_length = 0;
                break;
            case CTL('F'):
                set_position(line.position + 1);
                complete_context = completion_length = 0;
                break;
            case CTL('K'):
                printf(CSI("K"));
                if (!redraw_prompt(false)) {
                    line.length = line.position;
                    line.content[line.length] = 0;
                }
                complete_context = completion_length = 0;
                break;
            case CTL('R'):
                complete_context = completion_length = 0;
                line.reverse_search = true;
                set_line("");
                
                break;
            case CTL('T'):
                if (line.length < 2) {
                    printf("\a");
                    break;
                }
                if (line.position && line.position == line.length) {
                    line.position--;
                }
                if (line.position == 0) {
                    printf("\a");
                    break;
                }
                SWAP(line.content + line.position,
                     line.content + line.position - 1);
                line.position++;
                redraw_prompt(true);
                complete_context = completion_length = 0;
                break;
            case CTL('W'):
                delete_word(false);
                complete_context = completion_length = 0;
                break;
#ifndef _WIN32
            case CTL('Z'):
                set_line("");
                complete_context = completion_length = 0;
                raise(SIGSTOP);
                initialize(); // Reinitialize
                break;
#endif
            case '\r':
            case '\n':
                pthread_mutex_lock(&lines_lock);
                if (line.length == 0 && repeat_empty && history.last) {
                    add_entry(&lines, history.last->content);
                }
                else {
                    add_entry(&lines, line.content);
                }
                pthread_cond_signal(&lines_cond);
                pthread_mutex_unlock(&lines_lock);
                if (line.length) {
                    listent_t *dup = reverse_find(history.last, line.content, true);
                    if (dup) {
                        remove_entry(&history, dup);
                    }
                    add_entry(&history, line.content);
                    set_line("");
                    history_line = NULL;
                }
                complete_context = completion_length = 0;
                break;
            case CTL('H'):
            case 0x7F: // Backspace
                delete(1, false);
                complete_context = completion_length = 0;
                break;
            case 0x1B:
                switch (get_extended_key()) {
                    case MOD_SPECIAL(1): // Home
                    case MOD_SPECIAL(7):
                    case 'H':
                        set_position(0);
                        complete_context = completion_length = 0;
                        break;
                    case MOD_SPECIAL(8): // End
                    case 'F':
                        set_position(line.length);
                        complete_context = completion_length = 0;
                        break;
                    case MOD_SPECIAL(3): // Delete
                        delete(1, true);
                        complete_context = completion_length = 0;
                        break;
                    case 'A': // Up
                        if (!history_line) {
                            history_line = history.last;
                        }
                        else {
                            history_line = history_line->prev;
                        }
                        if (history_line) {
                            set_line(history_line->content);
                            complete_context = completion_length = 0;
                        }
                        else {
                            history_line = history.first;
                            printf("\a");
                        }
                        
                        break;
                    case 'B': // Down
                        if (!history_line) {
                            printf("\a");
                            break;
                        }
                        history_line = history_line->next;
                        if (history_line) {
                            set_line(history_line->content);
                            complete_context = completion_length = 0;
                        }
                        else {
                            set_line("");
                            complete_context = completion_length = 0;
                        }
                        break;
                    case 'C': // Right
                        set_position(line.position + 1);
                        complete_context = completion_length = 0;
                        break;
                    case 'D': // Left
                        set_position(line.position - 1);
                        complete_context = completion_length = 0;
                        break;
                    case MOD_ALT('b'):
                    case MOD_ALT('D'):
                        move_word(false);
                        complete_context = completion_length = 0;
                        break;
                    case MOD_ALT('f'):
                    case MOD_ALT('C'):
                        move_word(true);
                        complete_context = completion_length = 0;
                        break;
                    case MOD_ALT(0x7F): // ALT+Backspace
                        delete_word(false);
                        complete_context = completion_length = 0;
                        break;
                    case MOD_ALT('('): // ALT+Delete
                        delete_word(true);
                        complete_context = completion_length = 0;
                        break;
                    default:
                        printf("\a");
                        break;
                }
                break;
            case '\t': {
                char temp = line.content[line.position - completion_length];
                line.content[line.position - completion_length] = 0;
                char *completion = completer? completer(line.content, &complete_context) : NULL;
                line.content[line.position - completion_length] = temp;
                if (completion) {
                    if (completion_length) {
                        delete(completion_length, false);
                    }
                    insert(completion);
                    completion_length = strlen(completion);
                    free(completion);
                }
                else {
                    printf("\a");
                }
                break;
            }
            default:
                if (c >= ' ') {
                    char string[2] = {c, 0};
                    insert(string);
                    complete_context = completion_length = 0;
                }
                else {
                    printf("\a");
                }
                break;
        }
        fflush(stdout);
        pthread_mutex_unlock(&terminal_lock);
    }
    return 0;
}

char *CON_readline(const char *new_prompt)
{
    pthread_mutex_lock(&terminal_lock);
    const char *old_prompt = prompt;
    prompt = new_prompt;
    prompt_length = strlen(prompt);
    redraw_prompt(true);
    fflush(stdout);
    pthread_mutex_unlock(&terminal_lock);
    
    pthread_mutex_lock(&lines_lock);
    while (!lines.first) {
        pthread_cond_wait(&lines_cond, &lines_lock);
    }
    char *ret = strdup(lines.first->content);
    remove_entry(&lines, lines.first);
    pthread_mutex_unlock(&lines_lock);
    
    pthread_mutex_lock(&terminal_lock);
    prompt = old_prompt;
    prompt_length = strlen(prompt);
    redraw_prompt(true);
    fflush(stdout);
    pthread_mutex_unlock(&terminal_lock);
    return ret;
}

char *CON_readline_async(void)
{
    char *ret = NULL;
    pthread_mutex_lock(&lines_lock);
    if (lines.first) {
        ret = strdup(lines.first->content);
        remove_entry(&lines, lines.first);
    }
    pthread_mutex_unlock(&lines_lock);
    return ret;
}

bool CON_start(char *(*completer)(const char *substring, uintptr_t *context))
{
    if (!initialize()) {
        return false;
    }
    set_line("");
    pthread_t thread;
    return pthread_create(&thread, NULL, (void *)mainloop, completer) == 0;
}

void CON_attributed_print(const char *string, CON_attributes_t *attributes)
{
    if (!initialized) {
        printf("%s", string);
        return;
    }
    static bool pending_newline = false;
    pthread_mutex_lock(&terminal_lock);
    printf(ESC("8"));
    bool needs_reset = false;
    if (attributes) {
        if (attributes->color) {
            if (attributes->color >= 0x10) {
                printf(SGR("%d"), attributes->color - 0x11 + 90);
            }
            else {
                printf(SGR("%d"), attributes->color - 1 + 30);
            }
            needs_reset = true;
        }
        if (attributes->background) {
            if (attributes->background >= 0x10) {
                printf(SGR("%d"), attributes->background - 0x11 + 100);
            }
            else {
                printf(SGR("%d"), attributes->background - 1 + 40);
            }
            needs_reset = true;
        }
        if (attributes->bold) {
            printf(SGR("1"));
            needs_reset = true;
        }
        if (attributes->italic) {
            printf(SGR("3"));
            needs_reset = true;
        }
        if (attributes->underline) {
            printf(SGR("4"));
            needs_reset = true;
        }
    }
    const char *it = string;
    bool need_redraw_prompt = false;
    while (*it) {
        if (pending_newline) {
            need_redraw_prompt = true;
            printf("\n" CSI("K") "\n" CSI("A"));
            pending_newline = false;
            continue;
        }
        if (*it == '\n') {
            printf("%.*s", (int)(it - string), string);
            string = it + 1;
            pending_newline = true;
        }
        it++;
    }
    if (*string) {
        printf("%s", string);
    }
    if (needs_reset) {
        printf(SGR("0"));
    }
    printf(ESC("7") CSI("B"));
    if (need_redraw_prompt) {
        redraw_prompt(true);
    }
    else {
        set_position(line.position);
    }
    fflush(stdout);
    pthread_mutex_unlock(&terminal_lock);
}

void CON_print(const char *string)
{
    CON_attributed_print(string, NULL);
}

void CON_vprintf(const char *fmt, va_list args)
{
    char *string = NULL;
    vasprintf(&string, fmt, args);
    CON_attributed_print(string, NULL);
    free(string);
}

void CON_attributed_vprintf(const char *fmt, CON_attributes_t *attributes, va_list args)
{
    char *string = NULL;
    vasprintf(&string, fmt, args);
    CON_attributed_print(string, attributes);
    free(string);
}

void CON_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    CON_vprintf(fmt, args);
    va_end(args);
}


void CON_attributed_printf(const char *fmt, CON_attributes_t *attributes,...)
{
    va_list args;
    va_start(args, attributes);
    CON_attributed_vprintf(fmt, attributes, args);
    va_end(args);
}

void CON_set_async_prompt(const char *string)
{
    pthread_mutex_lock(&terminal_lock);
    prompt = string;
    prompt_length = strlen(string);
    redraw_prompt(true);
    fflush(stdout);
    pthread_mutex_unlock(&terminal_lock);
}

void CON_set_repeat_empty(bool repeat)
{
    repeat_empty = repeat;
}
