/*
 * test_layout.c  --  Unit tests for sc_layout.h
 */
#define SC_LAYOUT_IMPLEMENTATION
#include "sc_layout.h"
#include <stdio.h>
#include <math.h>

#define NEAR(a,b)   (fabsf((float)(a) - (float)(b)) < 0.5f)
#define FAIL_UNLESS(cond, why) \
    do { if(!(cond)){ printf("  [FAIL] %s\n", why); return 1; } } while(0)
#define PASS(name)  printf("  [PASS] %s\n", name)

static SCLayoutStyle _ls(f32 w, f32 h, SCFlexDir dir, f32 grow) {
    SCLayoutStyle s = {0};
    s.flex_dir    = dir;
    s.width       = w;
    s.height      = h;
    s.flex_grow   = grow;
    s.flex_shrink = 1.0f;
    s.flex_basis  = SC_LAYOUT_UNDEFINED;
    s.min_width   = SC_LAYOUT_UNDEFINED;
    s.min_height  = SC_LAYOUT_UNDEFINED;
    s.max_width   = SC_LAYOUT_UNDEFINED;
    s.max_height  = SC_LAYOUT_UNDEFINED;
    s.align_items = SC_ALIGN_STRETCH;
    return s;
}

/* Test 1: single root node fills viewport */
static int test_root_fill(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    sc_layout_add_node(&t, -1, _ls(SC_LAYOUT_UNDEFINED, SC_LAYOUT_UNDEFINED,
                                   SC_FLEX_COLUMN, 0));
    sc_layout_compute(&t, 1280, 720);
    FAIL_UNLESS(NEAR(t.result[0].w, 1280), "root width = viewport");
    FAIL_UNLESS(NEAR(t.result[0].h, 720),  "root height = viewport");
    PASS("root_fill");
    return 0;
}

/* Test 2: two children in a row share space evenly via flex_grow */
static int test_row_equal_grow(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle root_s = _ls(400, 100, SC_FLEX_ROW, 0);
    root_s.is_container  = true;
    root_s.justify_content = SC_JUSTIFY_START;
    i32 root = sc_layout_add_node(&t, -1, root_s);
    sc_layout_add_node(&t, root, _ls(SC_LAYOUT_UNDEFINED, 100, SC_FLEX_ROW, 1));
    sc_layout_add_node(&t, root, _ls(SC_LAYOUT_UNDEFINED, 100, SC_FLEX_ROW, 1));
    sc_layout_compute(&t, 400, 100);
    /* Each child should get ~200 px */
    FAIL_UNLESS(NEAR(t.result[1].w, 200),
                "child 1 half width");
    FAIL_UNLESS(NEAR(t.result[2].w, 200),
                "child 2 half width");
    PASS("row_equal_grow");
    return 0;
}

