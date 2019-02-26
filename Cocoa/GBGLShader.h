#import <Foundation/Foundation.h>

@interface GBGLShader : NSObject
- (instancetype)initWithName:(NSString *) shaderName;
- (void) renderBitmap:(void *)bitmap previous:(void*)previous sized:(NSSize)srcSize inRect:(NSRect)dstRect scale:(double)scale;
@end
