/*
 * sc_input.h  --  SilverCore Input Abstraction Layer
 *
 * State-based input subsystem: poll the current frame's state rather than
 * processing events.  Platform backends (GLFW, X11, Win32, Cocoa, WASM)
 * push into the same SCInputState struct; the application reads from it.
 *
 * Integration
 *   sc_input_poll(state)          — call once per frame (platform-specific)
 *   sc_input_key_down(state, key) — is key currently held?
 *   sc_input_key_pressed(state, k)— was key just pressed this frame?
 *   sc_input_mouse_pos(state)     — current mouse coordinates
 *
 * Thread safety: NOT thread-safe.  Call from the main thread only.
 */
#ifndef SC_INPUT_H
#define SC_INPUT_H

#include "sc_types.h"

/* -------------------------------------------------------------------------
 * Key codes (subset of USB HID usage table)
 * ---------------------------------------------------------------------- */
typedef enum SCKey {
    SC_KEY_UNKNOWN      = 0,

    SC_KEY_A            = 4,
    SC_KEY_B            = 5,
    SC_KEY_C            = 6,
    SC_KEY_D            = 7,
    SC_KEY_E            = 8,
    SC_KEY_F            = 9,
    SC_KEY_G            = 10,
    SC_KEY_H            = 11,
    SC_KEY_I            = 12,
    SC_KEY_J            = 13,
    SC_KEY_K            = 14,
    SC_KEY_L            = 15,
    SC_KEY_M            = 16,
    SC_KEY_N            = 17,
    SC_KEY_O            = 18,
    SC_KEY_P            = 19,
    SC_KEY_Q            = 20,
    SC_KEY_R            = 21,
    SC_KEY_S            = 22,
    SC_KEY_T            = 23,
    SC_KEY_U            = 24,
    SC_KEY_V            = 25,
    SC_KEY_W            = 26,
    SC_KEY_X            = 27,
    SC_KEY_Y            = 28,
    SC_KEY_Z            = 29,

    SC_KEY_1            = 30,
    SC_KEY_2            = 31,
    SC_KEY_3            = 32,
    SC_KEY_4            = 33,
    SC_KEY_5            = 34,
    SC_KEY_6            = 35,
    SC_KEY_7            = 36,
    SC_KEY_8            = 37,
    SC_KEY_9            = 38,
    SC_KEY_0            = 39,

    SC_KEY_ENTER        = 40,
    SC_KEY_ESCAPE       = 41,
    SC_KEY_BACKSPACE    = 42,
    SC_KEY_TAB          = 43,
    SC_KEY_SPACE        = 44,
    SC_KEY_MINUS        = 45,
    SC_KEY_EQUALS       = 46,
    SC_KEY_LEFTBRACE    = 47,
    SC_KEY_RIGHTBRACE   = 48,
    SC_KEY_BACKSLASH    = 49,
    SC_KEY_SEMICOLON    = 51,
    SC_KEY_APOSTROPHE   = 52,
    SC_KEY_GRAVE        = 53,
    SC_KEY_COMMA        = 54,
    SC_KEY_PERIOD       = 55,
    SC_KEY_SLASH        = 56,
    SC_KEY_CAPSLOCK     = 57,

    SC_KEY_F1           = 58,
    SC_KEY_F2           = 59,
    SC_KEY_F3           = 60,
    SC_KEY_F4           = 61,
    SC_KEY_F5           = 62,
    SC_KEY_F6           = 63,
    SC_KEY_F7           = 64,
    SC_KEY_F8           = 65,
    SC_KEY_F9           = 66,
    SC_KEY_F10          = 67,
    SC_KEY_F11          = 68,
    SC_KEY_F12          = 69,

    SC_KEY_LEFT         = 80,
    SC_KEY_RIGHT        = 79,
    SC_KEY_UP           = 82,
    SC_KEY_DOWN         = 81,
    SC_KEY_PAGEUP       = 75,
    SC_KEY_PAGEDOWN     = 78,
    SC_KEY_HOME         = 74,
    SC_KEY_END          = 77,
    SC_KEY_INSERT       = 73,
    SC_KEY_DELETE       = 76,

    SC_KEY_LSHIFT       = 225,
    SC_KEY_RSHIFT       = 229,
    SC_KEY_LCTRL        = 224,
    SC_KEY_RCTRL        = 228,
    SC_KEY_LALT         = 226,
    SC_KEY_RALT         = 230,
    SC_KEY_LSUPER       = 227,
    SC_KEY_RSUPER       = 231,

    SC_KEY_COUNT        = 256,
} SCKey;

