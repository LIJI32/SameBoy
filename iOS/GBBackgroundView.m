#import "GBBackgroundView.h"
#import "GBViewMetal.h"
#import "GBHapticManager.h"
#import "GBMenuViewController.h"
#import "GBViewController.h"
#import "GBROMManager.h"

double CGPointSquaredDistance(CGPoint a, CGPoint b)
{
    double deltaX = a.x - b.x;
    double deltaY = a.y - b.y;
    return deltaX * deltaX + deltaY * deltaY;
}

double CGPointAngle(CGPoint a, CGPoint b)
{
    double deltaX = a.x - b.x;
    double deltaY = a.y - b.y;
    return atan2(deltaY, deltaX);
}

static void positionView(UIImageView *view, CGPoint position)
{
    double center = view.image.size.width / 2 * [UIScreen mainScreen].scale;
    view.frame = (CGRect){
        {
            round(position.x - center) / [UIScreen mainScreen].scale,
            round(position.y - center) / [UIScreen mainScreen].scale
        },
        view.image.size
    };
}

static GB_key_mask_t angleToKeyMask(double angle)
{
    signed quantizedAngle = round(angle / M_PI * 16);
    if (quantizedAngle < 0) {
        quantizedAngle += 32;
    }
    switch (quantizedAngle) {
        case 32:
        case  0: return GB_KEY_RIGHT_MASK;
        case  1: return GB_KEY_RIGHT_MASK;
        case  2: return GB_KEY_RIGHT_MASK;
        case  3: return GB_KEY_RIGHT_MASK | GB_KEY_DOWN_MASK;
        case  4: return GB_KEY_RIGHT_MASK | GB_KEY_DOWN_MASK;
        case  5: return GB_KEY_DOWN_MASK;
        case  6: return GB_KEY_DOWN_MASK;
        case  7: return GB_KEY_DOWN_MASK;
            
        case  8: return GB_KEY_DOWN_MASK;
        case  9: return GB_KEY_DOWN_MASK;
        case 10: return GB_KEY_DOWN_MASK;
        case 11: return GB_KEY_LEFT_MASK | GB_KEY_DOWN_MASK;
        case 12: return GB_KEY_LEFT_MASK | GB_KEY_DOWN_MASK;
        case 13: return GB_KEY_LEFT_MASK;
        case 14: return GB_KEY_LEFT_MASK;
        case 15: return GB_KEY_LEFT_MASK;
            
        case 16: return GB_KEY_LEFT_MASK;
        case 17: return GB_KEY_LEFT_MASK;
        case 18: return GB_KEY_LEFT_MASK;
        case 19: return GB_KEY_LEFT_MASK | GB_KEY_UP_MASK;
        case 20: return GB_KEY_LEFT_MASK | GB_KEY_UP_MASK;
        case 21: return GB_KEY_UP_MASK;
        case 22: return GB_KEY_UP_MASK;
        case 23: return GB_KEY_UP_MASK;
            
        case 24: return GB_KEY_UP_MASK;
        case 25: return GB_KEY_UP_MASK;
        case 26: return GB_KEY_UP_MASK;
        case 27: return GB_KEY_RIGHT_MASK | GB_KEY_UP_MASK;
        case 28: return GB_KEY_RIGHT_MASK | GB_KEY_UP_MASK;
        case 29: return GB_KEY_RIGHT_MASK;
        case 30: return GB_KEY_RIGHT_MASK;
        case 31: return GB_KEY_RIGHT_MASK;
    }
    
    return 0;
}

@implementation GBBackgroundView
{
    NSMutableSet<UITouch *> *_touches;
    UITouch *_padTouch;
    CGPoint _padSwipeOrigin;
    UITouch *_screenTouch;
    UITouch *_logoTouch;
    CGPoint _screenSwipeOrigin;
    bool _screenSwiped;
    bool _inDynamicSpeedMode;
    bool _previewMode;
    
    UIView *_fadeView;
    UIImageView *_dpadView;
    UIImageView *_dpadShadowView;
    UIImageView *_aButtonView;
    UIImageView *_bButtonView;
    UIImageView *_startButtonView;
    UIImageView *_selectButtonView;
    UILabel *_screenLabel;
    
    UIVisualEffectView *_overlayView;
    UIView *_overlayViewContents;
    NSTimer *_fadeTimer;
    
    GB_key_mask_t _lastMask;
    bool _fullScreenMode;
}

