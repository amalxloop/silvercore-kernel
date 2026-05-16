/*
 * bench_gfx.c  --  Throughput benchmarks for sc_gfx.h (software backend)
 */
#define SC_GFX_IMPLEMENTATION
#define SC_LAYOUT_IMPLEMENTATION
#define SC_WIDGET_IMPLEMENTATION
#define SC_RUNTIME_IMPLEMENTATION
#define SC_GFX_BACKEND_SOFTWARE

#include "sc_gfx.h"
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static int bench_begin_end(void) {
    SCGfxDesc desc = {.backend=SC_BACKEND_SOFTWARE,.width=320,.height=240};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    const u32 N = 200;

    double t0 = now_sec();
    for (u32 i = 0; i < N; i++) {
        sc_gfx_begin_frame(ctx, SC_BLACK);
        sc_gfx_end_frame(ctx);
    }
    double t1 = now_sec();

    sc_gfx_shutdown(ctx);

    double sec = t1 - t0;
    double fps = (double)N / sec;
    printf("  begin/end 320x240: %u frames in %.3f s => %.0f fps\n",
           N, sec, fps);
    return 0;
}

static int bench_triangles(void) {
    SCGfxDesc desc = {.backend=SC_BACKEND_SOFTWARE,.width=320,.height=240};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    const u32 N = 20;
    const u32 TRIS = 500;

    SCGfxVertex2D *verts = (SCGfxVertex2D*)malloc(TRIS * 3 * sizeof(SCGfxVertex2D));
    for (u32 i = 0; i < TRIS * 3; i++) {
        verts[i].x = (f32)(rand() % 320);
        verts[i].y = (f32)(rand() % 240);
        verts[i].u = verts[i].v = 0;
        verts[i].r = 128; verts[i].g = 128; verts[i].b = 255; verts[i].a = 255;
    }

    SCGfxBufferDesc bd = {.type=SC_BUFFER_VERTEX,.usage=SC_BUFFER_STATIC,
                           .data=(u8*)verts,.size=TRIS*3*sizeof(SCGfxVertex2D)};
    SCGfxBuffer buf = sc_gfx_make_buffer(ctx, &bd);
    free(verts);

    SCGfxPipelineDesc pd = {0};
    pd.depth.depth_test = false;
    pd.depth.depth_write = false;
    SCGfxPipeline pip = sc_gfx_make_pipeline(ctx, &pd);

    SCGfxDrawCmd cmd = {0};
    cmd.pipeline = pip;
    cmd.vertex_buf = buf;
    cmd.vertex_count = TRIS * 3;

    double t0 = now_sec();
    for (u32 i = 0; i < N; i++) {
        sc_gfx_begin_frame(ctx, SC_BLACK);
        sc_gfx_submit(ctx, &cmd, 1);
        sc_gfx_end_frame(ctx);
    }
    double t1 = now_sec();

    sc_gfx_destroy_pipeline(ctx, pip);
    sc_gfx_destroy_buffer(ctx, buf);
    sc_gfx_shutdown(ctx);

    double sec = t1 - t0;
    double tris_per_sec = (double)N * (double)TRIS / sec;
    printf("  raster tris: %u frames x %u tris in %.3f s => %.0f tris/s\n",
           N, TRIS, sec, tris_per_sec);
    return 0;
}

static int bench_textured_tris(void) {
    SCGfxDesc desc = {.backend=SC_BACKEND_SOFTWARE,.width=320,.height=240};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    const u32 N = 10;
    const u32 TRIS = 500;

    u8 tex_data[64*64*4];
    for (u32 i = 0; i < 64*64*4; i++) tex_data[i] = (u8)(i % 256);
    SCGfxTextureDesc td = {.width=64,.height=64,.fmt=SC_PIXFMT_RGBA8,
                            .data=tex_data,.data_size=sizeof(tex_data)};
    SCGfxTexture tex = sc_gfx_make_texture(ctx, &td);

    SCGfxVertex2D *verts = (SCGfxVertex2D*)malloc(TRIS * 3 * sizeof(SCGfxVertex2D));
    for (u32 i = 0; i < TRIS * 3; i++) {
        verts[i].x = (f32)(rand() % 320);
        verts[i].y = (f32)(rand() % 240);
        verts[i].u = (f32)(rand() % 64) / 64.0f;
        verts[i].v = (f32)(rand() % 64) / 64.0f;
        verts[i].r = 255; verts[i].g = 255; verts[i].b = 255; verts[i].a = 255;
    }

    SCGfxBufferDesc bd = {.type=SC_BUFFER_VERTEX,.usage=SC_BUFFER_STATIC,
                           .data=(u8*)verts,.size=TRIS*3*sizeof(SCGfxVertex2D)};
    SCGfxBuffer buf = sc_gfx_make_buffer(ctx, &bd);
    free(verts);

    SCGfxPipelineDesc pd = {0};
    SCGfxPipeline pip = sc_gfx_make_pipeline(ctx, &pd);

    SCGfxDrawCmd cmd = {0};
    cmd.pipeline = pip;
    cmd.vertex_buf = buf;
    cmd.texture = tex;
    cmd.vertex_count = TRIS * 3;

    double t0 = now_sec();
    for (u32 i = 0; i < N; i++) {
        sc_gfx_begin_frame(ctx, SC_BLACK);
        sc_gfx_submit(ctx, &cmd, 1);
        sc_gfx_end_frame(ctx);
    }
    double t1 = now_sec();

    sc_gfx_destroy_texture(ctx, tex);
    sc_gfx_destroy_pipeline(ctx, pip);
    sc_gfx_destroy_buffer(ctx, buf);
    sc_gfx_shutdown(ctx);

    double sec = t1 - t0;
    double tris_per_sec = (double)N * (double)TRIS / sec;
    printf("  textured tris: %u frames x %u tris in %.3f s => %.0f tris/s\n",
           N, TRIS, sec, tris_per_sec);
    return 0;
}

