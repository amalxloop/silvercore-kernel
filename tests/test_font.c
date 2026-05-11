/*
 * test_font.c  --  Unit tests for sc_font.h (requires a .ttf font)
 *
 * If no font file is provided via TEST_FONT_PATH env variable, tests
 * that load a font are skipped.  Rasterisation tests always run.
 */
#define SC_GFX_IMPLEMENTATION
#define SC_LAYOUT_IMPLEMENTATION
#define SC_WIDGET_IMPLEMENTATION
#define SC_RUNTIME_IMPLEMENTATION
#define SC_FONT_IMPLEMENTATION
#define SC_GFX_BACKEND_SOFTWARE

#include "sc_font.h"
#include "sc_gfx.h"
#include "sc_widget.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FAIL_UNLESS(cond, why) \
    do { if(!(cond)){ printf("  [FAIL] %s\n", why); return 1; } } while(0)
#define PASS(name) printf("  [PASS] %s\n", name)

static int test_font_create_destroy(void) {
    const char *path = getenv("TEST_FONT_PATH");
    if (!path) { printf("  [SKIP] font_create_destroy (no TEST_FONT_PATH)\n"); return 0; }

    FILE *f = fopen(path, "rb");
    if (!f) { printf("  [SKIP] font_create_destroy (cannot open %s)\n", path); return 0; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = (unsigned char*)malloc((usize)sz);
    fread(data, 1, (usize)sz, f);
    fclose(f);

    SCFont *font = sc_font_create(data, (usize)sz, 16.0f);
    FAIL_UNLESS(font != NULL, "sc_font_create succeeds");
    FAIL_UNLESS(font->scale > 0, "font scale > 0");

    /* Measure "Hello" width */
    f32 w = sc_font_text_width(font, "Hello");
    FAIL_UNLESS(w > 0, "text_width > 0");

    sc_font_destroy(font);
    free(data);
    PASS("font_create_destroy");
    return 0;
}

static int test_font_atlas_upload(void) {
    const char *path = getenv("TEST_FONT_PATH");
    if (!path) { printf("  [SKIP] font_atlas_upload (no TEST_FONT_PATH)\n"); return 0; }

    FILE *f = fopen(path, "rb");
    if (!f) { printf("  [SKIP] font_atlas_upload (cannot open)\n"); return 0; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *data = (unsigned char*)malloc((usize)sz);
    fread(data, 1, (usize)sz, f);
    fclose(f);

    SCFont *font = sc_font_create(data, (usize)sz, 16.0f);
    FAIL_UNLESS(font != NULL, "font created");

    /* Create a tiny gfx context for texture upload */
    u8 arena_buf[SC_KB(64)];
    SCArena arena;
    sc_arena_init(&arena, arena_buf, sizeof(arena_buf));

    SCGfxDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.width  = 64;
    desc.height = 64;
    desc.backend = SC_BACKEND_SOFTWARE;
    desc.frame_arena = &arena;

    SCGfxContext *gfx = NULL;
    SCResult res = sc_gfx_init(&desc, &gfx);
    FAIL_UNLESS(sc_ok(res), "gfx init");
    FAIL_UNLESS(gfx != NULL, "gfx context");

    SCGfxTexture tex = sc_font_upload_atlas(font, gfx);
    FAIL_UNLESS(tex.id > 0, "atlas texture handle valid");

    sc_gfx_set_rasterize(gfx, true);

    /* Render a glyph by calling the text renderer */
    sc_gfx_begin_frame(gfx, SC_BLACK);
    sc_font_render_text(gfx, font, "A", 2.0f, 10.0f, SC_WHITE, tex);
    sc_gfx_end_frame(gfx);

    /* Verify framebuffer was written (at least some pixel lit) */
    bool had_pixels = false;
    usize total = (usize)gfx->width * (usize)gfx->height;
    for (usize i = 0; i < total; i++) {
        if (gfx->framebuffer[i*4+3] > 0) { had_pixels = true; break; }
    }
    FAIL_UNLESS(had_pixels, "framebuffer has rendered glyph pixels");

    sc_gfx_shutdown(gfx);
    sc_font_destroy(font);
    free(data);
    PASS("font_atlas_upload");
    return 0;
}

int main(void) {
    printf("=== sc_font tests ===\n");
    int fail = 0;
    fail += test_font_create_destroy();
    fail += test_font_atlas_upload();
    if (!fail) printf("All font tests passed.\n");
    return fail;
}
