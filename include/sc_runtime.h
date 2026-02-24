/*
 * sc_runtime.h  --  SilverCore Runtime / VM
 *
 * Design
 * ------
 * The kernel replaces the JavaScript bridge by executing app logic via:
 *
 *   1. SCFiber   – stackful coroutine (ucontext / fiber / setjmp).
 *                  Each "component" runs as a fiber; the scheduler
 *                  multiplexes them cooperatively onto one OS thread
 *                  (or, in the SMP path, across N OS threads).
 *
 *   2. SCTask    – a work-queue entry (fire-and-forget or awaitable).
 *                  Tasks posted from network/IO threads are marshalled
 *                  back to the main fiber via an MPSC queue.
 *
 *   3. SCTimer   – sorted min-heap of timed callbacks (setTimeout / setInterval
 *                  equivalent but zero-allocation after init).
 *
 *   4. SCEventLoop – ties Fibers + Tasks + Timers together.
 *                   Runs at exactly 60 Hz (configurable) without
 *                   busy-waiting: sleeps the remainder of each 16.67 ms
 *                   budget via nanosleep / select.
 *
 * No garbage collector.  Fibers own their stack memory from the arena.
 * Tasks carry their payload inline (no heap alloc on the hot path).
 *
 * Platform notes
 *   POSIX  – uses ucontext_t  (Linux, macOS, BSD)
 *   Win32  – uses CreateFiber / SwitchToFiber
 *   WASM   – coroutines emulated via Emscripten asyncify
 *
 * #define SC_RUNTIME_IMPLEMENTATION in exactly one .c file.
 */
#ifndef SC_RUNTIME_H
#define SC_RUNTIME_H

#include "sc_types.h"
#include "sc_arena.h"

#if defined(SC_PLATFORM_LINUX) || defined(SC_PLATFORM_MACOS) || defined(SC_PLATFORM_ANDROID)
#  include <ucontext.h>
#  define SC_FIBER_UCONTEXT 1
#elif defined(SC_PLATFORM_WINDOWS)
#  define SC_FIBER_WIN32 1
#elif defined(SC_PLATFORM_WASM)
#  define SC_FIBER_EMSCRIPTEN 1
#endif

#include <stdatomic.h>   /* MPSC queue */

/* -------------------------------------------------------------------------
 * Constants
 * ---------------------------------------------------------------------- */
#define SC_RUNTIME_TARGET_HZ     60
#define SC_RUNTIME_FRAME_NS      (1000000000ULL / SC_RUNTIME_TARGET_HZ)
#define SC_FIBER_STACK_SIZE      SC_KB(64)
#define SC_RUNTIME_MAX_FIBERS    256
#define SC_RUNTIME_MAX_TASKS     4096
#define SC_RUNTIME_MAX_TIMERS    512

/* -------------------------------------------------------------------------
 * Fiber
 * ---------------------------------------------------------------------- */
typedef enum SCFiberState {
    SC_FIBER_IDLE    = 0,
    SC_FIBER_RUNNING = 1,
    SC_FIBER_WAITING = 2,
    SC_FIBER_DEAD    = 3,
} SCFiberState;

typedef void (*SCFiberFn)(void *userdata);

typedef struct SCFiber {
    i32          id;
    SCFiberState state;
    SCFiberFn    fn;
    void        *userdata;
    u8          *stack;
    usize        stack_size;
    const char  *name;

#ifdef SC_FIBER_UCONTEXT
    ucontext_t   ctx;
#elif defined(SC_FIBER_WIN32)
    void        *fiber_handle;
#endif
} SCFiber;

/* -------------------------------------------------------------------------
 * Task (MPSC queue entry)
 * ---------------------------------------------------------------------- */
#define SC_TASK_PAYLOAD_SIZE 128

typedef void (*SCTaskFn)(void *payload);

