#import <Foundation/Foundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import "GBAudioClient.h"

static OSStatus render(
                    GBAudioClient *self,
                    AudioUnitRenderActionFlags *ioActionFlags,
                    const AudioTimeStamp *inTimeStamp,
                    UInt32 inBusNumber,
                    UInt32 inNumberFrames,
                    AudioBufferList *ioData)

{
    GB_sample_t *buffer = (GB_sample_t *)ioData->mBuffers[0].mData;

    self.renderBlock(self.rate, inNumberFrames, buffer);
    
    return noErr;
}

@implementation GBAudioClient
{
    AudioComponentInstance audioUnit;
}

-(id) initWithRendererBlock:(void (^)(UInt32 sampleRate, UInt32 nFrames, GB_sample_t *buffer)) block
              andSampleRate:(UInt32) rate
{
    if (!(self = [super init])) { 
        return nil;
    }

    // Configure the search parameters to find the default playback output unit
    // (called the kAudioUnitSubType_RemoteIO on iOS but
    // kAudioUnitSubType_DefaultOutput on Mac OS X)
    AudioComponentDescription defaultOutputDescription;
    defaultOutputDescription.componentType = kAudioUnitType_Output;
#if TARGET_OS_IPHONE
    defaultOutputDescription.componentSubType = kAudioUnitSubType_RemoteIO;
#else
    defaultOutputDescription.componentSubType = kAudioUnitSubType_DefaultOutput;
#endif
    defaultOutputDescription.componentManufacturer = kAudioUnitManufacturer_Apple;
    defaultOutputDescription.componentFlags = 0;
    defaultOutputDescription.componentFlagsMask = 0;

    // Get the default playback output unit
    AudioComponent defaultOutput = AudioComponentFindNext(NULL, &defaultOutputDescription);
    if (!defaultOutput) {
        NSLog(@"Can't find default output");
        return nil;
    }

    // Create a new unit based on this that we'll use for output
    OSErr err = AudioComponentInstanceNew(defaultOutput, &audioUnit);
    if (!audioUnit) {
        NSLog(@"Error creating unit: %hd", err);
        return nil;
    }

    // Set our tone rendering function on the unit
    AURenderCallbackStruct input;
    input.inputProc = (void*)render;
    input.inputProcRefCon = (__bridge void *)(self);
    err = AudioUnitSetProperty(audioUnit,
                               kAudioUnitProperty_SetRenderCallback,
                               kAudioUnitScope_Input,
                               0,
                               &input,
                               sizeof(input));
    if (err) {
        NSLog(@"Error setting callback: %hd", err);
        return nil;
    }

    AudioStreamBasicDescription streamFormat;
    streamFormat.mSampleRate = rate;
    streamFormat.mFormatID = kAudioFormatLinearPCM;
    streamFormat.mFormatFlags =
    kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked | kAudioFormatFlagsNativeEndian;
    streamFormat.mBytesPerPacket = 4;
    streamFormat.mFramesPerPacket = 1;
    streamFormat.mBytesPerFrame = 4;
    streamFormat.mChannelsPerFrame = 2;
    streamFormat.mBitsPerChannel = 2 * 8;
    err = AudioUnitSetProperty (audioUnit,
                                kAudioUnitProperty_StreamFormat,
                                kAudioUnitScope_Input,
                                0,
                                &streamFormat,
                                sizeof(AudioStreamBasicDescription));
    
    if (err) {
        NSLog(@"Error setting stream format: %hd", err);
        return nil;
    }
    err = AudioUnitInitialize(audioUnit);
    if (err) {
        NSLog(@"Error initializing unit: %hd", err);
        return nil;
    }

    self.renderBlock = block;
    _rate = rate;

    return self;
}

-(void) start
{
    OSErr err = AudioOutputUnitStart(audioUnit);
    if (err) {
        NSLog(@"Error starting unit: %hd", err);
        return;
    }
    _playing = true;

}


-(void) stop
{
    AudioOutputUnitStop(audioUnit);
    _playing = false;
}

-(void) dealloc 
{
    [self stop];
    AudioUnitUninitialize(audioUnit);
    AudioComponentInstanceDispose(audioUnit);
}

@end
