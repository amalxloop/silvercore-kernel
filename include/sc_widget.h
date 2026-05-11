/*
 * sc_widget.h  --  SilverCore Widget & Scene Graph
 *
 * A retained-mode widget tree that bridges the layout engine (sc_layout)
 * and the graphics layer (sc_gfx).
 *
 * Widget types (MVP)
 *   SC_WIDGET_RECT      solid/border rectangle
 *   SC_WIDGET_TEXT      single-line UTF-8 label (raster font)
 *   SC_WIDGET_IMAGE     stretched / tiled texture
 *   SC_WIDGET_CANVAS    custom paint callback
 *   SC_WIDGET_CONTAINER flex container (no visual output itself)
 *
 * Interaction model
 *   Each widget can register:
 *     on_hover / on_press / on_release / on_scroll
 *   Events are dispatched via sc_scene_dispatch_event().
 *
 * Animation
 *   Properties (x, y, w, h, alpha, color) are animatable via
 *   SCWidgetAnim.  The scene ticks animations during sc_scene_update().
 *
 * Memory
 *   All widgets live in a SCPool (fixed-size slab).
 *   Their string labels live in a per-scene SCArena.
 *
 * #define SC_WIDGET_IMPLEMENTATION in exactly one .c file.
 */
#ifndef SC_WIDGET_H
#define SC_WIDGET_H

#include "sc_types.h"
#include "sc_gfx.h"
#include "sc_layout.h"
#include "sc_arena.h"

/* Forward declaration for font system */
typedef struct SCFont SCFont;

/* -------------------------------------------------------------------------
 * Limits
 * ---------------------------------------------------------------------- */
#define SC_SCENE_MAX_WIDGETS  8192   /* 1024 cells × 4 sub-widgets + overhead */
#define SC_SCENE_MAX_ANIMS    1024
#define SC_WIDGET_MAX_TEXT    128

/* -------------------------------------------------------------------------
 * Widget types
 * ---------------------------------------------------------------------- */
typedef enum SCWidgetType {
    SC_WIDGET_CONTAINER = 0,
    SC_WIDGET_RECT      = 1,
    SC_WIDGET_TEXT      = 2,
    SC_WIDGET_IMAGE     = 3,
    SC_WIDGET_CANVAS    = 4,
} SCWidgetType;

/* -------------------------------------------------------------------------
 * Event types
 * ---------------------------------------------------------------------- */
typedef enum SCEventType {
    SC_EVENT_MOUSE_MOVE   = 0,
    SC_EVENT_MOUSE_DOWN   = 1,
    SC_EVENT_MOUSE_UP     = 2,
    SC_EVENT_MOUSE_SCROLL = 3,
    SC_EVENT_KEY_DOWN     = 4,
    SC_EVENT_KEY_UP       = 5,
    SC_EVENT_RESIZE       = 6,
} SCEventType;

typedef struct SCEvent {
    SCEventType type;
    f32  mouse_x, mouse_y;
    f32  scroll_x, scroll_y;
    i32  key_code;
    u32  modifiers;
    u32  window_w, window_h;
} SCEvent;

/* -------------------------------------------------------------------------
 * Animation (simple linear tween on a single f32 property)
 * ---------------------------------------------------------------------- */
typedef enum SCAnimProp {
    SC_ANIM_X     = 0,
    SC_ANIM_Y     = 1,
    SC_ANIM_W     = 2,
    SC_ANIM_H     = 3,
    SC_ANIM_ALPHA = 4,
    SC_ANIM_RED   = 5,
    SC_ANIM_GREEN = 6,
    SC_ANIM_BLUE  = 7,
} SCAnimProp;

typedef enum SCEasing {
    SC_EASE_LINEAR   = 0,
    SC_EASE_IN_QUAD  = 1,
    SC_EASE_OUT_QUAD = 2,
    SC_EASE_INOUT    = 3,
    SC_EASE_SPRING   = 4,
} SCEasing;

