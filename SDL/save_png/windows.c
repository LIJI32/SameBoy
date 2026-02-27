#define COBJMACROS
#include "save_png.h"
#include <windows.h>
#include <wincodec.h>

static wchar_t *utf8_to_wide(const char *utf8)
{
    int length = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
    if (length <= 0) {
        return NULL;
    }
    
    wchar_t *ret = malloc(length * sizeof(*ret));
    if (!ret) {
        return NULL;
    }
    
    if (!MultiByteToWideChar(CP_UTF8, 0, utf8, -1, ret, length)) {
        free(ret);
        return NULL;
    }
    
    return ret;
}

bool save_png(const char *filename, uint32_t width, uint32_t height, const void *pixels, SDL_PixelFormat *pixel_format)
{
    bool success = false;
    wchar_t *wfilename = utf8_to_wide(filename);
    
    IWICImagingFactory *factory = NULL;
    IWICStream *stream = NULL;
    IWICBitmapEncoder *encoder = NULL;
    IWICBitmapFrameEncode *frame = NULL;
    uint8_t *row = malloc(width * 3);

    CoInitialize(NULL);
    
    
    if (CoCreateInstance(&CLSID_WICImagingFactory,
                         NULL,
                         CLSCTX_INPROC_SERVER,
                         &IID_IWICImagingFactory,
                         (void **)&factory)) {
        goto done;
    }
    
    if (IWICImagingFactory_CreateStream(factory, &stream)) goto done;
    if (IWICStream_InitializeFromFilename(stream, wfilename, GENERIC_WRITE)) goto done;
    if (IWICImagingFactory_CreateEncoder(factory, &GUID_ContainerFormatPng, NULL, &encoder)) goto done;
    if (IWICBitmapEncoder_Initialize(encoder, (IStream *)stream, WICBitmapEncoderNoCache)) goto done;
    if (IWICBitmapEncoder_CreateNewFrame(encoder, &frame, NULL)) goto done;
    if (IWICBitmapFrameEncode_Initialize(frame, NULL)) goto done;
    if (IWICBitmapFrameEncode_SetSize(frame, width, height)) goto done;
    
    WICPixelFormatGUID pixel_format_guid = GUID_WICPixelFormat24bppRGB;
    if (IWICBitmapFrameEncode_SetPixelFormat(frame, &pixel_format_guid)) goto done;
    
    for (uint32_t y = 0; y < height; y++) {
        const uint32_t *src = (uint32_t *)pixels + y * width;
        uint8_t *dest = row;
        
        for (uint32_t x = 0; x < width; x++) {
            uint8_t dummy;
            SDL_GetRGBA(*(src++), pixel_format, &dest[2], &dest[1], &dest[0], &dummy);
            dest += 3;
        }
        
        if (IWICBitmapFrameEncode_WritePixels(frame, 1, width * 3, width * 3, (void *)row)) goto done;
    }
    
    if (IWICBitmapFrameEncode_Commit(frame)) goto done;
    if (IWICBitmapEncoder_Commit(encoder)) goto done;
    
    success = true;
    
done:
    if (frame) IWICBitmapFrameEncode_Release(frame);
    if (encoder) IWICBitmapEncoder_Release(encoder);
    if (stream) IWICStream_Release(stream);
    if (factory) IWICImagingFactory_Release(factory);
    
    CoUninitialize();
    free(wfilename);
    free(row);
    
    return success;
}
