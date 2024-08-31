#import <CoreImage/CoreImage.h>
#import "GBViewMetal.h"
#pragma clang diagnostic ignored "-Wpartial-availability"
#if !TARGET_OS_IPHONE
#import "../Cocoa/NSObject+DefaultsObserver.h"
#endif

static const vector_float2 rect[] =
{
    {-1, -1},
    { 1, -1},
    {-1,  1},
    { 1,  1},
};

@implementation GBViewMetal
{
    id<MTLDevice> _device;
    id<MTLTexture> _texture, _previousTexture;
    id<MTLBuffer> _vertices;
    id<MTLRenderPipelineState> _pipelineState;
    id<MTLCommandQueue> _commandQueue;
    id<MTLBuffer> _frameBlendingModeBuffer;
    id<MTLBuffer> _outputResolutionBuffer;
    vector_float2 _outputResolution;
    id<MTLCommandBuffer> _commandBuffer;
    bool _waitedForFrame;
    _Atomic unsigned _pendingFrames;
}

+ (bool)isSupported
{
#if TARGET_OS_IPHONE
    return true;
#else
    if (MTLCopyAllDevices) {
        return [MTLCopyAllDevices() count];
    }
    return false;
#endif
}
- (void) allocateTextures
{
    if (!_device) return;
    
    MTLTextureDescriptor *texture_descriptor = [[MTLTextureDescriptor alloc] init];
    
    texture_descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
    
    texture_descriptor.width = GB_get_screen_width(self.gb);
    texture_descriptor.height = GB_get_screen_height(self.gb);
    
    _texture = [_device newTextureWithDescriptor:texture_descriptor];
    _previousTexture = [_device newTextureWithDescriptor:texture_descriptor];

}

- (void)createInternalView
{
    MTKView *view = [[MTKView alloc] initWithFrame:self.frame device:(_device = MTLCreateSystemDefaultDevice())];
    view.delegate = self;
    self.internalView = view;
    view.paused = true;
    view.enableSetNeedsDisplay = true;
    view.framebufferOnly = false;
    
    _vertices = [_device newBufferWithBytes:rect
                                   length:sizeof(rect)
                                  options:MTLResourceStorageModeShared];
    
    static const GB_frame_blending_mode_t default_blending_mode = GB_FRAME_BLENDING_MODE_DISABLED;
    _frameBlendingModeBuffer = [_device newBufferWithBytes:&default_blending_mode
                                          length:sizeof(default_blending_mode)
                                         options:MTLResourceStorageModeShared];
    
    _outputResolutionBuffer = [_device newBufferWithBytes:&_outputResolution
                                                   length:sizeof(_outputResolution)
                                                  options:MTLResourceStorageModeShared];
    
    _outputResolution = (simd_float2){view.drawableSize.width, view.drawableSize.height};
    /* TODO: NSObject+DefaultsObserver can replace the less flexible `addDefaultObserver` in iOS */
#if TARGET_OS_IPHONE
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(loadShader) name:@"GBFilterChanged" object:nil];
    [self loadShader];
#else
    [self observeStandardDefaultsKey:@"GBFilter" selector:@selector(loadShader)];
#endif
}

