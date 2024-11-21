#import <AVFoundation/AVFoundation.h>
#import <CoreAudio/CoreAudio.h>
#import <Core/gb.h>
#import "GBAudioClient.h"
#import "Document.h"
#import "GBApp.h"
#import "HexFiend/HexFiend.h"
#import "GBMemoryByteArray.h"
#import "GBWarningPopover.h"
#import "GBCheatWindowController.h"
#import "GBTerminalTextFieldCell.h"
#import "BigSurToolbar.h"
#import "GBPaletteEditorController.h"
#import "GBCheatSearchController.h"
#import "GBObjectView.h"
#import "GBPaletteView.h"
#import "GBHexStatusBarRepresenter.h"
#import "NSObject+DefaultsObserver.h"

#define likely(x)   GB_likely(x)
#define unlikely(x) GB_unlikely(x)

@implementation NSString (relativePath)

- (NSString *)pathRelativeToDirectory:(NSString *)directory
{
    NSMutableArray<NSString *> *baseComponents = [[directory pathComponents] mutableCopy];
    NSMutableArray<NSString *> *selfComponents = [[self pathComponents] mutableCopy];
    
    while (baseComponents.count) {
        if (![baseComponents.firstObject isEqualToString:selfComponents.firstObject]) {
            break;
        }
        
        [baseComponents removeObjectAtIndex:0];
        [selfComponents removeObjectAtIndex:0];
    }
    while (baseComponents.count) {
        [baseComponents removeObjectAtIndex:0];
        [selfComponents insertObject:@".." atIndex:0];
    }
    return [selfComponents componentsJoinedByString:@"/"];
}

@end

#define GB_MODEL_PAL_BIT_OLD 0x1000

/* Todo: The general Objective-C coding style conflicts with SameBoy's. This file needs a cleanup. */
/* Todo: Split into category files! This is so messy!!! */


@interface Document ()
@property GBAudioClient *audioClient;
@end

@implementation Document
{
    GB_gameboy_t _gb;
    volatile bool _running;
    volatile bool _stopping;
    NSConditionLock *_hasDebuggerInput;
    NSMutableArray *_debuggerInputQueue;
    
    NSMutableAttributedString *_pendingConsoleOutput;
    NSRecursiveLock *_consoleOutputLock;
    NSTimer *_consoleOutputTimer;
    NSTimer *_hexTimer;
    
    bool _fullScreen;
    bool _inSyncInput;
    NSString *_debuggerCommandWhilePaused;
    HFController *_hexController;
    
    NSString *_lastConsoleInput;
    HFLineCountingRepresenter *_lineRep;
    GBHexStatusBarRepresenter *_statusRep;
    
    CVImageBufferRef _cameraImage;
    AVCaptureSession *_cameraSession;
    AVCaptureConnection *_cameraConnection;
    AVCaptureStillImageOutput *_cameraOutput;
    
    GB_oam_info_t _oamInfo[40];
    
    NSMutableData *_currentPrinterImageData;
    
    bool _romWarningIssued;
    
    NSMutableString *_capturedOutput;
    bool _logToSideView;
    bool _shouldClearSideView;
    enum model _currentModel;
    bool _usesAutoModel;
    
    bool _rewind;
    bool _modelsChanging;
    
    NSCondition *_audioLock;
    GB_sample_t *_audioBuffer;
    size_t _audioBufferSize;
    size_t _audioBufferPosition;
    size_t _audioBufferNeeded;
    double _volume;
    
    bool _borderModeChanged;
    
    /* Link cable*/
    Document *_master;
    Document *_slave;
    signed _linkOffset;
    bool _linkCableBit;
    
    NSSavePanel *_audioSavePanel;
    bool _isRecordingAudio;
    
    void (^ volatile _pendingAtomicBlock)();
    
    NSDate *_fileModificationTime;
    __weak NSThread *_emulationThread;
    
    GBCheatSearchController *_cheatSearchController;
}

static void boot_rom_load(GB_gameboy_t *gb, GB_boot_rom_t type)
{
    Document *self = (__bridge Document *)GB_get_user_data(gb);
    [self loadBootROM: type];
}

static void vblank(GB_gameboy_t *gb, GB_vblank_type_t type)
{
    Document *self = (__bridge Document *)GB_get_user_data(gb);
    [self vblankWithType:type];
}

static void consoleLog(GB_gameboy_t *gb, const char *string, GB_log_attributes_t attributes)
{
    Document *self = (__bridge Document *)GB_get_user_data(gb);
    [self log:string withAttributes: attributes];
}

static char *consoleInput(GB_gameboy_t *gb)
{
    Document *self = (__bridge Document *)GB_get_user_data(gb);
    return [self getDebuggerInput];
}

static char *asyncConsoleInput(GB_gameboy_t *gb)
{
    Document *self = (__bridge Document *)GB_get_user_data(gb);
    char *ret = [self getAsyncDebuggerInput];
    return ret;
}

static uint32_t rgbEncode(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    return (r << 0) | (g << 8) | (b << 16) | 0xFF000000;
}

static void cameraRequestUpdate(GB_gameboy_t *gb)
{
    Document *self = (__bridge Document *)GB_get_user_data(gb);
    [self cameraRequestUpdate];
}

static uint8_t cameraGetPixel(GB_gameboy_t *gb, uint8_t x, uint8_t y)
{
    Document *self = (__bridge Document *)GB_get_user_data(gb);
    return [self cameraGetPixelAtX:x andY:y];
}

static void printImage(GB_gameboy_t *gb, uint32_t *image, uint8_t height,
                       uint8_t top_margin, uint8_t bottom_margin, uint8_t exposure)
{
    Document *self = (__bridge Document *)GB_get_user_data(gb);
    [self printImage:image height:height topMargin:top_margin bottomMargin:bottom_margin exposure:exposure];
}

static void printDone(GB_gameboy_t *gb)
{
    Document *self = (__bridge Document *)GB_get_user_data(gb);
    [self printDone];
}

static void setWorkboyTime(GB_gameboy_t *gb, time_t t)
{
    [[NSUserDefaults standardUserDefaults] setInteger:time(NULL) - t forKey:@"GBWorkboyTimeOffset"];
}

static time_t getWorkboyTime(GB_gameboy_t *gb)
{
    return time(NULL) - [[NSUserDefaults standardUserDefaults] integerForKey:@"GBWorkboyTimeOffset"];
}

static void audioCallback(GB_gameboy_t *gb, GB_sample_t *sample)
{
    Document *self = (__bridge Document *)GB_get_user_data(gb);
    [self gotNewSample:sample];
}

static void rumbleCallback(GB_gameboy_t *gb, double amp)
{
    Document *self = (__bridge Document *)GB_get_user_data(gb);
    [self rumbleChanged:amp];
}


static void _linkCableBitStart(GB_gameboy_t *gb, bool bit_to_send)
{
    Document *self = (__bridge Document *)GB_get_user_data(gb);
    [self _linkCableBitStart:bit_to_send];
}

static bool _linkCableBitEnd(GB_gameboy_t *gb)
{
    Document *self = (__bridge Document *)GB_get_user_data(gb);
    return [self _linkCableBitEnd];
}

static void infraredStateChanged(GB_gameboy_t *gb, bool on)
{
    Document *self = (__bridge Document *)GB_get_user_data(gb);
    [self infraredStateChanged:on];
}

static void debuggerReloadCallback(GB_gameboy_t *gb)
{
    Document *self = (__bridge Document *)GB_get_user_data(gb);
    dispatch_sync(dispatch_get_main_queue(), ^{
        bool wasRunning = self->_running;
        self->_running = false; // Hack for output capture
        [self loadROM];
        self->_running = wasRunning;
        GB_reset(gb);
    });
}

- (instancetype)init 
{
    self = [super init];
    if (self) {
        _hasDebuggerInput = [[NSConditionLock alloc] initWithCondition:0];
        _debuggerInputQueue = [[NSMutableArray alloc] init];
        _consoleOutputLock = [[NSRecursiveLock alloc] init];
        _audioLock = [[NSCondition alloc] init];
        _volume = [[NSUserDefaults standardUserDefaults] doubleForKey:@"GBVolume"];
    }
    return self;
}

- (GB_model_t)internalModel
{
    switch (_currentModel) {
        case MODEL_DMG:
            return (GB_model_t)[[NSUserDefaults standardUserDefaults] integerForKey:@"GBDMGModel"];
            
        case MODEL_NONE:
        case MODEL_QUICK_RESET:
        case MODEL_AUTO:
        case MODEL_CGB:
            return (GB_model_t)[[NSUserDefaults standardUserDefaults] integerForKey:@"GBCGBModel"];
            
        case MODEL_SGB: {
            GB_model_t model = (GB_model_t)[[NSUserDefaults standardUserDefaults] integerForKey:@"GBSGBModel"];
            if (model == (GB_MODEL_SGB | GB_MODEL_PAL_BIT_OLD)) {
                model = GB_MODEL_SGB_PAL;
            }
            return model;
        }
        
        case MODEL_MGB:
            return GB_MODEL_MGB;
        
        case MODEL_AGB:
            return (GB_model_t)[[NSUserDefaults standardUserDefaults] integerForKey:@"GBAGBModel"];
    }
}

- (void)updatePalette
{
    GB_set_palette(&_gb, [GBPaletteEditorController userPalette]);
}

- (void)initCommon
{
    GB_init(&_gb, [self internalModel]);
    GB_set_user_data(&_gb, (__bridge void *)(self));
    GB_set_boot_rom_load_callback(&_gb, (GB_boot_rom_load_callback_t)boot_rom_load);
    GB_set_vblank_callback(&_gb, (GB_vblank_callback_t) vblank);
    GB_set_log_callback(&_gb, (GB_log_callback_t) consoleLog);
    GB_set_input_callback(&_gb, (GB_input_callback_t) consoleInput);
    GB_set_async_input_callback(&_gb, (GB_input_callback_t) asyncConsoleInput);
    [self updatePalette];
    GB_set_rgb_encode_callback(&_gb, rgbEncode);
    GB_set_camera_get_pixel_callback(&_gb, cameraGetPixel);
    GB_set_camera_update_request_callback(&_gb, cameraRequestUpdate);
    GB_apu_set_sample_callback(&_gb, audioCallback);
    GB_set_rumble_callback(&_gb, rumbleCallback);
    GB_set_infrared_callback(&_gb, infraredStateChanged);
    GB_debugger_set_reload_callback(&_gb, debuggerReloadCallback);
    
    GB_gameboy_t *gb = &_gb;
    __unsafe_unretained Document *weakSelf = self;
    
    [self observeStandardDefaultsKey:@"GBColorCorrection" withBlock:^(NSNumber *value) {
        GB_set_color_correction_mode(gb, value.unsignedIntValue);
    }];
    
    [self observeStandardDefaultsKey:@"GBLightTemperature" withBlock:^(NSNumber *value) {
        GB_set_light_temperature(gb, value.doubleValue);
    }];
    
    [self observeStandardDefaultsKey:@"GBInterferenceVolume" withBlock:^(NSNumber *value) {
        GB_set_interference_volume(gb, value.doubleValue);
    }];
    
    GB_set_border_mode(&_gb, (GB_border_mode_t) [[NSUserDefaults standardUserDefaults] integerForKey:@"GBBorderMode"]);
    [self observeStandardDefaultsKey:@"GBBorderMode" withBlock:^(NSNumber *value) {
        _borderModeChanged = true;
    }];
    
    [self observeStandardDefaultsKey:@"GBHighpassFilter" withBlock:^(NSNumber *value) {
        GB_set_highpass_filter_mode(gb, value.unsignedIntValue);
    }];
    
    [self observeStandardDefaultsKey:@"GBRewindLength" withBlock:^(NSNumber *value) {
        [weakSelf performAtomicBlock:^{
            GB_set_rewind_length(gb, value.unsignedIntValue);
        }];
    }];
    
    [self observeStandardDefaultsKey:@"GBRTCMode" withBlock:^(NSNumber *value) {
        GB_set_rtc_mode(gb, value.unsignedIntValue);
    }];
    
    [self observeStandardDefaultsKey:@"GBRumbleMode" withBlock:^(NSNumber *value) {
        GB_set_rumble_mode(gb, value.unsignedIntValue);
    }];
    
    [self observeStandardDefaultsKey:@"GBDebuggerFont" withBlock:^(NSString *value) {
        [self updateFonts];
    }];
    
    [self observeStandardDefaultsKey:@"GBDebuggerFontSize" withBlock:^(NSString *value) {
        [self updateFonts];
    }];
}

- (void)updateMinSize
{
    self.mainWindow.contentMinSize = NSMakeSize(GB_get_screen_width(&_gb), GB_get_screen_height(&_gb));
    if (self.mainWindow.contentView.bounds.size.width < GB_get_screen_width(&_gb) ||
        self.mainWindow.contentView.bounds.size.width < GB_get_screen_height(&_gb)) {
        [self.mainWindow zoom:nil];
    }
    self.osdView.usesSGBScale = GB_get_screen_width(&_gb) == 256;
}

