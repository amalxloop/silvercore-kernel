/*
 * sc_layout.h  --  SilverCore Flexbox Layout Engine
 *
 * A pure-C, cache-optimised subset of CSS Flexbox (Level 1).
 *
 * Scope (MVP):
 *   - Single-axis flex (row / column)
 *   - justify-content: flex-start / center / flex-end / space-between / space-around
 *   - align-items:     flex-start / center / flex-end / stretch
 *   - flex-grow / flex-shrink / flex-basis
 *   - margin / padding (all 4 sides)
 *   - fixed min/max constraints
 *   - wrap: nowrap (wrap is a stretch goal)
 *
 * Data layout
 *   All node attributes live in flat parallel arrays (SoA) to maximise
 *   cache utilisation during the two layout passes.
 *
 * Two-pass algorithm
 *   Pass 1  – compute "intrinsic" sizes bottom-up
 *   Pass 2  – distribute free space and set final rects top-down
 *
 * Integration
 *   #define SC_LAYOUT_IMPLEMENTATION
 *   #include "sc_layout.h"
 *   in exactly one .c file.
 */
#ifndef SC_LAYOUT_H
#define SC_LAYOUT_H

#include "sc_types.h"
#include "sc_math.h"

/* -------------------------------------------------------------------------
 * Enumerations
 * ---------------------------------------------------------------------- */
typedef enum SCFlexDir {
    SC_FLEX_ROW    = 0,
    SC_FLEX_COLUMN = 1,
} SCFlexDir;

typedef enum SCJustify {
    SC_JUSTIFY_START        = 0,
    SC_JUSTIFY_CENTER       = 1,
    SC_JUSTIFY_END          = 2,
    SC_JUSTIFY_SPACE_BETWEEN= 3,
    SC_JUSTIFY_SPACE_AROUND = 4,
} SCJustify;

typedef enum SCAlignItems {
    SC_ALIGN_START   = 0,
    SC_ALIGN_CENTER  = 1,
    SC_ALIGN_END     = 2,
    SC_ALIGN_STRETCH = 3,
} SCAlignItems;

/* SC_LAYOUT_UNDEFINED: dimension is not yet set / user hasn't constrained it */
#define SC_LAYOUT_UNDEFINED  -1.0f
#define SC_LAYOUT_AUTO       -2.0f  /* shrink-to-content in cross axis       */

/* -------------------------------------------------------------------------
 * Node style (set by the app)
 * ---------------------------------------------------------------------- */
typedef struct SCLayoutStyle {
    /* Container (only meaningful if is_container) */
    SCFlexDir    flex_dir;
    SCJustify    justify_content;
    SCAlignItems align_items;
    bool         is_container;

    /* Item sizing */
    f32  width;         /* SC_LAYOUT_UNDEFINED = fill parent / grow */
    f32  height;
    f32  min_width;
    f32  min_height;
    f32  max_width;     /* SC_LAYOUT_UNDEFINED = no cap              */
    f32  max_height;

    /* Flex item properties */
    f32  flex_grow;     /* 0 = don't grow  */
    f32  flex_shrink;   /* 1 = default     */
    f32  flex_basis;    /* SC_LAYOUT_UNDEFINED = use width/height   */

    /* Spacing (box model) */
    SCEdgeInsets margin;
    SCEdgeInsets padding;
} SCLayoutStyle;

/* -------------------------------------------------------------------------
 * Node result (written by sc_layout_compute)
 * ---------------------------------------------------------------------- */
typedef struct SCLayoutResult {
    f32 x, y;           /* position relative to parent content area */
    f32 w, h;           /* final outer size (includes margin)        */
} SCLayoutResult;

/* -------------------------------------------------------------------------
 * Layout tree
 *
 * Nodes are stored as compact SOA arrays.  The tree topology is encoded
 * with parent/first-child/next-sibling indices (no pointers).
 * ---------------------------------------------------------------------- */
#define SC_LAYOUT_MAX_NODES 8192   /* must match SC_SCENE_MAX_WIDGETS */