typedef struct SCWidgetAnim {
    i32       widget_id;   /* target widget (-1 = free slot)   */
    SCAnimProp prop;
    SCEasing   easing;
    f32        from, to;
    f32        elapsed, duration;
    bool       loop;
    bool       active;
} SCWidgetAnim;

/* -------------------------------------------------------------------------
 * Widget
 * ---------------------------------------------------------------------- */
struct SCScene;   /* forward declaration */

typedef void (*SCPaintFn)(struct SCScene*, i32 widget_id,
                           SCGfxContext*, SCRect2f bounds);
typedef void (*SCEventFn)(struct SCScene*, i32 widget_id,
                           const SCEvent *ev);

typedef struct SCWidget {
    i32          id;           /* index in scene->pool                */
    SCWidgetType type;
    bool         visible;
    bool         interactive;

    /* Visual properties (animated via SCWidgetAnim) */
    f32          x, y, w, h;
    f32          alpha;
    SCColor      color;
    SCColor      border_color;
    f32          border_width;
    f32          corner_radius; /* future: rounded rects              */

    /* Text (SC_WIDGET_TEXT) */
    char         text[SC_WIDGET_MAX_TEXT];
    f32          font_size;
    SCColor      text_color;

    /* Image (SC_WIDGET_IMAGE) */
    SCGfxTexture texture;

    /* Custom paint (SC_WIDGET_CANVAS) */
    SCPaintFn    on_paint;

    /* Event callbacks */
    SCEventFn    on_hover;
    SCEventFn    on_press;
    SCEventFn    on_release;
    SCEventFn    on_scroll;

    /* User data pointer (app-defined) */
    void        *user_data;

    /* Tree links (mirrors SCLayoutTree) */
    i32          layout_node;  /* index into scene->layout           */
} SCWidget;

/* -------------------------------------------------------------------------
 * Scene
 * ---------------------------------------------------------------------- */
typedef struct SCScene {
    /* Memory */
    SCArena        arena;        /* widget string / misc scratch       */
    u8             _arena_buf[SC_MB(2)];

    /* Widgets */
    SCWidget       widgets[SC_SCENE_MAX_WIDGETS];
    u32            widget_count;

    /* Animations */
    SCWidgetAnim   anims[SC_SCENE_MAX_ANIMS];
    u32            anim_count;

    /* Layout */
    SCLayoutTree   layout;

    /* Graphics context (borrowed) */
    SCGfxContext  *gfx;

    /* Default font for text widgets */
    SCFont        *font;
    SCGfxTexture   font_atlas_tex;

    /* Viewport */
    f32            viewport_w, viewport_h;

    /* Frame timing */
    f64            time;       /* seconds since init                  */
    f32            dt;         /* last frame delta in seconds         */
} SCScene;

/* -------------------------------------------------------------------------
 * Scene API
 * ---------------------------------------------------------------------- */
void  sc_scene_init   (SCScene *s, SCGfxContext *gfx, f32 w, f32 h);
void  sc_scene_resize (SCScene *s, f32 w, f32 h);
void  sc_scene_update (SCScene *s, f32 dt);
void  sc_scene_render (SCScene *s);
void  sc_scene_dispatch_event(SCScene *s, const SCEvent *ev);

/* Widget creation */
i32   sc_widget_add   (SCScene *s, i32 parent_layout, SCWidgetType type,
                        SCLayoutStyle layout);
i32   sc_widget_rect  (SCScene *s, i32 parent, SCColor color, SCLayoutStyle ls);
i32   sc_widget_text  (SCScene *s, i32 parent, const char *txt,
                        f32 font_size, SCColor color, SCLayoutStyle ls);
i32   sc_widget_image (SCScene *s, i32 parent, SCGfxTexture tex, SCLayoutStyle ls);
i32   sc_widget_canvas(SCScene *s, i32 parent, SCPaintFn fn, SCLayoutStyle ls);

/* Widget mutation */
void  sc_widget_set_text (SCScene *s, i32 id, const char *txt);
void  sc_widget_set_color(SCScene *s, i32 id, SCColor c);
void  sc_widget_set_alpha(SCScene *s, i32 id, f32 a);
void  sc_widget_set_visible(SCScene *s, i32 id, bool v);

