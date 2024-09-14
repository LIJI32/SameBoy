#import <UIKit/UIKit.h>

@interface GBLibraryViewController : UITabBarController
#ifdef APPSTORE
@property bool enableAnimations;
#endif
@end