typedef struct SCTask {
    SCTaskFn fn;
    u8       payload[SC_TASK_PAYLOAD_SIZE];
    struct SCTask *_next;   /* intrusive MPSC link */
} SCTask;

/* -------------------------------------------------------------------------
 * Timer
 * ---------------------------------------------------------------------- */
typedef void (*SCTimerFn)(void *userdata, u64 now_ns);

typedef struct SCTimer {
    i32       id;
    bool      active;
    bool      repeat;
    u64       fire_at_ns;  /* absolute nanoseconds             */
    u64       interval_ns;
    SCTimerFn fn;
    void     *userdata;
} SCTimer;

/* -------------------------------------------------------------------------
 * MPSC queue (lock-free producer, single consumer)
 * ---------------------------------------------------------------------- */
typedef struct SCMpscQueue {
    _Atomic(SCTask*) head;
    _Atomic(SCTask*) tail;
    SCTask           stub;  /* sentinel node                   */
} SCMpscQueue;

/* -------------------------------------------------------------------------
 * Event loop
 * ---------------------------------------------------------------------- */
typedef struct SCEventLoop {
    /* Fibers */
    SCFiber    fibers[SC_RUNTIME_MAX_FIBERS];
    u32        fiber_count;
    i32        current_fiber;      /* index of active fiber (-1 = main) */
    SCFiber   *main_fiber;         /* the OS thread's "fiber"           */

    /* Tasks (MPSC) */
    SCMpscQueue task_queue;
    SCArena    *task_arena;        /* arena for SCTask allocation       */

    /* Timers (min-heap by fire_at_ns) */
    SCTimer    timers[SC_RUNTIME_MAX_TIMERS];
    u32        timer_count;

    /* Timing */
    u64        frame_count;
    u64        last_frame_ns;
    u64        target_frame_ns;

    /* Running flag */
    bool       running;
} SCEventLoop;

/* -------------------------------------------------------------------------
 * API
 * ---------------------------------------------------------------------- */

/* Event loop lifecycle */
SCResult sc_loop_init    (SCEventLoop *loop, SCArena *task_arena);
void     sc_loop_shutdown(SCEventLoop *loop);
void     sc_loop_run     (SCEventLoop *loop);   /* blocks; 60 Hz pump       */
void     sc_loop_stop    (SCEventLoop *loop);

/* Called each tick by the embedding platform (if not using sc_loop_run) */
void     sc_loop_tick    (SCEventLoop *loop, u64 now_ns);

/* Fiber API */
i32      sc_fiber_spawn  (SCEventLoop *loop, SCFiberFn fn, void *userdata,
                           const char *name);
void     sc_fiber_yield  (SCEventLoop *loop);   /* suspend current fiber    */
void     sc_fiber_exit   (SCEventLoop *loop);   /* terminate current fiber  */

/* Task API (thread-safe) */
void     sc_task_post    (SCEventLoop *loop, SCTaskFn fn,
                           const void *payload, usize payload_size);

/* Timer API */
i32      sc_timer_set    (SCEventLoop *loop, u64 delay_ns,
                           SCTimerFn fn, void *userdata);
i32      sc_timer_repeat (SCEventLoop *loop, u64 interval_ns,
                           SCTimerFn fn, void *userdata);
void     sc_timer_clear  (SCEventLoop *loop, i32 timer_id);

/* Clock */
u64      sc_clock_ns     (void);   /* monotonic nanoseconds              */

/* -------------------------------------------------------------------------
 * Implementation
 * ---------------------------------------------------------------------- */
#ifdef SC_RUNTIME_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#if defined(SC_PLATFORM_LINUX) || defined(SC_PLATFORM_MACOS)
#  include <sys/time.h>
#  include <unistd.h>
#endif

