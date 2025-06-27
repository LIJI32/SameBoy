#import "GBHexStatusBarRepresenter.h"
#import <HexFiend/HFFunctions.h>

@interface GBHexStatusBarView : NSView
{
    NSCell *_cell;
    NSSize _cellSize;
    GBHexStatusBarRepresenter *_representer;
    NSDictionary *_cellAttributes;
    bool _registeredForAppNotifications;
}

- (void)setRepresenter:(GBHexStatusBarRepresenter *)rep;
- (void)setString:(NSString *)string;

@end


@implementation GBHexStatusBarView

- (void)_sharedInitStatusBarView
{
    NSMutableParagraphStyle *style = [[NSParagraphStyle defaultParagraphStyle] mutableCopy];
    [style setAlignment:NSCenterTextAlignment];
    style.lineBreakMode = NSLineBreakByTruncatingTail;
    _cellAttributes = @{
        NSForegroundColorAttributeName: [NSColor windowFrameTextColor],
        NSFontAttributeName: [NSFont labelFontOfSize:[NSFont smallSystemFontSize]],
        NSParagraphStyleAttributeName: style,
    };
    _cell = [[NSCell alloc] initTextCell:@""];
    [_cell setAlignment:NSCenterTextAlignment];
    [_cell setBackgroundStyle:NSBackgroundStyleRaised];
}

- (instancetype)initWithFrame:(NSRect)frame
{
    self = [super initWithFrame:frame];
    [self _sharedInitStatusBarView];
    return self;
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
    self = [super initWithCoder:coder];
    [self _sharedInitStatusBarView];
    return self;
}

- (BOOL)isFlipped
{
    return true;
}

- (void)setRepresenter:(GBHexStatusBarRepresenter *)rep
{
    _representer = rep;
}

- (void)setString:(NSString *)string
{
    [_cell setAttributedStringValue:[[NSAttributedString alloc] initWithString:string attributes:_cellAttributes]];
    _cellSize = [_cell cellSize];
    [self setNeedsDisplay:true];
}

- (void)drawRect:(NSRect)clip
{
    NSRect bounds = [self bounds];
    NSRect cellRect = NSMakeRect(NSMinX(bounds), HFCeil(NSMidY(bounds) - _cellSize.height / 2), NSWidth(bounds), _cellSize.height);
    [_cell drawWithFrame:cellRect inView:self];
}

- (void)setFrame:(NSRect)frame
{
    [super setFrame:frame];
    [self.window setContentBorderThickness:frame.origin.y + frame.size.height forEdge:NSMinYEdge];
}

- (void)mouseDown:(NSEvent *)event
{
    _representer.useDecimalLength ^= true;
}

- (void)windowDidChangeKeyStatus:(NSNotification *)note
{
    [self setNeedsDisplay:true];
}

- (void)viewDidMoveToWindow
{
    HFRegisterViewForWindowAppearanceChanges(self, @selector(windowDidChangeKeyStatus:), !_registeredForAppNotifications);
    _registeredForAppNotifications = true;
    [self.window setContentBorderThickness:self.frame.origin.y + self.frame.size.height forEdge:NSMinYEdge];
    [super viewDidMoveToWindow];
}

- (void)viewWillMoveToWindow:(NSWindow *)newWindow
{
    HFUnregisterViewForWindowAppearanceChanges(self, NO);
    [super viewWillMoveToWindow:newWindow];
}

- (void)dealloc
{
    HFUnregisterViewForWindowAppearanceChanges(self, _registeredForAppNotifications);
}

@end

@implementation GBHexStatusBarRepresenter

- (instancetype)init
{
    self = [super init];
    return self;
}

- (NSView *)createView {
    GBHexStatusBarView *view = [[GBHexStatusBarView alloc] initWithFrame:NSMakeRect(0, 0, 100, 18)];
    [view setRepresenter:self];
    [view setAutoresizingMask:NSViewWidthSizable];
    return view;
}

- (NSString *)describeLength:(unsigned long long)length
{
    if (self.useDecimalLength) {
        return [NSString stringWithFormat:@"%llu byte%s", length, length == 1 ? "" : "s"];
    }
    return [NSString stringWithFormat:@"$%llX byte%s", length, length == 1 ? "" : "s"];
}

- (NSString *)describeOffset:(unsigned long long)offset isRangeEnd:(bool)isRangeEnd
{
    if (!_gb) {
        return [NSString stringWithFormat:@"$%llX", offset];
    }
    return @(GB_debugger_describe_address(_gb, offset + _baseAddress, offset < 0x4000? -1 :_bankForDescription, false, isRangeEnd));
}


- (NSString *)stringForEmptySelectionAtOffset:(unsigned long long)offset length:(unsigned long long)length
{
    return [self describeOffset:offset isRangeEnd:false];
}

- (NSString *)stringForSingleByteSelectionAtOffset:(unsigned long long)offset length:(unsigned long long)length
{
    return [NSString stringWithFormat:@"Byte %@ selected", [self describeOffset:offset isRangeEnd:false]];
}

- (NSString *)stringForSingleRangeSelection:(HFRange)range length:(unsigned long long)length
{
    return [NSString stringWithFormat:@"Range %@ to %@ selected (%@)",
            [self describeOffset:range.location isRangeEnd:false],
            [self describeOffset:range.location + range.length isRangeEnd:true],
            [self describeLength:range.length]];
}


- (void)updateString
{
    NSString *string = nil;
    HFController *controller = [self controller];
    if (controller) {
        unsigned long long length = [controller contentsLength];
        NSArray *ranges = [controller selectedContentsRanges];
        NSUInteger rangeCount = [ranges count];
        if (rangeCount == 1) {
            HFRange range = [ranges[0] HFRange];
            if (range.length == 0) {
                string = [self stringForEmptySelectionAtOffset:range.location length:length];
            }
            else if (range.length == 1) {
                string = [self stringForSingleByteSelectionAtOffset:range.location length:length];
            }
            else {
                string = [self stringForSingleRangeSelection:range length:length];
            }
        }
        else {
            string = @"Multiple ranges selected";
        }
    }
    if (! string) string = @"";
    [[self view] setString:string];
}

- (void)setUseDecimalLength:(bool)useDecimalLength
{
    _useDecimalLength = useDecimalLength;
    [self updateString];
}

- (void)setBaseAddress:(uint16_t)baseAddress
{
    _baseAddress = baseAddress;
    [self updateString];
}

- (void) setBankForDescription:(uint16_t)bankForDescription
{
    _bankForDescription = bankForDescription;
    [self updateString];
}

- (void)controllerDidChange:(HFControllerPropertyBits)bits
{
    if (bits & (HFControllerContentLength | HFControllerSelectedRanges)) {
        [self updateString];
    }
}

+ (NSPoint)defaultLayoutPosition
{
    return NSMakePoint(0, -1);
}

@end
