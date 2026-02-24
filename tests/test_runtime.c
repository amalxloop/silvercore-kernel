/*
 * test_runtime.c  --  Unit tests for sc_runtime.h
 */
#define SC_RUNTIME_IMPLEMENTATION
#define SC_GFX_IMPLEMENTATION
#define SC_LAYOUT_IMPLEMENTATION
#define SC_WIDGET_IMPLEMENTATION
#define SC_GFX_BACKEND_SOFTWARE

#include "sc_runtime.h"
#include <stdio.h>
#include <stdatomic.h>

#define FAIL_UNLESS(cond, why) \
    do { if(!(cond)){ printf("  [FAIL] %s\n", why); return 1; } } while(0)
#define PASS(name) printf("  [PASS] %s\n", name)

static _Atomic int g_task_ran;
static _Atomic int g_timer_fired;

static void _task_fn(void *payload) {
    int *val = (int*)payload;
    atomic_fetch_add(&g_task_ran, *val);
}

static void _timer_fn(void *userdata, u64 now_ns) {
    (void)userdata; (void)now_ns;
    atomic_fetch_add(&g_timer_fired, 1);
}

static int test_task_queue(void) {
    u8 ta_buf[SC_KB(64)];
    SCArena ta;
    sc_arena_init(&ta, ta_buf, sizeof(ta_buf));

    SCEventLoop loop;
    sc_loop_init(&loop, &ta);

    int val = 7;
    sc_task_post(&loop, _task_fn, &val, sizeof(val));
    sc_task_post(&loop, _task_fn, &val, sizeof(val));

    atomic_store(&g_task_ran, 0);
    sc_loop_tick(&loop, sc_clock_ns());

    FAIL_UNLESS(atomic_load(&g_task_ran) == 14, "two tasks ran");
    sc_loop_shutdown(&loop);
    PASS("task_queue");
    return 0;
}

static int test_timer(void) {
    u8 ta_buf[SC_KB(64)];
    SCArena ta;
    sc_arena_init(&ta, ta_buf, sizeof(ta_buf));

    SCEventLoop loop;
    sc_loop_init(&loop, &ta);

    atomic_store(&g_timer_fired, 0);

    /* Timer that fires immediately (delay = 0) */
    sc_timer_set(&loop, 0, _timer_fn, NULL);

    sc_loop_tick(&loop, sc_clock_ns() + 1000000ULL /* 1 ms ahead */);
    FAIL_UNLESS(atomic_load(&g_timer_fired) >= 1, "timer fired");

    sc_loop_shutdown(&loop);
    PASS("timer");
    return 0;
}

static int test_clock(void) {
    u64 a = sc_clock_ns();
    /* Busy-wait a tiny bit */
    volatile u64 dummy = 0;
    for (int i = 0; i < 100000; i++) dummy += i;
    u64 b = sc_clock_ns();
    FAIL_UNLESS(b > a, "clock monotonic");
    PASS("clock");
    return 0;
}

int main(void) {
    printf("=== sc_runtime tests ===\n");
    int fail = 0;
    fail += test_task_queue();
    fail += test_timer();
    fail += test_clock();
    if (!fail) printf("All runtime tests passed.\n");
    return fail;
}
