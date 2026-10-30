#import "GBLuaConsoleController.h"

#define MAX_CONSOLE_LENGTH 0x40000

@implementation GBLuaConsoleController
{
    NSTextView *_textView;
    NSTextField *_statusLabel;
    NSButton *_reloadButton;
    NSButton *_stopButton;
}

static NSButton *makeButton(NSString *title, id target, SEL action, NSRect frame)
{
    NSButton *button = [[NSButton alloc] initWithFrame:frame];
    button.title = title;
    button.bezelStyle = NSBezelStyleRounded;
    button.target = target;
    button.action = action;
    button.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin;
    return button;
}

- (instancetype)initWithActionTarget:(id)target
{
    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 600, 360)
                                                   styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                                             NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                                                     backing:NSBackingStoreBuffered
                                                       defer:true];
    window.releasedWhenClosed = false;
    window.minSize = NSMakeSize(420, 200);
    self = [super initWithWindow:window];
    if (!self) return nil;

    NSView *content = window.contentView;
    NSRect bounds = content.bounds;
    const CGFloat barHeight = 40;
    const CGFloat buttonWidth = 80;

    CGFloat x = bounds.size.width - 8;
    NSButton *clearButton = makeButton(@"Clear", self, @selector(clearPressed:),
                                       NSMakeRect(x -= buttonWidth, bounds.size.height - barHeight + 4, buttonWidth, 32));
    _stopButton = makeButton(@"Stop", target, @selector(stopLuaScript:),
                             NSMakeRect(x -= buttonWidth, bounds.size.height - barHeight + 4, buttonWidth, 32));
    _reloadButton = makeButton(@"Restart", target, @selector(reloadLuaScript:),
                               NSMakeRect(x -= buttonWidth, bounds.size.height - barHeight + 4, buttonWidth, 32));
    NSButton *openButton = makeButton(@"Open…", target, @selector(openLuaScript:),
                                      NSMakeRect(x -= buttonWidth, bounds.size.height - barHeight + 4, buttonWidth, 32));

    _statusLabel = [[NSTextField alloc] initWithFrame:NSMakeRect(12, bounds.size.height - barHeight + 10, x - 20, 20)];
    _statusLabel.editable = false;
    _statusLabel.selectable = false;
    _statusLabel.bordered = false;
    _statusLabel.drawsBackground = false;
    _statusLabel.cell.lineBreakMode = NSLineBreakByTruncatingMiddle;
    _statusLabel.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin;

    NSScrollView *scrollView = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, bounds.size.width, bounds.size.height - barHeight)];
    scrollView.hasVerticalScroller = true;
    scrollView.borderType = NSNoBorder;
    scrollView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;

    _textView = [[NSTextView alloc] initWithFrame:scrollView.contentView.bounds];
    _textView.editable = false;
    _textView.richText = false;
    _textView.verticallyResizable = true;
    _textView.horizontallyResizable = false;
    _textView.autoresizingMask = NSViewWidthSizable;
    _textView.textContainer.widthTracksTextView = true;
    _textView.textContainerInset = NSMakeSize(4, 4);
    if (@available(macOS 10.15, *)) {
        _textView.font = [NSFont monospacedSystemFontOfSize:11 weight:NSFontWeightRegular];
    }
    else {
        _textView.font = [NSFont userFixedPitchFontOfSize:11];
    }
    scrollView.documentView = _textView;

    [content addSubview:_statusLabel];
    [content addSubview:openButton];
    [content addSubview:_reloadButton];
    [content addSubview:_stopButton];
    [content addSubview:clearButton];
    [content addSubview:scrollView];

    window.title = @"Lua Console";
    [window center];
    [self setScriptPath:nil running:false];
    return self;
}

- (void)appendText:(NSString *)text
{
    NSTextStorage *storage = _textView.textStorage;
    NSScrollView *scrollView = _textView.enclosingScrollView;
    bool atBottom = NSMaxY(scrollView.documentVisibleRect) >= NSMaxY(_textView.bounds) - 4;

    [storage beginEditing];
    [storage appendAttributedString:[[NSAttributedString alloc] initWithString:text
                                                                    attributes:@{
        NSFontAttributeName: _textView.font,
        NSForegroundColorAttributeName: [NSColor textColor],
    }]];
    if (storage.length > MAX_CONSOLE_LENGTH) {
        [storage deleteCharactersInRange:NSMakeRange(0, storage.length - MAX_CONSOLE_LENGTH / 2)];
    }
    [storage endEditing];

    if (atBottom) {
        [_textView scrollRangeToVisible:NSMakeRange(storage.length, 0)];
    }
}

- (void)clear
{
    [_textView.textStorage setAttributedString:[[NSAttributedString alloc] init]];
}

- (IBAction)clearPressed:(id)sender
{
    [self clear];
}

- (void)setScriptPath:(NSString *)path running:(bool)running
{
    if (!path) {
        _statusLabel.stringValue = @"No script loaded";
    }
    else {
        _statusLabel.stringValue = [NSString stringWithFormat:@"%@ – %@",
                                    path.lastPathComponent, running? @"Running" : @"Stopped"];
        _statusLabel.toolTip = path;
    }
    _reloadButton.enabled = path != nil;
    _reloadButton.title = running? @"Restart" : @"Start";
    _stopButton.enabled = running;
}

@end
