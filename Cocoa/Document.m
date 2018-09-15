#include <AVFoundation/AVFoundation.h>
#include <CoreAudio/CoreAudio.h>
#include <Core/gb.h>
#include "GBAudioClient.h"
#include "Document.h"
#include "AppDelegate.h"
#include "HexFiend/HexFiend.h"
#include "GBMemoryByteArray.h"
#include "GBWarningPopover.h"

/* Todo: The general Objective-C coding style conflicts with SameBoy's. This file needs a cleanup. */
/* Todo: Split into category files! This is so messy!!! */

enum model {
    MODEL_NONE,
    MODEL_DMG,
    MODEL_CGB,
    MODEL_AGB,
};

static const GB_model_t cocoa_to_internal_model[] =
{
    [MODEL_DMG] = GB_MODEL_DMG_B,
    [MODEL_CGB] = GB_MODEL_CGB_E,
    [MODEL_AGB] = GB_MODEL_AGB
};

@interface Document ()
{
    
    NSMutableAttributedString *pending_console_output;
    NSRecursiveLock *console_output_lock;
    NSTimer *console_output_timer;
    
    bool fullScreen;
    bool in_sync_input;
    HFController *hex_controller;

    NSString *lastConsoleInput;
    HFLineCountingRepresenter *lineRep;

    CVImageBufferRef cameraImage;
    AVCaptureSession *cameraSession;
    AVCaptureConnection *cameraConnection;
    AVCaptureStillImageOutput *cameraOutput;
    
    GB_oam_info_t oamInfo[40];
    uint16_t oamCount;
    uint8_t oamHeight;
    bool oamUpdating;
    
    NSMutableData *currentPrinterImageData;
    enum {GBAccessoryNone, GBAccessoryPrinter} accessory;
    
    bool rom_warning_issued;
    
    NSMutableString *capturedOutput;
    bool logToSideView;
    bool shouldClearSideView;
    enum model current_model;
    
    bool rewind;
}

@property GBAudioClient *audioClient;
- (void) vblank;
- (void) log: (const char *) log withAttributes: (GB_log_attributes) attributes;
- (char *) getDebuggerInput;
- (char *) getAsyncDebuggerInput;
- (void) cameraRequestUpdate;
- (uint8_t) cameraGetPixelAtX:(uint8_t)x andY:(uint8_t)y;
- (void) printImage:(uint32_t *)image height:(unsigned) height
          topMargin:(unsigned) topMargin bottomMargin: (unsigned) bottomMargin
           exposure:(unsigned) exposure;
@end

static void vblank(GB_gameboy_t *gb)
{
    Document *self = (__bridge Document *)GB_get_user_data(gb);
    [self vblank];
}

static void consoleLog(GB_gameboy_t *gb, const char *string, GB_log_attributes attributes)
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

@implementation Document
{
    GB_gameboy_t gb;
    volatile bool running;
    volatile bool stopping;
    NSConditionLock *has_debugger_input;
    NSMutableArray *debugger_input_queue;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        has_debugger_input = [[NSConditionLock alloc] initWithCondition:0];
        debugger_input_queue = [[NSMutableArray alloc] init];
        console_output_lock = [[NSRecursiveLock alloc] init];
    }
    return self;
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

/* Todo: Unify the 3 init functions */
- (void) initDMG
{
    current_model = MODEL_DMG;
    GB_init(&gb, cocoa_to_internal_model[current_model]);
    GB_load_boot_rom(&gb, [[self bootROMPathForName:@"dmg_boot"] UTF8String]);
    [self initCommon];
}

- (void) initCGB
{
    if ([[NSUserDefaults standardUserDefaults] boolForKey:@"EmulateAGB"]) {
        current_model = MODEL_AGB;
        GB_init(&gb, cocoa_to_internal_model[current_model]);
        GB_load_boot_rom(&gb, [[self bootROMPathForName:@"agb_boot"] UTF8String]);
    }
    else {
        current_model = MODEL_CGB;
        GB_init(&gb, cocoa_to_internal_model[current_model]);
        GB_load_boot_rom(&gb, [[self bootROMPathForName:@"cgb_boot"] UTF8String]);
    }
    [self initCommon];
}

- (void) initCommon
{
    GB_set_user_data(&gb, (__bridge void *)(self));
    GB_set_vblank_callback(&gb, (GB_vblank_callback_t) vblank);
    GB_set_log_callback(&gb, (GB_log_callback_t) consoleLog);
    GB_set_input_callback(&gb, (GB_input_callback_t) consoleInput);
    GB_set_async_input_callback(&gb, (GB_input_callback_t) asyncConsoleInput);
    GB_set_color_correction_mode(&gb, (GB_color_correction_mode_t) [[NSUserDefaults standardUserDefaults] integerForKey:@"GBColorCorrection"]);
    GB_set_rgb_encode_callback(&gb, rgbEncode);
    GB_set_camera_get_pixel_callback(&gb, cameraGetPixel);
    GB_set_camera_update_request_callback(&gb, cameraRequestUpdate);
    GB_set_highpass_filter_mode(&gb, (GB_highpass_mode_t) [[NSUserDefaults standardUserDefaults] integerForKey:@"GBHighpassFilter"]);
    GB_set_rewind_length(&gb, [[NSUserDefaults standardUserDefaults] integerForKey:@"GBRewindLength"]);
    [self loadROM];
}

