/*
 * test_arena.c  --  Unit tests for sc_arena.h
 */
#include "sc_arena.h"
#include <stdio.h>
#include <string.h>

#define PASS(name)  printf("  [PASS] %s\n", name)
#define FAIL(name)  do { printf("  [FAIL] %s\n", name); return 1; } while(0)
#define ASSERT(cond, name) do { if (!(cond)) FAIL(name); } while(0)

static int test_arena_basic(void) {
    u8 buf[1024];
    SCArena a;
    sc_arena_init(&a, buf, sizeof(buf));

    ASSERT(sc_arena_remaining(&a) == 1024, "initial remaining");

    void *p1 = sc_arena_push(&a, 64);
    ASSERT(p1 != NULL, "push 64");

    void *p2 = sc_arena_push(&a, 64);
    ASSERT(p2 != NULL, "push 64 again");
    ASSERT(p2 != p1,   "distinct pointers");

    sc_arena_reset(&a);
    ASSERT(a.offset == 0, "reset to 0");

    /* Overflow */
    void *p3 = sc_arena_push(&a, 2048);
    ASSERT(p3 == NULL, "overflow returns NULL");

    PASS("arena_basic");
    return 0;
}

static int test_arena_types(void) {
    u8 buf[4096];
    SCArena a;
    sc_arena_init(&a, buf, sizeof(buf));

    typedef struct { i32 x; f32 y; } Pair;
    Pair *p = sc_arena_push_type(&a, Pair);
    ASSERT(p != NULL, "push_type non-null");
    ASSERT(p->x == 0 && p->y == 0.0f, "zeroed");
    p->x = 42; p->y = 3.14f;

    Pair *arr = sc_arena_push_array(&a, Pair, 16);
    ASSERT(arr != NULL, "push_array non-null");
    ASSERT(arr != p,    "distinct from previous");

    PASS("arena_types");
    return 0;
}

static int test_arena_temp(void) {
    u8 buf[1024];
    SCArena a;
    sc_arena_init(&a, buf, sizeof(buf));

    sc_arena_push(&a, 128);
    usize saved = a.offset;

    SCArenaTemp t = sc_arena_temp_begin(&a);
    sc_arena_push(&a, 256);
    ASSERT(a.offset > saved, "temp push");

    sc_arena_temp_end(t);
    ASSERT(a.offset == saved, "temp restored");

    PASS("arena_temp");
    return 0;
}

static int test_pool_basic(void) {
    typedef struct { i32 val; char pad[60]; } Item;

    u8 buf[SC_KB(16)];
    SCPool pool;
    /* Do not wrap the init call in SC_ASSERT — assert is stripped in Release
     * builds (NDEBUG), which would leave pool uninitialised. */
    SCResult init_result = sc_pool_init(&pool, buf, sizeof(buf), sizeof(Item));
    ASSERT(sc_ok(init_result), "pool init ok");

    u32 cap = pool.capacity;
    ASSERT(cap > 0, "pool has capacity");

    Item *a = sc_pool_alloc_type(&pool, Item);
    ASSERT(a != NULL, "alloc 1");
    ASSERT(pool.count == 1, "count 1");
    a->val = 99;

    Item *b = sc_pool_alloc_type(&pool, Item);
    ASSERT(b != NULL, "alloc 2");
    ASSERT(b != a,    "distinct ptrs");
    ASSERT(pool.count == 2, "count 2");

    sc_pool_free(&pool, a);
    ASSERT(pool.count == 1, "free 1");

    Item *c = sc_pool_alloc_type(&pool, Item);
    ASSERT(c != NULL, "realloc");
    ASSERT(pool.count == 2, "recount 2");

    /* Drain the pool */
    while (pool.free_list) { sc_pool_alloc(&pool); }
    void *oom = sc_pool_alloc(&pool);
    ASSERT(oom == NULL, "OOM returns NULL");

    PASS("pool_basic");
    return 0;
}

int main(void) {
    printf("=== sc_arena tests ===\n");
    int fail = 0;
    fail += test_arena_basic();
    fail += test_arena_types();
    fail += test_arena_temp();
    fail += test_pool_basic();
    if (fail == 0) printf("All arena tests passed.\n");
    return fail;
}
