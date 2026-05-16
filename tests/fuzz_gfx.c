/*
 * fuzz_gfx.c  --  Fuzz harness for sc_gfx resource operations
 *
 * Randomly creates / destroys buffers, textures, shaders, pipelines and
 * submits draw commands.  Use with AddressSanitizer to catch
 * use-after-free, double-free, and slot-leak bugs in the free-list
 * allocator and resource tables.
 *
 * Build:
 *   clang -fsanitize=address -I include -I tools \
 *         tests/fuzz_gfx.c -lm -o fuzz_gfx
 *
 * Usage:
 *   ./fuzz_gfx [iterations] [seed]
 */
#define SC_GFX_IMPLEMENTATION
#define SC_GFX_BACKEND_SOFTWARE
#include "sc_types.h"
#include "sc_gfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static u32 g_rng_state;

static u32 xorshift32(void) {
    u32 x = g_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng_state = x;
    return x;
}

static int rand_range(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(xorshift32() % (u32)(hi - lo + 1));
}

int main(int argc, char **argv) {
    u32 iters  = (argc > 1) ? (u32)atoi(argv[1]) : 10000;
    u32 seed   = (argc > 2) ? (u32)atoi(argv[2]) : (u32)time(NULL);
    g_rng_state = seed ? seed : 1;

    printf("fuzz_gfx: %u iterations, seed=%u\n", iters, seed);

    SCGfxDesc desc = {.backend=SC_BACKEND_SOFTWARE,.width=64,.height=48};
    SCGfxContext *ctx = NULL;
    SCResult r = sc_gfx_init(&desc, &ctx);
    if (r != SC_OK) {
        fprintf(stderr, "sc_gfx_init failed: %d\n", r);
        return 1;
    }

    u8 dummy_data[64*64*4];
    for (u32 i = 0; i < sizeof(dummy_data); i++)
        dummy_data[i] = (u8)(xorshift32() & 0xFF);

    int buf_alive   = 0;
    int tex_alive   = 0;
    int shd_alive   = 0;
    int pip_alive   = 0;
    int buf_peak    = 0;
    int tex_peak    = 0;
    int shd_peak    = 0;
    int pip_peak    = 0;
    int buf_count   = 0;
    int tex_count   = 0;
    int shd_count   = 0;
    int pip_count   = 0;

#define MAX_OPS 16
    SCGfxBuffer   bufs[MAX_OPS];
    SCGfxTexture  texs[MAX_OPS];
    SCGfxShader   shds[MAX_OPS];
    SCGfxPipeline pips[MAX_OPS];

    for (u32 i = 0; i < MAX_OPS; i++) {
        bufs[i].id = 0; texs[i].id = 0;
        shds[i].id = 0; pips[i].id = 0;
    }

    for (u32 iter = 0; iter < iters; iter++) {
        int op = rand_range(0, 11);

        switch (op) {
            /* Buffer ops (0-2) */
            case 0: {
                u32 sz = (u32)rand_range(4, 1024);
                SCGfxBufferDesc bd = {.type=(SCBufferType)rand_range(0,2),
                                       .usage=(SCBufferUsage)rand_range(0,2),
                                       .data=dummy_data,.size=sz};
                int slot = rand_range(0, MAX_OPS-1);
                if (sc_gfx_buf_valid(bufs[slot]))
                    { sc_gfx_destroy_buffer(ctx, bufs[slot]); buf_alive--; }
                bufs[slot] = sc_gfx_make_buffer(ctx, &bd);
                if (sc_gfx_buf_valid(bufs[slot])) {
                    buf_alive++;
                    if (buf_alive > buf_peak) buf_peak = buf_alive;
                }
                buf_count++;
                break;
            }
            case 1: {
                if (buf_alive > 0) {
                    int slot = rand_range(0, MAX_OPS-1);
                    while (!sc_gfx_buf_valid(bufs[slot]))
                        slot = (slot + 1) % MAX_OPS;
                    sc_gfx_destroy_buffer(ctx, bufs[slot]);
                    bufs[slot].id = 0;
                    buf_alive--;
                }
                break;
            }
            case 2: {
                if (buf_alive > 0) {
                    int slot = rand_range(0, MAX_OPS-1);
                    while (!sc_gfx_buf_valid(bufs[slot]))
                        slot = (slot + 1) % MAX_OPS;
                    u32 sz = (u32)rand_range(4, 512);
                    u8 upd[512];
                    for (u32 j = 0; j < sz && j < sizeof(upd); j++)
                        upd[j] = (u8)(xorshift32() & 0xFF);
                    sc_gfx_update_buffer(ctx, bufs[slot], upd, sz);
                }
                break;
            }

            /* Texture ops (3-5) */
            case 3: {
                u32 tw = (u32)rand_range(1,64);
                u32 th = (u32)rand_range(1,64);
                SCPixelFormat tf = (SCPixelFormat)rand_range(0,4);
                u32 bpp = (tf == SC_PIXFMT_R8) ? 1 : (tf == SC_PIXFMT_RG8 ? 2 : 4);
                SCGfxTextureDesc td = {.width=tw,.height=th,
                                        .fmt=tf,
                                        .data=dummy_data,
                                        .data_size=tw*th*bpp};
                int slot = rand_range(0, MAX_OPS-1);
                if (sc_gfx_tex_valid(texs[slot]))
                    { sc_gfx_destroy_texture(ctx, texs[slot]); tex_alive--; }
                texs[slot] = sc_gfx_make_texture(ctx, &td);
                if (sc_gfx_tex_valid(texs[slot])) {
                    tex_alive++;
                    if (tex_alive > tex_peak) tex_peak = tex_alive;
                }
                tex_count++;
                break;
            }
            case 4: {
                if (tex_alive > 0) {
                    int slot = rand_range(0, MAX_OPS-1);
                    while (!sc_gfx_tex_valid(texs[slot]))
                        slot = (slot + 1) % MAX_OPS;
                    sc_gfx_destroy_texture(ctx, texs[slot]);
                    texs[slot].id = 0;
                    tex_alive--;
                }
                break;
            }

            /* Shader ops (5-6) */
            case 5: {
                SCGfxShaderDesc sd = {0};
                int slot = rand_range(0, MAX_OPS-1);
                if (sc_gfx_shd_valid(shds[slot]))
                    { sc_gfx_destroy_shader(ctx, shds[slot]); shd_alive--; }
                shds[slot] = sc_gfx_make_shader(ctx, &sd);
                if (sc_gfx_shd_valid(shds[slot])) {
                    shd_alive++;
                    if (shd_alive > shd_peak) shd_peak = shd_alive;
                }
                shd_count++;
                break;
            }
            case 6: {
                if (shd_alive > 0) {
                    int slot = rand_range(0, MAX_OPS-1);
                    while (!sc_gfx_shd_valid(shds[slot]))
                        slot = (slot + 1) % MAX_OPS;
                    sc_gfx_destroy_shader(ctx, shds[slot]);
                    shds[slot].id = 0;
                    shd_alive--;
                }
                break;
            }

            /* Pipeline ops (7-8) */
            case 7: {
                SCGfxPipelineDesc pd = {0};
                pd.depth.depth_test  = (bool)rand_range(0,1);
                pd.depth.depth_write = (bool)rand_range(0,1);
                pd.blend.enabled     = (bool)rand_range(0,1);
                int slot = rand_range(0, MAX_OPS-1);
                if (sc_gfx_pip_valid(pips[slot]))
                    { sc_gfx_destroy_pipeline(ctx, pips[slot]); pip_alive--; }
                pips[slot] = sc_gfx_make_pipeline(ctx, &pd);
                if (sc_gfx_pip_valid(pips[slot])) {
                    pip_alive++;
                    if (pip_alive > pip_peak) pip_peak = pip_alive;
                }
                pip_count++;
                break;
            }
            case 8: {
                if (pip_alive > 0) {
                    int slot = rand_range(0, MAX_OPS-1);
                    while (!sc_gfx_pip_valid(pips[slot]))
                        slot = (slot + 1) % MAX_OPS;
                    sc_gfx_destroy_pipeline(ctx, pips[slot]);
                    pips[slot].id = 0;
                    pip_alive--;
                }
                break;
            }

            /* Draw submit (9) */
            case 9: {
                SCGfxBuffer vb = {0};
                for (int s = 0; s < MAX_OPS; s++) {
                    if (sc_gfx_buf_valid(bufs[s])) { vb = bufs[s]; break; }
                }
                SCGfxPipeline pip = {0};
                for (int s = 0; s < MAX_OPS; s++) {
                    if (sc_gfx_pip_valid(pips[s])) { pip = pips[s]; break; }
                }
                if (sc_gfx_buf_valid(vb)) {
                    SCGfxDrawCmd cmd = {0};
                    cmd.pipeline = pip;
                    cmd.vertex_buf = vb;
                    cmd.vertex_count = (u32)rand_range(3, 64);
                    cmd.depth = (f32)rand_range(0, 100) / 100.0f;
                    for (int j = 0; j < rand_range(0, 3); j++) {
                        sc_gfx_begin_frame(ctx, SC_BLACK);
                        sc_gfx_submit(ctx, &cmd, 1);
                        sc_gfx_end_frame(ctx);
                    }
                }
                break;
            }

            /* Frame ops (10-11) */
            case 10:
                sc_gfx_begin_frame(ctx, SC_BLACK);
                sc_gfx_end_frame(ctx);
                break;
            case 11: {
                /* Resize fuzz */
                u32 w = (u32)rand_range(16, 640);
                u32 h = (u32)rand_range(16, 480);
                sc_gfx_resize(ctx, w, h);
                break;
            }
        }

        /* Periodically clean all resources */
        if (iter > 0 && iter % 500 == 0) {
            for (int s = 0; s < MAX_OPS; s++) {
                if (sc_gfx_buf_valid(bufs[s]))  { sc_gfx_destroy_buffer(ctx, bufs[s]); bufs[s].id = 0; buf_alive--; }
                if (sc_gfx_tex_valid(texs[s]))  { sc_gfx_destroy_texture(ctx, texs[s]); texs[s].id = 0; tex_alive--; }
                if (sc_gfx_shd_valid(shds[s]))  { sc_gfx_destroy_shader(ctx, shds[s]); shds[s].id = 0; shd_alive--; }
                if (sc_gfx_pip_valid(pips[s]))  { sc_gfx_destroy_pipeline(ctx, pips[s]); pips[s].id = 0; pip_alive--; }
            }
        }
    }

    /* Final cleanup */
    for (int s = 0; s < MAX_OPS; s++) {
        if (sc_gfx_buf_valid(bufs[s]))
            sc_gfx_destroy_buffer(ctx, bufs[s]);
        if (sc_gfx_tex_valid(texs[s]))
            sc_gfx_destroy_texture(ctx, texs[s]);
        if (sc_gfx_shd_valid(shds[s]))
            sc_gfx_destroy_shader(ctx, shds[s]);
        if (sc_gfx_pip_valid(pips[s]))
            sc_gfx_destroy_pipeline(ctx, pips[s]);
    }

    sc_gfx_shutdown(ctx);

    printf("  Buffers  : %d created, %d peak alive\n", buf_count, buf_peak);
    printf("  Textures : %d created, %d peak alive\n", tex_count, tex_peak);
    printf("  Shaders  : %d created, %d peak alive\n", shd_count, shd_peak);
    printf("  Pipelines: %d created, %d peak alive\n", pip_count, pip_peak);
    printf("fuzz_gfx: OK (no crashes)\n");
    return 0;
}