- (void) vblank
{
    [self.view flip];
    GB_set_pixels_output(&gb, self.view.pixels);
    if (self.vramWindow.isVisible) {
        dispatch_async(dispatch_get_main_queue(), ^{
            self.view.mouseHidingEnabled = (self.mainWindow.styleMask & NSFullScreenWindowMask) != 0;
            [self reloadVRAMData: nil];
        });
    }
    if (self.view.isRewinding) {
        rewind = true;
    }
}

- (void) run
{
    running = true;
    GB_set_pixels_output(&gb, self.view.pixels);
    self.view.gb = &gb;
    GB_set_sample_rate(&gb, 96000);
    self.audioClient = [[GBAudioClient alloc] initWithRendererBlock:^(UInt32 sampleRate, UInt32 nFrames, GB_sample_t *buffer) {
        GB_apu_copy_buffer(&gb, buffer, nFrames);
    } andSampleRate:96000];
    if (![[NSUserDefaults standardUserDefaults] boolForKey:@"Mute"]) {
        [self.audioClient start];
    }
    NSTimer *hex_timer = [NSTimer timerWithTimeInterval:0.25 target:self selector:@selector(reloadMemoryView) userInfo:nil repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:hex_timer forMode:NSDefaultRunLoopMode];
    while (running) {
        if (rewind) {
            rewind = false;
            GB_rewind_pop(&gb);
            if (!GB_rewind_pop(&gb)) {
                rewind = self.view.isRewinding;
            }
        }
        else {
            GB_run(&gb);
        }
    }
    [hex_timer invalidate];
    [self.audioClient stop];
    self.audioClient = nil;
    self.view.mouseHidingEnabled = NO;
    GB_save_battery(&gb, [[[self.fileName stringByDeletingPathExtension] stringByAppendingPathExtension:@"sav"] UTF8String]);
    stopping = false;
}

- (void) start
{
    if (running) return;
    self.view.mouseHidingEnabled = (self.mainWindow.styleMask & NSFullScreenWindowMask) != 0;
    [[[NSThread alloc] initWithTarget:self selector:@selector(run) object:nil] start];
}

- (void) stop
{
    if (!running) return;
    GB_debugger_set_disabled(&gb, true);
    if (GB_debugger_is_stopped(&gb)) {
        [self interruptDebugInputRead];
    }
    stopping = true;
    running = false;
    while (stopping);
    GB_debugger_set_disabled(&gb, false);
}

- (IBAction)reset:(id)sender
{
    [self stop];

    if ([sender tag] != MODEL_NONE) {
        current_model = (enum model)[sender tag];
    }
    
    static NSString * const boot_names[] = {@"dmg_boot", @"cgb_boot", @"agb_boot"};
    GB_load_boot_rom(&gb, [[self bootROMPathForName:boot_names[current_model - 1]] UTF8String]);

    if ([sender tag] == MODEL_NONE) {
        GB_reset(&gb);
    }
    else {
        GB_switch_model_and_reset(&gb, cocoa_to_internal_model[current_model]);
    }

    if ([sender tag] != 0) {
        /* User explictly selected a model, save the preference */
        [[NSUserDefaults standardUserDefaults] setBool:current_model == MODEL_DMG forKey:@"EmulateDMG"];
        [[NSUserDefaults standardUserDefaults] setBool:current_model== MODEL_AGB forKey:@"EmulateAGB"];
    }
    
    /* Reload the ROM, SAV and SYM files */
    [self loadROM];

    [self start];

    if (hex_controller) {
        /* Verify bank sanity, especially when switching models. */
        [(GBMemoryByteArray *)(hex_controller.byteArray) setSelectedBank:0];
        [self hexUpdateBank:self.memoryBankInput ignoreErrors:true];
    }
}

- (IBAction)togglePause:(id)sender
{
    if (running) {
        [self stop];
    }
    else {
        [self start];
    }
}

- (void)dealloc
{
    [cameraSession stopRunning];
    GB_free(&gb);
    if (cameraImage) {
        CVBufferRelease(cameraImage);
    }
}

- (void)windowControllerDidLoadNib:(NSWindowController *)aController {
    [super windowControllerDidLoadNib:aController];
    
    /* Close Open Panels, if any */
    for (NSWindow *window in [[NSApplication sharedApplication] windows]) {
        if ([window isKindOfClass:[NSOpenPanel class]]) {
            [(NSOpenPanel *)window cancel:self];
        }
    }
    
    NSMutableParagraphStyle *paragraph_style = [[NSMutableParagraphStyle alloc] init];
    [paragraph_style setLineSpacing:2];
        
    self.debuggerSideViewInput.font = [NSFont userFixedPitchFontOfSize:12];
    self.debuggerSideViewInput.textColor = [NSColor whiteColor];
    self.debuggerSideViewInput.defaultParagraphStyle = paragraph_style;
    [self.debuggerSideViewInput setString:@"registers\nbacktrace\n"];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(updateSideView)
                                                 name:NSTextDidChangeNotification
                                               object:self.debuggerSideViewInput];
    
    self.consoleOutput.textContainerInset = NSMakeSize(4, 4);
    [self.view becomeFirstResponder];
    self.view.shouldBlendFrameWithPrevious = ![[NSUserDefaults standardUserDefaults] boolForKey:@"DisableFrameBlending"];
    CGRect window_frame = self.mainWindow.frame;
    window_frame.size.width  = MAX([[NSUserDefaults standardUserDefaults] integerForKey:@"LastWindowWidth"],
                                  window_frame.size.width);
    window_frame.size.height = MAX([[NSUserDefaults standardUserDefaults] integerForKey:@"LastWindowHeight"],
                                   window_frame.size.height);
    [self.mainWindow setFrame:window_frame display:YES];
    self.vramStatusLabel.cell.backgroundStyle = NSBackgroundStyleRaised;
    
    
    [self.feedSaveButton removeFromSuperview];
    
    self.consoleWindow.title = [NSString stringWithFormat:@"Debug Console – %@", [[self.fileURL path] lastPathComponent]];
    
    /* contentView.superview.subviews.lastObject is the titlebar view */
    NSView *titleView = self.printerFeedWindow.contentView.superview.subviews.lastObject;
    [titleView addSubview: self.feedSaveButton];
    self.feedSaveButton.frame = (NSRect){{268, 2}, {48, 17}};
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(updateHighpassFilter)
                                                 name:@"GBHighpassFilterChanged"
                                               object:nil];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(updateColorCorrectionMode)
                                                 name:@"GBColorCorrectionChanged"
                                               object:nil];
    
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(updateRewindLength)
                                                 name:@"GBRewindLengthChanged"
                                               object:nil];
    
    if ([[NSUserDefaults standardUserDefaults] boolForKey:@"EmulateDMG"]) {
        [self initDMG];
    }
    else {
        [self initCGB];
    }
    
    [self start];

}