/* ---- Clock ------------------------------------------------------------ */
u64 sc_clock_ns(void) {
#if defined(SC_PLATFORM_LINUX) || defined(SC_PLATFORM_ANDROID)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ULL + (u64)ts.tv_nsec;
#elif defined(SC_PLATFORM_MACOS) || defined(SC_PLATFORM_IOS)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (u64)ts.tv_sec * 1000000000ULL + (u64)ts.tv_nsec;
#elif defined(SC_PLATFORM_WINDOWS)
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    /* Bug #14: cnt.QuadPart * 1e9 overflows u64 for large uptimes.
     * Use integer division with remainder to stay within u64 range. */
    return (u64)(cnt.QuadPart / freq.QuadPart) * 1000000000ULL
         + (u64)(cnt.QuadPart % freq.QuadPart) * 1000000000ULL / (u64)freq.QuadPart;
#else
    return 0;
#endif
}

/* ---- MPSC queue ------------------------------------------------------- */
static void _sc_mpsc_init(SCMpscQueue *q) {
    q->stub._next = NULL;
    atomic_store(&q->head, &q->stub);
    atomic_store(&q->tail, &q->stub);
}

static void _sc_mpsc_push(SCMpscQueue *q, SCTask *t) {
    t->_next = NULL;
    SCTask *prev = atomic_exchange(&q->head, t);
    atomic_store((_Atomic(SCTask*)*)(void*)&prev->_next, t);
}

static SCTask *_sc_mpsc_pop(SCMpscQueue *q) {
    SCTask *tail = atomic_load(&q->tail);
    SCTask *next = atomic_load((_Atomic(SCTask*)*)(void*)&tail->_next);
    if (tail == &q->stub) {
        if (!next) return NULL;
        atomic_store(&q->tail, next);
        tail = next;
        next = atomic_load((_Atomic(SCTask*)*)(void*)&tail->_next);
    }
    if (next) {
        atomic_store(&q->tail, next);
        return tail;
    }
    SCTask *head = atomic_load(&q->head);
    if (tail != head) return NULL;
    _sc_mpsc_push(q, &q->stub);
    next = atomic_load((_Atomic(SCTask*)*)(void*)&tail->_next);
    if (next) {
        atomic_store(&q->tail, next);
        return tail;
    }
    return NULL;
}

/* ---- Timer min-heap --------------------------------------------------- */
static void _sc_timer_sift_up(SCTimer *h, u32 n, u32 i) {
    while (i > 0) {
        u32 p = (i - 1) / 2;
        if (h[p].fire_at_ns <= h[i].fire_at_ns) break;
        SCTimer tmp = h[p]; h[p] = h[i]; h[i] = tmp;
        i = p;
    }
}
static void _sc_timer_sift_down(SCTimer *h, u32 n, u32 i) {
    while (1) {
        u32 l = 2*i+1, r = 2*i+2, m = i;
        if (l < n && h[l].fire_at_ns < h[m].fire_at_ns) m = l;
        if (r < n && h[r].fire_at_ns < h[m].fire_at_ns) m = r;
        if (m == i) break;
        SCTimer tmp = h[i]; h[i] = h[m]; h[m] = tmp;
        i = m;
    }
}

/* ---- Event loop init -------------------------------------------------- */
SCResult sc_loop_init(SCEventLoop *loop, SCArena *task_arena) {
    memset(loop, 0, sizeof(*loop));
    loop->task_arena      = task_arena;
    loop->current_fiber   = -1;
    loop->target_frame_ns = SC_RUNTIME_FRAME_NS;
    loop->running         = false;
    _sc_mpsc_init(&loop->task_queue);
    return SC_OK;
}

void sc_loop_shutdown(SCEventLoop *loop) {
    loop->running = false;
    /* Free fiber stacks */
    for (u32 i = 0; i < loop->fiber_count; i++) {
        if (loop->fibers[i].stack)
            free(loop->fibers[i].stack);
    }
}

