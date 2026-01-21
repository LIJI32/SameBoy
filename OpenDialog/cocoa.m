#import <AppKit/AppKit.h>
#include "open_dialog.h"


char *do_open_rom_dialog(void)
{
    @autoreleasepool {
        int stderr_fd = dup(STDERR_FILENO);
        close(STDERR_FILENO);
        NSWindow *key = [NSApp keyWindow];
        NSOpenPanel *dialog = [NSOpenPanel openPanel];
        dialog.title = @"Open ROM";
        dialog.allowedFileTypes = @[@"gb", @"gbc", @"sgb", @"isx"];
        if ([dialog runModal] != NSModalResponseOK) return nil;
        [key makeKeyAndOrderFront:nil];
        NSString *ret = [[[dialog URLs] firstObject] path];
        dup2(stderr_fd, STDERR_FILENO);
        if (ret) {
            return strdup(ret.fileSystemRepresentation);
        }
        return NULL;
    }
}

char *do_open_folder_dialog(void)
{
    @autoreleasepool {
        int stderr_fd = dup(STDERR_FILENO);
        close(STDERR_FILENO);
        NSWindow *key = [NSApp keyWindow];
        NSOpenPanel *dialog = [NSOpenPanel openPanel];
        dialog.title = @"Select Boot ROMs Folder";
        dialog.canChooseDirectories = true;
        dialog.canChooseFiles = false;
        if ([dialog runModal] != NSModalResponseOK) return nil;
        [key makeKeyAndOrderFront:nil];
        NSString *ret = [[[dialog URLs] firstObject] path];
        dup2(stderr_fd, STDERR_FILENO);
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
        int stderr_fd = dup(STDERR_FILENO);
        close(STDERR_FILENO);
        NSWindow *key = [NSApp keyWindow];
        NSSavePanel *dialog = [NSSavePanel savePanel];
        dialog.title = @"Audio recording save location";
        dialog.allowedFileTypes = @[@"aiff", @"aif", @"aifc", @"wav", @"raw", @"pcm"];
        if ([dialog runModal] != NSModalResponseOK) return nil;
        [key makeKeyAndOrderFront:nil];
        NSString *ret = [[dialog URL] path];
        dup2(stderr_fd, STDERR_FILENO);
        if (ret) {
            return strdup(ret.fileSystemRepresentation);
        }
        return NULL;
    }
}