/* -------------------------------------------------------------------------
 * Mouse buttons
 * ---------------------------------------------------------------------- */
typedef enum SCMouseButton {
    SC_MOUSE_LEFT       = 0,
    SC_MOUSE_RIGHT      = 1,
    SC_MOUSE_MIDDLE     = 2,
    SC_MOUSE_BACK       = 3,
    SC_MOUSE_FORWARD    = 4,
    SC_MOUSE_COUNT      = 5,
} SCMouseButton;

/* -------------------------------------------------------------------------
 * Gamepad axes / buttons (simplified subset)
 * ---------------------------------------------------------------------- */
#define SC_GAMEPAD_AXIS_COUNT   6
#define SC_GAMEPAD_BUTTON_COUNT 16

typedef enum SCGamepadAxis {
    SC_GAMEPAD_AXIS_LEFTX   = 0,
    SC_GAMEPAD_AXIS_LEFTY   = 1,
    SC_GAMEPAD_AXIS_RIGHTX  = 2,
    SC_GAMEPAD_AXIS_RIGHTY  = 3,
    SC_GAMEPAD_AXIS_LTRIGGER= 4,
    SC_GAMEPAD_AXIS_RTRIGGER= 5,
} SCGamepadAxis;

/* -------------------------------------------------------------------------
 * Per-frame input snapshot
 * ---------------------------------------------------------------------- */
typedef struct SCInputState {
    /* Keyboard: raw HID usage codes */
    bool  keys_cur[SC_KEY_COUNT];
    bool  keys_prev[SC_KEY_COUNT];
    u32   key_count;           /* number of keys pressed this frame */
    u32   char_codepoints[16]; /* UTF-32 codepoints typed this frame */
    u32   char_count;

    /* Mouse */
    f32   mouse_x, mouse_y;
    f32   mouse_dx, mouse_dy;  /* delta from previous frame */
    f32   scroll_x, scroll_y;
    bool  mouse_buttons_cur[SC_MOUSE_COUNT];
    bool  mouse_buttons_prev[SC_MOUSE_COUNT];

    /* Touch (up to 4 simultaneous points) */
    u32   touch_count;
    struct { f32 x, y; bool active; } touches[4];

    /* Gamepad (up to 1 controller) */
    bool  gamepad_present;
    f32   gamepad_axes[SC_GAMEPAD_AXIS_COUNT];
    bool  gamepad_buttons_cur[SC_GAMEPAD_BUTTON_COUNT];
    bool  gamepad_buttons_prev[SC_GAMEPAD_BUTTON_COUNT];
} SCInputState;

/* -------------------------------------------------------------------------
 * Query helpers
 * ---------------------------------------------------------------------- */

SC_INLINE bool sc_input_key_down    (const SCInputState *s, SCKey k) { return s->keys_cur[k]; }
SC_INLINE bool sc_input_key_up      (const SCInputState *s, SCKey k) { return !s->keys_cur[k]; }
SC_INLINE bool sc_input_key_pressed (const SCInputState *s, SCKey k) { return s->keys_cur[k] && !s->keys_prev[k]; }
SC_INLINE bool sc_input_key_released(const SCInputState *s, SCKey k) { return !s->keys_cur[k] && s->keys_prev[k]; }

