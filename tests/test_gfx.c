/*
 * test_gfx.c  --  Unit tests for sc_gfx.h
 *
 * By default tests the software backend.  Define SC_GFX_BACKEND_VULKAN
 * (or METAL / D3D12) and pass -DSC_TEST_BACKEND=SC_BACKEND_VULKAN to
 * run against a hardware backend.
 */
#if !defined(SC_GFX_BACKEND_SOFTWARE) && \
    !defined(SC_GFX_BACKEND_VULKAN) && \
    !defined(SC_GFX_BACKEND_METAL) && \
    !defined(SC_GFX_BACKEND_D3D12) && \
    !defined(SC_GFX_BACKEND_WGPU)
#define SC_GFX_BACKEND_SOFTWARE
#endif

/* When SC_TEST_NO_IMPL is defined, the implementation is provided by a
   separately-compiled backend header (Vulkan/Metal/D3D12). */
#ifndef SC_TEST_NO_IMPL
#ifndef SC_GFX_IMPLEMENTATION
#define SC_GFX_IMPLEMENTATION
#endif
#ifndef SC_LAYOUT_IMPLEMENTATION
#define SC_LAYOUT_IMPLEMENTATION
#endif
#ifndef SC_WIDGET_IMPLEMENTATION
#define SC_WIDGET_IMPLEMENTATION
#endif
#ifndef SC_RUNTIME_IMPLEMENTATION
#define SC_RUNTIME_IMPLEMENTATION
#endif
#endif /* !SC_TEST_NO_IMPL */

#ifndef SC_TEST_BACKEND
#define SC_TEST_BACKEND SC_BACKEND_SOFTWARE
#endif

#include "sc_gfx.h"
#include <stdio.h>

#define FAIL_UNLESS(cond, why) \
    do { if(!(cond)){ printf("  [FAIL] %s\n", why); return 1; } } while(0)
#define PASS(name) printf("  [PASS] %s\n", name)

static int test_init_shutdown(void) {
    SCGfxDesc desc = {
        .backend = SC_TEST_BACKEND,
        .width   = 320, .height = 240,
    };
    SCGfxContext *ctx = NULL;
    FAIL_UNLESS(sc_ok(sc_gfx_init(&desc, &ctx)), "gfx_init ok");
    FAIL_UNLESS(ctx != NULL, "ctx non-null");
    FAIL_UNLESS(sc_gfx_active_backend(ctx) == SC_TEST_BACKEND, "backend sw");
    sc_gfx_shutdown(ctx);
    PASS("gfx_init_shutdown");
    return 0;
}

static int test_resources(void) {
    SCGfxDesc desc = {.backend=SC_TEST_BACKEND,.width=64,.height=64};
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
    SCGfxDesc desc = {.backend=SC_TEST_BACKEND,.width=128,.height=128};
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
    SCGfxDesc desc = {.backend = SC_TEST_BACKEND, .width = 0, .height = 0};
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
    SCGfxDesc desc = {.backend = SC_TEST_BACKEND, .width = 64, .height = 64};
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
    SCGfxDesc desc = {.backend = SC_TEST_BACKEND, .width = 32, .height = 32};
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
    SCGfxDesc desc = {.backend = SC_TEST_BACKEND, .width = 64, .height = 64};
    SCGfxContext *ctx = NULL;
    FAIL_UNLESS(sc_ok(sc_gfx_init(&desc, &ctx)), "init ok");
    sc_gfx_shutdown(ctx);
    PASS("init_shutdown_no_resources");
    return 0;
}

/* ===== Buffer / shader / pipeline tests for resource management ========== */