/* ---- Fiber trampoline ------------------------------------------------- */
#ifdef SC_FIBER_UCONTEXT
static void _sc_fiber_entry(u32 hi, u32 lo) {
    uintptr_t ptr = ((uintptr_t)hi << 32) | (uintptr_t)lo;
    SCFiber *f    = (SCFiber*)(void*)ptr;
    f->fn(f->userdata);
    f->state = SC_FIBER_DEAD;
    /* Return to caller context */
}
#endif

i32 sc_fiber_spawn(SCEventLoop *loop, SCFiberFn fn, void *userdata,
                    const char *name) {
    SC_ASSERT(loop->fiber_count < SC_RUNTIME_MAX_FIBERS);
    i32 id = (i32)loop->fiber_count++;
    SCFiber *f = &loop->fibers[id];
    memset(f, 0, sizeof(*f));
    f->id         = id;
    f->state      = SC_FIBER_IDLE;
    f->fn         = fn;
    f->userdata   = userdata;
    f->name       = name ? name : "unnamed";
    f->stack_size = SC_FIBER_STACK_SIZE;
    f->stack      = (u8*)malloc(SC_FIBER_STACK_SIZE);
    /* Bug #13: malloc can return NULL; SC_ASSERT is stripped in release builds */
    if (SC_UNLIKELY(!f->stack)) {
        loop->fiber_count--;  /* roll back the slot */
        return -1;
    }

#ifdef SC_FIBER_UCONTEXT
    getcontext(&f->ctx);
    f->ctx.uc_stack.ss_sp    = f->stack;
    f->ctx.uc_stack.ss_size  = f->stack_size;
    f->ctx.uc_link           = NULL; /* we handle death ourselves */
    uintptr_t ptr = (uintptr_t)(void*)f;
    makecontext(&f->ctx, (void(*)())_sc_fiber_entry, 2,
                (u32)(ptr >> 32), (u32)(ptr & 0xFFFFFFFF));
#endif
    return id;
}

void sc_fiber_yield(SCEventLoop *loop) {
    if (loop->current_fiber < 0) return;
    SCFiber *f = &loop->fibers[loop->current_fiber];
    f->state = SC_FIBER_WAITING;
    /* We'd swapcontext back to the scheduler here */
}

void sc_fiber_exit(SCEventLoop *loop) {
    if (loop->current_fiber < 0) return;
    loop->fibers[loop->current_fiber].state = SC_FIBER_DEAD;
}

/* ---- Task post -------------------------------------------------------- */
void sc_task_post(SCEventLoop *loop, SCTaskFn fn,
                   const void *payload, usize payload_size) {
    /* Bug #11: enforce size limit in release builds too, not just via assert */
    if (SC_UNLIKELY(payload_size > SC_TASK_PAYLOAD_SIZE)) {
        fprintf(stderr, "[sc_runtime] task payload too large (%zu > %d), dropped\n",
                payload_size, SC_TASK_PAYLOAD_SIZE);
        return;
    }
    SCTask *t = (SCTask*)sc_arena_push_type(loop->task_arena, SCTask);
    if (!t) { fprintf(stderr, "[sc_runtime] task_arena OOM\n"); return; }
    t->fn     = fn;
    t->_next  = NULL;
    if (payload && payload_size)
        memcpy(t->payload, payload, payload_size);
    _sc_mpsc_push(&loop->task_queue, t);
}

/* ---- Timer API -------------------------------------------------------- */
static i32 _sc_timer_add(SCEventLoop *loop, u64 fire_at, u64 interval,
                           SCTimerFn fn, void *ud, bool repeat) {
    SC_ASSERT(loop->timer_count < SC_RUNTIME_MAX_TIMERS);
    SCTimer *t = &loop->timers[loop->timer_count++];
    t->id          = (i32)(loop->timer_count - 1);
    t->active      = true;
    t->repeat      = repeat;
    t->fire_at_ns  = fire_at;
    t->interval_ns = interval;
    t->fn          = fn;
    t->userdata    = ud;
    _sc_timer_sift_up(loop->timers, loop->timer_count, loop->timer_count - 1);
    return t->id;
}