- (void)reloadThemeImages
{
    _aButtonView.image      = [_layout.theme imageNamed:@"buttonA"];
    _bButtonView.image      = [_layout.theme imageNamed:@"buttonB"];
    _startButtonView.image  = [_layout.theme imageNamed:@"button2"];
    _selectButtonView.image = [_layout.theme imageNamed:@"button2"];
    self.usesSwipePad = self.usesSwipePad;
}

- (instancetype)initWithLayout:(GBLayout *)layout;
{
    self = [super initWithImage:nil];
    if (!self) return nil;
    
    _layout = layout;
    _touches = [NSMutableSet set];
    
    _screenLabel = [[UILabel alloc] initWithFrame:CGRectZero];
    _screenLabel.text = @"Tap the Game Boy screen to open the menu and load a ROM from the library.";
    _screenLabel.font = [UIFont systemFontOfSize:20 weight:UIFontWeightMedium];
    _screenLabel.textAlignment = NSTextAlignmentCenter;
    _screenLabel.textColor = [UIColor whiteColor];
    _screenLabel.lineBreakMode = NSLineBreakByWordWrapping;
    _screenLabel.numberOfLines = 0;
    [self addSubview:_screenLabel];
    
    _dpadView = [[UIImageView alloc] initWithImage:[_layout.theme imageNamed:@"dpad"]];
    _aButtonView = [[UIImageView alloc] initWithImage:[_layout.theme imageNamed:@"buttonA"]];
    _bButtonView = [[UIImageView alloc] initWithImage:[_layout.theme imageNamed:@"buttonB"]];
    _startButtonView = [[UIImageView alloc] initWithImage:[_layout.theme imageNamed:@"button2"]];
    _selectButtonView = [[UIImageView alloc] initWithImage:[_layout.theme imageNamed:@"button2"]];
    
    _dpadShadowView = [[UIImageView alloc] initWithImage:[UIImage imageNamed:@"dpadShadow"]];
    _dpadShadowView.hidden = true;
    _gbView = [[GBViewMetal alloc] initWithFrame:CGRectZero];
    
    _fadeView = [[UIView alloc] initWithFrame:self.frame];
    _fadeView.backgroundColor = [UIColor colorWithWhite:0 alpha:0];
    _fadeView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    
    [self addSubview:_dpadView];
    [self addSubview:_aButtonView];
    [self addSubview:_bButtonView];
    [self addSubview:_startButtonView];
    [self addSubview:_selectButtonView];
    [self addSubview:_fadeView];
    [self addSubview:_gbView];
    
    [_dpadView addSubview:_dpadShadowView];
    
    UIVisualEffect *effect = [UIBlurEffect effectWithStyle:(UIBlurEffectStyle)UIBlurEffectStyleDark];
    _overlayView = [[UIVisualEffectView alloc] initWithEffect:effect];
    _overlayView.frame = CGRectMake(8, 8, 32, 32);
    _overlayView.layer.cornerRadius = 12;
    _overlayView.layer.masksToBounds = true;
    _overlayView.alpha = 0;
    
    if (@available(iOS 13.0, *)) {
        _overlayViewContents = [[UIImageView alloc] init];
        _overlayViewContents.tintColor = [UIColor whiteColor];
        _overlayViewContents.contentMode = UIViewContentModeCenter;
    }
    else {
        _overlayViewContents = [[UILabel alloc] init];
        ((UILabel *)_overlayViewContents).font = [UIFont systemFontOfSize:UIFont.systemFontSize weight:UIFontWeightMedium];
        ((UILabel *)_overlayViewContents).textColor = [UIColor whiteColor];
    }
    _overlayViewContents.frame = CGRectMake(8, 8, 160, 20.5);
    [_overlayView.contentView addSubview:_overlayViewContents];
    [_gbView addSubview:_overlayView];
    
    return self;
}