- (void) initMemoryView
{
    hex_controller = [[HFController alloc] init];
    [hex_controller setBytesPerColumn:1];
    [hex_controller setFont:[NSFont userFixedPitchFontOfSize:12]];
    [hex_controller setEditMode:HFOverwriteMode];
    
    [hex_controller setByteArray:[[GBMemoryByteArray alloc] initWithDocument:self]];

    /* Here we're going to make three representers - one for the hex, one for the ASCII, and one for the scrollbar.  To lay these all out properly, we'll use a fourth HFLayoutRepresenter. */
    HFLayoutRepresenter *layoutRep = [[HFLayoutRepresenter alloc] init];
    HFHexTextRepresenter *hexRep = [[HFHexTextRepresenter alloc] init];
    HFStringEncodingTextRepresenter *asciiRep = [[HFStringEncodingTextRepresenter alloc] init];
    HFVerticalScrollerRepresenter *scrollRep = [[HFVerticalScrollerRepresenter alloc] init];
    lineRep = [[HFLineCountingRepresenter alloc] init];
    HFStatusBarRepresenter *statusRep = [[HFStatusBarRepresenter alloc] init];

    lineRep.lineNumberFormat = HFLineNumberFormatHexadecimal;

    /* Add all our reps to the controller. */
    [hex_controller addRepresenter:layoutRep];
    [hex_controller addRepresenter:hexRep];
    [hex_controller addRepresenter:asciiRep];
    [hex_controller addRepresenter:scrollRep];
    [hex_controller addRepresenter:lineRep];
    [hex_controller addRepresenter:statusRep];

    /* Tell the layout rep which reps it should lay out. */
    [layoutRep addRepresenter:hexRep];
    [layoutRep addRepresenter:scrollRep];
    [layoutRep addRepresenter:asciiRep];
    [layoutRep addRepresenter:lineRep];
    [layoutRep addRepresenter:statusRep];


    [(NSView *)[hexRep view] setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];

    /* Grab the layout rep's view and stick it into our container. */
    NSView *layoutView = [layoutRep view];
    NSRect layoutViewFrame = self.memoryView.frame;
    [layoutView setFrame:layoutViewFrame];
    [layoutView setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable | NSViewMaxYMargin];
    [self.memoryView addSubview:layoutView];

    self.memoryBankItem.enabled = false;
}

+ (BOOL)autosavesInPlace {
    return YES;
}

- (NSString *)windowNibName {
    // Override returning the nib file name of the document
    // If you need to use a subclass of NSWindowController or if your document supports multiple NSWindowControllers, you should remove this method and override -makeWindowControllers instead.
    return @"Document";
}

- (BOOL)readFromFile:(NSString *)fileName ofType:(NSString *)type
{
    return YES;
}

- (void) loadROM
{
    NSString *rom_warnings = [self captureOutputForBlock:^{
        GB_load_rom(&gb, [self.fileName UTF8String]);
        GB_load_battery(&gb, [[[self.fileName stringByDeletingPathExtension] stringByAppendingPathExtension:@"sav"] UTF8String]);
        GB_debugger_clear_symbols(&gb);
        GB_debugger_load_symbol_file(&gb, [[[NSBundle mainBundle] pathForResource:@"registers" ofType:@"sym"] UTF8String]);
        GB_debugger_load_symbol_file(&gb, [[[self.fileName stringByDeletingPathExtension] stringByAppendingPathExtension:@"sym"] UTF8String]);
    }];
    if (rom_warnings && !rom_warning_issued) {
        rom_warning_issued = true;
        [GBWarningPopover popoverWithContents:rom_warnings onWindow:self.mainWindow];
    }
}

- (void)close
{
    [[NSUserDefaults standardUserDefaults] setInteger:self.mainWindow.frame.size.width forKey:@"LastWindowWidth"];
    [[NSUserDefaults standardUserDefaults] setInteger:self.mainWindow.frame.size.height forKey:@"LastWindowHeight"];
    [self stop];
    [self.consoleWindow close];
    [super close];
}

- (IBAction) interrupt:(id)sender
{
    [self log:"^C\n"];
    GB_debugger_break(&gb);
    if (!running) {
        [self start];
    }
    [self.consoleWindow makeKeyAndOrderFront:nil];
    [self.consoleInput becomeFirstResponder];
}

- (IBAction)mute:(id)sender
{
    if (self.audioClient.isPlaying) {
        [self.audioClient stop];
    }
    else {
        [self.audioClient start];
    }
    [[NSUserDefaults standardUserDefaults] setBool:!self.audioClient.isPlaying forKey:@"Mute"];
}

