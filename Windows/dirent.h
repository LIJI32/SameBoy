#include <windows.h>

struct dirent {
    char d_name[MAX_PATH * 3 + 1];
};

typedef struct {
    HANDLE handle;
    WIN32_FIND_DATAW entry;
    struct dirent out_entry;
} DIR;

DIR *opendir(const char *filename);
int closedir(DIR *dir);
struct dirent *readdir(DIR *dir);
