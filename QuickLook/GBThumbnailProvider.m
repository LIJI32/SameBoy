#import "GBThumbnailProvider.h"

extern OSStatus GBQuickLookRender(CGContextRef cgContext, CFURLRef url, bool showBorder);

@interface QLThumbnailReply (private)
@property unsigned iconFlavor;
@end

@implementation GBThumbnailProvider

- (void)provideThumbnailForFileRequest:(QLFileThumbnailRequest *)request completionHandler:(void (^)(QLThumbnailReply *, NSError *))handler
{
    CGSize size = {64, 64};
    CGSize maximumSize = request.maximumSize;
    while (size.width < maximumSize.width / 2 &&
           size.width < maximumSize.height / 2) {
        size.width *= 2;
    }
    size.height = size.width;
    QLThumbnailReply *reply = [QLThumbnailReply replyWithContextSize:size drawingBlock: ^BOOL(CGContextRef context) {
        return !GBQuickLookRender(context, (__bridge CFURLRef)request.fileURL, true);
    }];
    reply.iconFlavor = 0;
    handler(reply, nil);
}

@end
