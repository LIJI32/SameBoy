#import <Foundation/Foundation.h>
#import <Core/gb.h>

@interface GBGizmoAudioClient : NSObject
@property (nonatomic, readonly) unsigned rate;
@property (nonatomic, readonly, getter=isPlaying) bool playing;
@property double volume;
- (void)start;
- (void)stop;
- (void)pushSample:(GB_sample_t *)sample __attribute__((objc_direct));
@end
