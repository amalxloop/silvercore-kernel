/*
 * sc_font.h  --  SilverCore Font Rendering Module
 *
 * Uses stb_truetype for font loading and glyph rasterization.
 * Each SCFont represents one font face at a specific pixel size.
 * Glyphs are lazily rendered and cached in a packed atlas texture.
 *
 * #define SC_FONT_IMPLEMENTATION
 * #include "sc_font.h"
 */
#ifndef SC_FONT_H
#define SC_FONT_H

#include "sc_types.h"
#include "sc_gfx.h"

#define SC_FONT_ATLAS_W        1024
#define SC_FONT_ATLAS_H        1024
#define SC_FONT_MAX_GLYPH_CACHE 512
#define SC_FONT_ATLAS_FORMAT   SC_PIXFMT_R8

typedef struct SCFontGlyph {
    u32  codepoint;
    u16  x, y;          /* pixel position in atlas */
    u16  w, h;          /* glyph bitmap size */
    f32  xoff, yoff;    /* offset from cursor */
    f32  advance;        /* x advance */
    f32  s0, t0, s1, t1; /* normalized atlas UV */
} SCFontGlyph;

/* Opaque font handle. The full struct is defined in the implementation section
 * because it depends on stb_truetype types that are not available here. */
typedef struct SCFont SCFont;

SCFont *sc_font_create(const void *ttf_data, usize ttf_size, f32 pixel_height);
void    sc_font_destroy(SCFont *font);
f32     sc_font_text_width(SCFont *font, const char *text);

/* Uploads the font's glyph atlas to a GPU texture. Call after creating the
 * font and whenever new glyphs have been cached. Returns the texture handle. */
SCGfxTexture sc_font_upload_atlas(SCFont *font, SCGfxContext *gfx);

void sc_font_render_text(SCGfxContext *gfx, SCFont *font,
                          const char *text, f32 x, f32 y, SCColor color,
                          SCGfxTexture atlas_tex);

#ifdef SC_FONT_IMPLEMENTATION

#define STB_TRUETYPE_IMPLEMENTATION
#include "../tools/stb_truetype.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

struct SCFont {
    stbtt_fontinfo  info;
    f32             size, scale;
    int             ascent, descent, line_gap;

    u8              atlas[SC_FONT_ATLAS_W * SC_FONT_ATLAS_H];
    u32             atlas_cursor_x, atlas_cursor_y, atlas_row_h;

    SCFontGlyph     glyph_cache[SC_FONT_MAX_GLYPH_CACHE];
    u32             glyph_cache_count;
    u32             glyph_ins_idx;
};

static SCFontGlyph *_sc_font_find_glyph(SCFont *font, u32 codepoint) {
    for (u32 i = 0; i < font->glyph_cache_count; i++) {
        if (font->glyph_cache[i].codepoint == codepoint)
            return &font->glyph_cache[i];
    }
    return NULL;
}

static SCFontGlyph *_sc_font_cache_glyph(SCFont *font, u32 codepoint) {
    /* Get glyph bitmap */
    int gw, gh, goff_x, goff_y;
    float sc = font->scale;
    unsigned char *bitmap = stbtt_GetCodepointBitmap(
        &font->info, sc, sc, (int)codepoint, &gw, &gh, &goff_x, &goff_y);

    int advance;
    stbtt_GetCodepointHMetrics(&font->info, (int)codepoint, &advance, NULL);

    if (!bitmap) {
        /* Could not render – register a zero-size slot so we don't retry */
        SCFontGlyph *g = &font->glyph_cache[font->glyph_ins_idx % SC_FONT_MAX_GLYPH_CACHE];
        font->glyph_ins_idx++;
        if (font->glyph_cache_count < SC_FONT_MAX_GLYPH_CACHE)
            font->glyph_cache_count++;
        memset(g, 0, sizeof(*g));
        g->codepoint = codepoint;
        g->w = g->h = 0;
        return g;
    }

    /* Pack into atlas (simple row-based packer) */
    if (font->atlas_cursor_x + (u32)gw > SC_FONT_ATLAS_W) {
        font->atlas_cursor_x = 0;
        font->atlas_cursor_y += font->atlas_row_h + 1;
        font->atlas_row_h = 0;
    }
    if (font->atlas_cursor_y + (u32)gh > SC_FONT_ATLAS_H) {
        /* Atlas full: just return best-effort */
        stbtt_FreeBitmap(bitmap, NULL);
        return NULL;
    }

    if ((u32)gh > font->atlas_row_h) font->atlas_row_h = (u32)gh;

    /* Blit glyph bitmap into atlas */
    for (int row = 0; row < gh; row++) {
        u32 dst_off = (font->atlas_cursor_y + (u32)row) * SC_FONT_ATLAS_W + font->atlas_cursor_x;
        u32 src_off = (u32)row * (u32)gw;
        memcpy(&font->atlas[dst_off], &bitmap[src_off], (usize)gw);
    }

    stbtt_FreeBitmap(bitmap, NULL);

    /* Store in cache (circular eviction) */
    SCFontGlyph *g = &font->glyph_cache[font->glyph_ins_idx % SC_FONT_MAX_GLYPH_CACHE];
    font->glyph_ins_idx++;
    if (font->glyph_cache_count < SC_FONT_MAX_GLYPH_CACHE)
        font->glyph_cache_count++;

    g->codepoint = codepoint;
    g->x         = font->atlas_cursor_x;
    g->y         = font->atlas_cursor_y;
    g->w         = (u16)gw;
    g->h         = (u16)gh;
    g->xoff      = (f32)goff_x;
    g->yoff      = (f32)goff_y;
    g->advance   = (f32)advance * sc;
    g->s0        = (f32)font->atlas_cursor_x / (f32)SC_FONT_ATLAS_W;
    g->t0        = (f32)font->atlas_cursor_y / (f32)SC_FONT_ATLAS_H;
    g->s1        = (f32)(font->atlas_cursor_x + gw) / (f32)SC_FONT_ATLAS_W;
    g->t1        = (f32)(font->atlas_cursor_y + gh) / (f32)SC_FONT_ATLAS_H;

    font->atlas_cursor_x += (u32)gw + 1;

    return g;
}