- (void)vblankWithType:(GB_vblank_type_t)type
{
    if (_gbsVisualizer) {
        dispatch_async(dispatch_get_main_queue(), ^{
            [_gbsVisualizer setNeedsDisplay:true];
        });
    }
    if (type != GB_VBLANK_TYPE_REPEAT) {
        [self.view flip];
        if (_borderModeChanged) {
            dispatch_sync(dispatch_get_main_queue(), ^{
                size_t previous_width = GB_get_screen_width(&_gb);
                GB_set_border_mode(&_gb, (GB_border_mode_t) [[NSUserDefaults standardUserDefaults] integerForKey:@"GBBorderMode"]);
                if (GB_get_screen_width(&_gb) != previous_width) {
                    [self.view screenSizeChanged];
                    [self updateMinSize];
                }
            });
            _borderModeChanged = false;
        }
        GB_set_pixels_output(&_gb, self.view.pixels);
    }
    
    if (self.vramWindow.isVisible) {
        dispatch_async(dispatch_get_main_queue(), ^{
            self.view.mouseHidingEnabled = (self.mainWindow.styleMask & NSWindowStyleMaskFullScreen) != 0;
            [self reloadVRAMData: nil];
        });
    }
    if (self.view.isRewinding) {
        _rewind = true;
        [self.osdView displayText:@"Rewinding…"];
    }
}

- (void)gotNewSample:(GB_sample_t *)sample
{
    if (_gbsVisualizer) {
        [_gbsVisualizer addSample:sample];
    }
    [_audioLock lock];
    if (_audioClient.isPlaying) {
        if (_audioBufferPosition == _audioBufferSize) {
            if (_audioBufferSize >= 0x4000) {
                _audioBufferPosition = 0;
                [_audioLock unlock];
                return;
            }
            
            if (_audioBufferSize == 0) {
                _audioBufferSize = 512;
            }
            else {
                _audioBufferSize += _audioBufferSize >> 2;
            }
            _audioBuffer = realloc(_audioBuffer, sizeof(*sample) * _audioBufferSize);
        }
        if (_volume != 1) {
            sample->left *= _volume;
            sample->right *= _volume;
        }
        _audioBuffer[_audioBufferPosition++] = *sample;
    }
    if (_audioBufferPosition == _audioBufferNeeded) {
        [_audioLock signal];
        _audioBufferNeeded = 0;
    }
    [_audioLock unlock];
}

- (void)rumbleChanged:(double)amp
{
    [_view setRumble:amp];
}

- (void)preRun
{
    GB_set_pixels_output(&_gb, self.view.pixels);
    GB_set_sample_rate(&_gb, 96000);
    _audioClient = [[GBAudioClient alloc] initWithRendererBlock:^(UInt32 sampleRate, UInt32 nFrames, GB_sample_t *buffer) {
        [_audioLock lock];
        
        if (_audioBufferPosition < nFrames) {
            _audioBufferNeeded = nFrames;
            [_audioLock waitUntilDate:[NSDate dateWithTimeIntervalSinceNow:(double)(_audioBufferNeeded - _audioBufferPosition) / sampleRate]];
            _audioBufferNeeded = 0;
        }
        
        if (_stopping || GB_debugger_is_stopped(&_gb)) {
            memset(buffer, 0, nFrames * sizeof(*buffer));
            [_audioLock unlock];
            return;
        }
        
        if (_audioBufferPosition < nFrames) {
            // Not enough audio
            memset(buffer, 0, (nFrames - _audioBufferPosition) * sizeof(*buffer));
            memcpy(buffer, _audioBuffer, _audioBufferPosition * sizeof(*buffer));
            // Do not reset the audio position to avoid more underflows
        }
        else if (_audioBufferPosition < nFrames + 4800) {
            memcpy(buffer, _audioBuffer, nFrames * sizeof(*buffer));
            memmove(_audioBuffer, _audioBuffer + nFrames, (_audioBufferPosition - nFrames) * sizeof(*buffer));
            _audioBufferPosition = _audioBufferPosition - nFrames;
        }
        else {
            memcpy(buffer, _audioBuffer + (_audioBufferPosition - nFrames), nFrames * sizeof(*buffer));
            _audioBufferPosition = 0;
        }
        [_audioLock unlock];
    } andSampleRate:96000];
    if (![[NSUserDefaults standardUserDefaults] boolForKey:@"Mute"]) {
        [_audioClient start];
    }
    _hexTimer = [NSTimer timerWithTimeInterval:0.25 target:self selector:@selector(reloadMemoryView) userInfo:nil repeats:true];
    [[NSRunLoop mainRunLoop] addTimer:_hexTimer forMode:NSDefaultRunLoopMode];
    
    /* Clear pending alarms, don't play alarms while playing */
    if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBNotificationsUsed"]) {
        NSUserNotificationCenter *center = [NSUserNotificationCenter defaultUserNotificationCenter];
        for (NSUserNotification *notification in [center scheduledNotifications]) {
            if ([notification.identifier isEqualToString:self.fileURL.path]) {
                [center removeScheduledNotification:notification];
                break;
            }
        }
        
        for (NSUserNotification *notification in [center deliveredNotifications]) {
            if ([notification.identifier isEqualToString:self.fileURL.path]) {
                [center removeDeliveredNotification:notification];
                break;
            }
        }
    }
}

static unsigned *multiplication_table_for_frequency(unsigned frequency)
{
    unsigned *ret = malloc(sizeof(*ret) * 0x100);
    for (unsigned i = 0; i < 0x100; i++) {
        ret[i] = i * frequency;
    }
    return ret;
}

- (void)run
{
    assert(!_master);
    [self preRun];
    if (_slave) {
        [_slave preRun];
        unsigned *masterTable = multiplication_table_for_frequency(GB_get_clock_rate(&_gb));
        unsigned *slaveTable = multiplication_table_for_frequency(GB_get_clock_rate(&_slave->_gb));
        while (_running) {
            if (_linkOffset <= 0) {
                _linkOffset += slaveTable[GB_run(&_gb)];
            }
            else {
                _linkOffset -= masterTable[GB_run(&_slave->_gb)];
            }
            if (unlikely(_pendingAtomicBlock)) {
                _pendingAtomicBlock();
                _pendingAtomicBlock = nil;
            }
        }
        free(masterTable);
        free(slaveTable);
        [_slave postRun];
    }
    else {
        while (_running) {
            if (_rewind) {
                _rewind = false;
                GB_rewind_pop(&_gb);
                if (!GB_rewind_pop(&_gb)) {
                    _rewind = self.view.isRewinding;
                }
            }
            else {
                GB_run(&_gb);
            }
            if (unlikely(_pendingAtomicBlock)) {
                _pendingAtomicBlock();
                _pendingAtomicBlock = nil;
            }
        }
    }
    [self postRun];
    _stopping = false;
}

- (void)postRun
{
    [_hexTimer invalidate];
    [_audioLock lock];
    _audioBufferPosition = _audioBufferNeeded = 0;
    [_audioLock signal];
    [_audioLock unlock];
    [_audioClient stop];
    _audioClient = nil;
    self.view.mouseHidingEnabled = false;
    GB_save_battery(&_gb, self.savPath.UTF8String);
    GB_save_cheats(&_gb, self.chtPath.UTF8String);
    unsigned time_to_alarm = GB_time_to_alarm(&_gb);
    
    if (time_to_alarm) {
        [NSUserNotificationCenter defaultUserNotificationCenter].delegate = (id)[NSApp delegate];
        NSUserNotification *notification = [[NSUserNotification alloc] init];
        NSString *friendlyName = [[self.fileURL lastPathComponent] stringByDeletingPathExtension];
        NSRegularExpression *regex = [NSRegularExpression regularExpressionWithPattern:@"\\([^)]+\\)|\\[[^\\]]+\\]" options:0 error:nil];
        friendlyName = [regex stringByReplacingMatchesInString:friendlyName options:0 range:NSMakeRange(0, [friendlyName length]) withTemplate:@""];
        friendlyName = [friendlyName stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        
        notification.title = [NSString stringWithFormat:@"%@ Played an Alarm", friendlyName];
        notification.informativeText = [NSString stringWithFormat:@"%@ requested your attention by playing a scheduled alarm", friendlyName];
        notification.identifier = self.fileURL.path;
        notification.deliveryDate = [NSDate dateWithTimeIntervalSinceNow:time_to_alarm];
        notification.soundName = NSUserNotificationDefaultSoundName;
        [[NSUserNotificationCenter defaultUserNotificationCenter] scheduleNotification:notification];
        [[NSUserDefaults standardUserDefaults] setBool:true forKey:@"GBNotificationsUsed"];
    }
    [_view setRumble:0];
}

- (void) start
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [self updateDebuggerButtons];
        [_slave updateDebuggerButtons];
    });
    self.gbsPlayPauseButton.state = true;
    self.view.mouseHidingEnabled = (self.mainWindow.styleMask & NSWindowStyleMaskFullScreen) != 0;
    if (_master) {
        [_master start];
        return;
    }
    if (_running) return;
    _running = true;
    NSThread *emulationThraed = [[NSThread alloc] initWithTarget:self selector:@selector(run) object:nil];
    _emulationThread = emulationThraed;
    [emulationThraed start];
}

- (void) stop
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [self updateDebuggerButtons];
        [_slave updateDebuggerButtons];
    });
    self.gbsPlayPauseButton.state = false;
    if (_master) {
        if (!_master->_running) return;
        GB_debugger_set_disabled(&_gb, true);
        if (GB_debugger_is_stopped(&_gb)) {
            [self interruptDebugInputRead];
        }
        [_master stop];
        GB_debugger_set_disabled(&_gb, false);
        return;
    }
    if (!_running) return;
    GB_debugger_set_disabled(&_gb, true);
    if (GB_debugger_is_stopped(&_gb)) {
        [self interruptDebugInputRead];
    }
    [_audioLock lock];
    _stopping = true;
    [_audioLock signal];
    [_audioLock unlock];
    _running = false;
    while (_stopping) {
        [_audioLock lock];
        [_audioLock signal];
        [_audioLock unlock];
    }
    GB_debugger_set_disabled(&_gb, false);
}

- (NSString *)bootROMPathForName:(NSString *)name
{
    NSURL *url = [[NSUserDefaults standardUserDefaults] URLForKey:@"GBBootROMsFolder"];
    if (url) {
        NSString *path = [url path];
        path = [path stringByAppendingPathComponent:name];
        path = [path stringByAppendingPathExtension:@"bin"];
        if ([[NSFileManager defaultManager] fileExistsAtPath:path]) {
            return path;
        }
    }
    
    return [[NSBundle mainBundle] pathForResource:name ofType:@"bin"];
}

- (void)loadBootROM: (GB_boot_rom_t)type
{
    static NSString *const names[] = {
        [GB_BOOT_ROM_DMG_0] = @"dmg0_boot",
        [GB_BOOT_ROM_DMG] = @"dmg_boot",
        [GB_BOOT_ROM_MGB] = @"mgb_boot",
        [GB_BOOT_ROM_SGB] = @"sgb_boot",
        [GB_BOOT_ROM_SGB2] = @"sgb2_boot",
        [GB_BOOT_ROM_CGB_0] = @"cgb0_boot",
        [GB_BOOT_ROM_CGB] = @"cgb_boot",
        [GB_BOOT_ROM_CGB_E] = @"cgbE_boot",
        [GB_BOOT_ROM_AGB_0] = @"agb0_boot",
        [GB_BOOT_ROM_AGB] = @"agb_boot",
    };
    NSString *name = names[type];
    NSString *path = [self bootROMPathForName:name];
    /* These boot types are not commonly available, and they are indentical
       from an emulator perspective, so fall back to the more common variants
       if they can't be found. */
    if (!path && type == GB_BOOT_ROM_CGB_E) {
        [self loadBootROM:GB_BOOT_ROM_CGB];
        return;
    }
    if (!path && type == GB_BOOT_ROM_AGB_0) {
        [self loadBootROM:GB_BOOT_ROM_AGB];
        return;
    }
    GB_load_boot_rom(&_gb, [path UTF8String]);
}

- (enum model)bestModelForROM
{
    uint8_t *rom = GB_get_direct_access(&_gb, GB_DIRECT_ACCESS_ROM, NULL, NULL);
    if (!rom) return MODEL_CGB;
    
    if (rom[0x143] & 0x80) { // Has CGB features
        return MODEL_CGB;
    }
    if (rom[0x146] == 3) { // Has SGB features
        return MODEL_SGB;
    }
    
    if (rom[0x14B] == 1) { // Nintendo-licensed (most likely has boot ROM palettes)
        return MODEL_CGB;
    }

    if (rom[0x14B] == 0x33 &&
        rom[0x144] == '0' &&
        rom[0x145] == '1') { // Ditto
        return MODEL_CGB;
    }
    
    return MODEL_DMG;
}

- (IBAction)reset:(id)sender
{
    [self stop];
    size_t old_width = GB_get_screen_width(&_gb);
    
    if ([sender tag] > MODEL_NONE) {
        /* User explictly selected a model, save the preference */
        _currentModel = (enum model)[sender tag];
        _usesAutoModel = _currentModel == MODEL_AUTO;
        [[NSUserDefaults standardUserDefaults] setInteger:_currentModel forKey:@"GBEmulatedModel"];
    }
    
    /* Reload the ROM, SAV and SYM files */
    [self loadROM];

    if ([sender tag] == MODEL_QUICK_RESET) {
        GB_quick_reset(&_gb);
    }
    else {
        GB_switch_model_and_reset(&_gb, [self internalModel]);
    }
    
    if (old_width != GB_get_screen_width(&_gb)) {
        [self.view screenSizeChanged];
    }
    
    [self updateMinSize];
    

    [self start];

    if (_hexController) {
        /* Verify bank sanity, especially when switching models. */
        [(GBMemoryByteArray *)(_hexController.byteArray) setSelectedBank:0];
        [self hexUpdateBank:self.memoryBankInput ignoreErrors:true];
    }
    
    char title[17];
    GB_get_rom_title(&_gb, title);
    [self.osdView displayText:[NSString stringWithFormat:@"SameBoy v" GB_VERSION "\n%s\n%08X", title, GB_get_rom_crc32(&_gb)]];
}

