#import <Foundation/Foundation.h>

#include <Core/gb.h>

/**
 CallbackBridgeDelegate is implemented by objects that intend to receive events from a GB_gameboy_t using a
 CallbackBridge.
 */
@protocol CallbackBridgeDelegate <NSObject>
@required

/**
 Tells the receiver to load the given boot rom type.

 The receiver is expected to invoke GB_load_boot_rom with the appropriate boot rom path for the given type.
 */
- (void)loadBootROM:(GB_boot_rom_t)type;

/**
 Informs the receiver that a new audio sample was received.

 The provided sample should be appended to an audio buffer for playback.
 */
- (void)gotNewSample:(nonnull GB_sample_t *)sample;

@optional

/**
 Informs the receiver that the PPU has entered vblank mode.

 The receiver will typically use this event to swap the pixel buffer using GB_set_pixels_output.
 */
- (void)vblank;

- (void)log:(nonnull const char *)log withAttributes:(GB_log_attributes)attributes;

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
 A CallbackBridge can be used to connect a GB_gameboy_t's callbacks to an Objective-C instance.

 An instance of this class is typically owned by the object that conforms to the CallbackBridgeDelegate protocol, and
 that same object is provided to this instance's init method as the delegate.
 */
__attribute__((objc_subclassing_restricted))
@interface CallbackBridge: NSObject

/** Sets user data on gb to self and connects all applicable callbacks to the delegate. */
- (nonnull instancetype)initWithGB:(nonnull GB_gameboy_t *)gb delegate:(nonnull id<CallbackBridgeDelegate>)delegate;
- (nonnull instancetype)init NS_UNAVAILABLE;

/**
 Connects gb's printer to the delegate.

 The delegate must implement printImage:height:topMargin:bottomMargin:exposure: or this method will assert.
 */
- (void)connectPrinter;

/**
 Connects the link cable for both gb and partnerGB to the delegate.

 The delegate must implement both linkCableBitStart: and linkCableBitEnd or this method will assert.
 */
- (void)connectLinkCableWithPartner:(nonnull GB_gameboy_t *)partnerGB;

/**
 The object that implements the relevant callbacks.

 Setting this value after initialization is not presently supported.
 */
@property (nonatomic, nullable, weak, readonly) id<CallbackBridgeDelegate> delegate;

@end
