#import "GBOpenGLView.h"
#import "GBView.h"
#include <OpenGL/gl.h>

@implementation GBOpenGLView

- (void)drawRect:(NSRect)dirtyRect {
    if (!self.shader) {
        self.shader = [[GBGLShader alloc] initWithName:[[NSUserDefaults standardUserDefaults] objectForKey:@"GBFilter"]];
    }
    
    GBView *gbview = (GBView *)self.superview;
    double scale = self.window.backingScaleFactor;
    glViewport(
        gbview.viewport.origin.x * scale,
        gbview.viewport.origin.y * scale,
        gbview.viewport.size.width * scale,
        gbview.viewport.size.height * scale);
    
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    [self.shader renderBitmap:CGBitmapContextGetData(gbview.currentBuffer)
                     previous:gbview.shouldBlendFrameWithPrevious? CGBitmapContextGetData(gbview.previousBuffer) : NULL
                        sized:NSMakeSize(CGBitmapContextGetWidth(gbview.currentBuffer), CGBitmapContextGetHeight(gbview.currentBuffer))
                       inRect:gbview.viewport
                        scale:scale];
    glFlush();
}

- (instancetype)initWithFrame:(NSRect)frameRect pixelFormat:(NSOpenGLPixelFormat *)format
{
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(filterChanged) name:@"GBFilterChanged" object:nil];
    return [super initWithFrame:frameRect pixelFormat:format];
}

- (void) filterChanged
{
    self.shader = nil;
    [self setNeedsDisplay:YES];
}
@end
