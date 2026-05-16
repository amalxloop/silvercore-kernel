/*
 * test_input.c  --  Unit tests for sc_input.h
 */
#include "sc_input.h"
#include <stdio.h>
#include <string.h>

#define FAIL_UNLESS(cond, why) \
    do { if(!(cond)){ printf("  [FAIL] %s\n", why); return 1; } } while(0)
#define PASS(name) printf("  [PASS] %s\n", name)

static int test_init_state(void) {
    SCInputState s;
    sc_input_init(&s);
    FAIL_UNLESS(s.key_count == 0, "key_count zero");
    FAIL_UNLESS(s.char_count == 0, "char_count zero");
    FAIL_UNLESS(s.mouse_x == 0 && s.mouse_y == 0, "mouse zero");
    FAIL_UNLESS(s.touch_count == 0, "touch_count zero");
    FAIL_UNLESS(!s.gamepad_present, "gamepad not present");
    for (u32 i = 0; i < SC_KEY_COUNT; i++)
        FAIL_UNLESS(!s.keys_cur[i] && !s.keys_prev[i], "all keys false");
    PASS("init_state");
    return 0;
}

static int test_key_down_up(void) {
    SCInputState s;
    sc_input_init(&s);

    FAIL_UNLESS(!sc_input_key_down(&s, SC_KEY_A), "A not down");
    FAIL_UNLESS(!sc_input_key_pressed(&s, SC_KEY_A), "A not pressed");
    FAIL_UNLESS(sc_input_key_up(&s, SC_KEY_A), "A is up");

    sc_input_set_key(&s, SC_KEY_A, true);
    FAIL_UNLESS(sc_input_key_down(&s, SC_KEY_A), "A down after set");
    FAIL_UNLESS(sc_input_key_pressed(&s, SC_KEY_A), "A pressed (first frame)");
    FAIL_UNLESS(!sc_input_key_up(&s, SC_KEY_A), "A not up");
    FAIL_UNLESS(s.key_count == 1, "key_count 1");

    sc_input_set_key(&s, SC_KEY_A, false);
    FAIL_UNLESS(!sc_input_key_down(&s, SC_KEY_A), "A not down after release");
    FAIL_UNLESS(!sc_input_key_pressed(&s, SC_KEY_A), "A not pressed");
    FAIL_UNLESS(sc_input_key_up(&s, SC_KEY_A), "A up after release");
    FAIL_UNLESS(s.key_count == 0, "key_count 0 after release");

    PASS("key_down_up");
    return 0;
}

static int test_key_pressed_released(void) {
    SCInputState s;
    sc_input_init(&s);

    sc_input_set_key(&s, SC_KEY_B, true);
    FAIL_UNLESS(sc_input_key_pressed(&s, SC_KEY_B), "B pressed first frame");

    /* end frame: cur -> prev */
    sc_input_end_frame(&s);
    FAIL_UNLESS(sc_input_key_down(&s, SC_KEY_B), "B still down");
    FAIL_UNLESS(!sc_input_key_pressed(&s, SC_KEY_B), "B not pressed second frame");
    FAIL_UNLESS(!sc_input_key_released(&s, SC_KEY_B), "B not released");

    sc_input_set_key(&s, SC_KEY_B, false);
    FAIL_UNLESS(sc_input_key_released(&s, SC_KEY_B), "B released");
    FAIL_UNLESS(!sc_input_key_down(&s, SC_KEY_B), "B not down");

    PASS("key_pressed_released");
    return 0;
}