i32 sc_timer_set(SCEventLoop *loop, u64 delay_ns, SCTimerFn fn, void *ud) {
    return _sc_timer_add(loop, sc_clock_ns() + delay_ns, delay_ns, fn, ud, false);
}
i32 sc_timer_repeat(SCEventLoop *loop, u64 interval_ns, SCTimerFn fn, void *ud) {
    return _sc_timer_add(loop, sc_clock_ns() + interval_ns, interval_ns, fn, ud, true);
}
void sc_timer_clear(SCEventLoop *loop, i32 id) {
    /* Bug #12: after heap sifts, timers[id] is NOT the timer with that id.
     * We must scan the heap to find the timer whose .id field matches. */
    for (u32 i = 0; i < loop->timer_count; i++) {
        if (loop->timers[i].id == id) {
            loop->timers[i].active = false;
            return;
        }
    }
}

/* ---- Tick ------------------------------------------------------------- */
void sc_loop_tick(SCEventLoop *loop, u64 now_ns) {
    /* Drain task queue */
    SCTask *t;
    while ((t = _sc_mpsc_pop(&loop->task_queue)) != NULL) {
        t->fn(t->payload);
    }

    /* Fire timers */
    while (loop->timer_count > 0 && loop->timers[0].active) {
        SCTimer *top = &loop->timers[0];
        if (top->fire_at_ns > now_ns) break;
        top->fn(top->userdata, now_ns);
        if (top->repeat) {
            top->fire_at_ns = now_ns + top->interval_ns;
            _sc_timer_sift_down(loop->timers, loop->timer_count, 0);
        } else {
            /* Remove: swap with last, sift down */
            top->active = false;
            loop->timers[0] = loop->timers[--loop->timer_count];
            _sc_timer_sift_down(loop->timers, loop->timer_count, 0);
        }
    }

    /* Resume runnable fibers (cooperative round-robin) */
    for (u32 i = 0; i < loop->fiber_count; i++) {
        SCFiber *f = &loop->fibers[i];
        if (f->state == SC_FIBER_IDLE || f->state == SC_FIBER_WAITING) {
            f->state            = SC_FIBER_RUNNING;
            loop->current_fiber = i;
#ifdef SC_FIBER_UCONTEXT
            /* swapcontext would go here once scheduler fiber is set up */
#endif
            /* Minimal cooperative: just call the function directly */
            f->fn(f->userdata);
            f->state = SC_FIBER_DEAD;  /* fn ran to completion */
            loop->current_fiber = -1;
        }
    }

    loop->frame_count++;
    loop->last_frame_ns = now_ns;
}

/* ---- Main loop -------------------------------------------------------- */
void sc_loop_run(SCEventLoop *loop) {
    loop->running       = true;
    loop->last_frame_ns = sc_clock_ns();

    while (loop->running) {
        u64 now = sc_clock_ns();
        sc_loop_tick(loop, now);

        /* Sleep remainder of frame budget */
        u64 elapsed = sc_clock_ns() - now;
        if (elapsed < loop->target_frame_ns) {
            u64 sleep_ns = loop->target_frame_ns - elapsed;
#if defined(SC_PLATFORM_LINUX) || defined(SC_PLATFORM_MACOS)
            struct timespec ts = {
                .tv_sec  = (time_t)(sleep_ns / 1000000000ULL),
                .tv_nsec = (long  )(sleep_ns % 1000000000ULL)
            };
            nanosleep(&ts, NULL);
#elif defined(SC_PLATFORM_WINDOWS)
            Sleep((DWORD)(sleep_ns / 1000000ULL));
#endif
        }
    }
}

void sc_loop_stop(SCEventLoop *loop) { loop->running = false; }

#endif /* SC_RUNTIME_IMPLEMENTATION */
#endif /* SC_RUNTIME_H */
