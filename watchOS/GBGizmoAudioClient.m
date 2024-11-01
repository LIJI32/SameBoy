#import "GBGizmoAudioClient.h"
#import <AVFAudio/AVFAudio.h>

#define MAX_SAMPLES 4096
#define MIN_SAMPLES 1920
#define DEFAULT_SAMPLES 2048

@implementation GBGizmoAudioClient
{
    AVAudioPCMBuffer *_pendingBuffer;
    AVAudioEngine *_engine;
    AVAudioPlayerNode *_player;
    AVAudioFormat *_format;
    bool _acitve;
    _Atomic unsigned _queueSize;
}

- (instancetype)init
{
    self = [super init];
    _engine = [[AVAudioEngine alloc] init];
    _player = [[AVAudioPlayerNode alloc] init];
    [_engine attachNode:_player];
    [_engine connect:_player to:[_engine mainMixerNode] format:nil];
    _format = [_player outputFormatForBus:0];
    return self;
}

- (void)start
{
    if (_acitve) return;
    _queueSize = 0;
    _pendingBuffer = nil;
    [_engine startAndReturnError:nil];
    [_player play];
    _acitve = true;
}

- (unsigned)rate
{
    return _format.sampleRate;
}

- (void)stop
{
    if (!_acitve) return;;
    [_player stop];
    [_engine stop];
    _acitve = false;
    _pendingBuffer = nil;
    _queueSize = 0;
}

- (bool)isPlaying
{
    return _acitve;
}

- (double)volume
{
    return _engine.mainMixerNode.outputVolume;
}

- (void)setVolume:(double)volume
{
    _engine.mainMixerNode.outputVolume = volume;
}

- (void)pushSample:(GB_sample_t *)sample
{
    if (GB_unlikely(!_acitve)) return;
    if (GB_unlikely(!_pendingBuffer)) {
        _pendingBuffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:_format frameCapacity:MAX_SAMPLES];
    }
    unsigned length = _pendingBuffer.frameLength;
    if (GB_likely(length < MAX_SAMPLES)) {
        _pendingBuffer.frameLength++;
        assert(_pendingBuffer.mutableAudioBufferList->mNumberBuffers == 2);
        ((Float32 *)_pendingBuffer.mutableAudioBufferList->mBuffers[0].mData)[length] = sample->left / (float)0x8000;
        ((Float32 *)_pendingBuffer.mutableAudioBufferList->mBuffers[1].mData)[length] = sample->right / (float)0x8000;
        length++;
    }
    if (_queueSize >= 2) return;
    unsigned target = _queueSize == 0? DEFAULT_SAMPLES : MIN_SAMPLES;
    if (GB_unlikely(length >= target)) {
        _queueSize++;
        [_player scheduleBuffer:_pendingBuffer completionHandler:^{
            _queueSize--;
        }];
        _pendingBuffer = nil;
    }
    
}
@end
