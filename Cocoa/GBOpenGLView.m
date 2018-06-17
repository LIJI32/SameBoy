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
    glViewport(0, 0, self.bounds.size.width * scale, self.bounds.size.height * scale);
    
    if (gbview.shouldBlendFrameWithPrevious) {
        [self.shader renderBitmap:gbview.currentBuffer
                         previous:gbview.previousBuffer
                           inSize:self.bounds.size
                            scale:scale];
    }
    else {
        [self.shader renderBitmap:gbview.currentBuffer
                         previous:NULL
                           inSize:self.bounds.size
                            scale:scale];
    }
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