/* Font */
void  sc_scene_set_font(SCScene *s, SCFont *font);

/* Animation */
i32   sc_anim_push(SCScene *s, i32 widget_id,
                   SCAnimProp prop, f32 from, f32 to,
                   f32 duration_secs, SCEasing easing, bool loop);
void  sc_anim_stop(SCScene *s, i32 anim_id);

/* -------------------------------------------------------------------------
 * Implementation
 * ---------------------------------------------------------------------- */
#ifdef SC_WIDGET_IMPLEMENTATION

#include <string.h>
#include <math.h>
#include <stdio.h>
#include "sc_font.h"

/* ---- Easing functions ------------------------------------------------- */
static f32 _sc_ease(SCEasing e, f32 t) {
    t = SC_CLAMP(t, 0.0f, 1.0f);
    switch (e) {
        case SC_EASE_LINEAR:   return t;
        case SC_EASE_IN_QUAD:  return t * t;
        case SC_EASE_OUT_QUAD: return t * (2.0f - t);
        case SC_EASE_INOUT:    return t < 0.5f ? 2*t*t : -1+(4-2*t)*t;
        case SC_EASE_SPRING: {
            /* damped spring: overshoot then settle */
            f32 c = 1.0f - expf(-6.0f * t) * cosf(20.0f * t);
            return c;
        }
        default: return t;
    }
}

static f32 *_sc_widget_prop_ptr(SCScene *s, i32 wid, SCAnimProp p) {
    /* Bug #8: guard against out-of-bounds widget id */
    if (wid < 0 || (u32)wid >= s->widget_count) return NULL;
    SCWidget *w = &s->widgets[wid];
    switch (p) {
        case SC_ANIM_X:     return &w->x;
        case SC_ANIM_Y:     return &w->y;
        case SC_ANIM_W:     return &w->w;
        case SC_ANIM_H:     return &w->h;
        case SC_ANIM_ALPHA: return &w->alpha;
        case SC_ANIM_RED:   return &w->color.r;
        case SC_ANIM_GREEN: return &w->color.g;
        case SC_ANIM_BLUE:  return &w->color.b;
        default:            return NULL;
    }
}

/* ---- Init / resize ---------------------------------------------------- */
void sc_scene_init(SCScene *s, SCGfxContext *gfx, f32 w, f32 h) {
    memset(s, 0, sizeof(*s));
    sc_arena_init(&s->arena, s->_arena_buf, sizeof(s->_arena_buf));
    sc_layout_tree_init(&s->layout);
    s->gfx        = gfx;
    s->viewport_w = w;
    s->viewport_h = h;
}

void sc_scene_resize(SCScene *s, f32 w, f32 h) {
    s->viewport_w = w;
    s->viewport_h = h;
}

/* ---- Widget creation -------------------------------------------------- */
i32 sc_widget_add(SCScene *s, i32 parent_layout, SCWidgetType type,
                   SCLayoutStyle ls) {
    SC_ASSERT(s->widget_count < SC_SCENE_MAX_WIDGETS);
    i32 wid = (i32)s->widget_count++;
    SCWidget *w = &s->widgets[wid];
    memset(w, 0, sizeof(*w));
    w->id      = wid;
    w->type    = type;
    w->visible = true;
    w->alpha   = 1.0f;
    w->color   = SC_WHITE;
    w->layout_node = sc_layout_add_node(&s->layout, parent_layout, ls);
    return wid;
}

i32 sc_widget_rect(SCScene *s, i32 parent_layout, SCColor color,
                    SCLayoutStyle ls) {
    i32 id = sc_widget_add(s, parent_layout, SC_WIDGET_RECT, ls);
    s->widgets[id].color = color;
    return id;
}