- (GBViewController *)viewController
{
    return (GBViewController *)[UIApplication sharedApplication].delegate;
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    if (_previewMode) return;
    if (_fullScreenMode) {
        self.fullScreenMode = false;
        return;
    }
    static const double dpadRadius = 75;
    CGPoint dpadLocation = _layout.dpadLocation;
    double factor = [UIScreen mainScreen].scale;
    dpadLocation.x /= factor;
    dpadLocation.y /= factor;
    CGRect logoRect = _layout.logoRect;
    
    logoRect.origin.x /= factor;
    logoRect.origin.y /= factor;
    logoRect.size.width /= factor;
    logoRect.size.height /= factor;
    
    for (UITouch *touch in touches) {
        CGPoint point = [touch locationInView:self];
        if (CGRectContainsPoint(self.gbView.frame, point) && !_screenTouch) {
            if (self.viewController.runMode != GBRunModeNormal) {
                self.viewController.runMode = GBRunModeNormal;
                [self fadeOverlayOut];
            }
            else {
                _screenTouch = touch;
                _screenSwipeOrigin = point;
                _screenSwiped = false;
                _inDynamicSpeedMode = false;
                _overlayView.alpha = 0;
                [_fadeTimer invalidate];
                _fadeTimer = nil;
                if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBDynamicSpeed"]) {
                    self.viewController.runMode = GBRunModePaused;
                    [self displayOverlayWithImage:@"pause" orTitle:@"Paused"];
                }
            }
        }
        else if (CGRectContainsPoint(logoRect, point) && !_logoTouch) {
            _logoTouch = touch;
        }
        else if (!_padTouch) {
            if (fabs(point.x - dpadLocation.x) <= dpadRadius &&
                fabs(point.y - dpadLocation.y) <= dpadRadius) {
                _padTouch = touch;
                _padSwipeOrigin = point;
            }
        }
    }
    [_touches unionSet:touches];
    [self touchesChanged];
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    if (_padTouch && [touches containsObject:_padTouch]) {
        _padTouch = nil;
    }
    
    if (_screenTouch && [touches containsObject:_screenTouch]) {
        _screenTouch = nil;
        if (self.viewController.runMode == GBRunModePaused) {
            self.viewController.runMode = GBRunModeNormal;
            [self fadeOverlayOut];
        }
        if (!_screenSwiped) {
            self.window.backgroundColor = nil;
            [self.window.rootViewController presentViewController:[GBMenuViewController menu] animated:true completion:nil];
        }
        if (![[NSUserDefaults standardUserDefaults] boolForKey:@"GBSwipeLock"]) {
            if (self.viewController.runMode != GBRunModeNormal) {
                self.viewController.runMode = GBRunModeNormal;
                [self fadeOverlayOut];
            }
        }
    }
    
    if (_logoTouch && [touches containsObject:_logoTouch]) {
        
        double factor = [UIScreen mainScreen].scale;
        CGRect logoRect = _layout.logoRect;
        
        logoRect.origin.x /= factor;
        logoRect.origin.y /= factor;
        logoRect.size.width /= factor;
        logoRect.size.height /= factor;
        
        CGPoint point = [_logoTouch locationInView:self];
        if (CGRectContainsPoint(logoRect, point)) {
            self.window.backgroundColor = nil;
            [self.window.rootViewController presentViewController:[GBMenuViewController menu] animated:true completion:nil];
        }
        _logoTouch = nil;
    }

    [_touches minusSet:touches];
    [self touchesChanged];
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    [self touchesEnded:touches withEvent:event];
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event
{
    [self touchesChanged];
}

