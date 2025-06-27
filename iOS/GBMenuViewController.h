#import <UIKit/UIKit.h>

@interface GBMenuViewController : UIAlertController
+ (instancetype)menu;
@property (nonatomic) NSInteger selectedButtonIndex;
@property (nonatomic, strong) NSArray<UIButton *> *menuButtons;
@end
