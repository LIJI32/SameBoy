#import <Foundation/Foundation.h>

@interface GBGLShader : NSObject
- (instancetype)initWithName:(NSString *) shaderName;
- (void) renderBitmap: (void *)bitmap previous:(void*) previous inSize:(NSSize)size scale: (double) scale;
@end