- (IBAction)toggleBlend:(id)sender
{
    self.view.shouldBlendFrameWithPrevious ^= YES;
    [[NSUserDefaults standardUserDefaults] setBool:!self.view.shouldBlendFrameWithPrevious forKey:@"DisableFrameBlending"];
}

- (BOOL)validateUserInterfaceItem:(id<NSValidatedUserInterfaceItem>)anItem
{
    if([anItem action] == @selector(mute:)) {
        [(NSMenuItem*)anItem setState:!self.audioClient.isPlaying];
    }
    else if ([anItem action] == @selector(togglePause:)) {
        [(NSMenuItem*)anItem setState:(!running) || (GB_debugger_is_stopped(&gb))];
        return !GB_debugger_is_stopped(&gb);
    }
    else if ([anItem action] == @selector(reset:) && anItem.tag != MODEL_NONE) {
        [(NSMenuItem*)anItem setState:anItem.tag == current_model];
    }
    else if ([anItem action] == @selector(toggleBlend:)) {
        [(NSMenuItem*)anItem setState:self.view.shouldBlendFrameWithPrevious];
    }
    else if ([anItem action] == @selector(interrupt:)) {
        if (![[NSUserDefaults standardUserDefaults] boolForKey:@"DeveloperMode"]) {
            return false;
        }
    }
    else if ([anItem action] == @selector(disconnectAllAccessories:)) {
        [(NSMenuItem*)anItem setState:accessory == GBAccessoryNone];
    }
    else if ([anItem action] == @selector(connectPrinter:)) {
        [(NSMenuItem*)anItem setState:accessory == GBAccessoryPrinter];
    }
    return [super validateUserInterfaceItem:anItem];
}


- (void) windowWillEnterFullScreen:(NSNotification *)notification
{
    fullScreen = true;
    self.view.mouseHidingEnabled = running;
}

- (void) windowWillExitFullScreen:(NSNotification *)notification
{
    fullScreen = false;
    self.view.mouseHidingEnabled = NO;
}

- (NSRect)windowWillUseStandardFrame:(NSWindow *)window defaultFrame:(NSRect)newFrame
{
    if (fullScreen) {
        return newFrame;
    }
    NSRect rect = window.contentView.frame;

    int titlebarSize = window.contentView.superview.frame.size.height - rect.size.height;
    int step = 160 / [[window screen] backingScaleFactor];

    rect.size.width = floor(rect.size.width / step) * step + step;
    rect.size.height = rect.size.width / 10 * 9 + titlebarSize;

    if (rect.size.width > newFrame.size.width) {
        rect.size.width = 160;
        rect.size.height = 144 + titlebarSize;
    }
    else if (rect.size.height > newFrame.size.height) {
        rect.size.width = 160;
        rect.size.height = 144 + titlebarSize;
    }

    rect.origin = window.frame.origin;
    rect.origin.y -= rect.size.height - window.frame.size.height;

    return rect;
}

- (void) appendPendingOutput
{
    [console_output_lock lock];
    if (shouldClearSideView) {
        shouldClearSideView = false;
        [self.debuggerSideView setString:@""];
    }
    if (pending_console_output) {
        NSTextView *textView = logToSideView? self.debuggerSideView : self.consoleOutput;
        
        [hex_controller reloadData];
        [self reloadVRAMData: nil];
        
        [textView.textStorage appendAttributedString:pending_console_output];
        [textView scrollToEndOfDocument:nil];
        if ([[NSUserDefaults standardUserDefaults] boolForKey:@"DeveloperMode"]) {
            [self.consoleWindow orderFront:nil];
        }
        pending_console_output = nil;
}
    [console_output_lock unlock];

}

- (void) log: (const char *) string withAttributes: (GB_log_attributes) attributes
{
    NSString *nsstring = @(string); // For ref-counting
    if (capturedOutput) {
        [capturedOutput appendString:nsstring];
        return;
    }
    
    
    NSFont *font = [NSFont userFixedPitchFontOfSize:12];
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
    
    [console_output_lock lock];
    if (!pending_console_output) {
        pending_console_output = attributed;
    }
    else {
        [pending_console_output appendAttributedString:attributed];
    }
    
    if (![console_output_timer isValid]) {
        console_output_timer = [NSTimer timerWithTimeInterval:(NSTimeInterval)0.05 repeats:NO block:^(NSTimer * _Nonnull timer) {
            [self appendPendingOutput];
        }];
        [[NSRunLoop mainRunLoop] addTimer:console_output_timer forMode:NSDefaultRunLoopMode];
    }
    
    [console_output_lock unlock];

    /* Make sure mouse is not hidden while debugging */
    self.view.mouseHidingEnabled = NO;
}

- (IBAction)showConsoleWindow:(id)sender
{
    [self.consoleWindow orderBack:nil];
}

- (IBAction)consoleInput:(NSTextField *)sender {
    NSString *line = [sender stringValue];
    if ([line isEqualToString:@""] && lastConsoleInput) {
        line = lastConsoleInput;
    }
    else if (line) {
        lastConsoleInput = line;
    }
    else {
        line = @"";
    }

    if (!in_sync_input) {
        [self log:">"];
    }
    [self log:[line UTF8String]];
    [self log:"\n"];
    [has_debugger_input lock];
    [debugger_input_queue addObject:line];
    [has_debugger_input unlockWithCondition:1];

    [sender setStringValue:@""];
}