typedef struct SCLayoutTree {
    u32   count;

    /* Topology */
    i32   parent      [SC_LAYOUT_MAX_NODES]; /* -1 = root        */
    i32   first_child [SC_LAYOUT_MAX_NODES]; /* -1 = leaf        */
    i32   next_sibling[SC_LAYOUT_MAX_NODES]; /* -1 = last child  */
    u32   child_count [SC_LAYOUT_MAX_NODES];

    /* Input */
    SCLayoutStyle  style [SC_LAYOUT_MAX_NODES];

    /* Output */
    SCLayoutResult result[SC_LAYOUT_MAX_NODES];

    /* Internal workspace (main-axis sizes before distribution) */
    f32  _base_size [SC_LAYOUT_MAX_NODES];
    f32  _grow_sum  [SC_LAYOUT_MAX_NODES]; /* per parent temp    */
} SCLayoutTree;

/* -------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

/* Initialise (zero) a tree. */
void sc_layout_tree_init(SCLayoutTree *t);

/* Add a node; returns node index.  parent = -1 for root. */
i32  sc_layout_add_node(SCLayoutTree *t, i32 parent, SCLayoutStyle style);

/* Set the available space for node 0 (root) and compute the entire tree. */
void sc_layout_compute(SCLayoutTree *t, f32 avail_w, f32 avail_h);

/* Convenience: get final screen rect for node (accumulates parent offsets). */
SCRect2f sc_layout_screen_rect(const SCLayoutTree *t, i32 node);

/* -------------------------------------------------------------------------
 * Implementation
 * ---------------------------------------------------------------------- */
#ifdef SC_LAYOUT_IMPLEMENTATION

#include <string.h>  /* memset */

void sc_layout_tree_init(SCLayoutTree *t) {
    memset(t, 0, sizeof(*t));
    for (u32 i = 0; i < SC_LAYOUT_MAX_NODES; i++) {
        t->parent      [i] = -1;
        t->first_child [i] = -1;
        t->next_sibling[i] = -1;
    }
}

i32 sc_layout_add_node(SCLayoutTree *t, i32 parent_idx, SCLayoutStyle style) {
    SC_ASSERT(t->count < SC_LAYOUT_MAX_NODES);
    i32 idx = (i32)t->count++;
    t->style[idx] = style;
    t->parent[idx] = parent_idx;
    if (parent_idx >= 0) {
        /* append to sibling chain */
        i32 prev = t->first_child[parent_idx];
        if (prev == -1) {
            t->first_child[parent_idx] = idx;
        } else {
            while (t->next_sibling[prev] != -1) prev = t->next_sibling[prev];
            t->next_sibling[prev] = idx;
        }
        t->child_count[parent_idx]++;
    }
    return idx;
}

/* ---- internal helpers ------------------------------------------------- */

static f32 _sc_margin_main(const SCLayoutStyle *s, SCFlexDir dir) {
    return dir == SC_FLEX_ROW
        ? s->margin.left + s->margin.right
        : s->margin.top  + s->margin.bottom;
}
static f32 _sc_margin_cross(const SCLayoutStyle *s, SCFlexDir dir) {
    return dir == SC_FLEX_ROW
        ? s->margin.top  + s->margin.bottom
        : s->margin.left + s->margin.right;
}
static f32 _sc_pad_main(const SCLayoutStyle *s, SCFlexDir dir) {
    return dir == SC_FLEX_ROW
        ? s->padding.left + s->padding.right
        : s->padding.top  + s->padding.bottom;
}
static f32 _sc_pad_cross(const SCLayoutStyle *s, SCFlexDir dir) {
    return dir == SC_FLEX_ROW
        ? s->padding.top  + s->padding.bottom
        : s->padding.left + s->padding.right;
}