- (void)touchesChanged
{
    if (_previewMode) return;
    if (!GB_is_inited(_gbView.gb)) return;
    GB_key_mask_t mask = 0;
    double factor = [UIScreen mainScreen].scale;
    double buttonRadiusSquared = 36 *  36 * factor * factor;
    double dpadRadius = 75 * factor;
    bool dpadHandled = false;
    if (_usesSwipePad) {
        dpadHandled = true;
        if (_padTouch) {
            CGPoint point = [_padTouch locationInView:self];
            double squaredDistance = CGPointSquaredDistance(point, _padSwipeOrigin);
            if (squaredDistance > 16 * 16) {
                double angle = CGPointAngle(point, _padSwipeOrigin);
                mask |= angleToKeyMask(angle);
                if (squaredDistance > 24 * 24) {
                    double deltaX = point.x - _padSwipeOrigin.x;
                    double deltaY = point.y - _padSwipeOrigin.y;
                    double distance = sqrt(squaredDistance);
                    _padSwipeOrigin.x = point.x - deltaX / distance * 24;
                    _padSwipeOrigin.y = point.y - deltaY / distance * 24;
                }
            }
        }
    }
    for (UITouch *touch in _touches) {
        if (_usesSwipePad && touch == _padTouch) continue;
        CGPoint point = [touch locationInView:self];
        
        if (touch == _screenTouch) {
            if (_inDynamicSpeedMode) {
                double delta = point.x - _screenSwipeOrigin.x;
                if (fabs(delta) < 32) {
                    self.viewController.runMode = GBRunModePaused;
                    [self displayOverlayWithImage:@"pause" orTitle:@"Paused"];
                    continue;
                }
                
                double speed = fabs(delta / _gbView.frame.size.width * 3);
                if (delta > 0) {
                    if (speed > 1) {
                        [self displayOverlayWithImage:@"forward" orTitle:@"Fast-forwarding…"];
                    }
                    else {
                        [self displayOverlayWithImage:@"play" orTitle:@"Forward…"];
                    }
                    GB_set_clock_multiplier(_gbView.gb, speed);
                    self.viewController.runMode = GBRunModeTurbo;
                }
                else {
                    [self displayOverlayWithImage:@"backward" orTitle:@"Rewinding…"];
                    GB_set_clock_multiplier(_gbView.gb, speed);
                    self.viewController.runMode = GBRunModeRewind;

                }
                continue;
            }
            if (_screenSwiped) continue;
            if (point.x - _screenSwipeOrigin.x > 32) {
                [self turboSwipe];
            }
            else if (point.x - _screenSwipeOrigin.x < -32) {
                [self rewindSwipe];
            }
            else if (point.y - _screenSwipeOrigin.y > 32) {
                [self saveSwipe];
            }
            else if (point.y - _screenSwipeOrigin.y < -32) {
                [self loadSwipe];
            }
            continue;
        }
        
        point.x *= factor;
        point.y *= factor;
        if (!dpadHandled &&
            (touch == _padTouch ||
                (fabs(point.x - _layout.dpadLocation.x) <= dpadRadius &&
                 fabs(point.y - _layout.dpadLocation.y) <= dpadRadius)
            ) && (fabs(point.x - _layout.dpadLocation.x) >= dpadRadius / 5 ||
                  fabs(point.y - _layout.dpadLocation.y) >= dpadRadius / 5)) {
            dpadHandled = true; // Don't handle the dpad twice
            double angle = CGPointAngle(point, _layout.dpadLocation);
            mask |= angleToKeyMask(angle);
        }
        else if (CGPointSquaredDistance(point, _layout.aLocation) <= buttonRadiusSquared) {
            mask |= GB_KEY_A_MASK;
        }
        else if (CGPointSquaredDistance(point, _layout.bLocation) <= buttonRadiusSquared) {
            mask |= GB_KEY_B_MASK;
        }
        else if (CGPointSquaredDistance(point, _layout.startLocation) <= buttonRadiusSquared) {
            mask |= GB_KEY_START_MASK;
        }
        else if (CGPointSquaredDistance(point, _layout.selectLocation) <= buttonRadiusSquared) {
            mask |= GB_KEY_SELECT_MASK;
        }
        else if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBEnableABCombo"] &&
                 CGPointSquaredDistance(point, _layout.abComboLocation) <= buttonRadiusSquared) {
            mask |= GB_KEY_A_MASK | GB_KEY_B_MASK;
        }
    }
    if (mask != _lastMask) {
        _aButtonView.image      = [_layout.theme imageNamed:(mask & GB_KEY_A_MASK)?      @"buttonAPressed" : @"buttonA"];
        _bButtonView.image      = [_layout.theme imageNamed:(mask & GB_KEY_B_MASK)?      @"buttonBPressed" : @"buttonB"];
        _startButtonView.image  = [_layout.theme imageNamed:(mask & GB_KEY_START_MASK) ? @"button2Pressed" : @"button2"];
        _selectButtonView.image = [_layout.theme imageNamed:(mask & GB_KEY_SELECT_MASK)? @"button2Pressed" : @"button2"];
        
        bool hidden = false;
        bool diagonal = false;
        double rotation = 0;
        switch (mask & (GB_KEY_RIGHT_MASK | GB_KEY_DOWN_MASK | GB_KEY_LEFT_MASK | GB_KEY_UP_MASK)) {
            case GB_KEY_RIGHT_MASK: break;
            case GB_KEY_RIGHT_MASK | GB_KEY_DOWN_MASK: diagonal = true; break;
            case GB_KEY_DOWN_MASK: rotation = M_PI_2; break;
            case GB_KEY_LEFT_MASK | GB_KEY_DOWN_MASK: diagonal = true; rotation = M_PI_2; break;
            case GB_KEY_LEFT_MASK: rotation = M_PI; break;
            case GB_KEY_LEFT_MASK | GB_KEY_UP_MASK: diagonal = true; rotation = M_PI; break;
            case GB_KEY_UP_MASK: rotation = -M_PI_2; break;
            case GB_KEY_RIGHT_MASK | GB_KEY_UP_MASK: diagonal = true; rotation = -M_PI_2; break;
            default:
                hidden = true;
        }
        
        _dpadShadowView.hidden = hidden;
        if (!hidden) {
            if (_usesSwipePad) {
                _dpadShadowView.image = [UIImage imageNamed:diagonal? @"swipepadShadowDiagonal" : @"swipepadShadow"];

            }
            else {
                _dpadShadowView.image = [UIImage imageNamed:diagonal? @"dpadShadowDiagonal" : @"dpadShadow"];
            }
            _dpadShadowView.transform = CGAffineTransformMakeRotation(rotation);
        }
        
        GB_set_key_mask(_gbView.gb, mask);
        if ((mask & ~_lastMask) && ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBButtonHaptics"])) {
            [[GBHapticManager sharedManager] doTapHaptic];
        }
        _lastMask = mask;
    }
}