- (void) interruptDebugInputRead
{
    [has_debugger_input lock];
    [debugger_input_queue addObject:[NSNull null]];
    [has_debugger_input unlockWithCondition:1];
}

- (void) updateSideView
{
    if (!GB_debugger_is_stopped(&gb)) {
        return;
    }
    
    if (![NSThread isMainThread]) {
        dispatch_sync(dispatch_get_main_queue(), ^{
            [self updateSideView];
        });
        return;
    }
    
    [console_output_lock lock];
    shouldClearSideView = true;
    [self appendPendingOutput];
    logToSideView = true;
    [console_output_lock unlock];
    
    for (NSString *line in [self.debuggerSideViewInput.string componentsSeparatedByString:@"\n"]) {
        NSString *stripped = [line stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]];
        if ([stripped length]) {
            char *dupped = strdup([stripped UTF8String]);
            GB_attributed_log(&gb, GB_LOG_BOLD, "%s:\n", dupped);
            GB_debugger_execute_command(&gb, dupped);
            GB_log(&gb, "\n");
            free(dupped);
        }
    }
    
    [console_output_lock lock];
    [self appendPendingOutput];
    logToSideView = false;
    [console_output_lock unlock];
}

- (char *) getDebuggerInput
{
    [self updateSideView];
    [self log:">"];
    in_sync_input = true;
    [has_debugger_input lockWhenCondition:1];
    NSString *input = [debugger_input_queue firstObject];
    [debugger_input_queue removeObjectAtIndex:0];
    [has_debugger_input unlockWithCondition:[debugger_input_queue count] != 0];
    in_sync_input = false;
    shouldClearSideView = true;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(NSEC_PER_SEC / 10)), dispatch_get_main_queue(), ^{
        if (shouldClearSideView) {
            shouldClearSideView = false;
            [self.debuggerSideView setString:@""];
        }
    });
    if ((id) input == [NSNull null]) {
        return NULL;
    }
    return strdup([input UTF8String]);
}

- (char *) getAsyncDebuggerInput
{
    [has_debugger_input lock];
    NSString *input = [debugger_input_queue firstObject];
    if (input) {
        [debugger_input_queue removeObjectAtIndex:0];
    }
    [has_debugger_input unlockWithCondition:[debugger_input_queue count] != 0];
    return input? strdup([input UTF8String]): NULL;
}

- (IBAction)saveState:(id)sender
{
    bool __block success = false;
    [self performAtomicBlock:^{
        success = GB_save_state(&gb, [[[self.fileName stringByDeletingPathExtension] stringByAppendingPathExtension:[NSString stringWithFormat:@"s%ld", (long)[sender tag] ]] UTF8String]) == 0;
    }];
    
    if (!success) {
        [GBWarningPopover popoverWithContents:@"Failed to write save state." onWindow:self.mainWindow];
        NSBeep();
    }
}

- (IBAction)loadState:(id)sender
{
    bool __block success = false;
    NSString *error =
    [self captureOutputForBlock:^{
        success = GB_load_state(&gb, [[[self.fileName stringByDeletingPathExtension] stringByAppendingPathExtension:[NSString stringWithFormat:@"s%ld", (long)[sender tag] ]] UTF8String]) == 0;
    }];
    
    if (!success) {
        if (error) {
            [GBWarningPopover popoverWithContents:error onWindow:self.mainWindow];
        }
        NSBeep();
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
    while (!GB_is_inited(&gb));
    return GB_read_memory(&gb, addr);
}

- (void) writeMemory:(uint16_t)addr value:(uint8_t)value
{
    while (!GB_is_inited(&gb));
    GB_write_memory(&gb, addr, value);
}

- (void) performAtomicBlock: (void (^)())block
{
    while (!GB_is_inited(&gb));
    bool was_running = running && !GB_debugger_is_stopped(&gb);
    if (was_running) {
        [self stop];
    }
    block();
    if (was_running) {
        [self start];
    }
}

- (NSString *) captureOutputForBlock: (void (^)())block
{
    capturedOutput = [[NSMutableString alloc] init];
    [self performAtomicBlock:block];
    NSString *ret = [capturedOutput stringByTrimmingCharactersInSet:[NSCharacterSet newlineCharacterSet]];
    capturedOutput = nil;
    return [ret length]? ret : nil;
}

+ (NSImage *) imageFromData:(NSData *)data width:(NSUInteger) width height:(NSUInteger) height scale:(double) scale
{
    CGDataProviderRef provider = CGDataProviderCreateWithCFData((CFDataRef) data);
    CGColorSpaceRef colorSpaceRef = CGColorSpaceCreateDeviceRGB();
    CGBitmapInfo bitmapInfo = kCGBitmapByteOrderDefault;
    CGColorRenderingIntent renderingIntent = kCGRenderingIntentDefault;
    
    CGImageRef iref = CGImageCreate(width,
                                    height,
                                    8,
                                    32,
                                    4 * width,
                                    colorSpaceRef,
                                    bitmapInfo,
                                    provider,
                                    NULL,
                                    YES,
                                    renderingIntent);
    CGDataProviderRelease(provider);
    CGColorSpaceRelease(colorSpaceRef);
    
    NSImage *ret = [[NSImage alloc] initWithCGImage:iref size:NSMakeSize(width * scale, height * scale)];
    CGImageRelease(iref);
    
    return ret;
}

- (void) reloadMemoryView
{
    if (self.memoryWindow.isVisible) {
        [hex_controller reloadData];
    }
}

- (IBAction) reloadVRAMData: (id) sender
{
    if (self.vramWindow.isVisible) {
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
                GB_draw_tileset(&gb, (uint32_t *)data.mutableBytes, palette_type, (palette_menu_index - 1) & 7);
                
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
                GB_draw_tilemap(&gb, (uint32_t *)data.mutableBytes, palette_type, (palette_menu_index - 2) & 7,
                                (GB_map_type_t) self.tilemapMapButton.indexOfSelectedItem,
                                (GB_tileset_type_t) self.TilemapSetButton.indexOfSelectedItem);
                
                self.tilemapImageView.image = [Document imageFromData:data width:256 height:256 scale:1.0];
                self.tilemapImageView.layer.magnificationFilter = kCAFilterNearest;
            }
            break;
                
            case 2:
            /* OAM */
            {
                oamCount = GB_get_oam_info(&gb, oamInfo, &oamHeight);
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (!oamUpdating) {
                        oamUpdating = true;
                        [self.spritesTableView reloadData];
                        oamUpdating = false;
                    }
                });
            }
            break;
            
            case 3:
            /* Palettes */
            {
                dispatch_async(dispatch_get_main_queue(), ^{
                    [self.paletteTableView reloadData];
                });
            }
            break;
        }
    }
}

