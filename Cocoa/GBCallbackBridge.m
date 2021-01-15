#import "GBCallbackBridge.h"

// MARK: - Callbacks

static void boot_rom_load(GB_gameboy_t *gb, GB_boot_rom_t type)
{
    GBCallbackBridge *self = (__bridge GBCallbackBridge *)GB_get_user_data(gb);
    [self.delegate loadBootROM:type];
}

static void vblank(GB_gameboy_t *gb)
{
    GBCallbackBridge *self = (__bridge GBCallbackBridge *)GB_get_user_data(gb);
    [self.delegate vblank];
}

static void consoleLog(GB_gameboy_t *gb, const char *string, GB_log_attributes attributes)
{
    GBCallbackBridge *self = (__bridge GBCallbackBridge *)GB_get_user_data(gb);
    [self.delegate log:string withAttributes: attributes];
}

static const char *consoleInput(GB_gameboy_t *gb)
{
    GBCallbackBridge *self = (__bridge GBCallbackBridge *)GB_get_user_data(gb);
    NSString *string = [self.delegate getDebuggerInput];
    if (string == nil) {
      return NULL;
    }
    return strdup(string.UTF8String);
}

char *asyncConsoleInput(GB_gameboy_t *gb)
{
    GBCallbackBridge *self = (__bridge GBCallbackBridge *)GB_get_user_data(gb);
    NSString *string = [self.delegate getAsyncDebuggerInput];
    if (string == nil) {
      return NULL;
    }
    return strdup(string.UTF8String);
}

static uint8_t cameraGetPixel(GB_gameboy_t *gb, uint8_t x, uint8_t y)
{
    GBCallbackBridge *self = (__bridge GBCallbackBridge *)GB_get_user_data(gb);
    return [self.delegate cameraGetPixelAtX:x andY:y];
}

static void cameraRequestUpdate(GB_gameboy_t *gb)
{
    GBCallbackBridge *self = (__bridge GBCallbackBridge *)GB_get_user_data(gb);
    [self.delegate cameraRequestUpdate];
}

static void printImage(GB_gameboy_t *gb, uint32_t *image, uint8_t height,
                       uint8_t top_margin, uint8_t bottom_margin, uint8_t exposure)
{
    GBCallbackBridge *self = (__bridge GBCallbackBridge *)GB_get_user_data(gb);
    [self.delegate printImage:image height:height topMargin:top_margin bottomMargin:bottom_margin exposure:exposure];
}

static void audioCallback(GB_gameboy_t *gb, GB_sample_t *sample)
{
    GBCallbackBridge *self = (__bridge GBCallbackBridge *)GB_get_user_data(gb);
    [self.delegate gotNewSample:sample];
}

static void rumbleCallback(GB_gameboy_t *gb, double amp)
{
    GBCallbackBridge *self = (__bridge GBCallbackBridge *)GB_get_user_data(gb);
    [self.delegate rumbleChanged:amp];
}

static void linkCableBitStart(GB_gameboy_t *gb, bool bit_to_send)
{
    GBCallbackBridge *self = (__bridge GBCallbackBridge *)GB_get_user_data(gb);
    [self.delegate linkCableBitStart:bit_to_send];
}

static bool linkCableBitEnd(GB_gameboy_t *gb)
{
    GBCallbackBridge *self = (__bridge GBCallbackBridge *)GB_get_user_data(gb);
    return [self.delegate linkCableBitEnd];
}

static void infraredStateChanged(GB_gameboy_t *gb, bool on)
{
    GBCallbackBridge *self = (__bridge GBCallbackBridge *)GB_get_user_data(gb);
    [self.delegate infraredStateChanged:on];
}

@implementation GBCallbackBridge {
    GB_gameboy_t *_gb;
}

- (instancetype)initWithGameboy:(GB_gameboy_t *)gb delegate:(id<GBCallbackBridgeDelegate>)delegate {
    self = [super init];
    if (self) {
        _gb = gb;
        _delegate = delegate;

        // Required callbacks.
        GB_set_user_data(gb, (__bridge void *)(self));
        GB_set_boot_rom_load_callback(gb, (GB_boot_rom_load_callback_t)boot_rom_load);

        // Optional callbacks.
        if ([delegate respondsToSelector:@selector(vblank)]) {
            GB_set_vblank_callback(gb, (GB_vblank_callback_t)vblank);
        }
        if ([delegate respondsToSelector:@selector(gotNewSample:)]) {
            GB_apu_set_sample_callback(gb, audioCallback);
        }
        if ([delegate respondsToSelector:@selector(log:withAttributes:)]) {
            GB_set_log_callback(gb, (GB_log_callback_t)consoleLog);
        }
        if ([delegate respondsToSelector:@selector(getDebuggerInput)]) {
            GB_set_input_callback(gb, (GB_input_callback_t)consoleInput);
        }
        if ([delegate respondsToSelector:@selector(getAsyncDebuggerInput)]) {
            GB_set_async_input_callback(gb, (GB_input_callback_t)asyncConsoleInput);
        }
        if ([delegate respondsToSelector:@selector(cameraGetPixelAtX:andY:)]) {
            GB_set_camera_get_pixel_callback(gb, cameraGetPixel);
        }
        if ([delegate respondsToSelector:@selector(cameraRequestUpdate)]) {
            GB_set_camera_update_request_callback(gb, cameraRequestUpdate);
        }
        if ([delegate respondsToSelector:@selector(rumbleChanged:)]) {
            GB_set_rumble_callback(gb, rumbleCallback);
        }
        if ([delegate respondsToSelector:@selector(infraredStateChanged:)]) {
            GB_set_infrared_callback(gb, infraredStateChanged);
        }
    }
    return self;
}

- (void)connectPrinter {
    assert([_delegate respondsToSelector:@selector(printImage:height:topMargin:bottomMargin:exposure:)]);
    GB_connect_printer(_gb, printImage);
}

- (void)connectLinkCableWithPartner:(GB_gameboy_t *)partnerGB {
    assert([_delegate respondsToSelector:@selector(linkCableBitStart:)]
           && [_delegate respondsToSelector:@selector(linkCableBitEnd)]);
    GB_set_serial_transfer_bit_start_callback(_gb, linkCableBitStart);
    GB_set_serial_transfer_bit_start_callback(partnerGB, linkCableBitStart);
    GB_set_serial_transfer_bit_end_callback(_gb, linkCableBitEnd);
    GB_set_serial_transfer_bit_end_callback(partnerGB, linkCableBitEnd);
}

@end
