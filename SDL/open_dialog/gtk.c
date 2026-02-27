#include "open_dialog.h"
#include <SDL.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <stdbool.h>
#include <string.h>

#define GTK_FILE_CHOOSER_ACTION_OPEN 0
#define GTK_FILE_CHOOSER_ACTION_SAVE 1
#define GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER 2
#define GTK_RESPONSE_ACCEPT -3
#define GTK_RESPONSE_CANCEL -6


void *_gtk_file_chooser_dialog_new (const char *title,
                                    void *parent,
                                    int action,
                                    const char *first_button_text,
                                    ...);
bool _gtk_init_check (int *argc, char ***argv);
int _gtk_dialog_run(void *);
void _g_free(void *);
void _gtk_widget_destroy(void *);
char *_gtk_file_chooser_get_filename(void *);
void _g_log_set_default_handler (void *function, void *data);
void *_gtk_file_filter_new(void);
void _gtk_file_filter_add_pattern(void *filter, const char *pattern);
void _gtk_file_filter_set_name(void *filter, const char *name);
void _gtk_file_chooser_add_filter(void *dialog, void *filter);
void _gtk_main_iteration(void);
bool _gtk_events_pending(void);
unsigned long _g_signal_connect_data(void *instance,
                                     const char *detailed_signal,
                                     void *c_handler,
                                     void *data,
                                     void *destroy_data,
                                     unsigned connect_flags);
void _gtk_file_chooser_set_current_name(void *dialog,
                                        const char *name);
void *_gtk_file_chooser_get_filter(void *dialog);
const char *_gtk_file_filter_get_name (void *dialog);
#define g_signal_connect(instance, detailed_signal, c_handler, data) \
g_signal_connect_data((instance), (detailed_signal), (c_handler), (data), NULL, 0)



