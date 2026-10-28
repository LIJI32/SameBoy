#import <AppKit/AppKit.h>
#include "open_dialog.h"
#include <fcntl.h>
#include <unistd.h>

static NSInteger run_modal(NSSavePanel *dialog)
{
    int stderr_fd = dup(STDERR_FILENO);
    if (stderr_fd >= 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        if (null_fd >= 0) {
            dup2(null_fd, STDERR_FILENO);
            close(null_fd);
        }
    }
    @try {
        return [dialog runModal];
    }
    @finally {
        if (stderr_fd >= 0) {
            dup2(stderr_fd, STDERR_FILENO);
            close(stderr_fd);
        }
    }
}

char *do_open_rom_dialog(void)
{
    @autoreleasepool {
        NSWindow *key = [NSApp keyWindow];
        NSOpenPanel *dialog = [NSOpenPanel openPanel];
        dialog.title = @"Open ROM";
        dialog.allowedFileTypes = @[@"gb", @"gbc", @"sgb", @"isx"];
        if (run_modal(dialog) != NSModalResponseOK) return nil;
        [key makeKeyAndOrderFront:nil];
        NSString *ret = [[[dialog URLs] firstObject] path];
        if (ret) {
            return strdup(ret.UTF8String);
        }
        return NULL;
    }
}

char *do_open_folder_dialog(void)
{
    @autoreleasepool {
        NSWindow *key = [NSApp keyWindow];
        NSOpenPanel *dialog = [NSOpenPanel openPanel];
        dialog.title = @"Select Boot ROMs Folder";
        dialog.canChooseDirectories = true;
        dialog.canChooseFiles = false;
        if (run_modal(dialog) != NSModalResponseOK) return nil;
        [key makeKeyAndOrderFront:nil];
        NSString *ret = [[[dialog URLs] firstObject] path];
        if (ret) {
            return strdup(ret.UTF8String);
        }
        return NULL;
    }
}

/* The Cocoa variant of this function isn't as fully featured as the GTK and Windows ones, as Mac users would use
   the fully featured Cocoa port of SameBoy anyway*/
char *do_save_recording_dialog(unsigned frequency)
{
    @autoreleasepool {
        NSWindow *key = [NSApp keyWindow];
        NSSavePanel *dialog = [NSSavePanel savePanel];
        dialog.title = @"Audio recording save location";
        dialog.allowedFileTypes = @[@"aiff", @"aif", @"aifc", @"wav", @"vgm", @"raw", @"pcm"];
        if (run_modal(dialog) != NSModalResponseOK) return nil;
        [key makeKeyAndOrderFront:nil];
        NSString *ret = [[dialog URL] path];
        if (ret) {
            return strdup(ret.UTF8String);
        }
        return NULL;
    }
}