SCFont *sc_font_create(const void *ttf_data, usize ttf_size, f32 pixel_height) {
    if (!ttf_data || ttf_size < 4) return NULL;
    SCFont *font = (SCFont *)calloc(1, sizeof(SCFont));
    if (!font) return NULL;

    int offset = stbtt_GetFontOffsetForIndex((const unsigned char *)ttf_data, 0);
    if (offset < 0) { free(font); return NULL; }

    if (!stbtt_InitFont(&font->info, (const unsigned char *)ttf_data, offset)) {
        free(font);
        return NULL;
    }

    font->size  = pixel_height;
    font->scale = stbtt_ScaleForPixelHeight(&font->info, pixel_height);
    stbtt_GetFontVMetrics(&font->info, &font->ascent, &font->descent, &font->line_gap);

    /* Clear atlas to zeros (transparent) */
    memset(font->atlas, 0, sizeof(font->atlas));

    return font;
}

void sc_font_destroy(SCFont *font) {
    if (font) free(font);
}

f32 sc_font_text_width(SCFont *font, const char *text) {
    f32 w = 0.0f;
    while (*text) {
        u32 cp = (u8)*text;
        SCFontGlyph *g = _sc_font_find_glyph(font, cp);
        if (!g) g = _sc_font_cache_glyph(font, cp);
        if (g) w += g->advance;
        text++;
    }
    return w;
}

SCGfxTexture sc_font_upload_atlas(SCFont *font, SCGfxContext *gfx) {
    SCGfxTextureDesc td;
    memset(&td, 0, sizeof(td));
    td.width     = SC_FONT_ATLAS_W;
    td.height    = SC_FONT_ATLAS_H;
    td.fmt       = SC_FONT_ATLAS_FORMAT;
    td.data      = font->atlas;
    td.data_size = sizeof(font->atlas);
    td.label     = "font_atlas";
    return sc_gfx_make_texture(gfx, &td);
}

void sc_font_render_text(SCGfxContext *gfx, SCFont *font,
                          const char *text, f32 x, f32 y, SCColor color,
                          SCGfxTexture atlas_tex)
{
    f32 cursor_x = x;
    /* Baseline: y + ascent * scale */
    f32 baseline_y = y + (f32)font->ascent * font->scale;

    while (*text) {
        u32 cp = (u8)*text;
        if (cp == '\n') {
            cursor_x = x;
            baseline_y += (f32)(font->descent - font->ascent + font->line_gap) * font->scale;
            text++;
            continue;
        }

        SCFontGlyph *g = _sc_font_find_glyph(font, cp);
        if (!g) g = _sc_font_cache_glyph(font, cp);

        if (g && g->w > 0 && g->h > 0) {
            f32 gx = cursor_x + g->xoff;
            f32 gy = baseline_y + g->yoff;
            SCRect2f dest = {gx, gy, (f32)g->w, (f32)g->h};
            sc_gfx_draw_sprite(gfx, dest, atlas_tex, color);
        }

        if (g) cursor_x += g->advance;
        text++;
    }
}

#endif /* SC_FONT_IMPLEMENTATION */
#endif /* SC_FONT_H */
