#import <AppKit/AppKit.h>
#import <SDL.h>
#import "save_png.h"

bool save_png(const char *filename, uint32_t width, uint32_t height, const void *pixels, SDL_PixelFormat *pixel_format)
{
    if (pixel_format->format != SDL_PIXELFORMAT_ABGR8888 &&
        pixel_format->format != SDL_PIXELFORMAT_XBGR8888) {
        return false;
    }
    size_t stride = width * 4;
    
    @autoreleasepool {
        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                        pixelsWide:width
                                                                        pixelsHigh:height
                                                                     bitsPerSample:8
                                                                   samplesPerPixel:3
                                                                          hasAlpha:false
                                                                          isPlanar:false
                                                                    colorSpaceName:NSDeviceRGBColorSpace
                                                                      bitmapFormat:0
                                                                       bytesPerRow:4 * width
                                                                      bitsPerPixel:32];
        memcpy(rep.bitmapData, pixels, stride * height);
        NSData *data = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        return [data writeToFile:@(filename) atomically:true];
    }
}
