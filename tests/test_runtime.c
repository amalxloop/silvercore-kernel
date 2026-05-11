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
static _Atomic int g_fiber_yield_count;
static _Atomic int g_fiber_run_order;

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

/* ---- Fiber test helpers ------------------------------------------------ */
typedef struct {
    SCEventLoop *loop;
    int          id;
    int          iter_count;
} FiberTestData;

static void _yield_fiber_fn(void *userdata) {
    FiberTestData *d = (FiberTestData*)userdata;
    for (int i = 0; i < d->iter_count; i++) {
        atomic_fetch_add(&g_fiber_yield_count, 1);
        sc_fiber_yield(d->loop);
    }
}

static void _ordered_fiber_fn(void *userdata) {
    FiberTestData *d = (FiberTestData*)userdata;
    int order = atomic_fetch_add(&g_fiber_run_order, 1);
    /* Record which order we ran in via userdata */
    d->id = order;
    sc_fiber_yield(d->loop);
    /* After resume, record again */
    order = atomic_fetch_add(&g_fiber_run_order, 1);
    d->iter_count = order; /* reuse field to store second-order value */
    sc_fiber_yield(d->loop);
}

/* ---- Fiber tests ------------------------------------------------------- */
static int test_fiber_yield(void) {
    u8 ta_buf[SC_KB(64)];
    SCArena ta;
    sc_arena_init(&ta, ta_buf, sizeof(ta_buf));

    SCEventLoop loop;
    sc_loop_init(&loop, &ta);

    FiberTestData d;
    d.loop       = &loop;
    d.id         = 0;
    d.iter_count = 5;

    atomic_store(&g_fiber_yield_count, 0);
    i32 fid = sc_fiber_spawn(&loop, _yield_fiber_fn, &d, "yield_test");
    FAIL_UNLESS(fid >= 0, "fiber spawned");

    /* Tick 5 times — fiber should yield each time and resume */
    for (int i = 0; i < 6; i++) {
        sc_loop_tick(&loop, sc_clock_ns());
    }

    FAIL_UNLESS(atomic_load(&g_fiber_yield_count) == 5, "fiber yielded 5 times");
    FAIL_UNLESS(loop.fibers[fid].state == SC_FIBER_DEAD, "fiber is DEAD after completion");

    sc_loop_shutdown(&loop);
    PASS("fiber_yield");
    return 0;
}

static int test_fiber_multi(void) {
    u8 ta_buf[SC_KB(64)];
    SCArena ta;
    sc_arena_init(&ta, ta_buf, sizeof(ta_buf));

    SCEventLoop loop;
    sc_loop_init(&loop, &ta);

    FiberTestData d[3];
    for (int i = 0; i < 3; i++) {
        d[i].loop       = &loop;
        d[i].id         = i;
        d[i].iter_count = 3;
        i32 fid = sc_fiber_spawn(&loop, _yield_fiber_fn, &d[i], "multi");
        FAIL_UNLESS(fid >= 0, "multi-fiber spawned");
    }

    atomic_store(&g_fiber_yield_count, 0);

    /* Run enough ticks for all fibers to complete (3 fibers × 3 iter each) */
    for (int i = 0; i < 12; i++) {
        sc_loop_tick(&loop, sc_clock_ns());
    }

    FAIL_UNLESS(atomic_load(&g_fiber_yield_count) == 9, "3 fibers yielded 3 times each = 9");
    for (int i = 0; i < 3; i++) {
        FAIL_UNLESS(loop.fibers[i].state == SC_FIBER_DEAD,
                    "multi-fiber all DEAD after completion");
    }

    sc_loop_shutdown(&loop);
    PASS("fiber_multi");
    return 0;
}

static int test_fiber_ordering(void) {
    u8 ta_buf[SC_KB(64)];
    SCArena ta;
    sc_arena_init(&ta, ta_buf, sizeof(ta_buf));

    SCEventLoop loop;
    sc_loop_init(&loop, &ta);

    FiberTestData d[3];
    for (int i = 0; i < 3; i++) {
        d[i].loop       = &loop;
        d[i].id         = -1;
        d[i].iter_count = -1;
        i32 fid = sc_fiber_spawn(&loop, _ordered_fiber_fn, &d[i], "ordered");
        FAIL_UNLESS(fid >= 0, "ordering fiber spawned");
    }

    atomic_store(&g_fiber_run_order, 0);

    /* First tick: each fiber runs once (3 total) — 0,1,2 assigned as first-order */
    sc_loop_tick(&loop, sc_clock_ns());
    FAIL_UNLESS(atomic_load(&g_fiber_run_order) == 3, "first pass: 3 runs");

    /* Second tick: each fiber resumes, 3,4,5 assigned as second-order */
    sc_loop_tick(&loop, sc_clock_ns());
    FAIL_UNLESS(atomic_load(&g_fiber_run_order) == 6, "second pass: 3 more runs");

    /* Third tick: each fiber resumes and fn returns (DEAD) */
    sc_loop_tick(&loop, sc_clock_ns());

    /* All fibers should be dead now (each yields twice then fn returns) */
    for (int i = 0; i < 3; i++) {
        FAIL_UNLESS(loop.fibers[i].state == SC_FIBER_DEAD,
                    "ordering fiber dead");
    }

    sc_loop_shutdown(&loop);
    PASS("fiber_ordering");
    return 0;
}

int main(void) {
    printf("=== sc_runtime tests ===\n");
    int fail = 0;
    fail += test_task_queue();
    fail += test_timer();
    fail += test_clock();
    printf("--- fiber tests ---\n");
    fail += test_fiber_yield();
    fail += test_fiber_multi();
    fail += test_fiber_ordering();
    if (!fail) printf("All runtime tests passed.\n");
    return fail;
}
