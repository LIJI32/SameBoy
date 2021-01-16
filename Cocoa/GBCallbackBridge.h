#import <Foundation/Foundation.h>

#include <Core/gb.h>

/** GB_connect_printer can use this callback to invoke the delegate for an already connected GBCallbackBridge. */
void GBCallbackPrintImage(GB_gameboy_t *_Nonnull gb, uint32_t *_Nonnull image, uint8_t height,
                          uint8_t top_margin, uint8_t bottom_margin, uint8_t exposure);

/**
 GBCallbackBridgeDelegate is implemented by objects that intend to receive events from a GB_gameboy_t using a
 GBGBCallbackBridge.
 */
@protocol GBCallbackBridgeDelegate <NSObject>
@required

/**
 Tells the receiver to load the given boot rom type.

 The receiver is expected to invoke GB_load_boot_rom with the appropriate boot rom path for the given type.
 */
- (void)loadBootROM:(GB_boot_rom_t)type;

@optional

/**
 Informs the receiver that the PPU has entered vblank mode.

 The receiver will typically use this event to swap the pixel buffer using GB_set_pixels_output.
 */
- (void)vblank;

/**
 Informs the receiver that a new audio sample was received.

 The provided sample should be appended to an audio buffer for playback.

 This method is required if a sample rate is provided by GB_set_sample_rate.
 */
- (void)gotNewSample:(nonnull GB_sample_t *)sample;

/**
 Informs the receiver that a log message has been generated.

 The log message is typically appended to a console.
 */
- (void)log:(nonnull NSString *)log withAttributes:(GB_log_attributes)attributes;

/**
 Asks the receiver for the next debugger input.

 This is a blocking invocation; until it returns, the emulator will be paused on a background thread.

 Returning nil will cause emulation to continue, and this method will not be invoked again until GB_debugger_break is
 invoked on the GB_gameboy_t again.
 */
- (nullable NSString *)getDebuggerInput;
- (nullable NSString *)getAsyncDebuggerInput;
- (uint8_t)cameraGetPixelAtX:(uint8_t)x andY:(uint8_t)y;
- (void)cameraRequestUpdate;
- (void)printImage:(nonnull uint32_t *)image
            height:(unsigned)height
         topMargin:(unsigned)topMargin
      bottomMargin:(unsigned)bottomMargin
          exposure:(unsigned)exposure;
- (void)rumbleChanged:(double)amp;
- (void)linkCableBitStart:(bool)bit;
- (bool)linkCableBitEnd;
- (void)infraredStateChanged:(bool)state;

@end

/**
 A GBCallbackBridge can be used to connect a GB_gameboy_t's callbacks to an Objective-C instance.

 An instance of this class is typically owned by the object that conforms to the GBCallbackBridgeDelegate protocol, and
 that same object is provided to this instance's init method as the delegate.
 */
__attribute__((objc_subclassing_restricted))
@interface GBCallbackBridge: NSObject

/** Immediately sets user data on gb to self and connects all implemented callbacks to the delegate. */
- (nonnull instancetype)initWithGameboy:(nonnull GB_gameboy_t *)gb
                               delegate:(nonnull id<GBCallbackBridgeDelegate>)delegate;
- (nonnull instancetype)init NS_UNAVAILABLE;

/** The delegate must implement both linkCableBitStart: and linkCableBitEnd or this method will assert. */
- (void)enableSerialCallbacks;

/**
 The object that implements the relevant callbacks.

 Setting this value after initialization is not presently supported.
 */
@property (nonatomic, nullable, weak, readonly) id<GBCallbackBridgeDelegate> delegate;

@end