i32 sc_widget_text(SCScene *s, i32 parent_layout, const char *txt,
                    f32 font_size, SCColor color, SCLayoutStyle ls) {
    i32 id = sc_widget_add(s, parent_layout, SC_WIDGET_TEXT, ls);
    SCWidget *w = &s->widgets[id];
    /* Bug #7: strncpy does not NUL-terminate when src >= dst size; use snprintf */
    snprintf(w->text, SC_WIDGET_MAX_TEXT, "%s", txt ? txt : "");
    w->font_size  = font_size;
    w->text_color = color;
    return id;
}

i32 sc_widget_image(SCScene *s, i32 parent_layout, SCGfxTexture tex,
                     SCLayoutStyle ls) {
    i32 id = sc_widget_add(s, parent_layout, SC_WIDGET_IMAGE, ls);
    s->widgets[id].texture = tex;
    return id;
}

i32 sc_widget_canvas(SCScene *s, i32 parent_layout, SCPaintFn fn,
                      SCLayoutStyle ls) {
    i32 id = sc_widget_add(s, parent_layout, SC_WIDGET_CANVAS, ls);
    s->widgets[id].on_paint = fn;
    return id;
}

/* ---- Mutation --------------------------------------------------------- */
void sc_widget_set_text(SCScene *s, i32 id, const char *txt) {
    /* Bug #7: guarantee NUL termination */
    snprintf(s->widgets[id].text, SC_WIDGET_MAX_TEXT, "%s", txt ? txt : "");
}
void sc_widget_set_color(SCScene *s, i32 id, SCColor c) {
    s->widgets[id].color = c;
}
void sc_widget_set_alpha(SCScene *s, i32 id, f32 a) {
    s->widgets[id].alpha = SC_CLAMP(a, 0.0f, 1.0f);
}
void sc_widget_set_visible(SCScene *s, i32 id, bool v) {
    s->widgets[id].visible = v;
}

/* ---- Font ------------------------------------------------------------- */
void sc_scene_set_font(SCScene *s, SCFont *font) {
    s->font = font;
    if (font) {
        s->font_atlas_tex = sc_font_upload_atlas(font, s->gfx);
    } else {
        SCGfxTexture null_tex = {0};
        s->font_atlas_tex = null_tex;
    }
}

/* ---- Animation -------------------------------------------------------- */
i32 sc_anim_push(SCScene *s, i32 widget_id, SCAnimProp prop,
                  f32 from, f32 to, f32 dur, SCEasing easing, bool loop) {
    /* Bug #9: validate widget_id before storing in animation slot */
    SC_ASSERT(widget_id >= 0 && (u32)widget_id < s->widget_count);
    if (widget_id < 0 || (u32)widget_id >= s->widget_count) return -1;
    SC_ASSERT(s->anim_count < SC_SCENE_MAX_ANIMS);
    i32 aid = (i32)s->anim_count++;
    SCWidgetAnim *a = &s->anims[aid];
    a->widget_id = widget_id;
    a->prop      = prop;
    a->easing    = easing;
    a->from      = from;
    a->to        = to;
    a->elapsed   = 0;
    a->duration  = dur;
    a->loop      = loop;
    a->active    = true;
    return aid;
}

void sc_anim_stop(SCScene *s, i32 aid) {
    if (aid >= 0 && aid < (i32)s->anim_count)
        s->anims[aid].active = false;
}

/* ---- Update ----------------------------------------------------------- */
void sc_scene_update(SCScene *s, f32 dt) {
    s->dt    = dt;
    s->time += dt;

    /* Tick animations */
    for (u32 i = 0; i < s->anim_count; i++) {
        SCWidgetAnim *a = &s->anims[i];
        if (!a->active) continue;
        a->elapsed += dt;
        f32 t = (a->duration > SC_EPSILON)
                ? a->elapsed / a->duration : 1.0f;
        if (t > 1.0f) {
            if (a->loop) { a->elapsed = fmodf(a->elapsed, a->duration); t = a->elapsed / a->duration; }
            else         { t = 1.0f; a->active = false; }
        }
        f32 *prop = _sc_widget_prop_ptr(s, a->widget_id, a->prop);
        if (prop) *prop = sc_lerpf(a->from, a->to, _sc_ease(a->easing, t));
    }

    /* Re-compute layout */
    sc_layout_compute(&s->layout, s->viewport_w, s->viewport_h);

    /* Sync layout results → widget rect */
    for (u32 i = 0; i < s->widget_count; i++) {
        SCWidget *w = &s->widgets[i];
        SCRect2f r  = sc_layout_screen_rect(&s->layout, w->layout_node);
        w->x = r.x; w->y = r.y; w->w = r.w; w->h = r.h;
    }
}