- (IBAction)togglePause:(id)sender
{
    if (_master) {
        [_master togglePause:sender];
        return;
    }
    if (_running) {
        [self stop];
    }
    else {
        [self start];
    }
}

- (void)dealloc
{
    [_cameraSession stopRunning];
    self.view.gb = NULL;
    GB_free(&_gb);
    if (_cameraImage) {
        CVBufferRelease(_cameraImage);
    }
    if (_audioBuffer) {
        free(_audioBuffer);
    }
}

- (NSFont *)debuggerFontOfSize:(unsigned)size
{
    if (!size) {
        size = [[NSUserDefaults standardUserDefaults] integerForKey:@"GBDebuggerFontSize"];
    }
    
    bool retry = false;
    
again:;
    NSString *selectedFont = [[NSUserDefaults standardUserDefaults] stringForKey:@"GBDebuggerFont"];
    if (@available(macOS 10.15, *)) {
        if ([selectedFont isEqual:@"SF Mono"]) {
            return [NSFont monospacedSystemFontOfSize:size weight:NSFontWeightRegular];
        }
    }
    
    NSFont *ret = [NSFont fontWithName:selectedFont size:size];
    if (ret) return ret;
    
    if (retry) {
        return [NSFont userFixedPitchFontOfSize:size];
    }
    [[NSUserDefaults standardUserDefaults] removeObjectForKey:@"GBDebuggerFont"];
    retry = true;
    goto again;
}


- (void)updateFonts
{
    _hexController.font = [self debuggerFontOfSize:12];
    [self.paletteView reloadData:self];
    [self.objectView reloadData:self];
    
    NSFont *newFont = [self debuggerFontOfSize:0];
    NSFont *newBoldFont = [[NSFontManager sharedFontManager] convertFont:newFont toHaveTrait:NSBoldFontMask];
    self.debuggerSideViewInput.font = newFont;
    for (NSTextView *view in @[_debuggerSideView, _consoleOutput]) {
        NSMutableAttributedString *newString = view.attributedString.mutableCopy;
        [view.attributedString enumerateAttribute:NSFontAttributeName
                                          inRange:NSMakeRange(0, view.attributedString.length)
                                          options:0
                                       usingBlock:^(NSFont *value, NSRange range, BOOL *stop) {
            if ([[NSFontManager sharedFontManager] fontNamed:value.fontName hasTraits:NSBoldFontMask]) {
                [newString addAttributes:@{
                    NSFontAttributeName: newBoldFont
                } range:range];
            }
            else {
                [newString addAttributes:@{
                    NSFontAttributeName: newFont
                } range:range];
            }
        }];
        [view.textStorage setAttributedString:newString];
    }
    [_consoleOutput scrollToEndOfDocument:nil];
}

- (void)windowControllerDidLoadNib:(NSWindowController *)aController 
{
    [super windowControllerDidLoadNib:aController];
    // Interface Builder bug?
    [self.consoleWindow setContentSize:self.consoleWindow.minSize];
    /* Close Open Panels, if any */
    for (NSWindow *window in [[NSApplication sharedApplication] windows]) {
        if ([window isKindOfClass:[NSOpenPanel class]]) {
            [(NSOpenPanel *)window cancel:self];
        }
    }
    
    NSMutableParagraphStyle *paragraph_style = [[NSMutableParagraphStyle alloc] init];
    [paragraph_style setLineSpacing:2];
        
    self.debuggerSideViewInput.font = [self debuggerFontOfSize:0];
    self.debuggerSideViewInput.textColor = [NSColor whiteColor];
    self.debuggerSideViewInput.defaultParagraphStyle = paragraph_style;
    [self.debuggerSideViewInput setString:@"registers\nbacktrace\n"];
    ((GBTerminalTextFieldCell *)self.consoleInput.cell).gb = &_gb;
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(updateSideView)
                                                 name:NSTextDidChangeNotification
                                               object:self.debuggerSideViewInput];
    
    self.consoleOutput.textContainerInset = NSMakeSize(4, 4);
    [self.view becomeFirstResponder];
    self.view.frameBlendingMode = [[NSUserDefaults standardUserDefaults] integerForKey:@"GBFrameBlendingMode"];
    CGRect window_frame = self.mainWindow.frame;
    window_frame.size.width  = MAX([[NSUserDefaults standardUserDefaults] integerForKey:@"LastWindowWidth"],
                                  window_frame.size.width);
    window_frame.size.height = MAX([[NSUserDefaults standardUserDefaults] integerForKey:@"LastWindowHeight"],
                                   window_frame.size.height);
    [self.mainWindow setFrame:window_frame display:true];
    self.vramStatusLabel.cell.backgroundStyle = NSBackgroundStyleRaised;
        
    NSUInteger height_diff = self.vramWindow.frame.size.height - self.vramWindow.contentView.frame.size.height;
    CGRect vram_window_rect = self.vramWindow.frame;
    vram_window_rect.size.height = 384 + height_diff + 48;
    [self.vramWindow setFrame:vram_window_rect display:true animate:false];
    
    
    self.consoleWindow.title = [NSString stringWithFormat:@"Debug Console – %@", [self.fileURL.path lastPathComponent]];
    self.memoryWindow.title = [NSString stringWithFormat:@"Memory – %@", [self.fileURL.path lastPathComponent]];
    self.vramWindow.title = [NSString stringWithFormat:@"VRAM Viewer – %@", [self.fileURL.path lastPathComponent]];
    
    self.consoleWindow.level = NSNormalWindowLevel;
    
    self.debuggerSplitView.dividerColor = self.debuggerVerticalLine.borderColor;
    [self.debuggerVerticalLine removeFromSuperview]; // No longer used, just there for the color
    if (@available(macOS 11.0, *)) {
        self.memoryWindow.toolbarStyle = NSWindowToolbarStyleExpanded;
        self.printerFeedWindow.toolbarStyle = NSWindowToolbarStyleUnifiedCompact;
        self.printerFeedWindow.toolbar.items[1].image =
            [NSImage imageWithSystemSymbolName:@"square.and.arrow.down"
                      accessibilityDescription:@"Save"];
        self.printerFeedWindow.toolbar.items[2].image =
            [NSImage imageWithSystemSymbolName:@"printer"
                      accessibilityDescription:@"Print"];
        self.printerFeedWindow.toolbar.items[1].bordered = false;
        self.printerFeedWindow.toolbar.items[2].bordered = false;
    }
    else {
        NSToolbarItem *spinner = self.printerFeedWindow.toolbar.items[0];
        [self.printerFeedWindow.toolbar removeItemAtIndex:0];
        [self.printerFeedWindow.toolbar insertItemWithItemIdentifier:spinner.itemIdentifier atIndex:2];
        [self.printerFeedWindow.toolbar removeItemAtIndex:1];
        [self.printerFeedWindow.toolbar insertItemWithItemIdentifier:NSToolbarPrintItemIdentifier
                                                             atIndex:1];
        [self.printerFeedWindow.toolbar insertItemWithItemIdentifier:NSToolbarFlexibleSpaceItemIdentifier
                                                             atIndex:2];
    }
    
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(updatePalette)
                                                 name:@"GBColorPaletteChanged"
                                               object:nil];
    
    __unsafe_unretained Document *weakSelf = self;
    [self observeStandardDefaultsKey:@"GBFrameBlendingMode"
                           withBlock:^(NSNumber *value) {
        weakSelf.view.frameBlendingMode = (GB_frame_blending_mode_t)value.unsignedIntValue;
    }];
    
    [self observeStandardDefaultsKey:@"GBDMGModel" withBlock:^(id newValue) {
        weakSelf->_modelsChanging = true;
        if (weakSelf->_currentModel == MODEL_DMG) {
            [weakSelf reset:nil];
        }
        weakSelf->_modelsChanging = false;
    }];
    
    [self observeStandardDefaultsKey:@"GBSGBModel" withBlock:^(id newValue) {
        weakSelf->_modelsChanging = true;
        if (weakSelf->_currentModel == MODEL_SGB) {
            [weakSelf reset:nil];
        }
        weakSelf->_modelsChanging = false;
    }];
    
    [self observeStandardDefaultsKey:@"GBCGBModel" withBlock:^(id newValue) {
        weakSelf->_modelsChanging = true;
        if (weakSelf->_currentModel == MODEL_CGB) {
            [weakSelf reset:nil];
        }
        weakSelf->_modelsChanging = false;
    }];
    
    [self observeStandardDefaultsKey:@"GBAGBModel" withBlock:^(id newValue) {
        weakSelf->_modelsChanging = true;
        if (weakSelf->_currentModel == MODEL_AGB) {
            [weakSelf reset:nil];
        }
        weakSelf->_modelsChanging = false;
    }];
    
    
    [self observeStandardDefaultsKey:@"GBVolume" withBlock:^(id newValue) {
        weakSelf->_volume = [[NSUserDefaults standardUserDefaults] doubleForKey:@"GBVolume"];
    }];
    
    
    _currentModel = [[NSUserDefaults standardUserDefaults] integerForKey:@"GBEmulatedModel"];
    _usesAutoModel = _currentModel == MODEL_AUTO;
    
    [self initCommon];
    self.view.gb = &_gb;
    self.view.osdView = _osdView;
    [self.view screenSizeChanged];
    if ([self loadROM]) {
        _mainWindow.alphaValue = 0; // Hack hack ugly hack
        dispatch_async(dispatch_get_main_queue(), ^{
            [self close];
        });
    }
    else {
        [self reset:nil];
    }
}

