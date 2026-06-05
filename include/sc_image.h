/*
 * sc_image.h  --  SilverCore Image Loading Module
 *
 * Loads PNG, JPEG, BMP, GIF, TGA, PSD, HDR, PIC, PNM from file or memory
 * using stb_image. Always decompresses to RGBA (4 channels) for direct
 * use with sc_gfx_make_texture().
 *
 * Usage:
 *   #define SC_IMAGE_IMPLEMENTATION
 *   #include "sc_image.h"
 */
#ifndef SC_IMAGE_H
#define SC_IMAGE_H

#include "sc_types.h"
#include "sc_gfx.h"

typedef struct SCImage {
    void   *pixels;    /* RGBA pixel data, 4 bytes per pixel */
    i32     width;
    i32     height;
    usize   data_size; /* width * height * 4 */
} SCImage;

/* Load an image from a file path. Returns zeroed struct on failure. */
SCImage sc_image_load(const char *path);

/* Load an image from a memory buffer. Returns zeroed struct on failure. */
SCImage sc_image_load_from_memory(const void *data, usize len);

/* Free pixel data allocated by sc_image_load / sc_image_load_from_memory.
 * Safe to call with a zeroed SCImage (pixels == NULL). */
void sc_image_free(SCImage *img);

/* Convenience: load a file and create an SCGfxTexture in one call.
 * The decompressed pixel data is freed internally after the texture
 * makes a copy.  Returns {0} on failure. */
SCGfxTexture sc_gfx_load_texture(SCGfxContext *ctx, const char *path);

#ifdef SC_IMAGE_IMPLEMENTATION

#define STB_IMAGE_IMPLEMENTATION
#include "../tools/stb_image.h"

#include <stdlib.h>
#include <string.h>

SCImage sc_image_load(const char *path) {
    SCImage img;
    memset(&img, 0, sizeof(img));
    int w, h, n;
    img.pixels = stbi_load(path, &w, &h, &n, 4);
    if (!img.pixels) return img;
    img.width     = w;
    img.height    = h;
    img.data_size = (usize)w * (usize)h * 4;
    return img;
}

SCImage sc_image_load_from_memory(const void *data, usize len) {
    SCImage img;
    memset(&img, 0, sizeof(img));
    int w, h, n;
    img.pixels = stbi_load_from_memory((const stbi_uc*)data, (int)len,
                                        &w, &h, &n, 4);
    if (!img.pixels) return img;
    img.width     = w;
    img.height    = h;
    img.data_size = (usize)w * (usize)h * 4;
    return img;
}

void sc_image_free(SCImage *img) {
    if (img && img->pixels) {
        stbi_image_free(img->pixels);
        img->pixels = NULL;
    }
}

SCGfxTexture sc_gfx_load_texture(SCGfxContext *ctx, const char *path) {
    SCImage img = sc_image_load(path);
    if (!img.pixels) {
        SCGfxTexture h = {0};
        return h;
    }
    SCGfxTextureDesc td;
    memset(&td, 0, sizeof(td));
    td.width     = (u32)img.width;
    td.height    = (u32)img.height;
    td.fmt       = SC_PIXFMT_RGBA8;
    td.data      = img.pixels;
    td.data_size = img.data_size;
    td.label     = path;
    SCGfxTexture tex = sc_gfx_make_texture(ctx, &td);
    sc_image_free(&img);
    return tex;
}

#endif /* SC_IMAGE_IMPLEMENTATION */
#endif /* SC_IMAGE_H */
