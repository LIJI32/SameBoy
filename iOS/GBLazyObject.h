#import <Foundation/Foundation.h>

@interface GBLazyObject : NSProxy
- (id)initWithConstructor:(id (^)(void))constructor;
@end