- (void)initMemoryView
{
    _hexController = [[HFController alloc] init];
    _hexController.font = [self debuggerFontOfSize:12];
    [_hexController setBytesPerColumn:1];
    [_hexController setEditMode:HFOverwriteMode];
    
    [_hexController setByteArray:[[GBMemoryByteArray alloc] initWithDocument:self]];

    /* Here we're going to make three representers - one for the hex, one for the ASCII, and one for the scrollbar.  To lay these all out properly, we'll use a fourth HFLayoutRepresenter. */
    HFLayoutRepresenter *layoutRep = [[HFLayoutRepresenter alloc] init];
    HFHexTextRepresenter *hexRep = [[HFHexTextRepresenter alloc] init];
    HFStringEncodingTextRepresenter *asciiRep = [[HFStringEncodingTextRepresenter alloc] init];
    HFVerticalScrollerRepresenter *scrollRep = [[HFVerticalScrollerRepresenter alloc] init];
    _lineRep = [[HFLineCountingRepresenter alloc] init];
    _statusRep = [[GBHexStatusBarRepresenter alloc] init];
    _statusRep.gb = &_gb;
    _statusRep.bankForDescription = -1;

    _lineRep.lineNumberFormat = HFLineNumberFormatHexadecimal;

    /* Add all our reps to the controller. */
    [_hexController addRepresenter:layoutRep];
    [_hexController addRepresenter:hexRep];
    [_hexController addRepresenter:asciiRep];
    [_hexController addRepresenter:scrollRep];
    [_hexController addRepresenter:_lineRep];
    [_hexController addRepresenter:_statusRep];

    /* Tell the layout rep which reps it should lay out. */
    [layoutRep addRepresenter:hexRep];
    [layoutRep addRepresenter:scrollRep];
    [layoutRep addRepresenter:asciiRep];
    [layoutRep addRepresenter:_lineRep];
    [layoutRep addRepresenter:_statusRep];


    [(NSView *)[hexRep view] setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

    /* Grab the layout rep's view and stick it into our container. */
    NSView *layoutView = [layoutRep view];
    NSRect layoutViewFrame = self.memoryView.frame;
    [layoutView setFrame:layoutViewFrame];
    [layoutView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable | NSViewMaxYMargin];
    [self.memoryView addSubview:layoutView];
    self.memoryView = layoutView;

    CGSize contentSize = _memoryWindow.contentView.frame.size;
    while (_hexController.bytesPerLine < 16) {
        contentSize.width += 4;
        [_memoryWindow setContentSize:contentSize];
    }
    while (_hexController.bytesPerLine > 16) {
        contentSize.width -= 4;
        [_memoryWindow setContentSize:contentSize];
    }
    
    self.memoryBankItem.enabled = false;
}

+ (BOOL)autosavesInPlace 
{
    return true;
}

- (NSString *)windowNibName 
{
    // Override returning the nib file name of the document
    // If you need to use a subclass of NSWindowController or if your document supports multiple NSWindowControllers, you should remove this method and override -makeWindowControllers instead.
    return @"Document";
}

- (BOOL)readFromFile:(NSString *)fileName ofType:(NSString *)type
{
    return true;
}

- (IBAction)changeGBSTrack:(id)sender
{
    if (!_running) {
        [self start];
    }
    [self performAtomicBlock:^{
        GB_gbs_switch_track(&_gb, self.gbsTracks.indexOfSelectedItem);
    }];
}
- (IBAction)gbsNextPrevPushed:(id)sender
{
    if (self.gbsNextPrevButton.selectedSegment == 0) {
        // Previous
        if (self.gbsTracks.indexOfSelectedItem == 0) {
            [self.gbsTracks selectItemAtIndex:self.gbsTracks.numberOfItems - 1];
        }
        else {
            [self.gbsTracks selectItemAtIndex:self.gbsTracks.indexOfSelectedItem - 1];
        }
    }
    else {
        // Next
        if (self.gbsTracks.indexOfSelectedItem == self.gbsTracks.numberOfItems - 1) {
            [self.gbsTracks selectItemAtIndex: 0];
        }
        else {
            [self.gbsTracks selectItemAtIndex:self.gbsTracks.indexOfSelectedItem + 1];
        }
    }
    [self changeGBSTrack:sender];
}

- (void)prepareGBSInterface: (GB_gbs_info_t *)info
{
    GB_set_rendering_disabled(&_gb, true);
    _view = nil;
    for (NSView *view in [_mainWindow.contentView.subviews copy]) {
        [view removeFromSuperview];
    }
    if (@available(macOS 11, *)) {
        [[NSBundle mainBundle] loadNibNamed:@"GBS11" owner:self topLevelObjects:nil];
    }
    else {
        [[NSBundle mainBundle] loadNibNamed:@"GBS" owner:self topLevelObjects:nil];
    }
    [_mainWindow setContentSize:self.gbsPlayerView.bounds.size];
    _mainWindow.styleMask &= ~NSWindowStyleMaskResizable;
    dispatch_async(dispatch_get_main_queue(), ^{ // Cocoa is weird, no clue why it's needed
        [_mainWindow standardWindowButton:NSWindowZoomButton].enabled = false;
    });
    [_mainWindow.contentView addSubview:self.gbsPlayerView];
    _mainWindow.movableByWindowBackground = true;
    [_mainWindow setContentBorderThickness:24 forEdge:NSRectEdgeMinY];

    self.gbsTitle.stringValue = [NSString stringWithCString:info->title encoding:NSISOLatin1StringEncoding] ?: @"GBS Player";
    self.gbsAuthor.stringValue = [NSString stringWithCString:info->author encoding:NSISOLatin1StringEncoding] ?: @"Unknown Composer";
    NSString *copyright = [NSString stringWithCString:info->copyright encoding:NSISOLatin1StringEncoding];
    if (copyright) {
        copyright = [@"©" stringByAppendingString:copyright];
    }
    self.gbsCopyright.stringValue = copyright ?: @"Missing copyright information";
    for (unsigned i = 0; i < info->track_count; i++) {
        [self.gbsTracks addItemWithTitle:[NSString stringWithFormat:@"Track %u", i + 1]];
    }
    [self.gbsTracks selectItemAtIndex:info->first_track];
    self.gbsPlayPauseButton.image.template = true;
    self.gbsPlayPauseButton.alternateImage.template = true;
    self.gbsRewindButton.image.template = true;
    for (unsigned i = 0; i < 2; i++) {
        [self.gbsNextPrevButton imageForSegment:i].template = true;
    }

    if (!_audioClient.isPlaying) {
        [_audioClient start];
    }
    
    if (@available(macOS 10.10, *)) {
        _mainWindow.titlebarAppearsTransparent = true;
    }
}

- (bool)isCartContainer
{
    return [self.fileName.pathExtension.lowercaseString isEqualToString:@"gbcart"];
}

- (NSString *)savPath
{
    if (self.isCartContainer) {
        return [self.fileName stringByAppendingPathComponent:@"battery.sav"];
    }
    
    return [[self.fileURL URLByDeletingPathExtension] URLByAppendingPathExtension:@"sav"].path;
}

- (NSString *)chtPath
{
    if (self.isCartContainer) {
        return [self.fileName stringByAppendingPathComponent:@"cheats.cht"];
    }
    
    return [[self.fileURL URLByDeletingPathExtension] URLByAppendingPathExtension:@"cht"].path;
}

- (NSString *)saveStatePath:(unsigned)index
{
    if (self.isCartContainer) {
        return [self.fileName stringByAppendingPathComponent:[NSString stringWithFormat:@"state.s%u", index]];
    }
    return [[self.fileURL URLByDeletingPathExtension] URLByAppendingPathExtension:[NSString stringWithFormat:@"s%u", index]].path;
}

- (NSString *)romPath
{
    NSString *fileName = self.fileName;
    if (self.isCartContainer) {
        NSArray *paths = [[NSString stringWithContentsOfFile:[fileName stringByAppendingPathComponent:@"rom.gbl"]
                                                    encoding:NSUTF8StringEncoding
                                                       error:nil] componentsSeparatedByString:@"\n"];
        fileName = nil;
        bool needsRebuild = false;
        for (NSString *path in paths) {
            NSURL *url = [NSURL URLWithString:path relativeToURL:self.fileURL];
            if ([[NSFileManager defaultManager] fileExistsAtPath:url.path]) {
                if (fileName && ![fileName isEqualToString:url.path]) {
                    needsRebuild = true;
                    break;
                }
                fileName = url.path;
            }
            else {
                needsRebuild = true;
            }
        }
        if (fileName && needsRebuild) {
            [[NSString stringWithFormat:@"%@\n%@\n%@",
              [fileName pathRelativeToDirectory:self.fileName],
              fileName,
              [[NSURL fileURLWithPath:fileName].fileReferenceURL.absoluteString substringFromIndex:strlen("file://")]]
             writeToFile:[self.fileName stringByAppendingPathComponent:@"rom.gbl"]
             atomically:false
             encoding:NSUTF8StringEncoding
             error:nil];
        }
    }
    
    return fileName;
}

static bool is_path_writeable(const char *path)
{
    if (!access(path, W_OK)) return true;
    int fd = creat(path, 0644);
    if (fd == -1) return false;
    close(fd);
    unlink(path);
    return true;
}

- (int)loadROM
{
    __block int ret = 0;
    NSString *fileName = self.romPath;
    if (!fileName) {
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:@"Could not locate the ROM referenced by this Game Boy Cartridge"];
        [alert setAlertStyle:NSAlertStyleCritical];
        [alert runModal];
        return 1;
    }
    
    NSString *rom_warnings = [self captureOutputForBlock:^{
        GB_debugger_clear_symbols(&_gb);
        if ([[[fileName pathExtension] lowercaseString] isEqualToString:@"isx"]) {
            ret = GB_load_isx(&_gb, fileName.UTF8String);
            if (!self.isCartContainer) {
                GB_load_battery(&_gb, [[self.fileURL URLByDeletingPathExtension] URLByAppendingPathExtension:@"ram"].path.UTF8String);
            }
        }
        else if ([[[fileName pathExtension] lowercaseString] isEqualToString:@"gbs"]) {
            __block GB_gbs_info_t info;
            ret = GB_load_gbs(&_gb, fileName.UTF8String, &info);
            [self prepareGBSInterface:&info];
        }
        else {
            ret = GB_load_rom(&_gb, [fileName UTF8String]);
        }
        if (GB_save_battery_size(&_gb)) {
            if (!is_path_writeable(self.savPath.UTF8String)) {
                GB_log(&_gb, "The save path for this ROM is not writeable, progress will not be saved.\n");
            }
        }
        GB_load_battery(&_gb, self.savPath.UTF8String);
        GB_load_cheats(&_gb, self.chtPath.UTF8String, true);
        dispatch_async(dispatch_get_main_queue(), ^{
            [self.cheatWindowController cheatsUpdated];
        });
        GB_debugger_load_symbol_file(&_gb, [[[NSBundle mainBundle] pathForResource:@"registers" ofType:@"sym"] UTF8String]);
        GB_debugger_load_symbol_file(&_gb, [[fileName stringByDeletingPathExtension] stringByAppendingPathExtension:@"sym"].UTF8String);
    }];
    if (ret) {
        NSAlert *alert = [[NSAlert alloc] init];
        [alert setMessageText:rom_warnings?: @"Could not load ROM"];
        [alert setAlertStyle:NSAlertStyleCritical];
        [alert runModal];
    }
    else if (rom_warnings && !_romWarningIssued) {
        _romWarningIssued = true;
        [GBWarningPopover popoverWithContents:rom_warnings onWindow:self.mainWindow];
    }
    _fileModificationTime = [[NSFileManager defaultManager] attributesOfItemAtPath:fileName error:nil][NSFileModificationDate];
    if (_usesAutoModel) {
        _currentModel = [self bestModelForROM];
    }
    return ret;
}

- (void)showWindows
{
    if (GB_is_inited(&_gb)) {
        if (![_fileModificationTime isEqualToDate:[[NSFileManager defaultManager] attributesOfItemAtPath:self.fileName error:nil][NSFileModificationDate]]) {
            [self reset:nil];
        }
    }
    [super showWindows];
}

- (void)close
{
    [self disconnectLinkCable];
    if (!self.gbsPlayerView) {
        [[NSUserDefaults standardUserDefaults] setInteger:self.mainWindow.frame.size.width forKey:@"LastWindowWidth"];
        [[NSUserDefaults standardUserDefaults] setInteger:self.mainWindow.frame.size.height forKey:@"LastWindowHeight"];
    }
    [self stop];
    [_consoleOutputLock lock];
    [_consoleOutputTimer invalidate];
    [_consoleOutputLock unlock];
    [self.consoleWindow close];
    [self.memoryWindow close];
    [self.vramWindow close];
    [self.printerFeedWindow close];
    [self.cheatsWindow close];
    [super close];
}

- (IBAction) interrupt:(id)sender
{
    [self log:"^C\n"];
    GB_debugger_break(&_gb);
    [self start];
    [self.consoleWindow makeKeyAndOrderFront:nil];
    [self.consoleInput becomeFirstResponder];
}

- (IBAction)mute:(id)sender
{
    if (_audioClient.isPlaying) {
        [_audioClient stop];
    }
    else {
        [_audioClient start];
        if (_volume == 0) {
            [GBWarningPopover popoverWithContents:@"Warning: Volume is set to to zero in the preferences panel" onWindow:self.mainWindow];
        }
    }
    [[NSUserDefaults standardUserDefaults] setBool:!_audioClient.isPlaying forKey:@"Mute"];
}

- (bool) isPaused
{
    if (self.partner) {
        return !self.partner->_running || GB_debugger_is_stopped(&_gb) || GB_debugger_is_stopped(&self.partner->_gb);
    }
    return (!_running) || GB_debugger_is_stopped(&_gb);
}

- (BOOL)validateUserInterfaceItem:(id<NSValidatedUserInterfaceItem>)anItem
{
    if ([anItem action] == @selector(mute:)) {
        if (_running) {
            [(NSMenuItem *)anItem setState:!_audioClient.isPlaying];
        }
        else {
            [(NSMenuItem *)anItem setState:[[NSUserDefaults standardUserDefaults] boolForKey:@"Mute"]];
        }
    }
    else if ([anItem action] == @selector(togglePause:)) {
        [(NSMenuItem *)anItem setState:self.isPaused];
        return !GB_debugger_is_stopped(&_gb);
    }
    else if ([anItem action] == @selector(reset:) && anItem.tag != MODEL_NONE && anItem.tag != MODEL_QUICK_RESET) {
        [(NSMenuItem *)anItem setState:(anItem.tag == _currentModel) || (anItem.tag == MODEL_AUTO && _usesAutoModel)];
    }
    else if ([anItem action] == @selector(interrupt:)) {
        if (![[NSUserDefaults standardUserDefaults] boolForKey:@"DeveloperMode"]) {
            return false;
        }
    }
    else if ([anItem action] == @selector(disconnectAllAccessories:)) {
        [(NSMenuItem *)anItem setState:GB_get_built_in_accessory(&_gb) == GB_ACCESSORY_NONE && !self.partner];
    }
    else if ([anItem action] == @selector(connectPrinter:)) {
        [(NSMenuItem *)anItem setState:GB_get_built_in_accessory(&_gb) == GB_ACCESSORY_PRINTER];
    }
    else if ([anItem action] == @selector(connectWorkboy:)) {
        [(NSMenuItem *)anItem setState:GB_get_built_in_accessory(&_gb) == GB_ACCESSORY_WORKBOY];
    }
    else if ([anItem action] == @selector(connectLinkCable:)) {
        [(NSMenuItem *)anItem setState:[(NSMenuItem *)anItem representedObject] == _master ||
                                       [(NSMenuItem *)anItem representedObject] == _slave];
    }
    else if ([anItem action] == @selector(toggleCheats:)) {
        [(NSMenuItem *)anItem setState:GB_cheats_enabled(&_gb)];
    }
    else if ([anItem action] == @selector(toggleDisplayBackground:)) {
        [(NSMenuItem *)anItem setState:!GB_is_background_rendering_disabled(&_gb)];
    }
    else if ([anItem action] == @selector(toggleDisplayObjects:)) {
        [(NSMenuItem *)anItem setState:!GB_is_object_rendering_disabled(&_gb)];
    }
    else if ([anItem action] == @selector(toggleAudioRecording:)) {
        [(NSMenuItem *)anItem setTitle:_isRecordingAudio? @"Stop Audio Recording" : @"Start Audio Recording…"];
    }
    else if ([anItem action] == @selector(toggleAudioChannel:)) {
        [(NSMenuItem *)anItem setState:!GB_is_channel_muted(&_gb, [anItem tag])];
    }
    else if ([anItem action] == @selector(increaseWindowSize:)) {
        return [self newRect:NULL forWindow:_mainWindow action:GBWindowResizeActionIncrease];
    }
    else if ([anItem action] == @selector(decreaseWindowSize:)) {
        return [self newRect:NULL forWindow:_mainWindow action:GBWindowResizeActionDecrease];
    }
    
    return [super validateUserInterfaceItem:anItem];
}


- (void) windowWillEnterFullScreen:(NSNotification *)notification
{
    _fullScreen = true;
    self.view.mouseHidingEnabled = _running;
}

- (void) windowWillExitFullScreen:(NSNotification *)notification
{
    _fullScreen = false;
    self.view.mouseHidingEnabled = false;
}

