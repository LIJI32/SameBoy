#pragma once

// As defined in the thumbnailer spec.
enum ErrorCode {
    ERROR_UNKNOWN_SCHEME_OR_MIME,
    ERROR_SPECIALIZED_THUMBNAILER_CONNECTION_FAILED, // Not applicable.
    ERROR_INVALID_DATA,                              // Any file can be decoded as a GB ROM, apparently!
    ERROR_THUMBNAIILING_THUMBNAIL,                   // We defer checking this to the generic thumbnailer.
    ERROR_COULD_NOT_WRITE,
    ERROR_UNSUPPORTED_FLAVOR,
};

struct _ThumbnailerSpecializedThumbnailer1;
extern struct _ThumbnailerSpecializedThumbnailer1 *thumbnailer_interface;
