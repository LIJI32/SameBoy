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

 The receiver is expected to invoke GB_load_boot_rom with the appropriate boot rom path.
 */
- (void)loadBootROM:(GB_boot_rom_t)type;

@optional

/**
 Informs the receiver that the PPU has entered vblank mode.

 The receiver will typically use this event to swap the pixel buffer using GB_set_pixels_output.
 */
- (void)vblank;

- (void)log:(const char * _Nonnull)log withAttributes:(GB_log_attributes)attributes;
- (char * _Nullable)getDebuggerInput;
- (char * _Nullable)getAsyncDebuggerInput;
- (uint8_t)cameraGetPixelAtX:(uint8_t)x andY:(uint8_t)y;
- (void)cameraRequestUpdate;
- (void)gotNewSample:(GB_sample_t * _Nonnull)sample;
- (void)printImage:(uint32_t * _Nonnull)image
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

/** Sets user data on gb to self and connects all applicable callbacks the delegate. */
- (nonnull instancetype)initWithGB:(GB_gameboy_t *_Nonnull)gb delegate:(nonnull id<CallbackBridgeDelegate>)delegate;
- (nonnull instancetype)init NS_UNAVAILABLE;

/**
 Connects gb's printer to the delegate.

 The delegate must implement printImage:height:topMargin:bottomMargin:exposure: or this method will assert.
 */
- (void)connectPrinter;

/**
 Connects the link cable for both gb and the partnerGB to the delegate.

 The delegate must implement both linkCableBitStart: and linkCableBitEnd or this method will assert.
 */
- (void)connectLinkCableWithPartner:(GB_gameboy_t *_Nonnull)partnerGB;

/**
 The object that implements the relevant callbacks.

 Setting this value after initialization is not presently supported.
 */
@property (nonatomic, nullable, weak, readonly) id<CallbackBridgeDelegate> delegate;

@end
