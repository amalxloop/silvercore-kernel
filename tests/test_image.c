/*
 * test_image.c  --  Tests for sc_image.h (image loading from file / memory)
 */
#define SC_GFX_IMPLEMENTATION
#define SC_LAYOUT_IMPLEMENTATION
#define SC_IMAGE_IMPLEMENTATION
#define SC_GFX_BACKEND_SOFTWARE
#include "sc_image.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_passed = 0, tests_failed = 0;

#define TEST(name) do { printf("  [TEST] %s ... ", name); } while(0)
#define PASS() do { puts("PASS"); tests_passed++; } while(0)
#define FAIL(msg) do { puts("FAIL: " msg); tests_failed++; } while(0)
#define ASSERT(cond, msg) do { if (!(cond)) { FAIL(msg); return; } } while(0)

static const unsigned char test_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x04,
    0x08, 0x06, 0x00, 0x00, 0x00, 0xa9, 0xf1, 0x9e, 0x7e, 0x00, 0x00, 0x00,
    0x17, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0,
    0x1f, 0x84, 0x91, 0x20, 0x9a, 0x00, 0x94, 0x0f, 0x07, 0x18, 0x02, 0x00,
    0x97, 0xe4, 0x27, 0xd9, 0xe0, 0x08, 0x96, 0xdd, 0x00, 0x00, 0x00, 0x00,
    0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};
static const int test_png_len = 80;

static void test_load_from_memory(void) {
    TEST("load_from_memory_4x4_rgba");
    SCImage img = sc_image_load_from_memory(test_png, (usize)test_png_len);
    ASSERT(img.pixels != NULL, "pixels is NULL");
    ASSERT(img.width  == 4, "width != 4");
    ASSERT(img.height == 4, "height != 4");
    ASSERT(img.data_size == (usize)4*4*4, "data_size mismatch");

    /* 4x4 pattern: top-left RED, top-right GREEN, bottom-left BLUE, bottom-right WHITE */
    usize stride = 4 * 4;
    u8 *p = (u8*)img.pixels;
    ASSERT(p[0]                     == 255 && p[1]                     == 0   && p[2]   == 0   && p[3]   == 255, "pixel(0,0) not red");
    ASSERT(p[0*stride + 2*4 + 0]    == 0   && p[0*stride + 2*4 + 1]    == 255 && p[0*stride+2*4+2]==0   && p[0*stride+2*4+3]==255, "pixel(2,0) not green");
    ASSERT(p[2*stride + 0*4 + 0]    == 0   && p[2*stride + 0*4 + 1]    == 0   && p[2*stride+0*4+2]==255 && p[2*stride+0*4+3]==255, "pixel(0,2) not blue");
    ASSERT(p[3*stride + 3*4 + 0]    == 255 && p[3*stride + 3*4 + 1]    == 255 && p[3*stride+3*4+2]==255 && p[3*stride+3*4+3]==255, "pixel(3,3) not white");

    sc_image_free(&img);
    ASSERT(img.pixels == NULL, "pixels not NULL after free");
    PASS();
}

static void test_load_nonexistent(void) {
    TEST("load_nonexistent_file");
    SCImage img = sc_image_load("/nonexistent/path.png");
    ASSERT(img.pixels == NULL, "pixels should be NULL for nonexistent file");
    ASSERT(img.width  == 0, "width should be 0");
    ASSERT(img.height == 0, "height should be 0");
    sc_image_free(&img);
    PASS();
}

static void test_load_from_memory_empty(void) {
    TEST("load_from_memory_empty");
    SCImage img = sc_image_load_from_memory("", 0);
    ASSERT(img.pixels == NULL, "pixels should be NULL for empty data");
    sc_image_free(&img);
    PASS();
}

static void test_double_free(void) {
    TEST("double_free_safe");
    SCImage img = sc_image_load_from_memory(test_png, (usize)test_png_len);
    ASSERT(img.pixels != NULL, "initial load failed");
    sc_image_free(&img);
    sc_image_free(&img);
    ASSERT(img.pixels == NULL, "pixels not NULL after double free");
    PASS();
}

static void test_texture_from_file(void) {
    TEST("texture_from_file");
    SCGfxDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.backend = SC_BACKEND_SOFTWARE;
    desc.width   = 64;
    desc.height  = 64;
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);
    ASSERT(ctx != NULL, "gfx_init failed");

    /* Write embedded PNG to temp file */
    const char *tmp_path = "/tmp/sc_test_img.png";
    FILE *f = fopen(tmp_path, "wb");
    ASSERT(f != NULL, "fopen tmp file failed");
    fwrite(test_png, 1, (usize)test_png_len, f);
    fclose(f);

    SCGfxTexture tex = sc_gfx_load_texture(ctx, tmp_path);
    ASSERT(sc_gfx_tex_valid(tex), "texture handle invalid");

    /* Clean up temp file */
    remove(tmp_path);
    sc_gfx_shutdown(ctx);
    PASS();
}

static void test_texture_from_memory(void) {
    TEST("texture_from_memory_small");
    SCGfxDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.backend = SC_BACKEND_SOFTWARE;
    desc.width   = 64;
    desc.height  = 64;
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);
    ASSERT(ctx != NULL, "gfx_init failed");

    SCImage img = sc_image_load_from_memory(test_png, (usize)test_png_len);
    ASSERT(img.pixels != NULL, "load from memory failed");

    SCGfxTextureDesc td;
    memset(&td, 0, sizeof(td));
    td.width     = (u32)img.width;
    td.height    = (u32)img.height;
    td.fmt       = SC_PIXFMT_RGBA8;
    td.data      = img.pixels;
    td.data_size = img.data_size;
    SCGfxTexture tex = sc_gfx_make_texture(ctx, &td);
    ASSERT(sc_gfx_tex_valid(tex), "texture handle invalid");

    sc_image_free(&img);
    sc_gfx_shutdown(ctx);
    PASS();
}

int main(void) {
    printf("=== sc_image tests ===\n");

    test_load_from_memory();
    test_load_nonexistent();
    test_load_from_memory_empty();
    test_double_free();
    test_texture_from_file();
    test_texture_from_memory();

    printf("  %d passed, %d failed out of %d\n",
           tests_passed, tests_failed, tests_passed + tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
