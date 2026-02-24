/*
 * sc_arena.h  --  SilverCore Memory Subsystem
 *
 * Two allocators:
 *
 *  SCArena   – linear / bump allocator
 *              O(1) alloc, O(1) full-reset, zero GC pauses.
 *              Thread-local ownership; no locking.
 *
 *  SCPool    – typed fixed-size slab pool
 *              O(1) alloc/free via free-list intrusive into each slot.
 *              Ideal for widgets, nodes, draw-commands, etc.
 *
 * Design goals
 *  - Zero stdlib malloc after startup
 *  - Cache-friendly: blocks are contiguous; allocations are cache-line
 *    aligned by default
 *  - No pointer-chasing on the hot path
 *
 * Usage sketch
 *  SCArena frame;
 *  sc_arena_init(&frame, backing_buf, SC_MB(4));
 *
 *  MyThing *t = sc_arena_push_type(&frame, MyThing);
 *  // ... use t ...
 *  sc_arena_reset(&frame);   // free everything in one instruction
 */
#ifndef SC_ARENA_H
#define SC_ARENA_H

#include "sc_types.h"
#include <stdlib.h>   /* only used in sc_arena_init_heap / sc_pool_init_heap */
#include <string.h>   /* memset */

/* =========================================================================
 * Arena (linear / bump allocator)
 * ====================================================================== */

typedef struct SCArena {
    u8    *base;      /* start of backing memory              */
    usize  size;      /* total capacity in bytes              */
    usize  offset;    /* next free byte                       */
    usize  prev_off;  /* offset before last push (undo once)  */
    u32    temp_depth;/* how many temp save-points are open   */
} SCArena;

/* Initialise with caller-owned buffer (no heap allocation). */
SC_INLINE void sc_arena_init(SCArena *a, void *buf, usize size) {
    SC_ASSERT(buf && size > 0);
    a->base       = (u8*)buf;
    a->size       = size;
    a->offset     = 0;
    a->prev_off   = 0;
    a->temp_depth = 0;
}

/* Initialise by heap-allocating the backing store. */
SC_INLINE SCResult sc_arena_init_heap(SCArena *a, usize size) {
    void *buf = malloc(size);
    if (SC_UNLIKELY(!buf)) return SC_ERR_OOM;
    sc_arena_init(a, buf, size);
    return SC_OK;
}

SC_INLINE void sc_arena_destroy_heap(SCArena *a) {
    free(a->base);
    memset(a, 0, sizeof(*a));
}

/* Core push – returns aligned pointer or NULL on overflow. */
SC_INLINE void *sc_arena_push_aligned(SCArena *a, usize nbytes, usize align) {
    SC_ASSERT(SC_IS_POW2(align));
    usize aligned = SC_ALIGN_UP(a->offset, align);
    /* Guard against usize overflow before the bounds check (bug #3) */
    if (SC_UNLIKELY(aligned < a->offset)) return NULL;          /* alignment wrapped */
    if (SC_UNLIKELY(nbytes > a->size - aligned)) return NULL;   /* overflow-safe OOB check */
    a->prev_off = a->offset;
    a->offset   = aligned + nbytes;
    void *ptr   = a->base + aligned;
    memset(ptr, 0, nbytes);
    return ptr;
}

/* Default: cache-line aligned. */
SC_INLINE void *sc_arena_push(SCArena *a, usize nbytes) {
    return sc_arena_push_aligned(a, nbytes, SC_CACHE_LINE_SIZE);
}

/* Typed helpers. */
#define sc_arena_push_type(arena, T) \
    ((T*)sc_arena_push_aligned((arena), sizeof(T), _Alignof(T)))

#define sc_arena_push_array(arena, T, n) \
    ((T*)sc_arena_push_aligned((arena), sizeof(T)*(n), _Alignof(T)))

/* Pop last allocation (one level). */
SC_INLINE void sc_arena_pop(SCArena *a) {
    a->offset = a->prev_off;
}

/* Reset to empty (keeps backing memory). */
SC_INLINE void sc_arena_reset(SCArena *a) {
    a->offset = 0; a->prev_off = 0;
}

/* Bytes remaining. */
SC_INLINE usize sc_arena_remaining(const SCArena *a) {
    return a->size - a->offset;
}

/* ---- Temporary save / restore ---------------------------------------- */
typedef struct SCArenaTemp { SCArena *arena; usize saved_off; } SCArenaTemp;

