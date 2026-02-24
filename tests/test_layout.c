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
    FAIL_UNLESS(NEAR(t.result[1].w, 200) || NEAR(t.result[1].w, 200),
                "child 1 half width");
    FAIL_UNLESS(NEAR(t.result[2].w, 200) || NEAR(t.result[2].w, 200),
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

int main(void) {
    printf("=== sc_layout tests ===\n");
    int fail = 0;
    fail += test_root_fill();
    fail += test_row_equal_grow();
    fail += test_justify_center();
    fail += test_column_stack();
    if (!fail) printf("All layout tests passed.\n");
    return fail;
}