SC_INLINE bool sc_input_mouse_down   (const SCInputState *s, SCMouseButton b) { return s->mouse_buttons_cur[b]; }
SC_INLINE bool sc_input_mouse_pressed(const SCInputState *s, SCMouseButton b) { return s->mouse_buttons_cur[b] && !s->mouse_buttons_prev[b]; }
SC_INLINE f32  sc_input_mouse_x      (const SCInputState *s) { return s->mouse_x; }
SC_INLINE f32  sc_input_mouse_y      (const SCInputState *s) { return s->mouse_y; }
SC_INLINE void sc_input_mouse_pos    (const SCInputState *s, f32 *x, f32 *y) { *x = s->mouse_x; *y = s->mouse_y; }

SC_INLINE bool sc_input_any_key_down(const SCInputState *s) { return s->key_count > 0; }

/* -------------------------------------------------------------------------
 * Initialiser  (zeroes everything)
 * ---------------------------------------------------------------------- */
SC_INLINE void sc_input_init(SCInputState *s) {
    for (u32 i = 0; i < SC_KEY_COUNT; i++)  s->keys_cur[i] = s->keys_prev[i] = false;
    for (u32 i = 0; i < SC_MOUSE_COUNT; i++) s->mouse_buttons_cur[i] = s->mouse_buttons_prev[i] = false;
    for (u32 i = 0; i < 4; i++)             s->touches[i].active = false;
    for (u32 i = 0; i < SC_GAMEPAD_BUTTON_COUNT; i++) s->gamepad_buttons_cur[i] = s->gamepad_buttons_prev[i] = false;
    s->mouse_x = s->mouse_y = s->mouse_dx = s->mouse_dy = 0;
    s->scroll_x = s->scroll_y = 0;
    s->key_count = s->char_count = 0;
    s->touch_count = 0;
    s->gamepad_present = false;
}

/* -------------------------------------------------------------------------
 * Frame transition  (call at END of each frame, before next sc_input_poll)
 *   cur -> prev, zero deltas
 * ---------------------------------------------------------------------- */
SC_INLINE void sc_input_end_frame(SCInputState *s) {
    for (u32 i = 0; i < SC_KEY_COUNT; i++)  s->keys_prev[i] = s->keys_cur[i];
    for (u32 i = 0; i < SC_MOUSE_COUNT; i++) s->mouse_buttons_prev[i] = s->mouse_buttons_cur[i];
    for (u32 i = 0; i < SC_GAMEPAD_BUTTON_COUNT; i++) s->gamepad_buttons_prev[i] = s->gamepad_buttons_cur[i];
    s->mouse_dx = s->mouse_dy = 0;
    s->scroll_x = s->scroll_y = 0;
    s->char_count = 0;
    s->key_count = 0;
}

/* -------------------------------------------------------------------------
 * Button/key press helpers  (for platform backends)
 * ---------------------------------------------------------------------- */
SC_INLINE void sc_input_set_key(SCInputState *s, SCKey k, bool down) {
    if (s->keys_cur[k] != down) {
        s->keys_cur[k] = down;
        if (down) s->key_count++;
        else if (s->key_count > 0) s->key_count--;
    }
}
SC_INLINE void sc_input_set_mouse_button(SCInputState *s, SCMouseButton b, bool down) {
    s->mouse_buttons_cur[b] = down;
}
SC_INLINE void sc_input_set_mouse_pos(SCInputState *s, f32 x, f32 y) {
    s->mouse_dx = x - s->mouse_x;
    s->mouse_dy = y - s->mouse_y;
    s->mouse_x = x; s->mouse_y = y;
}
SC_INLINE void sc_input_set_scroll(SCInputState *s, f32 dx, f32 dy) {
    s->scroll_x += dx; s->scroll_y += dy;
}
SC_INLINE void sc_input_add_char(SCInputState *s, u32 cp) {
    if (s->char_count < SC_ARRAY_LEN(s->char_codepoints))
        s->char_codepoints[s->char_count++] = cp;
}

#endif /* SC_INPUT_H */