static int test_buffer_data_persist(void) {
    SCGfxDesc desc = {.backend=SC_TEST_BACKEND,.width=64,.height=64};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    u8 data_a[32] = {1,2,3,4};
    SCGfxBufferDesc bd = {.type=SC_BUFFER_VERTEX,.data=data_a,.size=sizeof(data_a)};
    SCGfxBuffer buf = sc_gfx_make_buffer(ctx, &bd);
    FAIL_UNLESS(sc_gfx_buf_valid(buf), "buf with data");

    u8 data_b[64] = {5,6,7,8};
    bd.data = data_b; bd.size = sizeof(data_b);
    SCGfxBuffer buf2 = sc_gfx_make_buffer(ctx, &bd);
    FAIL_UNLESS(sc_gfx_buf_valid(buf2), "buf2");
    FAIL_UNLESS(buf.id != buf2.id, "distinct slot ids");

    sc_gfx_destroy_buffer(ctx, buf);
    sc_gfx_destroy_buffer(ctx, buf2);

    /* Free-list should recycle one of the freed slots */
    SCGfxBuffer buf3 = sc_gfx_make_buffer(ctx, &bd);
    FAIL_UNLESS(sc_gfx_buf_valid(buf3), "recycled slot buf");
    FAIL_UNLESS(buf3.id == buf.id || buf3.id == buf2.id, "slot reused");
    sc_gfx_destroy_buffer(ctx, buf3);

    sc_gfx_shutdown(ctx);
    PASS("buffer_data_persist");
    return 0;
}

