#import "GBPreviewProvider.h"

extern OSStatus GBQuickLookRender(CGContextRef cgContext, CFURLRef url, bool showBorder);

@implementation GBPreviewProvider

- (void)providePreviewForFileRequest:(QLFilePreviewRequest *)request completionHandler:(void (^)(QLPreviewReply *reply, NSError *error))handler
{
    QLPreviewReply* reply = [[QLPreviewReply alloc] initWithContextSize:CGSizeMake(640, 576)
                                                               isBitmap:true
                                                           drawingBlock:^BOOL (CGContextRef context, QLPreviewReply *reply, NSError **error) {
        return !GBQuickLookRender(context, (__bridge CFURLRef)request.fileURL, false);

    }];
        
    handler(reply, nil);
}

@end

