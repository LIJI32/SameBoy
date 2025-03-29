#include <windows.h>
#include <stdio.h>

#ifdef USE_MSVCRT_DLL
/* Stub-out functions imported by VS's msvcrt.lib but are not implemented in msvcrt.dll */

/* We don't use exceptions, these are used by the uncaught exception handler and can be replaced with aborts */

void *__current_exception(void)
{
    abort();
    return NULL;
}

void *__current_exception_context(void)
{
    abort();
    return NULL;
}

/* terminate uses a mangled symbol on msvcrt.dll */

void msvcrt_terminate(void) __asm__("?terminate@@YAXXZ");
void terminate(void)
{
    msvcrt_terminate();
}

/* We need to redirect these to msvcrt.dll's atexit */

int _crt_atexit(void (*function)(void))
{
    return ((typeof(atexit) *)GetProcAddress(GetModuleHandleA("msvcrt.dll"), "atexit"))(function);
}

int _register_onexit_function(void *table, void (*function)(void))
{
    return ((typeof(atexit) *)GetProcAddress(GetModuleHandleA("msvcrt.dll"), "atexit"))(function);
}

/* Various imported function we don't need and can nop-out */

int *__p__commode(void)
{
    static int dummy;
    return &dummy;
}

int _configthreadlocale(int flag)
{
    return 0;
}

errno_t _configure_narrow_argv(unsigned mode)
{
    return 0;
}

char *_get_narrow_winmain_command_line(void)
{
    return NULL;
}

int _initialize_narrow_environment()
{
    return 0;
}

int _initialize_onexit_table(void *table)
{
    return 0;
}

void _register_thread_local_exe_atexit_callback(const void *callback)
{
}

int _seh_filter_exe(unsigned exception_num, void *exception)
{
    return 0;
}

int _seh_filter_dll(unsigned ExceptionNum, struct _EXCEPTION_POINTERS *ExceptionPtr)
{
    return 0;
}

void _set_app_type(unsigned type)
{
}

int _set_new_mode(int new_mode)
{
    return 0;
}

int _execute_onexit_table(void *Table)
{
    return 0;
}

void __std_type_info_destroy_list(void *const root_node)
{
}
#endif
