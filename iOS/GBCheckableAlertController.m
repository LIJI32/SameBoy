#import "GBCheckableAlertController.h"

@implementation GBCheckableAlertController
{
    bool _addedChecks;
}

- (void)viewWillAppear:(BOOL)animated
{
    if (!_addedChecks && _selectedAction) {
        _addedChecks = true;
        NSMutableSet *set = [NSMutableSet setWithObject:self.view];
        while (set.count) {
            UIView *view = [set anyObject];
            [set removeObject:view];
            if ([view.debugDescription containsString:_selectedAction.debugDescription]) {
                UIImageView *checkImage = [[UIImageView alloc] initWithImage:[UIImage systemImageNamed:@"checkmark"]];
                CGRect bounds = view.bounds;
                CGRect frame = checkImage.frame;
                frame.origin.x = bounds.size.width - frame.size.width - 12;
                frame.origin.y = round((bounds.size.height - frame.size.height) / 2);
                checkImage.frame = frame;
                [view addSubview:checkImage];
                break;
            }
            [set addObjectsFromArray:view.subviews];
        }
    }
    [super viewWillAppear:animated];
}

@end