SC_INLINE SCArenaTemp sc_arena_temp_begin(SCArena *a) {
    SCArenaTemp t = {a, a->offset};
    a->temp_depth++;
    return t;
}

SC_INLINE void sc_arena_temp_end(SCArenaTemp t) {
    SC_ASSERT(t.arena->temp_depth > 0);
    t.arena->offset = t.saved_off;
    t.arena->temp_depth--;
}

/* =========================================================================
 * Pool allocator (typed fixed-size slab)
 * ====================================================================== */

/*
 * Free-list node embedded at the start of every free slot.
 * When the slot is in use, the caller owns that memory.
 */
typedef struct SCPoolFreeNode { struct SCPoolFreeNode *next; } SCPoolFreeNode;

typedef struct SCPool {
    u8              *base;      /* backing memory                */
    usize            slot_size; /* bytes per element (>= ptr)    */
    u32              capacity;  /* total slots                   */
    u32              count;     /* live allocations              */
    SCPoolFreeNode  *free_list; /* singly linked free list       */
} SCPool;

SC_INLINE SCResult sc_pool_init(SCPool *p, void *buf, usize buf_size, usize slot_size) {
    SC_ASSERT(buf && slot_size >= sizeof(SCPoolFreeNode));
    slot_size = SC_ALIGN_UP(slot_size, SC_CACHE_LINE_SIZE);
    u32 cap   = (u32)(buf_size / slot_size);
    if (cap == 0) return SC_ERR_INVALID_ARG;
    p->base      = (u8*)buf;
    p->slot_size = slot_size;
    p->capacity  = cap;
    p->count     = 0;
    /* Build free list through all slots */
    p->free_list = NULL;
    for (i32 i = (i32)cap - 1; i >= 0; i--) {
        SCPoolFreeNode *node = (SCPoolFreeNode*)(p->base + (usize)i * slot_size);
        node->next   = p->free_list;
        p->free_list = node;
    }
    return SC_OK;
}

SC_INLINE SCResult sc_pool_init_heap(SCPool *p, u32 cap, usize slot_size) {
    slot_size = SC_ALIGN_UP(slot_size, SC_CACHE_LINE_SIZE);
    /* Bug #1: guard against overflow in cap * slot_size before malloc */
    if (SC_UNLIKELY(cap == 0 || slot_size == 0)) return SC_ERR_INVALID_ARG;
    if (SC_UNLIKELY((usize)cap > (SIZE_MAX / slot_size))) return SC_ERR_OVERFLOW;
    void *buf = malloc((usize)cap * slot_size);
    if (!buf) return SC_ERR_OOM;
    return sc_pool_init(p, buf, (usize)cap * slot_size, slot_size);
}

SC_INLINE void sc_pool_destroy_heap(SCPool *p) {
    free(p->base);
    memset(p, 0, sizeof(*p));
}

SC_INLINE void *sc_pool_alloc(SCPool *p) {
    if (SC_UNLIKELY(!p->free_list)) return NULL;
    SCPoolFreeNode *node = p->free_list;
    p->free_list = node->next;
    p->count++;
    memset(node, 0, p->slot_size);
    return node;
}

SC_INLINE void sc_pool_free(SCPool *p, void *ptr) {
    SC_ASSERT(ptr);
    /* Bug #2: reject pointers outside the pool's backing buffer */
    SC_ASSERT((u8*)ptr >= p->base &&
              (u8*)ptr < p->base + (usize)p->capacity * p->slot_size);
    SC_ASSERT(((usize)((u8*)ptr - p->base) % p->slot_size) == 0);
    SCPoolFreeNode *node = (SCPoolFreeNode*)ptr;
    node->next   = p->free_list;
    p->free_list = node;
    p->count--;
}

#define sc_pool_alloc_type(pool, T) ((T*)sc_pool_alloc(pool))

/* =========================================================================
 * Memory stats (debug)
 * ====================================================================== */
typedef struct SCMemStats {
    usize arena_used;
    usize arena_capacity;
    u32   pool_count;
    u32   pool_capacity;
} SCMemStats;

SC_INLINE SCMemStats sc_arena_stats(const SCArena *a) {
    SCMemStats s = {a->offset, a->size, 0, 0};
    return s;
}

SC_INLINE SCMemStats sc_pool_stats(const SCPool *p) {
    SCMemStats s = {p->count * p->slot_size, p->capacity * p->slot_size,
                    p->count, p->capacity};
    return s;
}

#endif /* SC_ARENA_H */