- (BOOL)isMultipleTouchEnabled
{
    return true;
}

- (BOOL)isUserInteractionEnabled
{
    return true;
}

- (void)setLayout:(GBLayout *)layout
{
    _layout = layout;
    self.image = layout.background;
    
    positionView(_dpadView, layout.dpadLocation);
    positionView(_aButtonView, layout.aLocation);
    positionView(_bButtonView, layout.bLocation);
    positionView(_startButtonView, layout.startLocation);
    positionView(_selectButtonView, layout.selectLocation);
    
    CGRect screenFrame = layout.screenRect;
    screenFrame.origin.x /= [UIScreen mainScreen].scale;
    screenFrame.origin.y /= [UIScreen mainScreen].scale;
    screenFrame.size.width /= [UIScreen mainScreen].scale;
    screenFrame.size.height /= [UIScreen mainScreen].scale;
    
    if (_fullScreenMode) {
        CGRect fullScreenFrame = layout.fullScreenRect;
        fullScreenFrame.origin.x /= [UIScreen mainScreen].scale;
        fullScreenFrame.origin.y /= [UIScreen mainScreen].scale;
        fullScreenFrame.size.width /= [UIScreen mainScreen].scale;
        fullScreenFrame.size.height /= [UIScreen mainScreen].scale;
        _gbView.frame = fullScreenFrame;
    }
    else {
        _gbView.frame = screenFrame;
    }
    
    screenFrame.origin.x += 8;
    screenFrame.origin.y += 8;
    screenFrame.size.width -= 16;
    screenFrame.size.height -= 16;
    
    if (@available(iOS 13.0, *)) {
        self.overrideUserInterfaceStyle = layout.theme.isDark? UIUserInterfaceStyleDark : UIUserInterfaceStyleLight;
        self.tintColor = layout.theme.brandColor;
    }

    _screenLabel.frame = screenFrame;
}

- (void)setUsesSwipePad:(bool)usesSwipePad
{
    _usesSwipePad = usesSwipePad;
    _dpadView.image = nil; // Some bug in UIImage seems to trigger without this?
    _dpadView.image = [_layout.theme imageNamed:usesSwipePad? @"swipepad" : @"dpad"];
}