/* Pass 1: compute intrinsic main-axis size for each node (bottom-up). */
static f32 _sc_layout_intrinsic(SCLayoutTree *t, i32 idx) {
    SCLayoutStyle *s = &t->style[idx];
    SCFlexDir dir = s->flex_dir;

    if (t->first_child[idx] == -1) {
        /* leaf node */
        f32 base = (dir == SC_FLEX_ROW) ? s->width : s->height;
        if (base == SC_LAYOUT_UNDEFINED) base = 0.0f;
        if (s->min_width  != SC_LAYOUT_UNDEFINED && dir == SC_FLEX_ROW)
            base = SC_MAX(base, s->min_width);
        if (s->min_height != SC_LAYOUT_UNDEFINED && dir == SC_FLEX_COLUMN)
            base = SC_MAX(base, s->min_height);
        t->_base_size[idx] = base;
        return base + _sc_margin_main(s, dir);
    }

    /* container: sum children intrinsic sizes */
    f32 total = _sc_pad_main(s, dir);
    i32 ch = t->first_child[idx];
    while (ch != -1) {
        total += _sc_layout_intrinsic(t, ch);
        ch = t->next_sibling[ch];
    }
    t->_base_size[idx] = total;
    return total + _sc_margin_main(s, dir);
}

/* Pass 2: top-down space distribution. */
static void _sc_layout_distribute(SCLayoutTree *t, i32 idx,
                                    f32 avail_main, f32 avail_cross,
                                    f32 origin_x,   f32 origin_y) {
    SCLayoutStyle *s  = &t->style[idx];
    SCFlexDir      dir = s->flex_dir;

    /* Apply our own margin */
    f32 mx_start = (dir == SC_FLEX_ROW) ? s->margin.left  : s->margin.top;
    f32 my_start = (dir == SC_FLEX_ROW) ? s->margin.top   : s->margin.left;
    f32 mx_end   = (dir == SC_FLEX_ROW) ? s->margin.right : s->margin.bottom;
    f32 my_end   = (dir == SC_FLEX_ROW) ? s->margin.bottom: s->margin.right;
    SC_UNUSED(mx_end); SC_UNUSED(my_end);

    /* Final outer dimensions */
    f32 outer_main  = avail_main;
    f32 outer_cross = avail_cross;

    /* Clamp to explicit size if given */
    f32 expl_main  = (dir == SC_FLEX_ROW) ? s->width  : s->height;
    f32 expl_cross = (dir == SC_FLEX_ROW) ? s->height : s->width;
    if (expl_main  != SC_LAYOUT_UNDEFINED) outer_main  = expl_main;
    if (expl_cross != SC_LAYOUT_UNDEFINED) outer_cross = expl_cross;

    SCLayoutResult *r = &t->result[idx];
    r->x = origin_x + mx_start;
    r->y = origin_y + my_start;
    r->w = (dir == SC_FLEX_ROW) ? outer_main  : outer_cross;
    r->h = (dir == SC_FLEX_ROW) ? outer_cross : outer_main;

    if (t->first_child[idx] == -1) return; /* leaf */

    /* Content area */
    f32 content_main  = outer_main  - _sc_pad_main (s, dir);
    f32 content_cross = outer_cross - _sc_pad_cross(s, dir);
    f32 pad_start_main  = (dir == SC_FLEX_ROW) ? s->padding.left : s->padding.top;
    f32 pad_start_cross = (dir == SC_FLEX_ROW) ? s->padding.top  : s->padding.left;

    /* Gather children base sizes and flex-grow sum */
    f32 total_base  = 0.0f;
    f32 total_grow  = 0.0f;
    u32 nchild      = t->child_count[idx];
    SC_UNUSED(nchild);
    i32 ch = t->first_child[idx];
    while (ch != -1) {
        total_base += t->_base_size[ch];
        total_grow += t->style[ch].flex_grow;
        ch = t->next_sibling[ch];
    }

    f32 free_space = content_main - total_base;

    /* Justify-content: compute initial offset + gap */
    u32 nc = t->child_count[idx];
    f32 gap   = 0.0f;
    f32 cursor = pad_start_main + (dir == SC_FLEX_ROW ? r->x : r->y);
    /* we'll use local coords then add origin later */
    cursor = pad_start_main;

    switch (s->justify_content) {
        case SC_JUSTIFY_START:         cursor = pad_start_main; gap = 0; break;
        case SC_JUSTIFY_END:           cursor = pad_start_main + free_space; gap = 0; break;
        case SC_JUSTIFY_CENTER:        cursor = pad_start_main + free_space*0.5f; gap = 0; break;
        case SC_JUSTIFY_SPACE_BETWEEN: cursor = pad_start_main;
            gap = (nc > 1) ? free_space / (f32)(nc-1) : 0; break;
        case SC_JUSTIFY_SPACE_AROUND:  gap = free_space / (f32)nc;
            cursor = pad_start_main + gap*0.5f; break;
    }

    /* Distribute children */
    ch = t->first_child[idx];
    while (ch != -1) {
        SCLayoutStyle *cs = &t->style[ch];

        /* Flex-grow adjustment */
        f32 child_main = t->_base_size[ch];
        if (free_space > 0 && total_grow > SC_EPSILON && cs->flex_grow > 0) {
            child_main += (cs->flex_grow / total_grow) * free_space;
        }

        /* Cross-axis sizing */
        f32 child_cross;
        switch (s->align_items) {
            case SC_ALIGN_STRETCH:
                child_cross = content_cross
                    - _sc_margin_cross(cs, dir)
                    - _sc_pad_cross(cs, dir);
                break;
            default: {
                f32 expl = (dir == SC_FLEX_ROW) ? cs->height : cs->width;
                child_cross = (expl != SC_LAYOUT_UNDEFINED) ? expl : 0.0f;
                break;
            }
        }

        /* Cross-axis alignment offset */
        f32 cross_offset = pad_start_cross;
        f32 child_outer_cross = child_cross + _sc_margin_cross(cs, dir);
        switch (s->align_items) {
            case SC_ALIGN_CENTER: cross_offset = pad_start_cross + (content_cross - child_outer_cross)*0.5f; break;
            case SC_ALIGN_END:    cross_offset = pad_start_cross +  content_cross - child_outer_cross;        break;
            default: break;
        }

        /* Recursively distribute */
        f32 child_ox, child_oy;
        if (dir == SC_FLEX_ROW) {
            child_ox = r->x + cursor;
            child_oy = r->y + cross_offset;
            _sc_layout_distribute(t, ch, child_main, child_cross, child_ox, child_oy);
            cursor += child_main + gap;
        } else {
            child_ox = r->x + cross_offset;
            child_oy = r->y + cursor;
            _sc_layout_distribute(t, ch, child_cross, child_main, child_ox, child_oy);
            cursor += child_main + gap;
        }

        ch = t->next_sibling[ch];
    }
}

void sc_layout_compute(SCLayoutTree *t, f32 avail_w, f32 avail_h) {
    if (t->count == 0) return;
    /* Pass 1: intrinsic sizes (bottom-up via recursion) */
    _sc_layout_intrinsic(t, 0);
    /* Pass 2: distribute (top-down).
     * _sc_layout_distribute expects (avail_main, avail_cross) where main is
     * the flex-direction axis.  For the root we derive that from the root's
     * own flex_dir so the root gets the correct w/h when its sizes are UNDEFINED. */
    SCFlexDir root_dir = t->style[0].flex_dir;
    f32 root_main  = (root_dir == SC_FLEX_ROW) ? avail_w : avail_h;
    f32 root_cross = (root_dir == SC_FLEX_ROW) ? avail_h : avail_w;
    _sc_layout_distribute(t, 0, root_main, root_cross, 0, 0);
}

SCRect2f sc_layout_screen_rect(const SCLayoutTree *t, i32 node) {
    SCRect2f r = {t->result[node].x, t->result[node].y,
                  t->result[node].w, t->result[node].h};
    return r;
}

#endif /* SC_LAYOUT_IMPLEMENTATION */
#endif /* SC_LAYOUT_H */