/* ---- Render ----------------------------------------------------------- */
void sc_scene_render(SCScene *s) {
    for (u32 i = 0; i < s->widget_count; i++) {
        SCWidget *w = &s->widgets[i];
        if (!w->visible) continue;
        SCRect2f rect = {w->x, w->y, w->w, w->h};
        SCColor  col  = {w->color.r, w->color.g, w->color.b, w->color.a * w->alpha};

        switch (w->type) {
            case SC_WIDGET_CONTAINER: break;  /* no visual                */
            case SC_WIDGET_RECT:
                sc_gfx_draw_rect(s->gfx, rect, col);
                if (w->border_width > 0.0f) {
                    /* 4 border lines */
                    f32 bw = w->border_width;
                    SCColor bc = w->border_color;
                    sc_gfx_draw_line(s->gfx, sc_v2(w->x, w->y), sc_v2(w->x+w->w, w->y), bw, bc);
                    sc_gfx_draw_line(s->gfx, sc_v2(w->x+w->w, w->y), sc_v2(w->x+w->w, w->y+w->h), bw, bc);
                    sc_gfx_draw_line(s->gfx, sc_v2(w->x+w->w, w->y+w->h), sc_v2(w->x, w->y+w->h), bw, bc);
                    sc_gfx_draw_line(s->gfx, sc_v2(w->x, w->y+w->h), sc_v2(w->x, w->y), bw, bc);
                }
                break;
            case SC_WIDGET_TEXT:
                if (s->font && s->font_atlas_tex.id > 0 && strlen(w->text) > 0) {
                    sc_font_render_text(s->gfx, s->font, w->text,
                        w->x, w->y, w->text_color, s->font_atlas_tex);
                } else {
                    /* Fallback: draw a coloured rect proportional to text length */
                    sc_gfx_draw_rect(s->gfx,
                        (SCRect2f){w->x, w->y, (f32)strlen(w->text) * w->font_size * 0.6f, w->font_size},
                        w->text_color);
                }
                break;
            case SC_WIDGET_IMAGE:
                sc_gfx_draw_sprite(s->gfx, rect, w->texture, col);
                break;
            case SC_WIDGET_CANVAS:
                if (w->on_paint) w->on_paint(s, w->id, s->gfx, rect);
                break;
        }
    }
}

/* ---- Event dispatch --------------------------------------------------- */
void sc_scene_dispatch_event(SCScene *s, const SCEvent *ev) {
    for (u32 i = 0; i < s->widget_count; i++) {
        SCWidget *w = &s->widgets[i];
        if (!w->visible || !w->interactive) continue;
        SCRect2f r = {w->x, w->y, w->w, w->h};
        bool hit = sc_rect2f_contains(r, ev->mouse_x, ev->mouse_y);
        if (!hit) continue;
        switch (ev->type) {
            case SC_EVENT_MOUSE_MOVE:   if (w->on_hover)   w->on_hover(s, i, ev);   break;
            case SC_EVENT_MOUSE_DOWN:   if (w->on_press)   w->on_press(s, i, ev);   break;
            case SC_EVENT_MOUSE_UP:     if (w->on_release) w->on_release(s, i, ev); break;
            case SC_EVENT_MOUSE_SCROLL: if (w->on_scroll)  w->on_scroll(s, i, ev);  break;
            default: break;
        }
    }
    /* Resize broadcast */
    if (ev->type == SC_EVENT_RESIZE)
        sc_scene_resize(s, (f32)ev->window_w, (f32)ev->window_h);
}

#endif /* SC_WIDGET_IMPLEMENTATION */
#endif /* SC_WIDGET_H */