static int test_buffer_update(void) {
    SCGfxDesc desc = {.backend=SC_TEST_BACKEND,.width=64,.height=64};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    u8 data[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    SCGfxBufferDesc bd = {.type=SC_BUFFER_VERTEX,.data=data,.size=sizeof(data)};
    SCGfxBuffer buf = sc_gfx_make_buffer(ctx, &bd);
    FAIL_UNLESS(sc_gfx_buf_valid(buf), "buf for update");

    u8 new_data[8] = {100,101,102,103,104,105,106,107};
    sc_gfx_update_buffer(ctx, buf, new_data, sizeof(new_data));

    sc_gfx_destroy_buffer(ctx, buf);
    sc_gfx_shutdown(ctx);
    PASS("buffer_update");
    return 0;
}

static int test_buffer_null_desc(void) {
    SCGfxDesc desc = {.backend=SC_TEST_BACKEND,.width=32,.height=32};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    /* NULL desc === zero-size buffer (slot allocated, no data) */
    SCGfxBuffer buf = sc_gfx_make_buffer(ctx, NULL);
    FAIL_UNLESS(sc_gfx_buf_valid(buf), "null desc -> valid (empty) buf");
    sc_gfx_destroy_buffer(ctx, buf);

    sc_gfx_shutdown(ctx);
    PASS("buffer_null_desc");
    return 0;
}

static int test_shader_pipeline_create(void) {
    SCGfxDesc desc = {.backend=SC_TEST_BACKEND,.width=32,.height=32};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    SCGfxShaderDesc sd = {.vs_source="",.fs_source=""};
    SCGfxShader shd = sc_gfx_make_shader(ctx, &sd);
    FAIL_UNLESS(sc_gfx_shd_valid(shd), "shader valid");

    SCGfxPipelineDesc pd = {.shader=shd,.prim_type=SC_PRIM_TRIANGLES};
    SCGfxPipeline pip = sc_gfx_make_pipeline(ctx, &pd);
    FAIL_UNLESS(sc_gfx_pip_valid(pip), "pipeline valid");

    sc_gfx_destroy_pipeline(ctx, pip);
    sc_gfx_destroy_shader(ctx, shd);
    sc_gfx_shutdown(ctx);
    PASS("shader_pipeline_create");
    return 0;
}

static int test_shader_pipeline_null_desc(void) {
    SCGfxDesc desc = {.backend=SC_TEST_BACKEND,.width=32,.height=32};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    /* NULL desc allocates a slot (empty) */
    SCGfxShader shd = sc_gfx_make_shader(ctx, NULL);
    FAIL_UNLESS(sc_gfx_shd_valid(shd), "null shader desc -> valid (empty)");
    sc_gfx_destroy_shader(ctx, shd);

    SCGfxPipeline pip = sc_gfx_make_pipeline(ctx, NULL);
    FAIL_UNLESS(sc_gfx_pip_valid(pip), "null pipeline desc -> valid (empty)");
    sc_gfx_destroy_pipeline(ctx, pip);

    sc_gfx_shutdown(ctx);
    PASS("shader_pipeline_null_desc");
    return 0;
}

/* ===== Submit command tests ============================================== */

static int test_submit_user_vertex_buffer(void) {
    SCGfxDesc desc = {.backend=SC_TEST_BACKEND,.width=128,.height=128};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    SCGfxVertex2D verts[3] = {
        {10,10, 0,0, 255,0,0,255},
        {100,10, 0,0, 0,255,0,255},
        {10,100, 0,0, 0,0,255,255},
    };
    SCGfxBufferDesc bd = {.type=SC_BUFFER_VERTEX,.data=verts,.size=sizeof(verts)};
    SCGfxBuffer vb = sc_gfx_make_buffer(ctx, &bd);
    FAIL_UNLESS(sc_gfx_buf_valid(vb), "vb for submit");

    sc_gfx_begin_frame(ctx, sc_rgba(0,0,0,1));
    SCGfxDrawCmd cmd = {0};
    cmd.vertex_buf = vb;
    cmd.vertex_count = 3;
    sc_gfx_submit(ctx, &cmd, 1);
    sc_gfx_end_frame(ctx);

    SCGfxFrameStats s = sc_gfx_frame_stats(ctx);
    FAIL_UNLESS(s.draw_calls >= 1, "submit recorded");
    FAIL_UNLESS(s.vertex_count >= 3, "verts recorded");

    sc_gfx_destroy_buffer(ctx, vb);
    sc_gfx_shutdown(ctx);
    PASS("submit_user_vertex_buffer");
    return 0;
}

static int test_submit_index_buffer(void) {
    SCGfxDesc desc = {.backend=SC_TEST_BACKEND,.width=128,.height=128};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    SCGfxVertex2D verts[4] = {
        {10,10, 0,0, 255,255,255,255},
        {100,10, 1,0, 255,255,255,255},
        {100,100, 1,1, 255,255,255,255},
        {10,100, 0,1, 255,255,255,255},
    };
    u32 indices[6] = {0,1,2, 0,2,3};

    SCGfxBufferDesc vbd = {.type=SC_BUFFER_VERTEX,.data=verts,.size=sizeof(verts)};
    SCGfxBuffer vb = sc_gfx_make_buffer(ctx, &vbd);
    FAIL_UNLESS(sc_gfx_buf_valid(vb), "vb");

    SCGfxBufferDesc ibd = {.type=SC_BUFFER_INDEX,.data=indices,.size=sizeof(indices)};
    SCGfxBuffer ib = sc_gfx_make_buffer(ctx, &ibd);
    FAIL_UNLESS(sc_gfx_buf_valid(ib), "ib");

    sc_gfx_begin_frame(ctx, sc_rgba(0,0,0,1));
    SCGfxDrawCmd cmd = {0};
    cmd.vertex_buf = vb;
    cmd.index_buf = ib;
    cmd.vertex_count = 4;
    cmd.index_count = 6;
    cmd.base_vertex = 0;
    cmd.base_index = 0;
    sc_gfx_submit(ctx, &cmd, 1);
    sc_gfx_end_frame(ctx);

    SCGfxFrameStats s = sc_gfx_frame_stats(ctx);
    FAIL_UNLESS(s.draw_calls >= 1, "indexed draw recorded");
    FAIL_UNLESS(s.index_count >= 6, "index count recorded");

    sc_gfx_destroy_buffer(ctx, vb);
    sc_gfx_destroy_buffer(ctx, ib);
    sc_gfx_shutdown(ctx);
    PASS("submit_index_buffer");
    return 0;
}

static int test_submit_with_texture(void) {
    SCGfxDesc desc = {.backend=SC_TEST_BACKEND,.width=64,.height=64};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    u8 texel[4] = {128,64,32,255};
    SCGfxTextureDesc td = {.width=1,.height=1,.fmt=SC_PIXFMT_RGBA8,
                            .data=texel,.data_size=4};
    SCGfxTexture tex = sc_gfx_make_texture(ctx, &td);
    FAIL_UNLESS(sc_gfx_tex_valid(tex), "tex");

    SCGfxVertex2D verts[3] = {{0,0,0,0,255,255,255,255},
                               {10,0,0,0,255,255,255,255},
                               {0,10,0,0,255,255,255,255}};
    SCGfxBufferDesc bd = {.type=SC_BUFFER_VERTEX,.data=verts,.size=sizeof(verts)};
    SCGfxBuffer vb = sc_gfx_make_buffer(ctx, &bd);
    FAIL_UNLESS(sc_gfx_buf_valid(vb), "vb");

    sc_gfx_begin_frame(ctx, sc_rgba(0,0,0,1));
    SCGfxDrawCmd cmd = {0};
    cmd.vertex_buf = vb;
    cmd.texture = tex;
    cmd.vertex_count = 3;
    sc_gfx_submit(ctx, &cmd, 1);
    sc_gfx_end_frame(ctx);

    sc_gfx_destroy_buffer(ctx, vb);
    sc_gfx_destroy_texture(ctx, tex);
    sc_gfx_shutdown(ctx);
    PASS("submit_with_texture");
    return 0;
}

static int test_submit_multiple_cmds(void) {
    SCGfxDesc desc = {.backend=SC_TEST_BACKEND,.width=64,.height=64};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    SCGfxVertex2D verts[3] = {{0,0,0,0,255,255,255,255},
                               {1,0,0,0,255,255,255,255},
                               {0,1,0,0,255,255,255,255}};
    SCGfxBufferDesc bd = {.type=SC_BUFFER_VERTEX,.data=verts,.size=sizeof(verts)};
    SCGfxBuffer vb = sc_gfx_make_buffer(ctx, &bd);
    FAIL_UNLESS(sc_gfx_buf_valid(vb), "vb");

    sc_gfx_begin_frame(ctx, sc_rgba(0,0,0,1));
    SCGfxDrawCmd cmds[3];
    memset(cmds, 0, sizeof(cmds));
    for (int i = 0; i < 3; i++) {
        cmds[i].vertex_buf = vb;
        cmds[i].vertex_count = 3;
    }
    sc_gfx_submit(ctx, cmds, 3);
    sc_gfx_end_frame(ctx);

    SCGfxFrameStats s = sc_gfx_frame_stats(ctx);
    FAIL_UNLESS(s.draw_calls == 3, "3 draws recorded");

    sc_gfx_destroy_buffer(ctx, vb);
    sc_gfx_shutdown(ctx);
    PASS("submit_multiple_cmds");
    return 0;
}

static int test_submit_retained_across_frames(void) {
    SCGfxDesc desc = {.backend=SC_TEST_BACKEND,.width=32,.height=32};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    SCGfxVertex2D verts[3] = {{0,0,0,0,255,255,255,255},
                               {1,0,0,0,255,255,255,255},
                               {0,1,0,0,255,255,255,255}};
    SCGfxBufferDesc bd = {.type=SC_BUFFER_VERTEX,.data=verts,.size=sizeof(verts)};
    SCGfxBuffer vb = sc_gfx_make_buffer(ctx, &bd);
    FAIL_UNLESS(sc_gfx_buf_valid(vb), "vb");

    for (int f = 0; f < 3; f++) {
        sc_gfx_begin_frame(ctx, sc_rgba(0,0,0,1));
        SCGfxDrawCmd cmd = {0};
        cmd.vertex_buf = vb;
        cmd.vertex_count = 3;
        sc_gfx_submit(ctx, &cmd, 1);
        sc_gfx_end_frame(ctx);
    }

    sc_gfx_destroy_buffer(ctx, vb);
    sc_gfx_shutdown(ctx);
    PASS("submit_retained_across_frames");
    return 0;
}

static int test_multiple_buffers_and_clear(void) {
    SCGfxDesc desc = {.backend=SC_TEST_BACKEND,.width=32,.height=32};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    SCGfxVertex2D verts[3] = {{0,0,0,0,255,255,255,255},
                               {1,0,0,0,255,255,255,255},
                               {0,1,0,0,255,255,255,255}};
    SCGfxBufferDesc bd = {.type=SC_BUFFER_VERTEX,.data=verts,.size=sizeof(verts)};
    SCGfxBuffer bufs[5];
    for (int i = 0; i < 5; i++) {
        bufs[i] = sc_gfx_make_buffer(ctx, &bd);
        FAIL_UNLESS(sc_gfx_buf_valid(bufs[i]), "many bufs");
    }
    for (int i = 0; i < 5; i++) {
        sc_gfx_destroy_buffer(ctx, bufs[i]);
    }
    sc_gfx_shutdown(ctx);
    PASS("multiple_buffers_and_clear");
    return 0;
}

/* ===== Depth/stencil tests ================================================ */

static int test_depth_pipeline_create(void) {
    SCGfxDesc desc = {.backend=SC_TEST_BACKEND,.width=32,.height=32};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    SCGfxShaderDesc sd = {.vs_source="",.fs_source=""};
    SCGfxShader shd = sc_gfx_make_shader(ctx, &sd);
    FAIL_UNLESS(sc_gfx_shd_valid(shd), "depth shader");

    SCGfxDepthState ds = {0};
    ds.depth_test  = true;
    ds.depth_write = true;
    ds.depth_compare = SC_COMPARE_LEQUAL;

    SCGfxPipelineDesc pd = {.shader=shd, .prim_type=SC_PRIM_TRIANGLES, .depth=ds};
    SCGfxPipeline pip = sc_gfx_make_pipeline(ctx, &pd);
    FAIL_UNLESS(sc_gfx_pip_valid(pip), "depth pipeline");

    sc_gfx_destroy_pipeline(ctx, pip);
    sc_gfx_destroy_shader(ctx, shd);
    sc_gfx_shutdown(ctx);
    PASS("depth_pipeline_create");
    return 0;
}

static int test_depth_submit_discard(void) {
    /* Two overlapping triangles at different depths; the farther one
       should be discarded by depth testing. */
    SCGfxDesc desc = {.backend=SC_TEST_BACKEND,.width=32,.height=32};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    SCGfxVertex2D verts[3] = {{0,0,0,0,255,0,0,255},
                              {32,0,0,0,0,255,0,255},
                              {0,32,0,0,0,0,255,255}};
    SCGfxBufferDesc bd = {.type=SC_BUFFER_VERTEX,.data=verts,.size=sizeof(verts)};
    SCGfxBuffer vb = sc_gfx_make_buffer(ctx, &bd);
    FAIL_UNLESS(sc_gfx_buf_valid(vb), "vb depth");

    SCGfxDepthState ds = {.depth_test=true,.depth_write=true,.depth_compare=SC_COMPARE_LEQUAL};
    SCGfxPipelineDesc pd = {.depth=ds};
    SCGfxPipeline pip = sc_gfx_make_pipeline(ctx, &pd);
    FAIL_UNLESS(sc_gfx_pip_valid(pip), "depth pip");

    sc_gfx_begin_frame(ctx, sc_rgba(0,0,0,1));

    /* Near triangle (depth=0.0) */
    SCGfxDrawCmd near_cmd = {0};
    near_cmd.vertex_buf = vb;
    near_cmd.vertex_count = 3;
    near_cmd.pipeline = pip;
    near_cmd.depth = 0.0f;
    sc_gfx_submit(ctx, &near_cmd, 1);

    /* Far triangle (depth=0.5) — should be discarded */
    SCGfxDrawCmd far_cmd = {0};
    far_cmd.vertex_buf = vb;
    far_cmd.vertex_count = 3;
    far_cmd.pipeline = pip;
    far_cmd.depth = 0.5f;
    sc_gfx_submit(ctx, &far_cmd, 1);

    sc_gfx_end_frame(ctx);

    sc_gfx_destroy_pipeline(ctx, pip);
    sc_gfx_destroy_buffer(ctx, vb);
    sc_gfx_shutdown(ctx);
    PASS("depth_submit_discard");
    return 0;
}

static int test_depth_no_write(void) {
    /* depth_test=true, depth_write=false — reads depth but doesn't write.
       A second drawn triangle at same depth should pass. */
    SCGfxDesc desc = {.backend=SC_TEST_BACKEND,.width=16,.height=16};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    SCGfxVertex2D verts[3] = {{0,0,0,0,255,255,255,255},
                              {16,0,0,0,255,255,255,255},
                              {0,16,0,0,255,255,255,255}};
    SCGfxBufferDesc bd = {.type=SC_BUFFER_VERTEX,.data=verts,.size=sizeof(verts)};
    SCGfxBuffer vb = sc_gfx_make_buffer(ctx, &bd);

    SCGfxDepthState ds = {.depth_test=true,.depth_write=false,.depth_compare=SC_COMPARE_LEQUAL};
    SCGfxPipelineDesc pd = {.depth=ds};
    SCGfxPipeline pip = sc_gfx_make_pipeline(ctx, &pd);

    sc_gfx_begin_frame(ctx, sc_rgba(0,0,0,1));
    for (int i = 0; i < 3; i++) {
        SCGfxDrawCmd cmd = {0};
        cmd.vertex_buf = vb;
        cmd.vertex_count = 3;
        cmd.pipeline = pip;
        cmd.depth = 0.0f;
        sc_gfx_submit(ctx, &cmd, 1);
    }
    sc_gfx_end_frame(ctx);

    sc_gfx_destroy_pipeline(ctx, pip);
    sc_gfx_destroy_buffer(ctx, vb);
    sc_gfx_shutdown(ctx);
    PASS("depth_no_write");
    return 0;
}

/* ===== Thread-safety tests ================================================ */

static int test_lock_unlock_null(void) {
    sc_gfx_lock(NULL);
    sc_gfx_unlock(NULL);
    PASS("lock_unlock_null");
    return 0;
}

static int test_thread_safe_init(void) {
    SCGfxDesc desc = {.backend = SC_TEST_BACKEND, .width = 32, .height = 32,
                       .thread_safe = true};
    SCGfxContext *ctx = NULL;
    FAIL_UNLESS(sc_ok(sc_gfx_init(&desc, &ctx)), "thread_safe init ok");
    FAIL_UNLESS(ctx != NULL, "ctx non-null");
    /* Lock/unlock cycle - must not deadlock or crash */
    sc_gfx_lock(ctx);
    sc_gfx_begin_frame(ctx, sc_rgba(0,0,0,1));
    sc_gfx_end_frame(ctx);
    sc_gfx_unlock(ctx);
    sc_gfx_shutdown(ctx);
    PASS("thread_safe_init");
    return 0;
}

/* ===== Resize test ======================================================== */

static int test_resize_software(void) {
    SCGfxDesc desc = {.backend = SC_TEST_BACKEND, .width = 16, .height = 16};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);
    FAIL_UNLESS(ctx != NULL, "resize ctx");

    SCResult r = sc_gfx_resize(ctx, 32, 32);
    FAIL_UNLESS(r == SC_OK, "resize 32x32 ok");

    r = sc_gfx_resize(ctx, 0, 0);
    FAIL_UNLESS(r != SC_OK, "resize 0x0 fails");

    r = sc_gfx_resize(ctx, 64, 64);
    FAIL_UNLESS(r == SC_OK, "resize 64x64 ok");

    sc_gfx_shutdown(ctx);
    PASS("resize_software");
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
    fail += test_buffer_data_persist();
    fail += test_buffer_update();
    fail += test_buffer_null_desc();
    fail += test_shader_pipeline_create();
    fail += test_shader_pipeline_null_desc();
    fail += test_submit_user_vertex_buffer();
    fail += test_submit_index_buffer();
    fail += test_submit_with_texture();
    fail += test_submit_multiple_cmds();
    fail += test_submit_retained_across_frames();
    fail += test_multiple_buffers_and_clear();
    fail += test_depth_pipeline_create();
    fail += test_depth_submit_discard();
    fail += test_depth_no_write();
    fail += test_lock_unlock_null();
    fail += test_thread_safe_init();
    fail += test_resize_software();
    if (!fail) printf("All gfx tests passed.\n");
    return fail;
}