static int bench_indexed_tris(void) {
    SCGfxDesc desc = {.backend=SC_BACKEND_SOFTWARE,.width=320,.height=240};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    const u32 N = 20;
    const u32 TRIS = 500;

    SCGfxVertex2D *verts = (SCGfxVertex2D*)malloc(TRIS * 3 * sizeof(SCGfxVertex2D));
    for (u32 i = 0; i < TRIS * 3; i++) {
        verts[i].x = (f32)(rand() % 320);
        verts[i].y = (f32)(rand() % 240);
        verts[i].u = verts[i].v = 0;
        verts[i].r = 64; verts[i].g = 128; verts[i].b = 255; verts[i].a = 255;
    }

    SCGfxBufferDesc vbd = {.type=SC_BUFFER_VERTEX,.usage=SC_BUFFER_STATIC,
                            .data=(u8*)verts,.size=TRIS*3*sizeof(SCGfxVertex2D)};
    SCGfxBuffer buf = sc_gfx_make_buffer(ctx, &vbd);
    free(verts);

    u32 *indices = (u32*)malloc(TRIS * 3 * sizeof(u32));
    for (u32 i = 0; i < TRIS * 3; i++) indices[i] = i;
    SCGfxBufferDesc ibd = {.type=SC_BUFFER_INDEX,.usage=SC_BUFFER_STATIC,
                            .data=(u8*)indices,.size=TRIS*3*sizeof(u32)};
    SCGfxBuffer ibuf = sc_gfx_make_buffer(ctx, &ibd);
    free(indices);

    SCGfxPipelineDesc pd = {0};
    SCGfxPipeline pip = sc_gfx_make_pipeline(ctx, &pd);

    SCGfxDrawCmd cmd = {0};
    cmd.pipeline = pip;
    cmd.vertex_buf = buf;
    cmd.index_buf = ibuf;
    cmd.index_count = TRIS * 3;

    double t0 = now_sec();
    for (u32 i = 0; i < N; i++) {
        sc_gfx_begin_frame(ctx, SC_BLACK);
        sc_gfx_submit(ctx, &cmd, 1);
        sc_gfx_end_frame(ctx);
    }
    double t1 = now_sec();

    sc_gfx_destroy_buffer(ctx, ibuf);
    sc_gfx_destroy_buffer(ctx, buf);
    sc_gfx_destroy_pipeline(ctx, pip);
    sc_gfx_shutdown(ctx);

    double sec = t1 - t0;
    double tris_per_sec = (double)N * (double)TRIS / sec;
    printf("  indexed tris: %u frames x %u tris in %.3f s => %.0f tris/s\n",
           N, TRIS, sec, tris_per_sec);
    return 0;
}

static int bench_msaa(void) {
    SCGfxDesc desc = {.backend=SC_BACKEND_SOFTWARE,.width=160,.height=120,.sample_count=4};
    SCGfxContext *ctx = NULL;
    sc_gfx_init(&desc, &ctx);

    const u32 N = 10;
    const u32 TRIS = 200;

    SCGfxVertex2D *verts = (SCGfxVertex2D*)malloc(TRIS * 3 * sizeof(SCGfxVertex2D));
    for (u32 i = 0; i < TRIS * 3; i++) {
        verts[i].x = (f32)(rand() % 160);
        verts[i].y = (f32)(rand() % 120);
        verts[i].u = verts[i].v = 0;
        verts[i].r = 200; verts[i].g = 100; verts[i].b = 50; verts[i].a = 255;
    }

    SCGfxBufferDesc bd = {.type=SC_BUFFER_VERTEX,.usage=SC_BUFFER_STATIC,
                           .data=(u8*)verts,.size=TRIS*3*sizeof(SCGfxVertex2D)};
    SCGfxBuffer buf = sc_gfx_make_buffer(ctx, &bd);
    free(verts);

    SCGfxPipelineDesc pd = {0};
    SCGfxPipeline pip = sc_gfx_make_pipeline(ctx, &pd);

    SCGfxDrawCmd cmd = {0};
    cmd.pipeline = pip;
    cmd.vertex_buf = buf;
    cmd.vertex_count = TRIS * 3;

    double t0 = now_sec();
    for (u32 i = 0; i < N; i++) {
        sc_gfx_begin_frame(ctx, SC_BLACK);
        sc_gfx_submit(ctx, &cmd, 1);
        sc_gfx_end_frame(ctx);
    }
    double t1 = now_sec();

    sc_gfx_destroy_pipeline(ctx, pip);
    sc_gfx_destroy_buffer(ctx, buf);
    sc_gfx_shutdown(ctx);

    double sec = t1 - t0;
    double tris_per_sec = (double)N * (double)TRIS / sec;
    printf("  4x MSAA tris: %u frames x %u tris in %.3f s => %.0f tris/s\n",
           N, TRIS, sec, tris_per_sec);
    return 0;
}

int main(void) {
    printf("sc_gfx benchmarks (software backend)\n");
    printf("------------------------------------\n");
    bench_begin_end();
    bench_triangles();
    bench_textured_tris();
    bench_indexed_tris();
    bench_msaa();
    printf("------------------------------------\n");
    printf("Done.\n");
    return 0;
}