enum GBWindowResizeAction
{
    GBWindowResizeActionZoom,
    GBWindowResizeActionIncrease,
    GBWindowResizeActionDecrease,
};

- (bool)newRect:(NSRect *)rect forWindow:(NSWindow *)window action:(enum GBWindowResizeAction)action
{
    if (_fullScreen) return false;
    if (!rect) {
        rect = alloca(sizeof(*rect));
    }
    
    size_t width  = GB_get_screen_width(&_gb),
    height = GB_get_screen_height(&_gb);
    
    *rect = window.contentView.frame;
    
    unsigned titlebarSize = window.contentView.superview.frame.size.height - rect->size.height;
    
    unsigned stepX = width / [[window screen] backingScaleFactor];
    unsigned stepY = height / [[window screen] backingScaleFactor];
    
    if (action == GBWindowResizeActionDecrease) {
        if (rect->size.width <= width || rect->size.height <= height) {
            return false;
        }
    }
    
    typeof(floor) *roundFunc = action == GBWindowResizeActionDecrease? ceil : floor;
    unsigned currentFactor = MIN(roundFunc(rect->size.width / stepX), roundFunc(rect->size.height / stepY));
    
    rect->size.width = currentFactor * stepX;
    rect->size.height = currentFactor * stepY + titlebarSize;
    
    if (action == GBWindowResizeActionDecrease) {
        rect->size.width -= stepX;
        rect->size.height -= stepY;
    }
    else {
        rect->size.width += stepX;
        rect->size.height += stepY;
    }
    
    NSRect maxRect = [_mainWindow screen].visibleFrame;
    
    if (rect->size.width > maxRect.size.width ||
        rect->size.height > maxRect.size.height) {
        if (action == GBWindowResizeActionIncrease) {
            return false;
        }
        rect->size.width = width;
        rect->size.height = height + titlebarSize;
    }
    
    rect->origin = window.frame.origin;
    if (action == GBWindowResizeActionZoom) {
        rect->origin.y -= rect->size.height - window.frame.size.height;
    }
    else {
        rect->origin.y -= (rect->size.height - window.frame.size.height) / 2;
        rect->origin.x -= (rect->size.width - window.frame.size.width) / 2;
    }
    
    if (rect->origin.x < maxRect.origin.x) {
        rect->origin.x = maxRect.origin.x;
    }
    
    if (rect->origin.y < maxRect.origin.y) {
        rect->origin.y = maxRect.origin.y;
    }
    
    if (rect->origin.x + rect->size.width > maxRect.origin.x + maxRect.size.width) {
        rect->origin.x = maxRect.origin.x + maxRect.size.width - rect->size.width;
    }
    
    if (rect->origin.y + rect->size.height > maxRect.origin.y + maxRect.size.height) {
        rect->origin.y = maxRect.origin.y + maxRect.size.height - rect->size.height;
    }
    
    return true;
}

- (NSRect)windowWillUseStandardFrame:(NSWindow *)window defaultFrame:(NSRect)newFrame
{
    if (_fullScreen) {
        return newFrame;
    }
    [self newRect:&newFrame forWindow:window action:GBWindowResizeActionZoom];
    return newFrame;
}


- (IBAction)increaseWindowSize:(id)sender
{
    NSRect rect;
    if ([self newRect:&rect forWindow:_mainWindow action:GBWindowResizeActionIncrease]) {
        [_mainWindow setFrame:rect display:true animate:true];
    }
}

- (IBAction)decreaseWindowSize:(id)sender
{
    NSRect rect;
    if ([self newRect:&rect forWindow:_mainWindow action:GBWindowResizeActionDecrease]) {
        [_mainWindow setFrame:rect display:true animate:true];
    }
}

- (void) appendPendingOutput
{
    [_consoleOutputLock lock];
    if (_shouldClearSideView) {
        _shouldClearSideView = false;
        [self.debuggerSideView setString:@""];
    }
    if (_pendingConsoleOutput) {
        NSTextView *textView = _logToSideView? self.debuggerSideView : self.consoleOutput;
        
        [_hexController reloadData];
        [self reloadVRAMData: nil];
        
        [textView.textStorage appendAttributedString:_pendingConsoleOutput];
        [textView scrollToEndOfDocument:nil];
        if ([[NSUserDefaults standardUserDefaults] boolForKey:@"DeveloperMode"]) {
            [self.consoleWindow orderFront:nil];
        }
        _pendingConsoleOutput = nil;
    }
    [_consoleOutputLock unlock];
}

- (void)log:(const char *)string withAttributes:(GB_log_attributes_t)attributes
{
    NSString *nsstring = @(string); // For ref-counting
    if (_capturedOutput) {
        [_capturedOutput appendString:nsstring];
        return;
    }
    
    
    NSFont *font = [self debuggerFontOfSize:0];
    NSUnderlineStyle underline = NSUnderlineStyleNone;
    if (attributes & GB_LOG_BOLD) {
        font = [[NSFontManager sharedFontManager] convertFont:font toHaveTrait:NSBoldFontMask];
    }
    
    if (attributes &  GB_LOG_UNDERLINE_MASK) {
        underline = (attributes &  GB_LOG_UNDERLINE_MASK) == GB_LOG_DASHED_UNDERLINE? NSUnderlinePatternDot | NSUnderlineStyleSingle : NSUnderlineStyleSingle;
    }
    
    NSMutableParagraphStyle *paragraph_style = [[NSMutableParagraphStyle alloc] init];
    [paragraph_style setLineSpacing:2];
    NSMutableAttributedString *attributed =
    [[NSMutableAttributedString alloc] initWithString:nsstring
                                           attributes:@{NSFontAttributeName: font,
                                                        NSForegroundColorAttributeName: [NSColor whiteColor],
                                                        NSUnderlineStyleAttributeName: @(underline),
                                                        NSParagraphStyleAttributeName: paragraph_style}];
    
    [_consoleOutputLock lock];
    if (!_pendingConsoleOutput) {
        _pendingConsoleOutput = attributed;
    }
    else {
        [_pendingConsoleOutput appendAttributedString:attributed];
    }
    
    if (![_consoleOutputTimer isValid]) {
        _consoleOutputTimer = [NSTimer timerWithTimeInterval:(NSTimeInterval)0.05 target:self selector:@selector(appendPendingOutput) userInfo:nil repeats:false];
        [[NSRunLoop mainRunLoop] addTimer:_consoleOutputTimer forMode:NSDefaultRunLoopMode];
    }
    
    [_consoleOutputLock unlock];

    /* Make sure mouse is not hidden while debugging */
    self.view.mouseHidingEnabled = false;
}

- (IBAction)showConsoleWindow:(id)sender
{
    [self.consoleWindow orderBack:nil];
}

- (void)queueDebuggerCommand:(NSString *)command
{
    if (!_master && !_running && !GB_debugger_is_stopped(&_gb)) {
        _debuggerCommandWhilePaused = command;
        GB_debugger_break(&_gb);
        [self start];
        return;
    }
        
    if (!_inSyncInput) {
        [self log:">"];
    }
    [self log:[command UTF8String]];
    [self log:"\n"];
    [_hasDebuggerInput lock];
    [_debuggerInputQueue addObject:command];
    [_hasDebuggerInput unlockWithCondition:1];
}

- (IBAction)consoleInput:(NSTextField *)sender 
{
    NSString *line = [sender stringValue];
    if ([line isEqualToString:@""] && _lastConsoleInput) {
        line = _lastConsoleInput;
    }
    else if (line) {
        _lastConsoleInput = line;
    }
    else {
        line = @"";
    }
    
    [self queueDebuggerCommand: line];

    [sender setStringValue:@""];
}

- (void) interruptDebugInputRead
{
    [_hasDebuggerInput lock];
    [_debuggerInputQueue addObject:[NSNull null]];
    [_hasDebuggerInput unlockWithCondition:1];
}

- (void) updateSideView
{
    if (!GB_debugger_is_stopped(&_gb)) {
        return;
    }
    
    if (![NSThread isMainThread]) {
        dispatch_sync(dispatch_get_main_queue(), ^{
            [self updateSideView];
        });
        return;
    }
    
    [_consoleOutputLock lock];
    _shouldClearSideView = true;
    [self appendPendingOutput];
    _logToSideView = true;
    [_consoleOutputLock unlock];
    
    for (NSString *line in [self.debuggerSideViewInput.string componentsSeparatedByString:@"\n"]) {
        NSString *stripped = [line stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        if ([stripped length]) {
            char *dupped = strdup([stripped UTF8String]);
            GB_attributed_log(&_gb, GB_LOG_BOLD, "%s:\n", dupped);
            GB_debugger_execute_command(&_gb, dupped);
            GB_log(&_gb, "\n");
            free(dupped);
        }
    }
    
    [_consoleOutputLock lock];
    [self appendPendingOutput];
    _logToSideView = false;
    [_consoleOutputLock unlock];
}

- (char *)getDebuggerInput
{
    bool isPlaying = _audioClient.isPlaying;
    if (isPlaying) {
        [_audioClient stop];
    }
    [_audioLock lock];
    [_audioLock signal];
    [_audioLock unlock];
    _inSyncInput = true;
    [self updateSideView];
    dispatch_async(dispatch_get_main_queue(), ^{
        [self updateDebuggerButtons];
    });
    [self.partner updateDebuggerButtons];
    [self log:">"];
    if (_debuggerCommandWhilePaused) {
        NSString *command = _debuggerCommandWhilePaused;
        _debuggerCommandWhilePaused = nil;
        dispatch_async(dispatch_get_main_queue(), ^{
            [self queueDebuggerCommand:command];
        });
    }
    [_hasDebuggerInput lockWhenCondition:1];
    NSString *input = [_debuggerInputQueue firstObject];
    [_debuggerInputQueue removeObjectAtIndex:0];
    [_hasDebuggerInput unlockWithCondition:[_debuggerInputQueue count] != 0];
    _inSyncInput = false;
    _shouldClearSideView = true;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(NSEC_PER_SEC / 10)), dispatch_get_main_queue(), ^{
        if (_shouldClearSideView) {
            _shouldClearSideView = false;
            [self.debuggerSideView setString:@""];
        }
        [self updateDebuggerButtons];
        [self.partner updateDebuggerButtons];
    });
    if (isPlaying) {
        [_audioClient start];
    }
    if ((id) input == [NSNull null]) {
        return NULL;
    }
    return strdup([input UTF8String]);
}

- (char *) getAsyncDebuggerInput
{
    [_hasDebuggerInput lock];
    NSString *input = [_debuggerInputQueue firstObject];
    if (input) {
        [_debuggerInputQueue removeObjectAtIndex:0];
    }
    [_hasDebuggerInput unlockWithCondition:[_debuggerInputQueue count] != 0];
    if ((id)input == [NSNull null]) {
        return NULL;
    }
    return input? strdup([input UTF8String]): NULL;
}

- (IBAction)saveState:(id)sender
{
    bool __block success = false;
    [self performAtomicBlock:^{
        success = GB_save_state(&_gb, [self saveStatePath:[sender tag]].UTF8String) == 0;
    }];
    
    if (!success) {
        [GBWarningPopover popoverWithContents:@"Failed to write save state." onWindow:self.mainWindow];
        NSBeep();
    }
    else {
        [self.osdView displayText:@"State saved"];
    }
}

- (int)loadStateFile:(const char *)path noErrorOnNotFound:(bool)noErrorOnFileNotFound;
{
    int __block result = false;
    NSString *error =
    [self captureOutputForBlock:^{
        result = GB_load_state(&_gb, path);
    }];
    
    if (result == ENOENT && noErrorOnFileNotFound) {
        return ENOENT;
    }
    
    if (result) {
        NSBeep();
    }
    else {
        [self.osdView displayText:@"State loaded"];
    }
    if (error) {
        [GBWarningPopover popoverWithContents:error onWindow:self.mainWindow];
    }
    return result;
}

- (IBAction)loadState:(id)sender
{
    int ret = [self loadStateFile:[self saveStatePath:[sender tag]].UTF8String noErrorOnNotFound:true];
    if (ret == ENOENT && !self.isCartContainer) {
        [self loadStateFile:[[self.fileURL URLByDeletingPathExtension] URLByAppendingPathExtension:[NSString stringWithFormat:@"sn%ld", (long)[sender tag]]].path.UTF8String noErrorOnNotFound:false];
    }
}

- (IBAction)clearConsole:(id)sender
{
    [self.consoleOutput setString:@""];
}

- (void)log:(const char *)log
{
    [self log:log withAttributes:0];
}

- (uint8_t) readMemory:(uint16_t)addr
{
    while (!GB_is_inited(&_gb));
    return GB_safe_read_memory(&_gb, addr);
}

- (void) writeMemory:(uint16_t)addr value:(uint8_t)value
{
    while (!GB_is_inited(&_gb));
    GB_write_memory(&_gb, addr, value);
}

- (void)performAtomicBlock: (void (^)())block
{
    while (!GB_is_inited(&_gb));
    bool isRunning = _running && !GB_debugger_is_stopped(&_gb);
    if (_master) {
        isRunning |= _master->_running;
    }
    if (!isRunning) {
        block();
        return;
    }
    
    if (_master) {
        [_master performAtomicBlock:block];
        return;
    }
    
    if ([NSThread currentThread] == _emulationThread) {
        block();
        return;
    }
    
    _pendingAtomicBlock = block;
    while (_pendingAtomicBlock);
}