static int test_mouse(void) {
    SCInputState s;
    sc_input_init(&s);

    FAIL_UNLESS(sc_input_mouse_x(&s) == 0.0f, "mouse_x zero");
    FAIL_UNLESS(sc_input_mouse_y(&s) == 0.0f, "mouse_y zero");

    sc_input_set_mouse_pos(&s, 100.0f, 200.0f);
    FAIL_UNLESS(sc_input_mouse_x(&s) == 100.0f, "mouse_x 100");
    FAIL_UNLESS(sc_input_mouse_y(&s) == 200.0f, "mouse_y 200");
    FAIL_UNLESS(s.mouse_dx == 100.0f && s.mouse_dy == 200.0f, "delta set");

    sc_input_end_frame(&s);
    FAIL_UNLESS(s.mouse_dx == 0 && s.mouse_dy == 0, "delta zeroed after end_frame");

    sc_input_set_mouse_button(&s, SC_MOUSE_LEFT, true);
    FAIL_UNLESS(sc_input_mouse_down(&s, SC_MOUSE_LEFT), "left down");
    FAIL_UNLESS(sc_input_mouse_pressed(&s, SC_MOUSE_LEFT), "left pressed");

    sc_input_set_scroll(&s, 0.0f, 5.0f);
    FAIL_UNLESS(s.scroll_y == 5.0f, "scroll_y 5");

    PASS("mouse");
    return 0;
}

static int test_touch(void) {
    SCInputState s;
    sc_input_init(&s);

    FAIL_UNLESS(s.touch_count == 0, "no touches");
    s.touches[0].x = 50.0f; s.touches[0].y = 100.0f; s.touches[0].active = true;
    s.touch_count = 1;
    FAIL_UNLESS(s.touches[0].active, "touch 0 active");
    FAIL_UNLESS(s.touches[0].x == 50.0f, "touch 0 x");
    FAIL_UNLESS(s.touches[0].y == 100.0f, "touch 0 y");

    PASS("touch");
    return 0;
}

static int test_gamepad(void) {
    SCInputState s;
    sc_input_init(&s);

    FAIL_UNLESS(!s.gamepad_present, "no gamepad");
    s.gamepad_present = true;
    s.gamepad_axes[SC_GAMEPAD_AXIS_LEFTX] = 0.5f;
    FAIL_UNLESS(s.gamepad_axes[SC_GAMEPAD_AXIS_LEFTX] == 0.5f, "leftx 0.5");

    s.gamepad_buttons_cur[0] = true;
    FAIL_UNLESS(s.gamepad_buttons_cur[0], "button 0 down");

    sc_input_end_frame(&s);
    FAIL_UNLESS(s.gamepad_buttons_prev[0], "button 0 prev set");
    FAIL_UNLESS(s.gamepad_buttons_cur[0], "button 0 cur persists (platform must clear)");

    PASS("gamepad");
    return 0;
}

static int test_char_input(void) {
    SCInputState s;
    sc_input_init(&s);

    sc_input_add_char(&s, 0x41); /* 'A' */
    sc_input_add_char(&s, 0x42); /* 'B' */
    FAIL_UNLESS(s.char_count == 2, "2 chars");
    FAIL_UNLESS(s.char_codepoints[0] == 0x41, "char 0 = A");
    FAIL_UNLESS(s.char_codepoints[1] == 0x42, "char 1 = B");

    sc_input_end_frame(&s);
    FAIL_UNLESS(s.char_count == 0, "chars cleared after end_frame");

    PASS("char_input");
    return 0;
}

static int test_any_key_down(void) {
    SCInputState s;
    sc_input_init(&s);
    FAIL_UNLESS(!sc_input_any_key_down(&s), "no keys");

    sc_input_set_key(&s, SC_KEY_SPACE, true);
    FAIL_UNLESS(sc_input_any_key_down(&s), "space pressed");
    PASS("any_key_down");
    return 0;
}

int main(void) {
    printf("=== sc_input tests ===\n");
    int fail = 0;
    fail += test_init_state();
    fail += test_key_down_up();
    fail += test_key_pressed_released();
    fail += test_mouse();
    fail += test_touch();
    fail += test_gamepad();
    fail += test_char_input();
    fail += test_any_key_down();
    if (fail) printf("  %d input test(s) FAILED.\n", fail);
    else      printf("All input tests passed.\n");
    return fail;
}