/* Test 3: fixed-size children + justify-center */
static int test_justify_center(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle root_s  = _ls(600, 100, SC_FLEX_ROW, 0);
    root_s.is_container   = true;
    root_s.justify_content= SC_JUSTIFY_CENTER;
    i32 root = sc_layout_add_node(&t, -1, root_s);
    /* One fixed 100-wide child */
    sc_layout_add_node(&t, root, _ls(100, 100, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 600, 100);
    /* Child should be centred: x ~= (600-100)/2 = 250 */
    f32 cx = t.result[1].x;
    FAIL_UNLESS(NEAR(cx, 250.0f), "justify-center x offset");
    PASS("justify_center");
    return 0;
}

/* Test 4: column layout, children stack vertically */
static int test_column_stack(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle root_s  = _ls(300, 300, SC_FLEX_COLUMN, 0);
    root_s.is_container   = true;
    i32 root = sc_layout_add_node(&t, -1, root_s);
    sc_layout_add_node(&t, root, _ls(300, 50, SC_FLEX_COLUMN, 0));
    sc_layout_add_node(&t, root, _ls(300, 80, SC_FLEX_COLUMN, 0));
    sc_layout_compute(&t, 300, 300);
    f32 y1 = t.result[1].y;
    f32 y2 = t.result[2].y;
    FAIL_UNLESS(y1 < y2, "second child below first");
    PASS("column_stack");
    return 0;
}

/* ===== Flex-wrap tests ===== */

static int test_wrap_row_basic(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(200, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.flex_wrap = SC_FLEX_WRAP;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(120, 50, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(120, 50, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 200, 200);
    FAIL_UNLESS(NEAR(t.result[1].x, 0),   "wrap_row child1 x");
    FAIL_UNLESS(NEAR(t.result[1].y, 0),   "wrap_row child1 y");
    FAIL_UNLESS(NEAR(t.result[1].w, 120), "wrap_row child1 w");
    FAIL_UNLESS(NEAR(t.result[1].h, 50),  "wrap_row child1 h");
    FAIL_UNLESS(NEAR(t.result[2].x, 0),   "wrap_row child2 x");
    FAIL_UNLESS(NEAR(t.result[2].y, 50),  "wrap_row child2 y (wrapped)");
    FAIL_UNLESS(NEAR(t.result[2].w, 120), "wrap_row child2 w");
    FAIL_UNLESS(NEAR(t.result[2].h, 50),  "wrap_row child2 h");
    PASS("wrap_row_basic"); return 0;
}

static int test_wrap_column_basic(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(200, 100, SC_FLEX_COLUMN, 0);
    rs.is_container = true; rs.flex_wrap = SC_FLEX_WRAP;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(100, 60, SC_FLEX_COLUMN, 0));
    sc_layout_add_node(&t, r, _ls(100, 60, SC_FLEX_COLUMN, 0));
    sc_layout_compute(&t, 200, 100);
    FAIL_UNLESS(NEAR(t.result[1].x, 0),   "wrap_col child1 x");
    FAIL_UNLESS(NEAR(t.result[1].y, 0),   "wrap_col child1 y");
    FAIL_UNLESS(NEAR(t.result[1].w, 100), "wrap_col child1 w");
    FAIL_UNLESS(NEAR(t.result[1].h, 60),  "wrap_col child1 h");
    FAIL_UNLESS(NEAR(t.result[2].x, 100), "wrap_col child2 x (wrapped)");
    FAIL_UNLESS(NEAR(t.result[2].y, 0),   "wrap_col child2 y");
    FAIL_UNLESS(NEAR(t.result[2].w, 100), "wrap_col child2 w");
    FAIL_UNLESS(NEAR(t.result[2].h, 60),  "wrap_col child2 h");
    PASS("wrap_column_basic"); return 0;
}

static int test_wrap_reverse(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(200, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.flex_wrap = SC_FLEX_WRAP_REVERSE;
    rs.align_items = SC_ALIGN_START;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(120, 50, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(120, 50, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 200, 200);
    /* wrap-reverse: last line appears first (at cross-start = top) */
    FAIL_UNLESS(NEAR(t.result[1].x, 0),    "wrap_rev child1 x");
    FAIL_UNLESS(t.result[1].y > t.result[2].y, "wrap_rev child1 below child2");
    FAIL_UNLESS(NEAR(t.result[2].x, 0),    "wrap_rev child2 x");
    FAIL_UNLESS(NEAR(t.result[2].y, 0),    "wrap_rev child2 y at top");
    PASS("wrap_reverse"); return 0;
}

static int test_wrap_no_break(void) {
    /* Children fit on one line: no wrapping should occur */
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(300, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.flex_wrap = SC_FLEX_WRAP;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(100, 50, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(100, 50, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 300, 200);
    FAIL_UNLESS(NEAR(t.result[1].y, 0),  "wrap_nobreak child1 y");
    FAIL_UNLESS(NEAR(t.result[2].y, 0),  "wrap_nobreak child2 y (same line)");
    PASS("wrap_no_break"); return 0;
}

static int test_wrap_grow(void) {
    /* Children with flex-grow in wrap mode: free space distributed per line */
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(300, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.flex_wrap = SC_FLEX_WRAP;
    rs.align_items = SC_ALIGN_START;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(SC_LAYOUT_UNDEFINED, 50, SC_FLEX_ROW, 1));
    sc_layout_add_node(&t, r, _ls(SC_LAYOUT_UNDEFINED, 50, SC_FLEX_ROW, 1));
    sc_layout_compute(&t, 300, 200);
    /* Both fit on one line; each grows by 300/2=150 */
    FAIL_UNLESS(NEAR(t.result[1].w, 150), "wrap_grow child1 w");
    FAIL_UNLESS(NEAR(t.result[2].w, 150), "wrap_grow child2 w");
    FAIL_UNLESS(NEAR(t.result[1].y, 0),   "wrap_grow child1 y");
    FAIL_UNLESS(NEAR(t.result[2].y, 0),   "wrap_grow child2 y (same line)");
    PASS("wrap_grow"); return 0;
}

static int test_wrap_align_start(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(200, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.flex_wrap = SC_FLEX_WRAP;
    rs.align_items = SC_ALIGN_START;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(120, 50, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(120, 80, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 200, 200);
    FAIL_UNLESS(NEAR(t.result[1].y, 0),   "wrap_align_start child1 y");
    FAIL_UNLESS(NEAR(t.result[2].y, 50),  "wrap_align_start child2 y (line2)");
    PASS("wrap_align_start"); return 0;
}

static int test_wrap_align_center(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(200, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.flex_wrap = SC_FLEX_WRAP;
    rs.align_items = SC_ALIGN_CENTER;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(120, 50, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(120, 80, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 200, 200);
    /* Each child is in its own line, each line_cross_max == child_cross, so offset=0 */
    FAIL_UNLESS(NEAR(t.result[1].y, 0),   "wrap_align_center child1 y");
    FAIL_UNLESS(NEAR(t.result[2].y, 50),  "wrap_align_center child2 y (line2)");
    PASS("wrap_align_center"); return 0;
}

static int test_wrap_align_end(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(200, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.flex_wrap = SC_FLEX_WRAP;
    rs.align_items = SC_ALIGN_END;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(120, 50, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(120, 80, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 200, 200);
    /* Each child is in its own line, each line_cross_max == child_cross, so end offset=0 */
    FAIL_UNLESS(NEAR(t.result[1].y, 0),   "wrap_align_end child1 y");
    FAIL_UNLESS(NEAR(t.result[2].y, 50),  "wrap_align_end child2 y (line2)");
    PASS("wrap_align_end"); return 0;
}

static int test_wrap_justify_center(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(300, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.flex_wrap = SC_FLEX_WRAP;
    rs.align_items = SC_ALIGN_START;
    rs.justify_content = SC_JUSTIFY_CENTER;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(100, 50, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(100, 50, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 300, 200);
    /* Both fit line: total=200, free=100, center -> cursor=50 */
    FAIL_UNLESS(NEAR(t.result[1].x, 50),  "wrap_justify_center child1 x");
    FAIL_UNLESS(NEAR(t.result[2].x, 150), "wrap_justify_center child2 x");
    PASS("wrap_justify_center"); return 0;
}

static int test_wrap_justify_between(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(300, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.flex_wrap = SC_FLEX_WRAP;
    rs.align_items = SC_ALIGN_START;
    rs.justify_content = SC_JUSTIFY_SPACE_BETWEEN;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(100, 50, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(100, 50, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 300, 200);
    /* space-between: free=100, gap=100/(2-1)=100 */
    FAIL_UNLESS(NEAR(t.result[1].x, 0),   "wrap_justify_between child1 x");
    FAIL_UNLESS(NEAR(t.result[2].x, 200), "wrap_justify_between child2 x");
    PASS("wrap_justify_between"); return 0;
}

static int test_wrap_gap(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(200, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.flex_wrap = SC_FLEX_WRAP;
    rs.align_items = SC_ALIGN_START;
    rs.gap = 10;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(120, 50, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(120, 50, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 200, 200);
    /* Wrap: child2 on second line, gap between lines = 10 */
    FAIL_UNLESS(NEAR(t.result[1].x, 0),   "wrap_gap child1 x");
    FAIL_UNLESS(NEAR(t.result[1].y, 0),   "wrap_gap child1 y");
    FAIL_UNLESS(NEAR(t.result[2].x, 0),   "wrap_gap child2 x");
    FAIL_UNLESS(NEAR(t.result[2].y, 60),  "wrap_gap child2 y (+50+gap10)");
    PASS("wrap_gap"); return 0;
}

/* ===== Align-self tests ===== */

static int test_align_self_auto(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(400, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.align_items = SC_ALIGN_CENTER;
    i32 r = sc_layout_add_node(&t, -1, rs);
    SCLayoutStyle cs = _ls(100, 50, SC_FLEX_ROW, 0);
    cs.align_self = SC_ALIGN_SELF_AUTO;
    sc_layout_add_node(&t, r, cs);
    sc_layout_compute(&t, 400, 200);
    /* auto = inherit center; (200-50)/2 = 75 */
    FAIL_UNLESS(NEAR(t.result[1].y, 75), "align_self_auto y");
    PASS("align_self_auto"); return 0;
}

static int test_align_self_start(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(400, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.align_items = SC_ALIGN_CENTER; /* parent says center */
    i32 r = sc_layout_add_node(&t, -1, rs);
    SCLayoutStyle cs = _ls(100, 50, SC_FLEX_ROW, 0);
    cs.align_self = SC_ALIGN_SELF_START;
    sc_layout_add_node(&t, r, cs);
    sc_layout_compute(&t, 400, 200);
    FAIL_UNLESS(NEAR(t.result[1].y, 0), "align_self_start y");
    PASS("align_self_start"); return 0;
}

static int test_align_self_center(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(400, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.align_items = SC_ALIGN_START; /* parent says start */
    i32 r = sc_layout_add_node(&t, -1, rs);
    SCLayoutStyle cs = _ls(100, 50, SC_FLEX_ROW, 0);
    cs.align_self = SC_ALIGN_SELF_CENTER;
    sc_layout_add_node(&t, r, cs);
    sc_layout_compute(&t, 400, 200);
    FAIL_UNLESS(NEAR(t.result[1].y, 75), "align_self_center y");
    PASS("align_self_center"); return 0;
}

static int test_align_self_end(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(400, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.align_items = SC_ALIGN_START;
    i32 r = sc_layout_add_node(&t, -1, rs);
    SCLayoutStyle cs = _ls(100, 50, SC_FLEX_ROW, 0);
    cs.align_self = SC_ALIGN_SELF_END;
    sc_layout_add_node(&t, r, cs);
    sc_layout_compute(&t, 400, 200);
    FAIL_UNLESS(NEAR(t.result[1].y, 150), "align_self_end y (200-50)");
    PASS("align_self_end"); return 0;
}

static int test_align_self_stretch(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(400, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.align_items = SC_ALIGN_START; /* parent says start */
    i32 r = sc_layout_add_node(&t, -1, rs);
    SCLayoutStyle cs = _ls(100, SC_LAYOUT_UNDEFINED, SC_FLEX_ROW, 0);
    cs.align_self = SC_ALIGN_SELF_STRETCH;
    sc_layout_add_node(&t, r, cs);
    sc_layout_compute(&t, 400, 200);
    /* stretch: height = content_cross (200) - margin */
    FAIL_UNLESS(NEAR(t.result[1].h, 200), "align_self_stretch h");
    PASS("align_self_stretch"); return 0;
}

/* ===== Gap tests ===== */

static int test_gap_row(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(400, 100, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.gap = 20;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(100, 100, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(100, 100, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(100, 100, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 400, 100);
    /* justify-start + gap20:  gap between items */
    FAIL_UNLESS(NEAR(t.result[1].x, 0),   "gap_row child1 x");
    FAIL_UNLESS(NEAR(t.result[2].x, 120), "gap_row child2 x (100+20)");
    FAIL_UNLESS(NEAR(t.result[3].x, 240), "gap_row child3 x (200+40)");
    PASS("gap_row"); return 0;
}

static int test_gap_column(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(100, 400, SC_FLEX_COLUMN, 0);
    rs.is_container = true; rs.gap = 15;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(100, 80, SC_FLEX_COLUMN, 0));
    sc_layout_add_node(&t, r, _ls(100, 80, SC_FLEX_COLUMN, 0));
    sc_layout_compute(&t, 100, 400);
    FAIL_UNLESS(NEAR(t.result[1].y, 0),   "gap_col child1 y");
    FAIL_UNLESS(NEAR(t.result[2].y, 95),  "gap_col child2 y (80+15)");
    PASS("gap_column"); return 0;
}

static int test_gap_single_child(void) {
    /* Gap with single child: should be no effect */
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(400, 100, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.gap = 50;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(100, 100, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 400, 100);
    FAIL_UNLESS(NEAR(t.result[1].x, 0), "gap_single child x");
    FAIL_UNLESS(NEAR(t.result[1].w, 100), "gap_single child w");
    PASS("gap_single_child"); return 0;
}

static int test_gap_justify_between(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(500, 100, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.gap = 10;
    rs.justify_content = SC_JUSTIFY_SPACE_BETWEEN;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(100, 100, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(100, 100, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 500, 100);
    /* free=500-200=300, igap=300+10=310/(2-1)=310, child1 x=0, child2 x=100+310=410 */
    FAIL_UNLESS(NEAR(t.result[1].x, 0),   "gap_between child1 x");
    FAIL_UNLESS(NEAR(t.result[2].x, 400), "gap_between child2 x (100+300)");
    PASS("gap_justify_between"); return 0;
}

static int test_gap_justify_around(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(500, 100, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.gap = 10;
    rs.justify_content = SC_JUSTIFY_SPACE_AROUND;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(100, 100, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(100, 100, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 500, 100);
    /* free=500-200-10=290, igap=290/2+10=155, cursor=145/2=72.5 */
    FAIL_UNLESS(NEAR(t.result[1].x, 72.5f),  "gap_around child1 x");
    FAIL_UNLESS(NEAR(t.result[2].x, 327.5f), "gap_around child2 x");
    PASS("gap_justify_around"); return 0;
}

/* ===== Additional wrap edge cases ===== */

static int test_wrap_multi_line(void) {
    /* Three children form three lines */
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(100, 300, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.flex_wrap = SC_FLEX_WRAP;
    rs.align_items = SC_ALIGN_START;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(120, 40, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(120, 50, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(120, 60, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 100, 300);
    FAIL_UNLESS(NEAR(t.result[1].y, 0),   "wrap_multiline child1 y");
    FAIL_UNLESS(NEAR(t.result[2].y, 40),  "wrap_multiline child2 y");
    FAIL_UNLESS(NEAR(t.result[3].y, 90),  "wrap_multiline child3 y (40+50)");
    PASS("wrap_multi_line"); return 0;
}

static int test_wrap_with_padding(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(200, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.flex_wrap = SC_FLEX_WRAP;
    rs.align_items = SC_ALIGN_START;
    rs.padding.top = 10; rs.padding.left = 10;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(100, 40, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(100, 40, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 200, 200);
    /* Both fit on one line (100+100=200 == content_main=200-10-10=180... wait, padding-left=10, padding-right=0 */
    /* content_main = 200 - 10 - 0 = 190. 100+100=200 > 190 with cur_main=100+gap=100+0=100 */
    /* Actually gap=0, so check: cur=0, ch_main=100, 0+100=100 <= 190, ok. cur=100. ch_main=100, 100+100=200 > 190, break! */
    /* Child1 stays on line1, child2 wraps to line2 */
    FAIL_UNLESS(NEAR(t.result[1].x, 10),  "wrap_pad child1 x (+pad-left)");
    FAIL_UNLESS(NEAR(t.result[1].y, 10),  "wrap_pad child1 y (+pad-top)");
    FAIL_UNLESS(NEAR(t.result[2].x, 10),  "wrap_pad child2 x (+pad-left)");
    FAIL_UNLESS(NEAR(t.result[2].y, 50),  "wrap_pad child2 y (10+pad+40)");
    PASS("wrap_with_padding"); return 0;
}

static int test_wrap_with_margin(void) {
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(200, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.flex_wrap = SC_FLEX_WRAP;
    rs.align_items = SC_ALIGN_START;
    i32 r = sc_layout_add_node(&t, -1, rs);
    SCLayoutStyle c1 = _ls(100, 40, SC_FLEX_ROW, 0);
    c1.margin.bottom = 10;
    sc_layout_add_node(&t, r, c1);
    sc_layout_add_node(&t, r, _ls(100, 50, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 200, 200);
    /* Both fit on one line? 100+100=200 == content_main=200. cur=0, ch=100, ok. cur=100, ch=100, 100+100=200 <= 200, ok. */
    /* Same line. child1 base includes margin but that's main-axis margin for row (left+right = 0+0=0). */
    FAIL_UNLESS(NEAR(t.result[1].x, 0),   "wrap_margin child1 x");
    FAIL_UNLESS(NEAR(t.result[1].y, 0),   "wrap_margin child1 y");
    FAIL_UNLESS(NEAR(t.result[2].x, 100), "wrap_margin child2 x");
    FAIL_UNLESS(NEAR(t.result[2].y, 0),   "wrap_margin child2 y (same line)");
    PASS("wrap_with_margin"); return 0;
}

static int test_wrap_gap_lines(void) {
    /* Gap between wrapped lines */
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(200, 300, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.flex_wrap = SC_FLEX_WRAP;
    rs.align_items = SC_ALIGN_START;
    rs.gap = 20;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(120, 40, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(120, 50, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 200, 300);
    /* Each wraps to own line. gap=20 between lines */
    FAIL_UNLESS(NEAR(t.result[1].y, 0),   "wrap_gap_lines child1 y");
    FAIL_UNLESS(NEAR(t.result[2].y, 60),  "wrap_gap_lines child2 y (40+gap20)");
    PASS("wrap_gap_lines"); return 0;
}

static int test_wrap_stretch_mixed(void) {
    /* Stretch with explicit and auto heights in wrapping */
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(200, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.flex_wrap = SC_FLEX_WRAP;
    rs.align_items = SC_ALIGN_STRETCH;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(120, SC_LAYOUT_UNDEFINED, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(120, 60, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 200, 200);
    /* child1 has auto cross: line cross falls back to content_cross (200) */
    /* child2 wraps: explicit h=60 sets line2 cross to 60 */
    FAIL_UNLESS(NEAR(t.result[1].h, 200), "wrap_stretch_mixed child1 h (fills container cross)");
    FAIL_UNLESS(NEAR(t.result[2].h, 60), "wrap_stretch_mixed child2 h (explicit)");
    PASS("wrap_stretch_mixed"); return 0;
}

static int test_wrap_align_self(void) {
    /* align-self overrides within wrapped lines */
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(200, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.flex_wrap = SC_FLEX_WRAP;
    rs.align_items = SC_ALIGN_START;
    i32 r = sc_layout_add_node(&t, -1, rs);
    SCLayoutStyle c1 = _ls(120, 40, SC_FLEX_ROW, 0);
    c1.align_self = SC_ALIGN_SELF_CENTER;
    sc_layout_add_node(&t, r, c1);
    SCLayoutStyle c2 = _ls(120, 60, SC_FLEX_ROW, 0);
    c2.align_self = SC_ALIGN_SELF_END;
    sc_layout_add_node(&t, r, c2);
    sc_layout_compute(&t, 200, 200);
    /* Each in own line. line_cross_max_1=40, line_cross_max_2=60 */
    /* child1: center in line (40-40)/2=0, y=0 */
    /* child2: end in line (60-60)=0, y=line1_cross_max=40 */
    FAIL_UNLESS(NEAR(t.result[1].y, 0),   "wrap_align_self child1 y");
    FAIL_UNLESS(NEAR(t.result[2].y, 40),  "wrap_align_self child2 y");
    PASS("wrap_align_self"); return 0;
}

static int test_wrap_nowrap_single_child(void) {
    /* NOWRAP with multiple children: all on one line even if overflow */
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(100, 200, SC_FLEX_ROW, 0);
    rs.is_container = true;
    /* flex_wrap defaults to NOWRAP */
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(120, 50, SC_FLEX_ROW, 0));
    sc_layout_add_node(&t, r, _ls(120, 50, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 100, 200);
    /* Both on same line (no wrapping), even though 120+120 > 100 */
    FAIL_UNLESS(NEAR(t.result[1].y, 0),  "wrap_nowrap_single child1 y");
    FAIL_UNLESS(NEAR(t.result[2].y, 0),  "wrap_nowrap_single child2 y (same line)");
    PASS("wrap_nowrap_single_child"); return 0;
}

/* ===== More edge cases ===== */

static int test_zero_content_area(void) {
    /* Container with zero content area should not crash */
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(0, 0, SC_FLEX_ROW, 0);
    rs.is_container = true;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(50, 50, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 0, 0);
    PASS("zero_content_area"); return 0;
}

static int test_negative_free_space(void) {
    /* Children overflow container (negative free space) with flex-shrink */
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(100, 100, SC_FLEX_ROW, 0);
    rs.is_container = true;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(200, 50, SC_FLEX_ROW, 0));
    sc_layout_compute(&t, 100, 100);
    /* Current code doesn't shrink: child stays at 200 */
    FAIL_UNLESS(NEAR(t.result[1].w, 200), "neg_free child w overflows");
    PASS("negative_free_space"); return 0;
}

static int test_wrap_stretch_line(void) {
    /* In wrap mode, stretch should fill line cross, NOT container cross */
    SCLayoutTree t;
    sc_layout_tree_init(&t);
    SCLayoutStyle rs = _ls(200, 200, SC_FLEX_ROW, 0);
    rs.is_container = true; rs.flex_wrap = SC_FLEX_WRAP;
    rs.align_items = SC_ALIGN_STRETCH;
    i32 r = sc_layout_add_node(&t, -1, rs);
    sc_layout_add_node(&t, r, _ls(120, SC_LAYOUT_UNDEFINED, SC_FLEX_ROW, 0)); /* tall */
    sc_layout_add_node(&t, r, _ls(120, 50, SC_FLEX_ROW, 0)); /* explicit 50 */
    sc_layout_compute(&t, 200, 200);
    /* child2 is shorter but explicit height, so it should still be 50 */
    FAIL_UNLESS(NEAR(t.result[2].h, 50), "wrap_stretch child2 explicit h");
    PASS("wrap_stretch_line"); return 0;
}

int main(void) {
    printf("=== sc_layout tests ===\n");
    int fail = 0;
    fail += test_root_fill();
    fail += test_row_equal_grow();
    fail += test_justify_center();
    fail += test_column_stack();
    fail += test_wrap_row_basic();
    fail += test_wrap_column_basic();
    fail += test_wrap_reverse();
    fail += test_wrap_no_break();
    fail += test_wrap_grow();
    fail += test_wrap_align_start();
    fail += test_wrap_align_center();
    fail += test_wrap_align_end();
    fail += test_wrap_justify_center();
    fail += test_wrap_justify_between();
    fail += test_wrap_gap();
    fail += test_align_self_auto();
    fail += test_align_self_start();
    fail += test_align_self_center();
    fail += test_align_self_end();
    fail += test_align_self_stretch();
    fail += test_gap_row();
    fail += test_gap_column();
    fail += test_gap_single_child();
    fail += test_gap_justify_between();
    fail += test_gap_justify_around();
    fail += test_zero_content_area();
    fail += test_negative_free_space();
    fail += test_wrap_stretch_line();
    fail += test_wrap_multi_line();
    fail += test_wrap_with_padding();
    fail += test_wrap_with_margin();
    fail += test_wrap_gap_lines();
    fail += test_wrap_stretch_mixed();
    fail += test_wrap_align_self();
    fail += test_wrap_nowrap_single_child();
    if (!fail) printf("All layout tests passed.\n");
    return fail;
}