- (NSString *)captureOutputForBlock: (void (^)())block
{
    _capturedOutput = [[NSMutableString alloc] init];
    [self performAtomicBlock:block];
    NSString *ret = [_capturedOutput stringByTrimmingCharactersInSet:[NSCharacterSet newlineCharacterSet]];
    _capturedOutput = nil;
    return [ret length]? ret : nil;
}

+ (NSImage *) imageFromData:(NSData *)data width:(NSUInteger) width height:(NSUInteger) height scale:(double) scale
{
    
    NSImage *ret = [[NSImage alloc] initWithSize:NSMakeSize(width * scale, height * scale)];
    NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL
                                                                    pixelsWide:width
                                                                    pixelsHigh:height
                                                                 bitsPerSample:8
                                                               samplesPerPixel:3
                                                                      hasAlpha:false
                                                                      isPlanar:false
                                                                colorSpaceName:NSDeviceRGBColorSpace
                                                                  bitmapFormat:0
                                                                   bytesPerRow:4 * width
                                                                  bitsPerPixel:32];
    memcpy(rep.bitmapData, data.bytes, data.length);
    [ret addRepresentation:rep];
    return ret;
}

- (void) reloadMemoryView
{
    if (self.memoryWindow.isVisible) {
        [_hexController reloadData];
    }
    if (_cheatSearchController.window.isVisible) {
        if ([_cheatSearchController.tableView editedColumn] != 2) {
            [_cheatSearchController.tableView reloadData];
        }
    }
}

- (IBAction) reloadVRAMData: (id) sender
{
    if (self.vramWindow.isVisible) {
        uint8_t *io_regs = GB_get_direct_access(&_gb, GB_DIRECT_ACCESS_IO, NULL, NULL);
        switch ([self.vramTabView.tabViewItems indexOfObject:self.vramTabView.selectedTabViewItem]) {
            case 0:
            /* Tileset */
            {
                GB_palette_type_t palette_type = GB_PALETTE_NONE;
                NSUInteger palette_menu_index = self.tilesetPaletteButton.indexOfSelectedItem;
                if (palette_menu_index) {
                    palette_type = palette_menu_index > 8? GB_PALETTE_OAM : GB_PALETTE_BACKGROUND;
                }
                size_t bufferLength = 256 * 192 * 4;
                NSMutableData *data = [NSMutableData dataWithCapacity:bufferLength];
                data.length = bufferLength;
                GB_draw_tileset(&_gb, (uint32_t *)data.mutableBytes, palette_type, (palette_menu_index - 1) & 7);
                
                self.tilesetImageView.image = [Document imageFromData:data width:256 height:192 scale:1.0];
                self.tilesetImageView.layer.magnificationFilter = kCAFilterNearest;
            }
            break;
                
            case 1:
            /* Tilemap */
            {
                GB_palette_type_t palette_type = GB_PALETTE_NONE;
                NSUInteger palette_menu_index = self.tilemapPaletteButton.indexOfSelectedItem;
                if (palette_menu_index > 1) {
                    palette_type = palette_menu_index > 9? GB_PALETTE_OAM : GB_PALETTE_BACKGROUND;
                }
                else if (palette_menu_index == 1) {
                    palette_type = GB_PALETTE_AUTO;
                }
                
                size_t bufferLength = 256 * 256 * 4;
                NSMutableData *data = [NSMutableData dataWithCapacity:bufferLength];
                data.length = bufferLength;
                GB_draw_tilemap(&_gb, (uint32_t *)data.mutableBytes, palette_type, (palette_menu_index - 2) & 7,
                                (GB_map_type_t) self.tilemapMapButton.indexOfSelectedItem,
                                (GB_tileset_type_t) self.TilemapSetButton.indexOfSelectedItem);
                
                self.tilemapImageView.scrollRect = NSMakeRect(io_regs[GB_IO_SCX],
                                                              io_regs[GB_IO_SCY],
                                                              160, 144);
                self.tilemapImageView.image = [Document imageFromData:data width:256 height:256 scale:1.0];
                self.tilemapImageView.layer.magnificationFilter = kCAFilterNearest;
            }
            break;
                
            case 2:
            /* OAM */
            {
                _oamCount = GB_get_oam_info(&_gb, _oamInfo, &_oamHeight);
                dispatch_async(dispatch_get_main_queue(), ^{
                    [self.objectView reloadData:self];
                });
            }
            break;
            
            case 3:
            /* Palettes */
            {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [self.paletteView reloadData:self];
                });
            }
            break;
        }
    }
}

- (IBAction) showMemory:(id)sender
{
    if (!_hexController) {
        [self initMemoryView];
    }
    [self.memoryWindow makeKeyAndOrderFront:sender];
}

- (IBAction)hexGoTo:(id)sender
{
    NSString *error = [self captureOutputForBlock:^{
        uint16_t addr;
        uint16_t bank;
        if (GB_debugger_evaluate(&_gb, [[sender stringValue] UTF8String], &addr, &bank)) {
            return;
        }
        
        if (bank != (typeof(bank))-1) {
            GB_memory_mode_t mode = [(GBMemoryByteArray *)(_hexController.byteArray) mode];
            if (addr < 0x4000) {
                if (bank == 0) {
                    if (mode != GBMemoryROM && mode != GBMemoryEntireSpace) {
                        mode = GBMemoryEntireSpace;
                    }
                }
                else {
                    addr |= 0x4000;
                    mode = GBMemoryROM;
                }
            }
            else if (addr < 0x8000) {
                mode = GBMemoryROM;
            }
            else if (addr < 0xA000) {
                mode = GBMemoryVRAM;
            }
            else if (addr < 0xC000) {
                mode = GBMemoryExternalRAM;
            }
            else if (addr < 0xD000) {
                if (mode != GBMemoryRAM && mode != GBMemoryEntireSpace) {
                    mode = GBMemoryEntireSpace;
                }
            }
            else if (addr < 0xE000) {
                mode = GBMemoryRAM;
            }
            else {
                mode = GBMemoryEntireSpace;
            }
            [_memorySpaceButton selectItemAtIndex:mode];
            [self hexUpdateSpace:_memorySpaceButton.cell];
            [_memoryBankInput setStringValue:[NSString stringWithFormat:@"$%02x", bank]];
            [self hexUpdateBank:_memoryBankInput];
        }
        addr -= _lineRep.valueOffset;
        if (addr >= _hexController.byteArray.length) {
            GB_log(&_gb, "Value $%04x is out of range.\n", addr);
            return;
        }
        
        dispatch_async(dispatch_get_main_queue(), ^{
            [_hexController setSelectedContentsRanges:@[[HFRangeWrapper withRange:HFRangeMake(addr, 0)]]];
            [_hexController _ensureVisibilityOfLocation:addr];
            for (HFRepresenter *representer in _hexController.representers) {
                if ([representer isKindOfClass:[HFHexTextRepresenter class]]) {
                    [self.memoryWindow makeFirstResponder:representer.view];
                    break;
                }
            }
        });
    }];
    if (error) {
        NSBeep();
        [GBWarningPopover popoverWithContents:error onView:sender];
    }
}

- (void)hexUpdateBank:(NSControl *)sender ignoreErrors: (bool)ignore_errors
{
    NSString *error = [self captureOutputForBlock:^{
        uint16_t addr, bank;
        if (GB_debugger_evaluate(&_gb, [[sender stringValue] UTF8String], &addr, &bank)) {
            return;
        }

        if (bank == (uint16_t) -1) {
            bank = addr;
        }

        uint16_t n_banks = 1;
        switch ([(GBMemoryByteArray *)(_hexController.byteArray) mode]) {
            case GBMemoryROM: {
                size_t rom_size;
                GB_get_direct_access(&_gb, GB_DIRECT_ACCESS_ROM, &rom_size, NULL);
                n_banks = rom_size / 0x4000;
                break;
            }
            case GBMemoryVRAM:
                n_banks = GB_is_cgb(&_gb) ? 2 : 1;
                break;
            case GBMemoryExternalRAM: {
                size_t ram_size;
                GB_get_direct_access(&_gb, GB_DIRECT_ACCESS_CART_RAM, &ram_size, NULL);
                n_banks = (ram_size + 0x1FFF) / 0x2000;
                break;
            }
            case GBMemoryRAM:
                n_banks = GB_is_cgb(&_gb) ? 8 : 1;
                break;
            case GBMemoryEntireSpace:
                break;
        }

        bank %= n_banks;

        [sender setStringValue:[NSString stringWithFormat:@"$%x", bank]];
        [(GBMemoryByteArray *)(_hexController.byteArray) setSelectedBank:bank];
        _statusRep.bankForDescription = bank;
        dispatch_async(dispatch_get_main_queue(), ^{
            [_hexController reloadData];
        });
    }];
    
    if (error && !ignore_errors) {
        NSBeep();
        [GBWarningPopover popoverWithContents:error onView:sender];
    }
}

- (IBAction)hexUpdateBank:(NSControl *)sender
{
    [self hexUpdateBank:sender ignoreErrors:false];
}

- (IBAction)hexUpdateSpace:(NSPopUpButtonCell *)sender
{
    self.memoryBankItem.enabled = [sender indexOfSelectedItem] != GBMemoryEntireSpace;
    [_hexController setSelectedContentsRanges:@[[HFRangeWrapper withRange:HFRangeMake(0, 0)]]];
    GBMemoryByteArray *byteArray = (GBMemoryByteArray *)(_hexController.byteArray);
    [byteArray setMode:(GB_memory_mode_t)[sender indexOfSelectedItem]];
    uint16_t bank = -1;
    switch ((GB_memory_mode_t)[sender indexOfSelectedItem]) {
        case GBMemoryEntireSpace:
            _statusRep.baseAddress = _lineRep.valueOffset = 0;
            break;
        case GBMemoryROM:
            _statusRep.baseAddress = _lineRep.valueOffset = 0;
            GB_get_direct_access(&_gb, GB_DIRECT_ACCESS_ROM, NULL, &bank);
            break;
        case GBMemoryVRAM:
            _statusRep.baseAddress = _lineRep.valueOffset = 0x8000;
            GB_get_direct_access(&_gb, GB_DIRECT_ACCESS_VRAM, NULL, &bank);
            break;
        case GBMemoryExternalRAM:
            _statusRep.baseAddress = _lineRep.valueOffset = 0xA000;
            GB_get_direct_access(&_gb, GB_DIRECT_ACCESS_CART_RAM, NULL, &bank);
            break;
        case GBMemoryRAM:
            _statusRep.baseAddress = _lineRep.valueOffset = 0xC000;
            GB_get_direct_access(&_gb, GB_DIRECT_ACCESS_RAM, NULL, &bank);
            break;
    }
    byteArray.selectedBank = bank;
    _statusRep.bankForDescription = bank;
    if (bank != (uint16_t)-1) {
        [self.memoryBankInput setStringValue:[NSString stringWithFormat:@"$%x", byteArray.selectedBank]];
    }
    
    [_hexController reloadData];
    for (NSView *view in self.memoryView.subviews) {
        [view setNeedsDisplay:true];
    }
}

- (GB_gameboy_t *) gameboy
{
    return &_gb;
}

+ (BOOL)canConcurrentlyReadDocumentsOfType:(NSString *)typeName
{
    return true;
}

- (void)cameraRequestUpdate
{
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        @try {
            if (!_cameraSession) {
                if (@available(macOS 10.14, *)) {
                    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]) {
                        case AVAuthorizationStatusAuthorized:
                            break;
                        case AVAuthorizationStatusNotDetermined: {
                            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL granted) {
                                [self cameraRequestUpdate];
                            }];
                            return;
                        }
                        case AVAuthorizationStatusDenied:
                        case AVAuthorizationStatusRestricted:
                            GB_camera_updated(&_gb);
                            return;
                    }
                }

                NSError *error;
                AVCaptureDevice *device = [AVCaptureDevice defaultDeviceWithMediaType: AVMediaTypeVideo];
                AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice: device error: &error];
                CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions([[device activeFormat] formatDescription]);

                if (!input) {
                    GB_camera_updated(&_gb);
                    return;
                }
                
                double ratio = MAX(130.0 / dimensions.width, 114.0 / dimensions.height);

                _cameraOutput = [[AVCaptureStillImageOutput alloc] init];
                /* Greyscale is not widely supported, so we use YUV, whose first element is the brightness. */
                [_cameraOutput setOutputSettings: @{(id)kCVPixelBufferPixelFormatTypeKey: @(kYUVSPixelFormat),
                                                   (id)kCVPixelBufferWidthKey: @(round(dimensions.width * ratio)),
                                                   (id)kCVPixelBufferHeightKey: @(round(dimensions.height * ratio)),}];


                _cameraSession = [AVCaptureSession new];
                _cameraSession.sessionPreset = AVCaptureSessionPresetPhoto;

                [_cameraSession addInput: input];
                [_cameraSession addOutput: _cameraOutput];
                [_cameraSession startRunning];
                _cameraConnection = [_cameraOutput connectionWithMediaType: AVMediaTypeVideo];
            }

            [_cameraOutput captureStillImageAsynchronouslyFromConnection: _cameraConnection completionHandler: ^(CMSampleBufferRef sampleBuffer, NSError *error) {
                if (error) {
                    GB_camera_updated(&_gb);
                }
                else {
                    if (_cameraImage) {
                        CVBufferRelease(_cameraImage);
                        _cameraImage = NULL;
                    }
                    _cameraImage = CVBufferRetain(CMSampleBufferGetImageBuffer(sampleBuffer));
                    /* We only need the actual buffer, no need to ever unlock it. */
                    CVPixelBufferLockBaseAddress(_cameraImage, 0);
                }
                
                GB_camera_updated(&_gb);
            }];
        }
        @catch (NSException *exception) {
            /* I have not tested camera support on many devices, so we catch exceptions just in case. */
            GB_camera_updated(&_gb);
        }
    });
}

