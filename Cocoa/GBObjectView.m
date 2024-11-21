#import "GBObjectView.h"

@interface GBObjectViewItem : NSObject
@property IBOutlet NSView *view;
@property IBOutlet NSImageView *image;
@property IBOutlet NSTextField *oamAddress;
@property IBOutlet NSTextField *position;
@property IBOutlet NSTextField *attributes;
@property IBOutlet NSTextField *tile;
@property IBOutlet NSTextField *tileAddress;
@property IBOutlet NSImageView *warningIcon;
@property IBOutlet NSBox *verticalLine;
@end

@implementation GBObjectViewItem
{
    @public
    uint32_t _lastImageData[128];
    uint8_t _lastHeight;
}
@end

@implementation GBObjectView
{
    NSMutableArray<GBObjectViewItem *> *_items;
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
    self = [super initWithCoder:coder];
    _items = [NSMutableArray array];
    CGFloat height = self.frame.size.height;
    for (unsigned i = 0; i < 40; i++) {
        GBObjectViewItem *item = [[GBObjectViewItem alloc] init];
        [_items addObject:item];
        [[NSBundle mainBundle] loadNibNamed:@"GBObjectViewItem" owner:item topLevelObjects:nil];
        item.view.hidden = true;
        [self addSubview:item.view];
        [item.view setFrameOrigin:NSMakePoint((i % 4) * 120, height - (i / 4 * 68) - 68)];
        item.oamAddress.toolTip = @"OAM address";
        item.position.toolTip = @"Position";
        item.attributes.toolTip = @"Attributes";
        item.tile.toolTip = @"Tile index";
        item.tileAddress.toolTip = @"Tile address";
        item.warningIcon.toolTip = @"Dropped: too many objects in line";
        if ((i % 4) == 3) {
            [item.verticalLine removeFromSuperview];
        }
        item.view.autoresizingMask = NSViewMaxXMargin | NSViewMinYMargin;
    }
    return self;
}

- (void)reloadData:(Document *)document
{
    GB_oam_info_t *info = document.oamInfo;
    uint8_t length = document.oamCount;
    bool cgb = GB_is_cgb(document.gb);
    uint8_t height = document.oamHeight;
    NSFont *font = [document debuggerFontOfSize:11];
    NSFont *boldFont = [[NSFontManager sharedFontManager] convertFont:font toHaveTrait:NSBoldFontMask];
    
    for (unsigned i = 0; i < 40; i++) {
        GBObjectViewItem *item = _items[i];
        if (i >= length) {
            item.view.hidden = true;
        }
        else {
            item.view.hidden = false;
            
            item.oamAddress.font = boldFont;
            item.position.font = font;
            item.attributes.font = font;
            item.tile.font = font;
            item.tileAddress.font = font;
            
            item.oamAddress.stringValue = [NSString stringWithFormat:@"$%04X", info[i].oam_addr];
            item.position.stringValue = [NSString stringWithFormat:@"(%d, %d)",
                                         ((signed)(unsigned)info[i].x) - 8,
                                         ((signed)(unsigned)info[i].y) - 16];
            item.tile.stringValue = [NSString stringWithFormat:@"$%02X", info[i].tile];
            item.tileAddress.stringValue = [NSString stringWithFormat:@"$%04X", 0x8000 + info[i].tile * 0x10];
            item.warningIcon.hidden = !info[i].obscured_by_line_limit;
            if (cgb) {
                item.attributes.stringValue = [NSString stringWithFormat:@"%c%c%c%d%d",
                                               info[i].flags & 0x80? 'P' : '-',
                                               info[i].flags & 0x40? 'Y' : '-',
                                               info[i].flags & 0x20? 'X' : '-',
                                               info[i].flags & 0x08? 1 : 0,
                                               info[i].flags & 0x07];
            }
            else {
                item.attributes.stringValue = [NSString stringWithFormat:@"%c%c%c%d",
                                               info[i].flags & 0x80? 'P' : '-',
                                               info[i].flags & 0x40? 'Y' : '-',
                                               info[i].flags & 0x20? 'X' : '-',
                                               info[i].flags & 0x10? 1 : 0];
            }
            size_t imageSize =  8 * 4 * height;
            if (height == item->_lastHeight && memcmp(item->_lastImageData, info[i].image, imageSize) == 0) {
                continue;
            }
            memcpy(item->_lastImageData, info[i].image, imageSize);
            item->_lastHeight = height;
            item.image.image = [Document imageFromData:[NSData dataWithBytesNoCopy:info[i].image
                                                                            length:64 * 4 * 2
                                                                      freeWhenDone:false]
                                                 width:8
                                                height:height
                                                 scale:32.0 / height];
        }
    }
    
    NSRect frame = self.frame;
    CGFloat newHeight = MAX(68 * ((length + 3) / 4), self.superview.frame.size.height);
    frame.origin.y -= newHeight - frame.size.height;
    frame.size.height = newHeight;
    self.frame = frame;
}

- (void)drawRect:(NSRect)dirtyRect
{
    if (@available(macOS 10.14, *)) {
        [[NSColor alternatingContentBackgroundColors].lastObject setFill];
    }
    else {
        [[NSColor colorWithDeviceWhite:0.96 alpha:1] setFill];
    }
    NSRect frame = self.frame;
    for (unsigned i = 1; i <= 5; i++) {
        NSRectFill(NSMakeRect(0, frame.size.height - i * 68 * 2, frame.size.width, 68));
    }
}
@end
