#import "GBOptionViewController.h"

@implementation GBOptionViewController
{
    NSMutableArray<NSString *> *_options;
    NSMutableArray<void (^)(void)> *_actions;
    NSMutableArray<NSNumber *> *_checked;
}

- (instancetype)initWithHeader:(NSString *)header
{
    UITableViewStyle style = UITableViewStyleGrouped;
    if (@available(iOS 13.0, *)) {
        style = UITableViewStyleInsetGrouped;
    }
    self = [super initWithStyle:style];
    self.header = header;
    _options = [NSMutableArray array];
    _actions = [NSMutableArray array];
    _checked = [NSMutableArray array];
    return self;
}

- (NSString *)tableView:(UITableView *)tableView titleForHeaderInSection:(NSInteger)section
{
    return self.header;
}

- (NSString *)tableView:(UITableView *)tableView titleForFooterInSection:(NSInteger)section
{
    return self.footer;
}

- (NSInteger)numberOfSectionsInTableView:(UITableView *)tableView
{
    return 1;
}

- (NSInteger)tableView:(UITableView *)tableView
 numberOfRowsInSection:(NSInteger)section
{
    return _options.count;
}

- (void)addOption:(NSString *)title
    withCheckmark:(bool)checked
           action:(void (^)(void))block
{
    [_options addObject:title];
    [_actions addObject:block];
    [_checked addObject:@(checked)];
}

- (UITableViewCell *)tableView:(UITableView *)tableView cellForRowAtIndexPath:(NSIndexPath *)indexPath
{
    UITableViewCell *cell = [[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault reuseIdentifier:nil];
    cell.textLabel.text = _options[[indexPath indexAtPosition:1]];
    cell.accessoryType = _checked[[indexPath indexAtPosition:1]].boolValue? UITableViewCellAccessoryCheckmark : UITableViewCellAccessoryNone;
    
    return cell;
}

- (void)tableView:(UITableView *)tableView didSelectRowAtIndexPath:(NSIndexPath *)indexPath
{
    [self.presentingViewController dismissViewControllerAnimated:true completion:^{
        _actions[[indexPath indexAtPosition:1]]();
    }];
}

- (BOOL)isModalInPresentation
{
    return self.isModal;
}
@end
