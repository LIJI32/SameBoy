#import "GBLazyObject.h"
#import <Core/gb.h>

@implementation GBLazyObject
{
    id _target;
    id (^_constructor)(void);
}


- (id)initWithConstructor:(id (^)(void))constructor
{
    _constructor = constructor;
    return self;
}

- (NSMethodSignature *)methodSignatureForSelector:(SEL)sel
{
    if (GB_likely(!_target)) {
        _target = _constructor();
        _constructor = nil;
    }
    return [_target methodSignatureForSelector:sel];
}

- (void)forwardInvocation:(NSInvocation *)invocation
{
    if (GB_likely(!_target)) {
        _target = _constructor();
        _constructor = nil;
    }
    invocation.target = _target;
    [invocation invoke];
}

- (instancetype)self
{
    if (GB_likely(!_target)) {
        _target = _constructor();
        _constructor = nil;
    }
    return _target;
}

@end
