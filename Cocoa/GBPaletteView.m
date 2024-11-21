#import "GBPaletteView.h"

@interface GBPaletteViewItem : NSObject
@property IBOutlet NSView *view;
@property (strong) IBOutlet NSTextField *label;
@property (strong) IBOutlet NSTextField *color0;
@property (strong) IBOutlet NSTextField *color1;
@property (strong) IBOutlet NSTextField *color2;
@property (strong) IBOutlet NSTextField *color3;
@end

@implementation GBPaletteViewItem
@end

@implementation GBPaletteView
{
    NSMutableArray<NSTextField *> *_colors;
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
    self = [super initWithCoder:coder];
    _colors = [NSMutableArray array];
    CGFloat height = self.frame.size.height;
    for (unsigned i = 0; i < 16; i++) {
        GBPaletteViewItem *item = [[GBPaletteViewItem alloc] init];
        [[NSBundle mainBundle] loadNibNamed:@"GBPaletteViewRow" owner:item topLevelObjects:nil];
        [self addSubview:item.view];
        [item.view setFrameOrigin:NSMakePoint(0, height - (i * 24) - 24)];
        item.label.stringValue = [NSString stringWithFormat:@"%@ %d", i < 8? @"Background" : @"Object", i % 8];
        item.view.autoresizingMask = NSViewMaxXMargin | NSViewMinYMargin;
        [_colors addObject:item.color0];
        [_colors addObject:item.color1];
        [_colors addObject:item.color2];
        [_colors addObject:item.color3];
        
    }
    return self;
}

- (void)reloadData:(Document *)document
{
    GB_gameboy_t *gb = document.gb;
    uint8_t *bg = GB_get_direct_access(gb, GB_DIRECT_ACCESS_BGP, NULL, NULL);
    uint8_t *obj = GB_get_direct_access(gb, GB_DIRECT_ACCESS_OBP, NULL, NULL);
    NSFont *font = [document debuggerFontOfSize:13];
    
    for (unsigned i = 0; i < 4 * 8 * 2; i++) {
        uint8_t index = i % (4 * 8);
        uint8_t *palette = i >= 4 * 8 ? obj : bg;
        uint16_t color = (palette[(index << 1) + 1] << 8) | palette[(index << 1)];
        uint32_t nativeColor = GB_convert_rgb15(gb, color, false);
        
        uint8_t r = color & 0x1F,
                g = (color >> 5) & 0x1F,
                b = (color >> 10) & 0x1F;
        
        NSTextField *field = _colors[i];
        field.stringValue = [NSString stringWithFormat:@"$%04X", color];
        field.textColor = r * 3 + g * 4 + b * 2 > 120? [NSColor blackColor] : [NSColor whiteColor];
        field.toolTip = [NSString stringWithFormat:@"Red: %d, Green: %d, Blue: %d", r, g, b];
        field.font = font;
        field.backgroundColor = [NSColor colorWithRed:(nativeColor & 0xFF) / 255.0
                                                green:((nativeColor >> 8) & 0xFF) / 255.0
                                                 blue:((nativeColor >> 16) & 0xFF) / 255.0
                                                alpha:1.0];
    }
}

- (void)drawRect:(NSRect)dirtyRect
{
    NSRect frame = self.frame;
    if (@available(macOS 10.14, *)) {
        [[NSColor alternatingContentBackgroundColors].lastObject setFill];
    }
    else {
        [[NSColor colorWithDeviceWhite:0.96 alpha:1] setFill];
    }
    for (unsigned i = 1; i <= 8; i++) {
        NSRectFill(NSMakeRect(0, frame.size.height - i * 24 * 2, frame.size.width, 24));
    }
    
    if (@available(macOS 10.14, *)) {
        [[NSColor alternatingContentBackgroundColors].firstObject setFill];
    }
    else {
        [[NSColor controlBackgroundColor] setFill];
    }
    for (unsigned i = 0; i < 8; i++) {
        NSRectFill(NSMakeRect(0, frame.size.height - i * 24 * 2 - 24, frame.size.width, 24));
    }
}
@end
