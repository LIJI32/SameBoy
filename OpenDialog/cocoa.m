#import <AppKit/AppKit.h>
#include "open_dialog.h"


char *do_open_rom_dialog(void)
{
    @autoreleasepool {
        NSWindow *key = [NSApp keyWindow];
        NSOpenPanel *dialog = [NSOpenPanel openPanel];
        dialog.title = @"Open ROM";
        dialog.allowedFileTypes = @[@"gb", @"gbc", @"sgb", @"isx"];
        [dialog runModal];
        [key makeKeyAndOrderFront:nil];
        NSString *ret = [[[dialog URLs] firstObject] path];
        if (ret) {
            return strdup(ret.fileSystemRepresentation);
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
        [dialog runModal];
        [key makeKeyAndOrderFront:nil];
        NSString *ret = [[[dialog URLs] firstObject] path];
        if (ret) {
            return strdup(ret.fileSystemRepresentation);
        }
        return NULL;
    }
}