- (IBAction) showMemory:(id)sender
{
    if (!hex_controller) {
        [self initMemoryView];
    }
    [self.memoryWindow makeKeyAndOrderFront:sender];
}

- (IBAction)hexGoTo:(id)sender
{
    NSString *error = [self captureOutputForBlock:^{
        uint16_t addr;
        if (GB_debugger_evaluate(&gb, [[sender stringValue] UTF8String], &addr, NULL)) {
            return;
        }
        addr -= lineRep.valueOffset;
        if (addr >= hex_controller.byteArray.length) {
            GB_log(&gb, "Value $%04x is out of range.\n", addr);
            return;
        }
        [hex_controller setSelectedContentsRanges:@[[HFRangeWrapper withRange:HFRangeMake(addr, 0)]]];
        [hex_controller _ensureVisibilityOfLocation:addr];
        [self.memoryWindow makeFirstResponder:self.memoryView.subviews[0].subviews[0]];
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
        if (GB_debugger_evaluate(&gb, [[sender stringValue] UTF8String], &addr, &bank)) {
            return;
        }

        if (bank == (uint16_t) -1) {
            bank = addr;
        }

        uint16_t n_banks = 1;
        switch ([(GBMemoryByteArray *)(hex_controller.byteArray) mode]) {
            case GBMemoryROM: {
                size_t rom_size;
                GB_get_direct_access(&gb, GB_DIRECT_ACCESS_ROM, &rom_size, NULL);
                n_banks = rom_size / 0x4000;
                break;
            }
            case GBMemoryVRAM:
                n_banks = GB_is_cgb(&gb) ? 2 : 1;
                break;
            case GBMemoryExternalRAM: {
                size_t ram_size;
                GB_get_direct_access(&gb, GB_DIRECT_ACCESS_CART_RAM, &ram_size, NULL);
                n_banks = (ram_size + 0x1FFF) / 0x2000;
                break;
            }
            case GBMemoryRAM:
                n_banks = GB_is_cgb(&gb) ? 8 : 1;
                break;
            case GBMemoryEntireSpace:
                break;
        }

        bank %= n_banks;

        [sender setStringValue:[NSString stringWithFormat:@"$%x", bank]];
        [(GBMemoryByteArray *)(hex_controller.byteArray) setSelectedBank:bank];
        [hex_controller reloadData];
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
    GBMemoryByteArray *byteArray = (GBMemoryByteArray *)(hex_controller.byteArray);
    [byteArray setMode:(GB_memory_mode_t)[sender indexOfSelectedItem]];
    uint16_t bank;
    switch ((GB_memory_mode_t)[sender indexOfSelectedItem]) {
        case GBMemoryEntireSpace:
        case GBMemoryROM:
            lineRep.valueOffset = 0;
            GB_get_direct_access(&gb, GB_DIRECT_ACCESS_ROM, NULL, &bank);
            byteArray.selectedBank = bank;
            break;
        case GBMemoryVRAM:
            lineRep.valueOffset = 0x8000;
            GB_get_direct_access(&gb, GB_DIRECT_ACCESS_VRAM, NULL, &bank);
            byteArray.selectedBank = bank;
            break;
        case GBMemoryExternalRAM:
            lineRep.valueOffset = 0xA000;
            GB_get_direct_access(&gb, GB_DIRECT_ACCESS_CART_RAM, NULL, &bank);
            byteArray.selectedBank = bank;
            break;
        case GBMemoryRAM:
            lineRep.valueOffset = 0xC000;
            GB_get_direct_access(&gb, GB_DIRECT_ACCESS_RAM, NULL, &bank);
            byteArray.selectedBank = bank;
            break;
    }
    [self.memoryBankInput setStringValue:[NSString stringWithFormat:@"$%x", byteArray.selectedBank]];
    [hex_controller reloadData];
    [self.memoryView setNeedsDisplay:YES];
}

- (GB_gameboy_t *) gameboy
{
    return &gb;
}

+ (BOOL)canConcurrentlyReadDocumentsOfType:(NSString *)typeName
{
    return YES;
}

- (void)cameraRequestUpdate
{
    dispatch_async(dispatch_get_global_queue( DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        @try {
            if (!cameraSession) {
                NSError *error;
                AVCaptureDevice *device = [AVCaptureDevice defaultDeviceWithMediaType: AVMediaTypeVideo];
                AVCaptureDeviceInput *input = [AVCaptureDeviceInput deviceInputWithDevice: device error: &error];
                CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions([[[device formats] firstObject] formatDescription]);

                if (!input) {
                    GB_camera_updated(&gb);
                    return;
                }

                cameraOutput = [[AVCaptureStillImageOutput alloc] init];
                /* Greyscale is not widely supported, so we use YUV, whose first element is the brightness. */
                [cameraOutput setOutputSettings: @{(id)kCVPixelBufferPixelFormatTypeKey: @(kYUVSPixelFormat),
                                                   (id)kCVPixelBufferWidthKey: @(MAX(128, 112 * dimensions.width / dimensions.height)),
                                                   (id)kCVPixelBufferHeightKey: @(MAX(112, 128 * dimensions.height / dimensions.width)),}];


                cameraSession = [AVCaptureSession new];
                cameraSession.sessionPreset = AVCaptureSessionPresetPhoto;

                [cameraSession addInput: input];
                [cameraSession addOutput: cameraOutput];
                [cameraSession startRunning];
                cameraConnection = [cameraOutput connectionWithMediaType: AVMediaTypeVideo];
            }

            [cameraOutput captureStillImageAsynchronouslyFromConnection: cameraConnection completionHandler: ^(CMSampleBufferRef sampleBuffer, NSError *error) {
                if (error) {
                    GB_camera_updated(&gb);
                }
                else {
                    if (cameraImage) {
                        CVBufferRelease(cameraImage);
                        cameraImage = NULL;
                    }
                    cameraImage = CVBufferRetain(CMSampleBufferGetImageBuffer(sampleBuffer));
                    /* We only need the actual buffer, no need to ever unlock it. */
                    CVPixelBufferLockBaseAddress(cameraImage, 0);
                }
                
                GB_camera_updated(&gb);
            }];
        }
        @catch (NSException *exception) {
            /* I have not tested camera support on many devices, so we catch exceptions just in case. */
            GB_camera_updated(&gb);
        }
    });
}

- (uint8_t)cameraGetPixelAtX:(uint8_t)x andY:(uint8_t) y
{
    if (!cameraImage) {
        return 0;
    }

    uint8_t *baseAddress = (uint8_t *)CVPixelBufferGetBaseAddress(cameraImage);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(cameraImage);
    uint8_t offsetX = (CVPixelBufferGetWidth(cameraImage) - 128) / 2;
    uint8_t offsetY = (CVPixelBufferGetHeight(cameraImage) - 112) / 2;
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
            window_rect.size.height = 384 + height_diff + 48;
            break;
        case 1:
        case 2:
            window_rect.size.height = 512 + height_diff + 48;
            break;
        case 3:
            window_rect.size.height = 20 * 16 + height_diff + 24;
            break;
            
        default:
            break;
    }
    window_rect.origin.y -= window_rect.size.height;
    [self.vramWindow setFrame:window_rect display:YES animate:YES];
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
        uint8_t lcdc = ((uint8_t *)GB_get_direct_access(&gb, GB_DIRECT_ACCESS_IO, NULL, NULL))[GB_IO_LCDC];
        uint8_t *vram = GB_get_direct_access(&gb, GB_DIRECT_ACCESS_VRAM, NULL, NULL);
        
        if (map_type == GB_MAP_9C00 || (map_type == GB_MAP_AUTO && lcdc & 0x08)) {
            map_base = 0x1c00;
        }
        
        if (tileset_type == GB_TILESET_AUTO) {
            tileset_type = (lcdc & 0x10)? GB_TILESET_8800 : GB_TILESET_8000;
        }
        
        uint8_t tile = vram[map_base + map_offset];
        uint16_t tile_address = 0;
        if (tileset_type == GB_TILESET_8000) {
            tile_address = 0x8000 + tile * 0x10;
        }
        else {
            tile_address = 0x9000 + (int8_t)tile * 0x10;
        }
        
        if (GB_is_cgb(&gb)) {
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

- (NSInteger)numberOfRowsInTableView:(NSTableView *)tableView
{
    if (tableView == self.paletteTableView) {
        return 16; /* 8 BG palettes, 8 OBJ palettes*/
    }
    else if (tableView == self.spritesTableView) {
        return oamCount;
    }
    return 0;
}

- (id)tableView:(NSTableView *)tableView objectValueForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row
{
    NSUInteger columnIndex = [[tableView tableColumns] indexOfObject:tableColumn];
    if (tableView == self.paletteTableView) {
        if (columnIndex == 0) {
            return [NSString stringWithFormat:@"%s %d", row >=8 ? "Object" : "Background", (int)(row & 7)];
        }
        
        uint8_t *palette_data = GB_get_direct_access(&gb, row >= 8? GB_DIRECT_ACCESS_OBP : GB_DIRECT_ACCESS_BGP, NULL, NULL);

        uint16_t index = columnIndex - 1 + (row & 7) * 4;
        return @((palette_data[(index << 1) + 1] << 8) | palette_data[(index << 1)]);
    }
    else if (tableView == self.spritesTableView) {
        switch (columnIndex) {
            case 0:
                return [Document imageFromData:[NSData dataWithBytesNoCopy:oamInfo[row].image
                                                                    length:64 * 4
                                                             freeWhenDone:NO]
                                         width:8
                                        height:oamHeight
                                         scale:16.0/oamHeight];
            case 1:
                return @((int)oamInfo[row].x - 8);
            case 2:
                return @((int)oamInfo[row].y - 16);
            case 3:
                return [NSString stringWithFormat:@"$%02x", oamInfo[row].tile];
            case 4:
                return [NSString stringWithFormat:@"$%04x", 0x8000 + oamInfo[row].tile * 0x10];
            case 5:
                return [NSString stringWithFormat:@"$%04x", oamInfo[row].oam_addr];
            case 6:
                if (GB_is_cgb(&gb)) {
                    return [NSString stringWithFormat:@"%c%c%c%d%d",
                            oamInfo[row].flags & 0x80? 'P' : '-',
                            oamInfo[row].flags & 0x40? 'Y' : '-',
                            oamInfo[row].flags & 0x20? 'X' : '-',
                            oamInfo[row].flags & 0x08? 1 : 0,
                            oamInfo[row].flags & 0x07];
                }
                return [NSString stringWithFormat:@"%c%c%c%d",
                        oamInfo[row].flags & 0x80? 'P' : '-',
                        oamInfo[row].flags & 0x40? 'Y' : '-',
                        oamInfo[row].flags & 0x20? 'X' : '-',
                        oamInfo[row].flags & 0x10? 1 : 0];
            case 7:
                return oamInfo[row].obscured_by_line_limit? @"Dropped: Too many sprites in line": @"";

        }
    }
    return nil;
}

- (BOOL)tableView:(NSTableView *)tableView shouldSelectRow:(NSInteger)row
{
    return tableView == self.spritesTableView;
}

- (BOOL)tableView:(NSTableView *)tableView shouldEditTableColumn:(nullable NSTableColumn *)tableColumn row:(NSInteger)row
{
    return NO;
}

- (IBAction)showVRAMViewer:(id)sender
{
    [self.vramWindow makeKeyAndOrderFront:sender];
    [self reloadVRAMData: nil];
}

- (void) printImage:(uint32_t *)imageBytes height:(unsigned) height
          topMargin:(unsigned) topMargin bottomMargin: (unsigned) bottomMargin
           exposure:(unsigned) exposure
{
    uint32_t paddedImage[160 * (topMargin + height + bottomMargin)];
    memset(paddedImage, 0xFF, sizeof(paddedImage));
    memcpy(paddedImage + (160 * topMargin), imageBytes, 160 * height * sizeof(imageBytes[0]));
    if (!self.printerFeedWindow.isVisible) {
        currentPrinterImageData = [[NSMutableData alloc] init];
    }
    [currentPrinterImageData appendBytes:paddedImage length:sizeof(paddedImage)];
    self.feedImageView.image = [Document imageFromData:currentPrinterImageData
                                                 width:160
                                                height:currentPrinterImageData.length / 160 / sizeof(imageBytes[0])
                                                 scale:2.0];
    /* UI related code must run on main thread. */
    dispatch_async(dispatch_get_main_queue(), ^{
        NSRect frame = self.printerFeedWindow.frame;
        frame.size = self.feedImageView.image.size;
        frame.size.height += self.printerFeedWindow.frame.size.height - self.printerFeedWindow.contentView.frame.size.height;
        [self.printerFeedWindow setMaxSize:frame.size];
        [self.printerFeedWindow setFrame:frame display:NO animate: self.printerFeedWindow.isVisible];
        [self.printerFeedWindow orderFront:NULL];
    });
    
}
- (IBAction)savePrinterFeed:(id)sender
{
    bool shouldResume = running;
    [self stop];
    NSSavePanel * savePanel = [NSSavePanel savePanel];
    [savePanel setAllowedFileTypes:@[@"png"]];
    [savePanel beginSheetModalForWindow:self.printerFeedWindow completionHandler:^(NSInteger result){
        if (result == NSFileHandlingPanelOKButton) {
            [savePanel orderOut:self];
            CGImageRef cgRef = [self.feedImageView.image CGImageForProposedRect:NULL
                                                                        context:nil
                                                                          hints:nil];
            NSBitmapImageRep *imageRep = [[NSBitmapImageRep alloc] initWithCGImage:cgRef];
            [imageRep setSize:(NSSize){160, self.feedImageView.image.size.height / 2}];
            NSData *data = [imageRep representationUsingType:NSPNGFileType properties:@{}];
            [data writeToURL:savePanel.URL atomically:NO];
            [self.printerFeedWindow setIsVisible:NO];
        }
        if (shouldResume) {
            [self start];
        }
    }];
}

- (IBAction)disconnectAllAccessories:(id)sender
{
    [self performAtomicBlock:^{
        accessory = GBAccessoryNone;
        GB_disconnect_serial(&gb);
    }];
}

- (IBAction)connectPrinter:(id)sender
{
    [self performAtomicBlock:^{
            accessory = GBAccessoryPrinter;
        GB_connect_printer(&gb, printImage);
    }];
}

- (void) updateHighpassFilter
{
    if (GB_is_inited(&gb)) {
        GB_set_highpass_filter_mode(&gb, (GB_highpass_mode_t) [[NSUserDefaults standardUserDefaults] integerForKey:@"GBHighpassFilter"]);
    }
}

- (void) updateColorCorrectionMode
{
    if (GB_is_inited(&gb)) {
        GB_set_color_correction_mode(&gb, (GB_color_correction_mode_t) [[NSUserDefaults standardUserDefaults] integerForKey:@"GBColorCorrection"]);
    }
}

- (void) updateRewindLength
{
    [self performAtomicBlock:^{
        if (GB_is_inited(&gb)) {
            GB_set_rewind_length(&gb, [[NSUserDefaults standardUserDefaults] integerForKey:@"GBRewindLength"]);
        }
    }];
}

- (void)setFileURL:(NSURL *)fileURL
{
    [super setFileURL:fileURL];
    self.consoleWindow.title = [NSString stringWithFormat:@"Debug Console – %@", [[fileURL path] lastPathComponent]];
    
}

@end
