#import <Foundation/Foundation.h>
#import <Core/gb.h>

@interface GBAudioClient : NSObject
@property (nonatomic, strong) void (^renderBlock)(UInt32 sampleRate, UInt32 nFrames, GB_sample_t *buffer);
@property (nonatomic, readonly) UInt32 rate;
@property (nonatomic, readonly, getter=isPlaying) bool playing;
- (void)start;
- (void)stop;
- (id)initWithRendererBlock:(void (^)(UInt32 sampleRate, UInt32 nFrames, GB_sample_t *buffer)) block
              andSampleRate:(UInt32) rate;
@end
