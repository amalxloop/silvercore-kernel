/*
 * test_gfx.c  --  Unit tests for sc_gfx.h (software backend)
 */
#define SC_GFX_IMPLEMENTATION
#define SC_LAYOUT_IMPLEMENTATION
#define SC_WIDGET_IMPLEMENTATION
#define SC_RUNTIME_IMPLEMENTATION
#define SC_GFX_BACKEND_SOFTWARE

#include "sc_gfx.h"
#include <stdio.h>

#define FAIL_UNLESS(cond, why) \
    do { if(!(cond)){ printf("  [FAIL] %s\n", why); return 1; } } while(0)
#define PASS(name) printf("  [PASS] %s\n", name)

static int test_init_shutdown(void) {
    SCGfxDesc desc = {
        .backend = SC_BACKEND_SOFTWARE,
        .width   = 320, .height = 240,
    };
    SCGfxContext *ctx = NULL;
    FAIL_UNLESS(sc_ok(sc_gfx_init(&desc, &ctx)), "gfx_init ok");
    FAIL_UNLESS(ctx != NULL, "ctx non-null");
    FAIL_UNLESS(sc_gfx_active_backend(ctx) == SC_BACKEND_SOFTWARE, "backend sw");
    sc_gfx_shutdown(ctx);
    PASS("gfx_init_shutdown");
    return 0;
}

static int test_resources(void) {
    SCGfxDesc desc = {.backend=SC_BACKEND_SOFTWARE,.width=64,.height=64};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    /* Buffer */
    u8 data[256] = {0};
    SCGfxBufferDesc bd = {.type=SC_BUFFER_VERTEX,.usage=SC_BUFFER_STATIC,
                           .data=data,.size=sizeof(data)};
    SCGfxBuffer buf = sc_gfx_make_buffer(ctx, &bd);
    FAIL_UNLESS(sc_gfx_buf_valid(buf), "buffer valid");
    sc_gfx_destroy_buffer(ctx, buf);

    /* Texture */
    SCGfxTextureDesc td = {.width=8,.height=8,.fmt=SC_PIXFMT_RGBA8,
                            .data=data,.data_size=8*8*4};
    SCGfxTexture tex = sc_gfx_make_texture(ctx, &td);
    FAIL_UNLESS(sc_gfx_tex_valid(tex), "texture valid");
    sc_gfx_destroy_texture(ctx, tex);

    /* Shader */
    SCGfxShaderDesc sd = {.vs_source="void main(){}",.fs_source="void main(){}"};
    SCGfxShader shd = sc_gfx_make_shader(ctx, &sd);
    FAIL_UNLESS(sc_gfx_shd_valid(shd), "shader valid");
    sc_gfx_destroy_shader(ctx, shd);

    sc_gfx_shutdown(ctx);
    PASS("gfx_resources");
    return 0;
}

static int test_frame_loop(void) {
    SCGfxDesc desc = {.backend=SC_BACKEND_SOFTWARE,.width=128,.height=128};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    for (int i = 0; i < 3; i++) {
        sc_gfx_begin_frame(ctx, sc_rgba(0.1f,0.2f,0.3f,1.0f));
        sc_gfx_draw_rect(ctx, (SCRect2f){10,10,50,50}, sc_rgba(1,0,0,1));
        sc_gfx_draw_line(ctx, sc_v2(0,0), sc_v2(128,128), 2.0f,
                          sc_rgba(0,1,0,1));
        sc_gfx_end_frame(ctx);
    }

    SCGfxFrameStats s = sc_gfx_frame_stats(ctx);
    FAIL_UNLESS(s.draw_calls > 0, "draw calls recorded");

    sc_gfx_shutdown(ctx);
    PASS("gfx_frame_loop");
    return 0;
}

/* ===== Boundary / NULL-handling tests ===== */

static int test_shutdown_null(void) {
    /* Must not crash */
    sc_gfx_shutdown(NULL);
    PASS("shutdown_null");
    return 0;
}

static int test_set_rasterize_null(void) {
    /* Must not crash */
    sc_gfx_set_rasterize(NULL, false);
    sc_gfx_set_rasterize(NULL, true);
    PASS("set_rasterize_null");
    return 0;
}

static int test_init_zero_size(void) {
    /* width=0, height=0 should use defaults */
    SCGfxDesc desc = {.backend = SC_BACKEND_SOFTWARE, .width = 0, .height = 0};
    SCGfxContext *ctx = NULL;
    SCResult r = sc_gfx_init(&desc, &ctx);
    FAIL_UNLESS(r == SC_OK, "init zero size ok");
    FAIL_UNLESS(ctx != NULL, "ctx non-null");
    sc_gfx_shutdown(ctx);
    PASS("init_zero_size");
    return 0;
}

static int test_frame_stats_clean(void) {
    /* Frame stats should be zeroed after init and before any frame begins */
    SCGfxDesc desc = {.backend = SC_BACKEND_SOFTWARE, .width = 64, .height = 64};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);
    SCGfxFrameStats s = sc_gfx_frame_stats(ctx);
    FAIL_UNLESS(s.draw_calls == 0, "stats draw_calls = 0");
    FAIL_UNLESS(s.vertex_count == 0, "stats vertex_count = 0");
    FAIL_UNLESS(s.index_count == 0, "stats index_count = 0");
    sc_gfx_shutdown(ctx);
    PASS("frame_stats_clean");
    return 0;
}

static int test_destroy_invalid_handles(void) {
    /* Destroying invalid (id=0) handles must not crash */
    SCGfxDesc desc = {.backend = SC_BACKEND_SOFTWARE, .width = 32, .height = 32};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    SCGfxBuffer buf  = {0};
    SCGfxTexture tex = {0};
    SCGfxShader  shd = {0};
    SCGfxPipeline pip = {0};

    sc_gfx_destroy_buffer(ctx, buf);
    sc_gfx_destroy_texture(ctx, tex);
    sc_gfx_destroy_shader(ctx, shd);
    sc_gfx_destroy_pipeline(ctx, pip);

    sc_gfx_shutdown(ctx);
    PASS("destroy_invalid_handles");
    return 0;
}

static int test_init_shutdown_no_resources(void) {
    /* Init and shutdown without allocating any resources */
    SCGfxDesc desc = {.backend = SC_BACKEND_SOFTWARE, .width = 64, .height = 64};
    SCGfxContext *ctx = NULL;
    FAIL_UNLESS(sc_ok(sc_gfx_init(&desc, &ctx)), "init ok");
    sc_gfx_shutdown(ctx);
    PASS("init_shutdown_no_resources");
    return 0;
}

int main(void) {
    printf("=== sc_gfx tests ===\n");
    int fail = 0;
    fail += test_init_shutdown();
    fail += test_resources();
    fail += test_frame_loop();
    fail += test_shutdown_null();
    fail += test_set_rasterize_null();
    fail += test_init_zero_size();
    fail += test_frame_stats_clean();
    fail += test_destroy_invalid_handles();
    fail += test_init_shutdown_no_resources();
    if (!fail) printf("All gfx tests passed.\n");
    return fail;
}