- (void)displayOverlayWithImage:(NSString *)imageName orTitle:(NSString *)title
{
    if (@available(iOS 13.0, *)) {
        ((UIImageView *)_overlayViewContents).image = [UIImage systemImageNamed:imageName
                                                              withConfiguration:[UIImageSymbolConfiguration configurationWithWeight:UIImageSymbolWeightMedium]];
    }
    else {
        ((UILabel *)_overlayViewContents).text = title;
    }
    [_overlayViewContents sizeToFit];

    CGRect frame = _overlayViewContents.frame;
    frame.size.width = MAX(frame.size.width, 25);
    frame.size.height = MAX(frame.size.height, 22);
    _overlayViewContents.frame = frame;
    frame.origin = (CGPoint){8, 8};
    frame.size.width += 16;
    frame.size.height += 16;
    _overlayView.frame = frame;
    
    _overlayView.alpha = 1.0;
}

- (void)fadeOverlayOut
{
    [UIView animateWithDuration:1 animations:^{
        _overlayView.alpha = 0;
    }];
    [_fadeTimer invalidate];
    _fadeTimer = nil;
}

- (void)turboSwipe
{
    _screenSwiped = true;
    if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBDynamicSpeed"]) {
        _inDynamicSpeedMode = true;
    }
    [self displayOverlayWithImage:@"forward" orTitle:@"Fast-forwarding…"];
    self.viewController.runMode = GBRunModeTurbo;
}

- (void)rewindSwipe
{
    _screenSwiped = true;
    if ([[NSUserDefaults standardUserDefaults] boolForKey:@"GBDynamicSpeed"]) {
        _inDynamicSpeedMode = true;
    }
    [self displayOverlayWithImage:@"backward" orTitle:@"Rewinding…"];
    self.viewController.runMode = GBRunModeRewind;
}

- (NSString *)swipeStateFile
{
    return [[GBROMManager sharedManager] stateFile:1];
}

- (void)saveSwipe
{
    _screenSwiped = true;
    self.viewController.runMode = GBRunModeNormal;
    if (![[NSUserDefaults standardUserDefaults] boolForKey:@"GBSwipeState"]) {
        [self fadeOverlayOut];
        return;
    }
    [self displayOverlayWithImage:@"square.and.arrow.down" orTitle:@"Saved state to Slot 1"];
    _fadeTimer = [NSTimer scheduledTimerWithTimeInterval:1 repeats:false block:^(NSTimer *timer) {
        [self fadeOverlayOut];
    }];
    [self.viewController stop];
    [self.viewController saveStateToFile:self.swipeStateFile];
    [self.viewController start];
}

- (void)loadSwipe
{
    _screenSwiped = true;
    self.viewController.runMode = GBRunModeNormal;
    if (![[NSUserDefaults standardUserDefaults] boolForKey:@"GBSwipeState"]) {
        [self fadeOverlayOut];
        return;
    }
    [self displayOverlayWithImage:@"square.and.arrow.up" orTitle:@"Loaded state from Slot 1"];
    _fadeTimer = [NSTimer scheduledTimerWithTimeInterval:1 repeats:false block:^(NSTimer *timer) {
        [self fadeOverlayOut];
    }];
    [self.viewController stop];
    [self.viewController loadStateFromFile:self.swipeStateFile];
    [self.viewController start];
}

- (void)enterPreviewMode:(bool)showLabel
{
    if (showLabel) {
        _screenLabel.text = [NSString stringWithFormat:@"Previewing Theme “%@”", _layout.theme.name];
    }
    else {
        [_screenLabel removeFromSuperview];
        _screenLabel = nil;
    }
    _previewMode = true;
}

- (bool)fullScreenMode
{
    return _fullScreenMode;
}

- (void)setFullScreenMode:(bool)fullScreenMode
{
    if (fullScreenMode == _fullScreenMode) return;
    _fullScreenMode = fullScreenMode;
    [UIView animateWithDuration:1.0/3 animations:^{
        // Animating alpha has some weird quirks for some reason
        _fadeView.backgroundColor = [UIColor colorWithWhite:0 alpha:fullScreenMode];
        [self setLayout:_layout];
    }];
    [self.window.rootViewController setNeedsStatusBarAppearanceUpdate];
}

@end