- (uint8_t)cameraGetPixelAtX:(unsigned)x andY:(unsigned)y
{
    if (!_cameraImage) {
        return 0;
    }

    uint8_t *baseAddress = (uint8_t *)CVPixelBufferGetBaseAddress(_cameraImage);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(_cameraImage);
    unsigned offsetX = (CVPixelBufferGetWidth(_cameraImage) - 128) / 2;
    unsigned offsetY = (CVPixelBufferGetHeight(_cameraImage) - 112) / 2;
    uint8_t ret = baseAddress[(x + offsetX) * 2 + (y + offsetY) * bytesPerRow];

    return ret;
}

- (IBAction)toggleTilesetGrid:(NSButton *)sender
{
    if (sender.state) {
        self.tilesetImageView.horizontalGrids = @[
                                                  [[GBImageViewGridConfiguration alloc] initWithColor:[NSColor colorWithCalibratedRed:0 green:0 blue:0 alpha:0.25] size:8],
                                                  [[GBImageViewGridConfiguration alloc] initWithColor:[NSColor colorWithCalibratedRed:0 green:0 blue:0 alpha:0.5] size:128],
                                                  
        ];
        self.tilesetImageView.verticalGrids = @[
                                                  [[GBImageViewGridConfiguration alloc] initWithColor:[NSColor colorWithCalibratedRed:0 green:0 blue:0 alpha:0.25] size:8],
                                                  [[GBImageViewGridConfiguration alloc] initWithColor:[NSColor colorWithCalibratedRed:0 green:0 blue:0 alpha:0.5] size:64],
        ];
        self.tilemapImageView.horizontalGrids = @[
                                                  [[GBImageViewGridConfiguration alloc] initWithColor:[NSColor colorWithCalibratedRed:0 green:0 blue:0 alpha:0.25] size:8],
                                                  ];
        self.tilemapImageView.verticalGrids = @[
                                                [[GBImageViewGridConfiguration alloc] initWithColor:[NSColor colorWithCalibratedRed:0 green:0 blue:0 alpha:0.25] size:8],
                                                ];
    }
    else {
        self.tilesetImageView.horizontalGrids = nil;
        self.tilesetImageView.verticalGrids = nil;
        self.tilemapImageView.horizontalGrids = nil;
        self.tilemapImageView.verticalGrids = nil;
    }
}

- (IBAction)toggleScrollingDisplay:(NSButton *)sender
{
    self.tilemapImageView.displayScrollRect = sender.state;
}

- (IBAction)vramTabChanged:(NSSegmentedControl *)sender
{
    [self.vramTabView selectTabViewItemAtIndex:[sender selectedSegment]];
    [self reloadVRAMData:sender];
    [self.vramTabView.selectedTabViewItem.view addSubview:self.gridButton];
    self.gridButton.hidden = [sender selectedSegment] >= 2;

    NSUInteger height_diff = self.vramWindow.frame.size.height - self.vramWindow.contentView.frame.size.height;
    CGRect window_rect = self.vramWindow.frame;
    window_rect.origin.y += window_rect.size.height;
    switch ([sender selectedSegment]) {
        case 0:
        case 2:
            window_rect.size.height = 384 + height_diff + 48;
            break;
        case 1:
            window_rect.size.height = 512 + height_diff + 48;
            break;
        case 3:
            window_rect.size.height = 24 * 16 + height_diff;
            break;
            
        default:
            break;
    }
    window_rect.origin.y -= window_rect.size.height;
    [self.vramWindow setFrame:window_rect display:true animate:true];
}

- (void)mouseDidLeaveImageView:(GBImageView *)view
{
    self.vramStatusLabel.stringValue = @"";
}

- (void)imageView:(GBImageView *)view mouseMovedToX:(NSUInteger)x Y:(NSUInteger)y
{
    if (view == self.tilesetImageView) {
        uint8_t bank = x >= 128? 1 : 0;
        x &= 127;
        uint16_t tile = x / 8 + y / 8 * 16;
        self.vramStatusLabel.stringValue = [NSString stringWithFormat:@"Tile number $%02x at %d:$%04x", tile & 0xFF, bank, 0x8000 + tile * 0x10];
    }
    else if (view == self.tilemapImageView) {
        uint16_t map_offset = x / 8 + y / 8 * 32;
        uint16_t map_base = 0x1800;
        GB_map_type_t map_type = (GB_map_type_t) self.tilemapMapButton.indexOfSelectedItem;
        GB_tileset_type_t tileset_type = (GB_tileset_type_t) self.TilemapSetButton.indexOfSelectedItem;
        uint8_t lcdc = ((uint8_t *)GB_get_direct_access(&_gb, GB_DIRECT_ACCESS_IO, NULL, NULL))[GB_IO_LCDC];
        uint8_t *vram = GB_get_direct_access(&_gb, GB_DIRECT_ACCESS_VRAM, NULL, NULL);
        
        if (map_type == GB_MAP_9C00 || (map_type == GB_MAP_AUTO && lcdc & GB_LCDC_BG_MAP)) {
            map_base = 0x1C00;
        }
        
        if (tileset_type == GB_TILESET_AUTO) {
            tileset_type = (lcdc & GB_LCDC_TILE_SEL)? GB_TILESET_8800 : GB_TILESET_8000;
        }
        
        uint8_t tile = vram[map_base + map_offset];
        uint16_t tile_address = 0;
        if (tileset_type == GB_TILESET_8000) {
            tile_address = 0x8000 + tile * 0x10;
        }
        else {
            tile_address = 0x9000 + (int8_t)tile * 0x10;
        }
        
        if (GB_is_cgb(&_gb)) {
            uint8_t attributes = vram[map_base + map_offset + 0x2000];
            self.vramStatusLabel.stringValue = [NSString stringWithFormat:@"Tile number $%02x (%d:$%04x) at map address $%04x (Attributes: %c%c%c%d%d)",
                                                tile,
                                                attributes & 0x8? 1 : 0,
                                                tile_address,
                                                0x8000 + map_base + map_offset,
                                                (attributes & 0x80) ? 'P' : '-',
                                                (attributes & 0x40) ? 'V' : '-',
                                                (attributes & 0x20) ? 'H' : '-',
                                                attributes & 0x8? 1 : 0,
                                                attributes & 0x7
                                                ];
        }
        else {
            self.vramStatusLabel.stringValue = [NSString stringWithFormat:@"Tile number $%02x ($%04x) at map address $%04x",
                                                tile,
                                                tile_address,
                                                0x8000 + map_base + map_offset
                                                ];
        }

    }
}

- (GB_oam_info_t *)oamInfo
{
    return _oamInfo;
}

- (IBAction)showVRAMViewer:(id)sender
{
    [self.vramWindow makeKeyAndOrderFront:sender];
    [self reloadVRAMData: nil];
}

- (void)printImage:(uint32_t *)imageBytes height:(unsigned) height
         topMargin:(unsigned) topMargin bottomMargin: (unsigned) bottomMargin
          exposure:(unsigned) exposure
{
    uint32_t paddedImage[160 * (topMargin + height + bottomMargin)];
    memset(paddedImage, 0xFF, sizeof(paddedImage));
    memcpy(paddedImage + (160 * topMargin), imageBytes, 160 * height * sizeof(imageBytes[0]));
    if (!self.printerFeedWindow.isVisible) {
        _currentPrinterImageData = [[NSMutableData alloc] init];
    }
    [_currentPrinterImageData appendBytes:paddedImage length:sizeof(paddedImage)];
    /* UI related code must run on main thread. */
    dispatch_async(dispatch_get_main_queue(), ^{
        [_printerSpinner startAnimation:nil];
        self.feedImageView.image = [Document imageFromData:_currentPrinterImageData
                                                     width:160
                                                    height:_currentPrinterImageData.length / 160 / sizeof(imageBytes[0])
                                                     scale:2.0];
        NSRect frame = self.printerFeedWindow.frame;
        double oldHeight = frame.size.height;
        frame.size = self.feedImageView.image.size;
        [self.printerFeedWindow setContentMaxSize:frame.size];
        frame.size.height += self.printerFeedWindow.frame.size.height - self.printerFeedWindow.contentView.frame.size.height;
        frame.origin.y -= frame.size.height - oldHeight;
        [self.printerFeedWindow setFrame:frame display:false animate: self.printerFeedWindow.isVisible];
        [self.printerFeedWindow orderFront:NULL];
    });
    
}

- (void)printDone
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [_printerSpinner stopAnimation:nil];
    });
}

- (void)printDocument:(id)sender
{
    if (self.feedImageView.image.size.height == 0) {
        NSBeep(); return;
    }
    NSImageView *view = [[NSImageView alloc] initWithFrame:(NSRect){{0,0}, self.feedImageView.image.size}];
    view.image = self.feedImageView.image;
    [[NSPrintOperation printOperationWithView:view] runOperationModalForWindow:self.printerFeedWindow delegate:nil didRunSelector:NULL contextInfo:NULL];
}

- (IBAction)savePrinterFeed:(id)sender
{
    bool shouldResume = _running;
    [self stop];
    NSSavePanel *savePanel = [NSSavePanel savePanel];
    [savePanel setAllowedFileTypes:@[@"png"]];
    [savePanel beginSheetModalForWindow:self.printerFeedWindow completionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK) {
            [savePanel orderOut:self];
            CGImageRef cgRef = [self.feedImageView.image CGImageForProposedRect:NULL
                                                                        context:nil
                                                                          hints:nil];
            NSBitmapImageRep *imageRep = [[NSBitmapImageRep alloc] initWithCGImage:cgRef];
            [imageRep setSize:(NSSize){160, self.feedImageView.image.size.height / 2}];
            NSData *data = [imageRep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            [data writeToURL:savePanel.URL atomically:false];
            [self.printerFeedWindow setIsVisible:false];
        }
        if (shouldResume) {
            [self start];
        }
    }];
}

- (IBAction)disconnectAllAccessories:(id)sender
{
    [self disconnectLinkCable];
    [self performAtomicBlock:^{
        GB_disconnect_serial(&_gb);
    }];
}

- (IBAction)connectPrinter:(id)sender
{
    [self disconnectLinkCable];
    [self performAtomicBlock:^{
        GB_connect_printer(&_gb, printImage, printDone);
    }];
}

- (IBAction)connectWorkboy:(id)sender
{
    [self disconnectLinkCable];
    [self performAtomicBlock:^{
        GB_connect_workboy(&_gb, setWorkboyTime, getWorkboyTime);
    }];
}

- (void)setFileURL:(NSURL *)fileURL
{
    [super setFileURL:fileURL];
    self.consoleWindow.title = [NSString stringWithFormat:@"Debug Console – %@", [[fileURL path] lastPathComponent]];
    self.memoryWindow.title = [NSString stringWithFormat:@"Memory – %@", [[fileURL path] lastPathComponent]];
    self.vramWindow.title = [NSString stringWithFormat:@"VRAM Viewer – %@", [[fileURL path] lastPathComponent]];
}

- (BOOL)splitView:(GBSplitView *)splitView canCollapseSubview:(NSView *)subview;
{
    if ([[splitView arrangedSubviews] lastObject] == subview) {
        return true;
    }
    return false;
}

- (CGFloat)splitView:(GBSplitView *)splitView constrainMinCoordinate:(CGFloat)proposedMinimumPosition ofSubviewAt:(NSInteger)dividerIndex
{
    return 600;
}

- (CGFloat)splitView:(GBSplitView *)splitView constrainMaxCoordinate:(CGFloat)proposedMaximumPosition ofSubviewAt:(NSInteger)dividerIndex 
{
    return splitView.frame.size.width - 321;
}

- (BOOL)splitView:(GBSplitView *)splitView shouldAdjustSizeOfSubview:(NSView *)view 
{
    if ([[splitView arrangedSubviews] lastObject] == view) {
        return false;
    }
    return true;
}

- (void)splitViewDidResizeSubviews:(NSNotification *)notification
{
    GBSplitView *splitview = notification.object;
    if ([[[splitview arrangedSubviews] firstObject] frame].size.width < 600) {
        [splitview setPosition:600 ofDividerAtIndex:0];
    }
}

- (IBAction)showCheats:(id)sender
{
    [self.cheatsWindow makeKeyAndOrderFront:nil];
}

- (IBAction)showCheatSearch:(id)sender
{
    if (!_cheatSearchController) {
        _cheatSearchController = [GBCheatSearchController controllerWithDocument:self];
    }
    [_cheatSearchController.window makeKeyAndOrderFront:sender];
}

- (IBAction)toggleCheats:(id)sender
{
    GB_set_cheats_enabled(&_gb, !GB_cheats_enabled(&_gb));
}

- (void)disconnectLinkCable
{
    bool wasRunning = self->_running;
    Document *partner = _master ?: _slave;
    if (partner) {
        wasRunning |= partner->_running;
        [self stop];
        partner->_master = nil;
        partner->_slave = nil;
        _master = nil;
        _slave = nil;
        if (wasRunning) {
            [partner start];
            [self start];
        }
        GB_set_turbo_mode(&_gb, false, false);
        GB_set_turbo_mode(&partner->_gb, false, false);
    }
}