- (void)loadShader
{
    NSError *error = nil;
    NSString *shader_source = [NSString stringWithContentsOfFile:[[NSBundle mainBundle] pathForResource:@"MasterShader"
                                                                                                 ofType:@"metal"
                                                                                            inDirectory:@"Shaders"]
                                                        encoding:NSUTF8StringEncoding
                                                           error:nil];
    
    NSString *shader_name = [[NSUserDefaults standardUserDefaults] objectForKey:@"GBFilter"];
    NSString *scaler_source = [NSString stringWithContentsOfFile:[[NSBundle mainBundle] pathForResource:shader_name
                                                                                                 ofType:@"fsh"
                                                                                            inDirectory:@"Shaders"]
                                                        encoding:NSUTF8StringEncoding
                                                           error:nil];
    
    shader_source = [shader_source stringByReplacingOccurrencesOfString:@"{filter}"
                                                             withString:scaler_source];

    MTLCompileOptions *options = [[MTLCompileOptions alloc] init];
    options.fastMathEnabled = true;
    id<MTLLibrary> library = [_device newLibraryWithSource:shader_source
                                                   options:options
                                                     error:&error];
    if (error) {
        NSLog(@"Error: %@", error);
        if (!library) {
            return;
        }
    }
    
    id<MTLFunction> vertex_function = [library newFunctionWithName:@"vertex_shader"];
    id<MTLFunction> fragment_function = [library newFunctionWithName:@"fragment_shader"];
    
    // Set up a descriptor for creating a pipeline state object
    MTLRenderPipelineDescriptor *pipeline_state_descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipeline_state_descriptor.vertexFunction = vertex_function;
    pipeline_state_descriptor.fragmentFunction = fragment_function;
    pipeline_state_descriptor.colorAttachments[0].pixelFormat = ((MTKView *)self.internalView).colorPixelFormat;
    
    error = nil;
    _pipelineState = [_device newRenderPipelineStateWithDescriptor:pipeline_state_descriptor
                                                             error:&error];
    if (error)  {
        NSLog(@"Failed to created pipeline state, error %@", error);
        return;
    }
    
    _commandQueue = [_device newCommandQueue];
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size
{
    _outputResolution = (vector_float2){size.width, size.height};
    dispatch_async(dispatch_get_main_queue(), ^{
        [(MTKView *)self.internalView draw];
    });
}

- (void)drawInMTKView:(MTKView *)view
{
#if !TARGET_OS_IPHONE
    if (!(view.window.occlusionState & NSWindowOcclusionStateVisible)) return;
#endif
    if (!self.gb) return;
    if (_texture.width  != GB_get_screen_width(self.gb) ||
        _texture.height != GB_get_screen_height(self.gb)) {
        [self allocateTextures];
    }
    
    MTLRegion region = {
        {0, 0, 0},         // MTLOrigin
        {_texture.width, _texture.height, 1} // MTLSize
    };
    
    /* Don't start rendering if the previous frame hasn't finished yet. Either wait, or skip the frame */
    if (_commandBuffer && _commandBuffer.status != MTLCommandBufferStatusCompleted) {
        if (_waitedForFrame) return;
        [_commandBuffer waitUntilCompleted];
        _waitedForFrame = true;
    }
    else {
        _waitedForFrame = false;
    }

    GB_frame_blending_mode_t mode = [self frameBlendingMode];
    
    [_texture replaceRegion:region
               mipmapLevel:0
                 withBytes:[self currentBuffer]
               bytesPerRow:_texture.width * 4];

    if (mode) {
        [_previousTexture replaceRegion:region
                            mipmapLevel:0
                              withBytes:[self previousBuffer]
                            bytesPerRow:_texture.width * 4];
    }
        
    MTLRenderPassDescriptor *renderPassDescriptor = view.currentRenderPassDescriptor;
    _commandBuffer = [_commandQueue commandBuffer];

    if (renderPassDescriptor) {
        *(GB_frame_blending_mode_t *)[_frameBlendingModeBuffer contents] = mode;
        *(vector_float2 *)[_outputResolutionBuffer contents] = _outputResolution;

        id<MTLRenderCommandEncoder> renderEncoder =
            [_commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
        
        [renderEncoder setViewport:(MTLViewport){0.0, 0.0,
            _outputResolution.x,
            _outputResolution.y,
            -1.0, 1.0}];
        
        [renderEncoder setRenderPipelineState:_pipelineState];
        
        [renderEncoder setVertexBuffer:_vertices
                                 offset:0
                                atIndex:0];
        
        [renderEncoder setFragmentBuffer:_frameBlendingModeBuffer
                                   offset:0
                                  atIndex:0];
        
        [renderEncoder setFragmentBuffer:_outputResolutionBuffer
                                   offset:0
                                  atIndex:1];
        
        [renderEncoder setFragmentTexture:_texture
                                  atIndex:0];
        
        [renderEncoder setFragmentTexture:_previousTexture
                                   atIndex:1];
        
        [renderEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                          vertexStart:0
                          vertexCount:4];
        
        [renderEncoder endEncoding];
        
        [_commandBuffer presentDrawable:view.currentDrawable];
    }
    
    
    [_commandBuffer commit];
}

- (void)flip
{
    [super flip];
    if (_pendingFrames == 2) return;
    _pendingFrames++;
    dispatch_async(dispatch_get_main_queue(), ^{
        [(MTKView *)self.internalView draw];
        _pendingFrames--;
    });
}

#if !TARGET_OS_IPHONE
- (NSImage *)renderToImage
{
    CIImage *ciImage = [CIImage imageWithMTLTexture:[[(MTKView *)self.internalView currentDrawable] texture]
                                            options:@{
                                                kCIImageColorSpace: (__bridge_transfer id)CGColorSpaceCreateDeviceRGB(),
                                                kCIImageProperties: [NSNull null]
                                            }];
    ciImage = [ciImage imageByApplyingTransform:CGAffineTransformTranslate(CGAffineTransformMakeScale(1, -1),
                                                                           0, ciImage.extent.size.height)];
    CIContext *context = [CIContext context];
    CGImageRef cgImage = [context createCGImage:ciImage fromRect:ciImage.extent];
    NSImage *ret = [[NSImage alloc] initWithCGImage:cgImage size:self.internalView.bounds.size];
    CGImageRelease(cgImage);
    return ret;
}
#endif

@end
