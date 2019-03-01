#import "GBViewMetal.h"

static const vector_float2 rect[] =
{
    {-1, -1},
    { 1, -1},
    {-1,  1},
    { 1,  1},
};

@implementation GBViewMetal
{
    id<MTLDevice> device;
    id<MTLTexture> texture, previous_texture;
    id<MTLBuffer> vertices;
    id<MTLRenderPipelineState> pipeline_state;
    id<MTLCommandQueue> command_queue;
    id<MTLBuffer> mix_previous_buffer;
    id<MTLBuffer> output_resolution_buffer;
    vector_float2 output_resolution;
}

+ (bool)isSupported
{
    if (MTLCopyAllDevices) {
        return [MTLCopyAllDevices() count];
    }
    return false;
}

- (void) allocateTexturesForSize:(NSSize)size
{
    if (!device) return;
    
    MTLTextureDescriptor *texture_descriptor = [[MTLTextureDescriptor alloc] init];
    
    texture_descriptor.pixelFormat = MTLPixelFormatRGBA8Unorm;
    
    texture_descriptor.width = size.width;
    texture_descriptor.height = size.height;
    
    texture = [device newTextureWithDescriptor:texture_descriptor];
    previous_texture = [device newTextureWithDescriptor:texture_descriptor];

}

- (void)createInternalView
{
    MTKView *view = [[MTKView alloc] initWithFrame:self.frame device:(device = MTLCreateSystemDefaultDevice())];
    view.delegate = self;
    self.internalView = view;
    view.paused = YES;
    
    vertices = [device newBufferWithBytes:rect
                                   length:sizeof(rect)
                                  options:MTLResourceStorageModeShared];
    
    static const bool default_mix_value = false;
    mix_previous_buffer = [device newBufferWithBytes:&default_mix_value
                                              length:sizeof(default_mix_value)
                                             options:MTLResourceStorageModeShared];
    
    output_resolution_buffer = [device newBufferWithBytes:&output_resolution
                                                   length:sizeof(output_resolution)
                                                  options:MTLResourceStorageModeShared];
    
    [[NSNotificationCenter defaultCenter] addObserver:self selector:@selector(loadShader) name:@"GBFilterChanged" object:nil];
    [self loadShader];
}

- (void) loadShader
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
    options.fastMathEnabled = YES;
    id<MTLLibrary> library = [device newLibraryWithSource:shader_source
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
    pipeline_state = [device newRenderPipelineStateWithDescriptor:pipeline_state_descriptor
                                                             error:&error];
    if (error)  {
        NSLog(@"Failed to created pipeline state, error %@", error);
        return;
    }
    
    command_queue = [device newCommandQueue];
}

- (void)mtkView:(nonnull MTKView *)view drawableSizeWillChange:(CGSize)size
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [(MTKView *)self.internalView draw];
    });
}

- (void)drawInMTKView:(nonnull MTKView *)view
{
    if (!(view.window.occlusionState & NSWindowOcclusionStateVisible)) return;

    CGContextRef buffer = self.currentBuffer;
    NSSize bufferDimensions = NSMakeSize(CGBitmapContextGetWidth(buffer), CGBitmapContextGetHeight(buffer));
    
    if (texture.width  != bufferDimensions.width ||
        texture.height != bufferDimensions.height) {
        [self allocateTexturesForSize:bufferDimensions];
    }
    
    MTLRegion region = {
        {0, 0, 0},         // MTLOrigin
        {texture.width, texture.height, 1} // MTLSize
    };

    [texture replaceRegion:region
               mipmapLevel:0
                 withBytes:CGBitmapContextGetData(buffer)
               bytesPerRow:texture.width * 4];
    if ([self shouldBlendFrameWithPrevious]) {
        [previous_texture replaceRegion:region
                            mipmapLevel:0
                              withBytes:CGBitmapContextGetData(self.previousBuffer)
                            bytesPerRow:texture.width * 4];
    }
    
    MTLRenderPassDescriptor *render_pass_descriptor = view.currentRenderPassDescriptor;
    id<MTLCommandBuffer> command_buffer = [command_queue commandBuffer];

    if(render_pass_descriptor != nil)
    {
        NSRect viewport = self.widescreenEnabled ? self.bounds : self.viewport;
        double scale = self.window.backingScaleFactor;
        output_resolution = (vector_float2){viewport.size.width * scale, viewport.size.height * scale};

        *(bool *)[mix_previous_buffer contents] = [self shouldBlendFrameWithPrevious];
        *(vector_float2 *)[output_resolution_buffer contents] = output_resolution;

        id<MTLRenderCommandEncoder> render_encoder =
            [command_buffer renderCommandEncoderWithDescriptor:render_pass_descriptor];
        
        [render_encoder setViewport:(MTLViewport){
            viewport.origin.x * scale,
            viewport.origin.y * scale,
            viewport.size.width * scale,
            viewport.size.height * scale,
            -1.0, 1.0}];
        
        [render_encoder setRenderPipelineState:pipeline_state];
        
        [render_encoder setVertexBuffer:vertices
                                 offset:0
                                atIndex:0];
        
        [render_encoder setFragmentBuffer:mix_previous_buffer
                                   offset:0
                                  atIndex:0];
        
        [render_encoder setFragmentBuffer:output_resolution_buffer
                                   offset:0
                                  atIndex:1];
        
        [render_encoder setFragmentTexture:texture
                                  atIndex:0];
        
        [render_encoder setFragmentTexture:previous_texture
                                   atIndex:1];
        
        [render_encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip
                          vertexStart:0
                          vertexCount:4];
        
        [render_encoder endEncoding];
        
        [command_buffer presentDrawable:view.currentDrawable];
    }
    
    
    [command_buffer commit];
}

- (void)flip
{
    [super flip];
    dispatch_async(dispatch_get_main_queue(), ^{
        [(MTKView *)self.internalView draw];
    });
}

@end