- (void)connectLinkCable:(NSMenuItem *)sender
{
    [self disconnectAllAccessories:sender];
    Document *partner = [sender representedObject];
    [partner disconnectAllAccessories:sender];
    
    bool wasRunning = self->_running;
    [self stop];
    [partner stop];
    GB_set_turbo_mode(&partner->_gb, true, true);
    _slave = partner;
    partner->_master = self;
    _linkOffset = 0;
    GB_set_serial_transfer_bit_start_callback(&_gb, _linkCableBitStart);
    GB_set_serial_transfer_bit_start_callback(&partner->_gb, _linkCableBitStart);
    GB_set_serial_transfer_bit_end_callback(&_gb, _linkCableBitEnd);
    GB_set_serial_transfer_bit_end_callback(&partner->_gb, _linkCableBitEnd);
    if (wasRunning) {
        [self start];
    }
}

- (void)_linkCableBitStart:(bool)bit
{
    _linkCableBit = bit;
}

-(bool)_linkCableBitEnd
{
    bool ret = GB_serial_get_data_bit(&self.partner->_gb);
    GB_serial_set_data_bit(&self.partner->_gb, _linkCableBit);
    return ret;
}

- (void)infraredStateChanged:(bool)state
{
    if (self.partner) {
        GB_set_infrared_input(&self.partner->_gb, state);
    }
}

-(Document *)partner
{
    return _slave ?: _master;
}

- (bool)isSlave
{
    return _master;
}

- (GB_gameboy_t *)gb
{
    return &_gb;
}

- (NSImage *)takeScreenshot
{
    NSImage *ret = nil;
    if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBFilterScreenshots"]) {
        ret = [_view renderToImage];
    }
    if (!ret) {
        ret = [Document imageFromData:[NSData dataWithBytesNoCopy:_view.currentBuffer
                                                           length:GB_get_screen_width(&_gb) * GB_get_screen_height(&_gb) * 4
                                                     freeWhenDone:false]
                                width:GB_get_screen_width(&_gb)
                               height:GB_get_screen_height(&_gb)
                                scale:1.0];
    }
    return ret;
}

- (NSString *)screenshotFilename
{
    NSDate *date = [NSDate date];
    NSDateFormatter *dateFormatter = [[NSDateFormatter alloc] init];
    dateFormatter.dateStyle = NSDateFormatterLongStyle;
    dateFormatter.timeStyle = NSDateFormatterMediumStyle;
    return [[NSString stringWithFormat:@"%@ – %@.png",
             self.fileURL.lastPathComponent.stringByDeletingPathExtension,
             [dateFormatter stringFromDate:date]] stringByReplacingOccurrencesOfString:@":" withString:@"."]; // Gotta love Mac OS Classic

}

- (IBAction)saveScreenshot:(id)sender
{
    NSString *folder = [[NSUserDefaults standardUserDefaults] stringForKey:@"GBScreenshotFolder"];
    BOOL isDirectory = false;
    if (folder) {
        [[NSFileManager defaultManager] fileExistsAtPath:folder isDirectory:&isDirectory];
    }
    if (!folder) {
        bool shouldResume = _running;
        [self stop];
        NSOpenPanel *openPanel = [NSOpenPanel openPanel];
        openPanel.canChooseFiles = false;
        openPanel.canChooseDirectories = true;
        openPanel.message = @"Choose a folder for screenshots";
        [openPanel beginSheetModalForWindow:self.mainWindow completionHandler:^(NSInteger result) {
            if (result == NSModalResponseOK) {
                [[NSUserDefaults standardUserDefaults] setObject:openPanel.URL.path
                                                          forKey:@"GBScreenshotFolder"];
                [self saveScreenshot:sender];
            }
            if (shouldResume) {
                [self start];
            }
            
        }];
        return;
    }
    NSImage *image = [self takeScreenshot];
    NSDateFormatter *dateFormatter = [[NSDateFormatter alloc] init];
    dateFormatter.dateStyle = NSDateFormatterLongStyle;
    dateFormatter.timeStyle = NSDateFormatterMediumStyle;
    NSString *filename = [self screenshotFilename];
    filename = [folder stringByAppendingPathComponent:filename];
    unsigned i = 2;
    while ([[NSFileManager defaultManager] fileExistsAtPath:filename]) {
        filename = [[filename stringByDeletingPathExtension] stringByAppendingFormat:@" %d.png", i++];
    }
    
    NSBitmapImageRep *imageRep = (NSBitmapImageRep *)image.representations.firstObject;
    NSData *data = [imageRep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
    [data writeToFile:filename atomically:false];
    [self.osdView displayText:@"Screenshot saved"];
}

- (IBAction)saveScreenshotAs:(id)sender
{
    bool shouldResume = _running;
    [self stop];
    NSImage *image = [self takeScreenshot];
    NSSavePanel *savePanel = [NSSavePanel savePanel];
    [savePanel setNameFieldStringValue:[self screenshotFilename]];
    [savePanel beginSheetModalForWindow:self.mainWindow completionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK) {
            [savePanel orderOut:self];
            NSBitmapImageRep *imageRep = (NSBitmapImageRep *)image.representations.firstObject;
            NSData *data = [imageRep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
            [data writeToURL:savePanel.URL atomically:false];
            [[NSUserDefaults standardUserDefaults] setObject:savePanel.URL.path.stringByDeletingLastPathComponent
                                                      forKey:@"GBScreenshotFolder"];
        }
        if (shouldResume) {
            [self start];
        }
    }];
    [self.osdView displayText:@"Screenshot saved"];
}

- (IBAction)copyScreenshot:(id)sender
{
    NSImage *image = [self takeScreenshot];
    [[NSPasteboard generalPasteboard] clearContents];
    [[NSPasteboard generalPasteboard] writeObjects:@[image]];
    [self.osdView displayText:@"Screenshot copied"];
}

- (IBAction)toggleDisplayBackground:(id)sender
{
    GB_set_background_rendering_disabled(&_gb, !GB_is_background_rendering_disabled(&_gb));
}

- (IBAction)toggleDisplayObjects:(id)sender
{
    GB_set_object_rendering_disabled(&_gb, !GB_is_object_rendering_disabled(&_gb));
}

- (IBAction)newCartridgeInstance:(id)sender
{
    bool shouldResume = _running;
    [self stop];
    NSSavePanel *savePanel = [NSSavePanel savePanel];
    [savePanel setAllowedFileTypes:@[@"gbcart"]];
    [savePanel beginSheetModalForWindow:self.mainWindow completionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK) {
            [savePanel orderOut:self];
            NSString *romPath = self.romPath;
            [[NSFileManager defaultManager] trashItemAtURL:savePanel.URL resultingItemURL:nil error:nil];
            [[NSFileManager defaultManager] createDirectoryAtURL:savePanel.URL withIntermediateDirectories:false attributes:nil error:nil];
            [[NSString stringWithFormat:@"%@\n%@\n%@",
              [romPath pathRelativeToDirectory:savePanel.URL.path],
              romPath,
              [[NSURL fileURLWithPath:romPath].fileReferenceURL.absoluteString substringFromIndex:strlen("file://")]
            ] writeToURL:[savePanel.URL URLByAppendingPathComponent:@"rom.gbl"] atomically:false encoding:NSUTF8StringEncoding error:nil];
            [[NSDocumentController sharedDocumentController] openDocumentWithContentsOfURL:savePanel.URL display:true completionHandler:nil];
        }
        if (shouldResume) {
            [self start];
        }
    }];
}

- (IBAction)toggleAudioRecording:(id)sender
{

    bool shouldResume = _running;
    [self stop];
    if (_isRecordingAudio) {
        _isRecordingAudio = false;
        int error = GB_stop_audio_recording(&_gb);
        if (error) {
            NSAlert *alert = [[NSAlert alloc] init];
            [alert setMessageText:[NSString stringWithFormat:@"Could not finalize recording: %s", strerror(error)]];
            [alert setAlertStyle:NSAlertStyleCritical];
            [alert runModal];
        }
        else {
            [self.osdView displayText:@"Audio recording ended"];
        }
        if (shouldResume) {
            [self start];
        }
        return;
    }
    _audioSavePanel = [NSSavePanel savePanel];
    if (!self.audioRecordingAccessoryView) {
        [[NSBundle mainBundle] loadNibNamed:@"AudioRecordingAccessoryView" owner:self topLevelObjects:nil];
    }
    _audioSavePanel.accessoryView = self.audioRecordingAccessoryView;
    [self audioFormatChanged:self.audioFormatButton];
    
    [_audioSavePanel beginSheetModalForWindow:self.mainWindow completionHandler:^(NSInteger result) {
        if (result == NSModalResponseOK) {
            [_audioSavePanel orderOut:self];
            int error = GB_start_audio_recording(&_gb, _audioSavePanel.URL.fileSystemRepresentation, self.audioFormatButton.selectedTag);
            if (error) {
                NSAlert *alert = [[NSAlert alloc] init];
                [alert setMessageText:[NSString stringWithFormat:@"Could not start recording: %s", strerror(error)]];
                [alert setAlertStyle:NSAlertStyleCritical];
                [alert runModal];
            }
            else {
                [self.osdView displayText:@"Audio recording started"];
                _isRecordingAudio = true;
            }
        }
        if (shouldResume) {
            [self start];
        }
        _audioSavePanel = nil;
    }];
}

- (IBAction)audioFormatChanged:(NSPopUpButton *)sender
{
    switch ((GB_audio_format_t)sender.selectedTag) {
        case GB_AUDIO_FORMAT_RAW:
            _audioSavePanel.allowedFileTypes = @[@"raw", @"pcm"];
            break;
        case GB_AUDIO_FORMAT_AIFF:
            _audioSavePanel.allowedFileTypes = @[@"aiff", @"aif", @"aifc"];
            break;
        case GB_AUDIO_FORMAT_WAV:
            _audioSavePanel.allowedFileTypes = @[@"wav"];
            break;
    }
}

- (IBAction)toggleAudioChannel:(NSMenuItem *)sender
{
    GB_set_channel_muted(&_gb, sender.tag, !GB_is_channel_muted(&_gb, sender.tag));
}

- (IBAction)cartSwap:(id)sender
{
    bool wasRunning = _running;
    if (wasRunning) {
        [self stop];
    }
    [[NSDocumentController sharedDocumentController] beginOpenPanelWithCompletionHandler:^(NSArray<NSURL *> *urls) {
        if (urls.count == 1) {
            bool ok = true;
            for (Document *document in [NSDocumentController sharedDocumentController].documents) {
                if (document == self) continue;
                if ([document.fileURL isEqual:urls.firstObject]) {
                    NSAlert *alert = [[NSAlert alloc] init];
                    [alert setMessageText:[NSString stringWithFormat:@"‘%@’ is already open in another window. Close ‘%@’ before hot swapping it into this instance.",
                                                                     urls.firstObject.lastPathComponent, urls.firstObject.lastPathComponent]];
                    [alert setAlertStyle:NSAlertStyleCritical];
                    [alert runModal];
                    ok = false;
                    break;
                }
            }
            if (ok) {
                GB_save_battery(&_gb, self.savPath.UTF8String);
                self.fileURL = urls.firstObject;
                [self loadROM];
            }
        }
        if (wasRunning) {
            [self start];
        }
    }];
}

- (IBAction)reloadROM:(id)sender
{
    bool wasRunning = _running;
    if (wasRunning) {
        [self stop];
    }
    
    [self loadROM];

    if (wasRunning) {
        [self start];
    }
}

- (void)updateDebuggerButtons
{
    bool updateContinue = false;
    if (@available(macOS 10.10, *)) {
        if ([self.consoleInput.placeholderAttributedString.string isEqualToString:self.debuggerContinueButton.alternateTitle]) {
            [self.debuggerContinueButton mouseExited:nil];
            updateContinue = true;
        }
    }
    if (self.isPaused) {
        self.debuggerContinueButton.toolTip = self.debuggerContinueButton.title = @"Continue";
        self.debuggerContinueButton.alternateTitle = @"continue";
        self.debuggerContinueButton.imagePosition = NSImageOnly;
        if (@available(macOS 10.14, *)) {
            self.debuggerContinueButton.contentTintColor = nil;
        }
        self.debuggerContinueButton.image = [NSImage imageNamed:@"ContinueTemplate"];
        
        self.debuggerNextButton.enabled = true;
        self.debuggerStepButton.enabled = true;
        self.debuggerFinishButton.enabled = true;
        self.debuggerBackstepButton.enabled = true;
    }
    else {
        self.debuggerContinueButton.toolTip = self.debuggerContinueButton.title = @"Interrupt";
        self.debuggerContinueButton.alternateTitle = @"interrupt";
        self.debuggerContinueButton.imagePosition = NSImageOnly;
        if (@available(macOS 10.14, *)) {
            self.debuggerContinueButton.contentTintColor = [NSColor controlAccentColor];
        }
        self.debuggerContinueButton.image = [NSImage imageNamed:@"InterruptTemplate"];
        
        self.debuggerNextButton.enabled = false;
        self.debuggerStepButton.enabled = false;
        self.debuggerFinishButton.enabled = false;
        self.debuggerBackstepButton.enabled = false;
    }
    if (updateContinue) {
        [self.debuggerContinueButton mouseEntered:nil];
    }
}

- (IBAction)debuggerButtonPressed:(NSButton *)sender
{
    [self queueDebuggerCommand:sender.alternateTitle];
}

+ (NSArray<NSString *> *)readableTypes
{
    NSMutableSet *set = [NSMutableSet setWithArray:[super readableTypes]];
    for (NSString *type in @[@"gb", @"gbc", @"isx", @"gbs"]) {
        [set addObject:(__bridge_transfer NSString *)UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension,
                                                                                           (__bridge CFStringRef)type,
                                                                                           NULL)];
    }
    return [set allObjects];
}

@end