#define LAZY(symbol) static typeof(_##symbol) *symbol = NULL;\
if (symbol == NULL) symbol = dlsym(handle, #symbol);\
if (symbol == NULL) goto lazy_error
#define TRY_DLOPEN(name) handle = handle? handle : dlopen(name, RTLD_NOW)

void nop(void){}

static void wait_mouse_up(void)
{
    while (true) {
        if (!(SDL_GetMouseState(NULL, NULL) & SDL_BUTTON(SDL_BUTTON_LEFT))) break;
        SDL_Event event;
        SDL_PollEvent(&event);
    }
}

char *do_open_rom_dialog(void)
{
    wait_mouse_up();
    static void *handle = NULL;
    
    TRY_DLOPEN("libgtk-3.so");
    TRY_DLOPEN("libgtk-3.so.0");
    TRY_DLOPEN("libgtk-2.so");
    TRY_DLOPEN("libgtk-2.so.0");
    
    if (!handle) {
        goto lazy_error;
    }
    
    
    LAZY(gtk_init_check);
    LAZY(gtk_file_chooser_dialog_new);
    LAZY(gtk_dialog_run);
    LAZY(g_free);
    LAZY(gtk_widget_destroy);
    LAZY(gtk_file_chooser_get_filename);
    LAZY(g_log_set_default_handler);
    LAZY(gtk_file_filter_new);
    LAZY(gtk_file_filter_add_pattern);
    LAZY(gtk_file_filter_set_name);
    LAZY(gtk_file_chooser_add_filter);
    LAZY(gtk_events_pending);
    LAZY(gtk_main_iteration);
    
    /* Shut up GTK */
    g_log_set_default_handler(nop, NULL);
    
    gtk_init_check(0, 0);
    
    
    void *dialog = gtk_file_chooser_dialog_new("Open ROM",
                                               0,
                                               GTK_FILE_CHOOSER_ACTION_OPEN,
                                               "_Cancel", GTK_RESPONSE_CANCEL,
                                               "_Open", GTK_RESPONSE_ACCEPT,
                                               NULL );
    
    
    void *filter = gtk_file_filter_new();
    gtk_file_filter_add_pattern(filter, "*.gb");
    gtk_file_filter_add_pattern(filter, "*.gbc");
    gtk_file_filter_add_pattern(filter, "*.sgb");
    gtk_file_filter_add_pattern(filter, "*.isx");
    gtk_file_filter_set_name(filter, "Game Boy ROMs");
    gtk_file_chooser_add_filter(dialog, filter);
    
    int res = gtk_dialog_run(dialog);
    char *ret = NULL;
    
    if (res == GTK_RESPONSE_ACCEPT) { 
        char *filename;
        filename = gtk_file_chooser_get_filename(dialog);
        ret = strdup(filename);
        g_free(filename);
    }

    while (gtk_events_pending()) {
        gtk_main_iteration();
    }

    gtk_widget_destroy(dialog);

    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
    return ret;
    
lazy_error:
    fprintf(stderr, "Failed to display GTK dialog\n");
    return NULL;
}

char *do_open_folder_dialog(void)
{
    wait_mouse_up();
    static void *handle = NULL;
    
    TRY_DLOPEN("libgtk-3.so");
    TRY_DLOPEN("libgtk-3.so.0");
    TRY_DLOPEN("libgtk-2.so");
    TRY_DLOPEN("libgtk-2.so.0");
    
    if (!handle) {
        goto lazy_error;
    }
    
    
    LAZY(gtk_init_check);
    LAZY(gtk_file_chooser_dialog_new);
    LAZY(gtk_dialog_run);
    LAZY(g_free);
    LAZY(gtk_widget_destroy);
    LAZY(gtk_file_chooser_get_filename);
    LAZY(g_log_set_default_handler);
    LAZY(gtk_file_filter_new);
    LAZY(gtk_file_filter_add_pattern);
    LAZY(gtk_file_filter_set_name);
    LAZY(gtk_file_chooser_add_filter);
    LAZY(gtk_events_pending);
    LAZY(gtk_main_iteration);
    
    /* Shut up GTK */
    g_log_set_default_handler(nop, NULL);
    
    gtk_init_check(0, 0);
    
    
    void *dialog = gtk_file_chooser_dialog_new("Select Boot ROMs Folder",
                                               0,
                                               GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                               "_Cancel", GTK_RESPONSE_CANCEL,
                                               "_Open", GTK_RESPONSE_ACCEPT,
                                               NULL );
    
    
    int res = gtk_dialog_run(dialog);
    char *ret = NULL;
    
    if (res == GTK_RESPONSE_ACCEPT) {
        char *filename;
        filename = gtk_file_chooser_get_filename(dialog);
        ret = strdup(filename);
        g_free(filename);
    }
    
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
    
    gtk_widget_destroy(dialog);
    
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
    return ret;
    
lazy_error:
    fprintf(stderr, "Failed to display GTK dialog\n");
    return NULL;
}

static void filter_changed(void *dialog,
                           void *unused,
                           void *unused2)
{
    static void *handle = NULL;
    
    TRY_DLOPEN("libgtk-3.so");
    TRY_DLOPEN("libgtk-3.so.0");
    TRY_DLOPEN("libgtk-2.so");
    TRY_DLOPEN("libgtk-2.so.0");
    
    if (!handle) {
        goto lazy_error;
    }
    
    LAZY(gtk_file_chooser_get_filename);
    LAZY(gtk_file_chooser_set_current_name);
    LAZY(g_free);
    LAZY(gtk_file_chooser_get_filter);
    LAZY(gtk_file_filter_get_name);
    
    char *filename = gtk_file_chooser_get_filename(dialog);
    if (!filename) return;
    char *temp = filename + strlen(filename);
    char *basename = filename;
    bool deleted_extension = false;
    while (temp != filename) {
        temp--;
        if (*temp == '.' && !deleted_extension) {
            *temp = 0;
            deleted_extension = true;
        }
        else if (*temp == '/') {
            basename = temp + 1;
            break;
        }
    }
    
    char *new_filename = NULL;
    
    switch (gtk_file_filter_get_name(gtk_file_chooser_get_filter(dialog))[1]) {
        case 'p':
        default:
            asprintf(&new_filename, "%s.aiff", basename);
            break;
        case 'I':
            asprintf(&new_filename, "%s.wav", basename);
            break;
        case 'a':
            asprintf(&new_filename, "%s.raw", basename);
            break;
    }
    
    
    gtk_file_chooser_set_current_name(dialog, new_filename);
    free(new_filename);
    g_free(filename);
    return;
    
lazy_error:
    fprintf(stderr, "Failed updating the file extension\n");
}


char *do_save_recording_dialog(unsigned frequency)
{
    wait_mouse_up();
    static void *handle = NULL;
    
    TRY_DLOPEN("libgtk-3.so");
    TRY_DLOPEN("libgtk-3.so.0");
    TRY_DLOPEN("libgtk-2.so");
    TRY_DLOPEN("libgtk-2.so.0");
    
    if (!handle) {
        goto lazy_error;
    }
    
    
    LAZY(gtk_init_check);
    LAZY(gtk_file_chooser_dialog_new);
    LAZY(gtk_dialog_run);
    LAZY(g_free);
    LAZY(gtk_widget_destroy);
    LAZY(gtk_file_chooser_get_filename);
    LAZY(g_log_set_default_handler);
    LAZY(gtk_file_filter_new);
    LAZY(gtk_file_filter_add_pattern);
    LAZY(gtk_file_filter_set_name);
    LAZY(gtk_file_chooser_add_filter);
    LAZY(gtk_events_pending);
    LAZY(gtk_main_iteration);
    LAZY(g_signal_connect_data);
    LAZY(gtk_file_chooser_set_current_name);
    
    /* Shut up GTK */
    g_log_set_default_handler(nop, NULL);
    
    gtk_init_check(0, 0);
    
    
    void *dialog = gtk_file_chooser_dialog_new("Audio recording save location",
                                               0,
                                               GTK_FILE_CHOOSER_ACTION_SAVE,
                                               "_Cancel", GTK_RESPONSE_CANCEL,
                                               "_Save", GTK_RESPONSE_ACCEPT,
                                               NULL );
    
    
    void *filter = gtk_file_filter_new();
    gtk_file_filter_add_pattern(filter, "*.aiff");
    gtk_file_filter_add_pattern(filter, "*.aif");
    gtk_file_filter_add_pattern(filter, "*.aifc");
    gtk_file_filter_set_name(filter, "Apple AIFF");
    gtk_file_chooser_add_filter(dialog, filter);
    
    filter = gtk_file_filter_new();
    gtk_file_filter_add_pattern(filter, "*.wav");
    gtk_file_filter_set_name(filter, "RIFF WAVE");
    gtk_file_chooser_add_filter(dialog, filter);
    
    filter = gtk_file_filter_new();
    gtk_file_filter_add_pattern(filter, "*.raw");
    gtk_file_filter_add_pattern(filter, "*.pcm");
    static char raw_name[40];
#ifdef GB_BIG_ENDIAN
    sprintf(raw_name, "Raw PCM (Stereo %dHz, 16-bit BE)", frequency);
#else
    sprintf(raw_name, "Raw PCM (Stereo %dHz, 16-bit LE)", frequency);
#endif
    gtk_file_filter_set_name(filter, raw_name);
    gtk_file_chooser_add_filter(dialog, filter);
    
    g_signal_connect(dialog, "notify::filter", filter_changed, NULL);
    gtk_file_chooser_set_current_name(dialog, "Untitled.aiff");
    
    int res = gtk_dialog_run(dialog);
    char *ret = NULL;
    
    if (res == GTK_RESPONSE_ACCEPT) {
        char *filename;
        filename = gtk_file_chooser_get_filename(dialog);
        ret = strdup(filename);
        g_free(filename);
    }
    
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
    
    gtk_widget_destroy(dialog);
    
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
    return ret;
    
lazy_error:
    fprintf(stderr, "Failed to display GTK dialog\n");
    return NULL;
}
