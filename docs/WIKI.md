# SilverCore-UI — Complete Developer Wiki

> Everything you need to understand, extend, and ship the kernel.

---

## Table of Contents

1. [Design Philosophy](#1-design-philosophy)
2. [Repository Layout](#2-repository-layout)
3. [Build System](#3-build-system)
4. [Subsystem Reference](#4-subsystem-reference)
   - 4.1 [sc_types.h — Primitives](#41-sc_typesh--primitives)
   - 4.2 [sc_math.h — Math & SIMD](#42-sc_mathh--math--simd)
   - 4.3 [sc_arena.h — Memory](#43-sc_arenah--memory)
   - 4.4 [sc_layout.h — Flexbox Engine](#44-sc_layouth--flexbox-engine)
   - 4.5 [sc_gfx.h — Graphics Layer](#45-sc_gfxh--graphics-layer)
   - 4.6 [sc_widget.h — Widget & Scene](#46-sc_widgeth--widget--scene)
   - 4.7 [sc_runtime.h — Runtime & Event Loop](#47-sc_runtimeh--runtime--event-loop)
5. [Backend Porting Guide](#5-backend-porting-guide)
6. [Python Bindings](#6-python-bindings)
7. [WebAssembly / Emscripten](#7-webassembly--emscripten)
8. [Writing a New App](#8-writing-a-new-app)
9. [Test Suite](#9-test-suite)
10. [Performance Model](#10-performance-model)
11. [Coding Conventions](#11-coding-conventions)
12. [Roadmap & Extension Points](#12-roadmap--extension-points)
13. [Glossary](#13-glossary)

---

## 1. Design Philosophy

### No runtime middleman

Every existing cross-platform UI stack interposes at least one of:

- A garbage-collected VM (V8, Hermes, Dart)
- A reflection/bridge layer (React Native JS bridge, Flutter platform channels)
- An HTML/CSS layout engine running in a browser-inside-an-app (Electron)

SilverCore removes all of these. The build output is a native binary (or WASM module). App logic, layout math, and draw calls are in the same address space, compiled together by the same LLVM/GCC pass. There is no serialization, no thread hop, no GC pause between "my data changed" and "pixel on screen."

### Single-header, single-TU philosophy

Every subsystem is a **single-header library** — declaration and implementation live in one `.h` file, gated by an `_IMPLEMENTATION` macro. This means:

- Zero dependency management beyond CMake include paths.
- The entire kernel compiles as a **unity build** (`sc_kernel.c`) — one translation unit. The compiler can inline across subsystem boundaries as if you wrote it all in one file.
- Headers are still individually includable in consuming code without pulling in the implementation.

### Explicit over implicit memory

No hidden allocations. Every subsystem receives its memory as a parameter:

```c
sc_arena_init(&arena, my_buffer, sizeof(my_buffer));
sc_gfx_init(&desc, &ctx);   // desc.frame_arena = &arena
```

The hot path (per-frame updates, draw calls, layout compute) performs **zero calls to `malloc`/`free`**. All per-frame scratch goes through an `SCArena` that is `reset()`-ed at end-of-frame in one instruction.

---

## 2. Repository Layout

```
silvercore-kernel/
│
├── include/              Public API — include these from your app
│   ├── sc_types.h
│   ├── sc_math.h
│   ├── sc_arena.h
│   ├── sc_layout.h
│   ├── sc_gfx.h
│   ├── sc_widget.h
│   └── sc_runtime.h
│
├── backends/             GPU backend stubs — one per graphics API
│   ├── sc_backend_vulkan.h
│   ├── sc_backend_metal.h
│   └── sc_backend_d3d12.h
│
├── bindings/
│   └── python/
│       └── silvercore.py  ctypes wrapper + pure-Python simulation mode
│
├── tools/
│   └── wasm/
│       ├── CMakeLists.wasm.cmake   Emscripten target rules
│       └── silvercore.js           Browser JS glue (WASM loader + Canvas2D blit)
│
├── apps/
│   └── stock_dashboard/
│       └── stock_dashboard.c       Reference PoC — 1 024 live tickers @ 60 Hz
│
├── tests/
│   ├── CMakeLists.txt
│   ├── test_arena.c
│   ├── test_math.c
│   ├── test_layout.c
│   ├── test_gfx.c
│   └── test_runtime.c
│
└── CMakeLists.txt
```

---

## 3. Build System

### CMakeLists.txt — what it does

1. Writes `build/sc_kernel.c` at configure time — this is the unity build TU. It `#define`s all `_IMPLEMENTATION` macros then `#include`s every header in dependency order.
2. Compiles `sc_kernel_static` (`.a`) and optionally `sc_kernel` (`.so`/`.dylib`/`.dll`) from that single TU.
3. Builds `stock_dashboard` as a standalone executable (it defines its own `_IMPLEMENTATION` macros at the top).
4. Optionally builds the test suite via `add_subdirectory(tests)`.

### Configure + build

```bash
# Release build, all targets
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Debug + sanitizers
cmake -S . -B build-dbg -DCMAKE_BUILD_TYPE=Debug -DSC_ASAN=ON -DSC_UBSAN=ON
cmake --build build-dbg -j$(nproc)

# Without the shared library (reduces compile time)
cmake -S . -B build -DSC_SHARED=OFF
```

### CMake variables reference

| Variable | Type | Default | Meaning |
|---|---|---|---|
| `CMAKE_BUILD_TYPE` | string | `Release` | `Release` enables `-O3 -march=native -ffast-math` |
| `SC_TESTS` | bool | `ON` | Build `tests/` |
| `SC_SHARED` | bool | `ON` | Build `libsc_kernel.so` for Python ctypes |
| `SC_ASAN` | bool | `OFF` | `-fsanitize=address` |
| `SC_UBSAN` | bool | `OFF` | `-fsanitize=undefined` |
| `WASM` | bool | `OFF` | Include `tools/wasm/CMakeLists.wasm.cmake` |

### WASM build

Requires the Emscripten SDK activated in your shell:

```bash
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm -DWASM=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm -j$(nproc)
```

Output: `build-wasm/silvercore.js` + `build-wasm/silvercore.wasm`

---

## 4. Subsystem Reference

### 4.1 `sc_types.h` — Primitives

**Purpose:** Single include that every other header pulls in. Provides types, macros, and result codes so nothing depends on scattered `<stdint.h>` includes.

**Key types**

| C type | Meaning |
|---|---|
| `u8/u16/u32/u64` | Unsigned integers |
| `i8/i16/i32/i64` | Signed integers |
| `f32 / f64` | `float` / `double` |
| `usize / isize` | `size_t` / `ptrdiff_t` |
| `uptr` | `uintptr_t` |
| `SCResult` | Integer result code: `SC_OK=0`, negative = error |
| `SCColor` | `{f32 r,g,b,a}` — floating-point RGBA |
| `SCRect2f / SCRect2i` | Rectangle (x, y, w, h) |
| `SCEdgeInsets` | `{top, right, bottom, left}` — CSS-style spacing |

**Key macros**

| Macro | Use |
|---|---|
| `SC_INLINE` | `static inline __attribute__((always_inline))` |
| `SC_LIKELY(x)` / `SC_UNLIKELY(x)` | Branch prediction hints |
| `SC_PREFETCH(p)` | `__builtin_prefetch` |
| `SC_ASSERT(expr)` | Debug assertion — maps to `assert()` |
| `SC_STATIC_ASSERT(c)` | Compile-time assertion |
| `SC_ARRAY_LEN(a)` | `sizeof(a)/sizeof(a[0])` |
| `SC_KB(n)` / `SC_MB(n)` / `SC_GB(n)` | Size literals |
| `SC_ALIGN_UP(x,a)` | Round `x` up to alignment `a` (must be power-of-2) |
| `SC_MIN/MAX/CLAMP` | Value clamping |
| `sc_rgba(r,g,b,a)` | Construct `SCColor` from floats |
| `sc_rgba8(r,g,b,a)` | Construct `SCColor` from 0–255 bytes |

**Platform defines set automatically**

`SC_PLATFORM_LINUX`, `SC_PLATFORM_MACOS`, `SC_PLATFORM_IOS`, `SC_PLATFORM_WINDOWS`, `SC_PLATFORM_ANDROID`, `SC_PLATFORM_WASM`

**Compiler defines set automatically**

`SC_COMPILER_GCC`, `SC_COMPILER_CLANG`, `SC_COMPILER_MSVC`

---

### 4.2 `sc_math.h` — Math & SIMD

**Purpose:** 2-D / 3-D math types used by layout, graphics, and animation. All types are plain C structs — trivially copyable, no vtables.

**SIMD detection**

- SSE2 detected via `__SSE2__` → `SC_SIMD_SSE2`
- ARM NEON detected via `__ARM_NEON` → `SC_SIMD_NEON`
- Suppress with `-DSC_NO_SSE` or `-DSC_NO_NEON`

**Types**

| Type | Fields | Hot ops |
|---|---|---|
| `SCVec2` | `f32 x, y` | `sc_v2_add`, `sc_v2_sub`, `sc_v2_mul`, `sc_v2_dot`, `sc_v2_len`, `sc_v2_norm`, `sc_v2_lerp` |
| `SCVec3` | `f32 x, y, z` | same pattern + `sc_v3_cross` |
| `SCVec4` | `f32 x, y, z, w` — 16-byte aligned | SSE2/NEON `add`, `mul`, `dot` |
| `SCMat4` | `f32 m[16]` — 64-byte aligned, column-major | `sc_mat4_mul` (SSE2), `sc_mat4_ortho`, `sc_mat4_translate`, `sc_mat4_scale`, `sc_mat4_mul_vec4` |

**Constants:** `SC_PI`, `SC_TAU`, `SC_DEG2RAD`, `SC_RAD2DEG`, `SC_EPSILON` (1e-6f)

**Scalar helpers:** `sc_sqrtf`, `sc_fabsf`, `sc_floorf`, `sc_ceilf`, `sc_roundf`, `sc_sinf`, `sc_cosf`, `sc_tanf`, `sc_atan2f`, `sc_lerpf`

**Usage example**

```c
SCVec2 a = sc_v2(10.0f, 20.0f);
SCVec2 b = sc_v2(30.0f, 40.0f);
SCVec2 mid = sc_v2_lerp(a, b, 0.5f);   // {20, 30}

SCMat4 proj = sc_mat4_ortho(0, 1280, 720, 0, -1, 1);
```

---

### 4.3 `sc_arena.h` — Memory

**The core insight:** For UI workloads, almost all allocations have one of two lifetimes:

1. **Frame lifetime** — scratch data that can be discarded at end-of-frame.
2. **Object lifetime** — a widget/node that lives as long as the scene.

`sc_arena.h` provides exactly two allocators for these two lifetimes.

#### SCArena — Linear / bump allocator

```
backing memory: [..............................] capacity
                 ^offset moves forward on each push
```

- `O(1)` alloc: increment offset, return pointer.
- `O(1)` free-all: `offset = 0`.
- Cache-friendly: allocations are contiguous.
- **Not thread-safe** — one arena per thread.

```c
u8 buf[SC_MB(4)];
SCArena arena;
sc_arena_init(&arena, buf, sizeof(buf));

MyStruct *s   = sc_arena_push_type(&arena, MyStruct);
f32 *floats   = sc_arena_push_array(&arena, f32, 256);

/* Temporary save-point */
SCArenaTemp tmp = sc_arena_temp_begin(&arena);
void *scratch   = sc_arena_push(&arena, 1024);
/* ... */
sc_arena_temp_end(tmp);   /* scratch is gone, permanent allocs survive */

sc_arena_reset(&arena);   /* end-of-frame: everything gone */
```

**Heap-backed convenience:**

```c
SCArena a;
sc_arena_init_heap(&a, SC_MB(8));
/* ... */
sc_arena_destroy_heap(&a);
```

#### SCPool — Typed fixed-size slab

```
slot 0 [free→] slot 1 [free→] slot 2 [live] slot 3 [free→] ...
```

- `O(1)` alloc: pop free-list head.
- `O(1)` free: push to free-list.
- All slots same size, all cache-line aligned.
- Ideal for widget/node objects that are created and destroyed individually.

```c
u8 pool_mem[SC_KB(64)];
SCPool pool;
sc_pool_init(&pool, pool_mem, sizeof(pool_mem), sizeof(SCWidget));

SCWidget *w = sc_pool_alloc_type(&pool, SCWidget);
/* use w */
sc_pool_free(&pool, w);
```

#### Memory stats

```c
SCMemStats stats = sc_arena_stats(&arena);
printf("used %zu / %zu bytes\n", stats.arena_used, stats.arena_capacity);
```

---

### 4.4 `sc_layout.h` — Flexbox Engine

A pure-C, cache-optimised subset of CSS Flexbox Level 1. All node data lives in **flat parallel arrays (Structure-of-Arrays)** inside `SCLayoutTree` — no pointer chasing, maximal cache utilisation.

**Activate:** `#define SC_LAYOUT_IMPLEMENTATION` before the include in exactly one `.c` file.

#### Supported features

| Feature | Status |
|---|---|
| `flex-direction: row / column` | Yes |
| `justify-content: flex-start/center/flex-end/space-between/space-around` | Yes |
| `align-items: flex-start/center/flex-end/stretch` | Yes |
| `flex-grow / flex-shrink / flex-basis` | Yes |
| `margin` (all 4 sides) | Yes |
| `padding` (all 4 sides) | Yes |
| `min-width / min-height` | Yes |
| `max-width / max-height` | Yes |
| `flex-wrap` | Not yet (stretch goal) |
| `align-self` | Not yet |
| `gap` (CSS gap) | Not yet (use margin) |

#### Node limits

`SC_LAYOUT_MAX_NODES = 8192` — change this define before including if you need more.

#### API

```c
SCLayoutTree tree;
sc_layout_tree_init(&tree);

// parent = -1 means root
i32 root = sc_layout_add_node(&tree, -1, (SCLayoutStyle){
    .flex_dir        = SC_FLEX_COLUMN,
    .justify_content = SC_JUSTIFY_START,
    .align_items     = SC_ALIGN_STRETCH,
    .is_container    = true,
    .width           = 1280,
    .height          = 720,
});

i32 child = sc_layout_add_node(&tree, root, (SCLayoutStyle){
    .width      = SC_LAYOUT_UNDEFINED,  // fill
    .height     = 56,
    .flex_grow  = 1.0f,
});

sc_layout_compute(&tree, 1280, 720);

// Read results
SCLayoutResult r = tree.result[child];
printf("child: x=%.0f y=%.0f w=%.0f h=%.0f\n", r.x, r.y, r.w, r.h);

// Or get accumulated screen rect (includes parent offsets)
SCRect2f sr = sc_layout_screen_rect(&tree, child);
```

#### Two-pass algorithm

1. **Pass 1 (bottom-up) — `_sc_layout_intrinsic`:** Recursively computes each node's intrinsic main-axis size from its children's sizes.
2. **Pass 2 (top-down) — `_sc_layout_distribute`:** Distributes free space according to `flex-grow`, applies `justify-content` and `align-items`, writes final `x/y/w/h` into `tree.result[]`.

#### `SC_LAYOUT_UNDEFINED` vs explicit sizes

- `SC_LAYOUT_UNDEFINED` (-1.0): dimension is not explicitly set. The node will fill available space (main axis) or defer to parent's `align-items` (cross axis).
- Explicit value (e.g. `width = 120.0f`): hard-constrains that dimension.

---

### 4.5 `sc_gfx.h` — Graphics Layer

A thin, backend-agnostic API modelled after [sokol_gfx](https://github.com/floooh/sokol). One backend is compiled in at a time via a `#define`.

**Activate:** In your unity build TU:

```c
#define SC_GFX_IMPLEMENTATION
#define SC_GFX_BACKEND_SOFTWARE    // or VULKAN / METAL / D3D12
#include "sc_gfx.h"
```

#### Resource handles (index-based, generation-validated)

```c
SCGfxBuffer    buf = sc_gfx_make_buffer(ctx, &desc);
SCGfxTexture   tex = sc_gfx_make_texture(ctx, &desc);
SCGfxShader    shd = sc_gfx_make_shader(ctx, &desc);
SCGfxPipeline  pip = sc_gfx_make_pipeline(ctx, &desc);
```

All handles are `{ u32 id }`. `id == 0` means invalid. The generation field (in the slot table inside `SCGfxContext`) detects use-after-free in debug builds.

#### Frame lifecycle

```c
sc_gfx_begin_frame(ctx, clear_color);
    // record draw calls or use 2-D helpers
    sc_gfx_draw_rect(ctx, rect, color);
    sc_gfx_draw_line(ctx, a, b, width, color);
    sc_gfx_draw_sprite(ctx, dest, texture, tint);
    sc_gfx_submit(ctx, cmds, count);   // low-level retained draw commands
sc_gfx_end_frame(ctx);
```

#### 2-D helper pipeline

The built-in 2-D pipeline batches quads into `SCGfxVertex2D` (x, y, u, v, r, g, b, a — packed for cache efficiency) and flushes them through the active backend.

#### SCGfxDrawCmd — retained draw command

For advanced usage, fill an `SCGfxDrawCmd` struct and call `sc_gfx_submit`. This separates scene traversal from GPU submission and allows sorting by pipeline/texture to minimise state changes.

#### Frame stats

```c
SCGfxFrameStats stats = sc_gfx_frame_stats(ctx);
printf("%u draw calls, %u verts\n", stats.draw_calls, stats.vertex_count);
```

#### Pixel formats, buffer types, blend modes

All enums are in `sc_gfx.h`: `SCPixelFormat`, `SCBufferUsage`, `SCBufferType`, `SCBlendFactor`, `SCPrimType`.

---

### 4.6 `sc_widget.h` — Widget & Scene

The retained-mode UI layer. It owns a `SCLayoutTree`, a flat widget array, and an animation list. Each frame: tick animations → recompute layout → sync layout results into widget rects → render.

**Activate:** `#define SC_WIDGET_IMPLEMENTATION` in one TU.

#### Widget types

| Enum | Visual output |
|---|---|
| `SC_WIDGET_CONTAINER` | None — flex container only |
| `SC_WIDGET_RECT` | Filled rectangle, optional border |
| `SC_WIDGET_TEXT` | Single-line label (placeholder raster; replace with font engine) |
| `SC_WIDGET_IMAGE` | Stretched texture |
| `SC_WIDGET_CANVAS` | Custom `SCPaintFn` callback |

#### Scene lifecycle

```c
SCScene scene;
sc_scene_init(&scene, gfx_ctx, 1280.0f, 720.0f);

// Build widget tree once
i32 root = sc_widget_rect(&scene, -1, SC_BLACK, root_layout_style);
i32 label = sc_widget_text(&scene, scene.widgets[root].layout_node,
                            "Hello", 16.0f, SC_WHITE, label_style);

// Each frame
sc_gfx_begin_frame(gfx_ctx, clear_color);
    sc_scene_update(&scene, dt);    // tick animations + layout
    sc_scene_render(&scene);        // walk tree, emit draw calls
sc_gfx_end_frame(gfx_ctx);
```

#### Widget mutation (zero allocation)

```c
sc_widget_set_text (&scene, label_id, "New price: 123.45");
sc_widget_set_color(&scene, rect_id, sc_rgba(1,0,0,1));
sc_widget_set_alpha(&scene, widget_id, 0.5f);
sc_widget_set_visible(&scene, widget_id, false);
```

All mutations write directly into the flat widget array — no allocation, no notify, no diff.

#### Animations

```c
// Fade widget from 0 to 1 over 0.3 s, ease-out
sc_anim_push(&scene, widget_id,
             SC_ANIM_ALPHA,
             0.0f, 1.0f,
             0.3f, SC_EASE_OUT_QUAD, false);

// Slide x from 0 to 200 over 0.5 s, looping
sc_anim_push(&scene, widget_id,
             SC_ANIM_X,
             0.0f, 200.0f,
             0.5f, SC_EASE_SPRING, true);
```

Easing functions: `SC_EASE_LINEAR`, `SC_EASE_IN_QUAD`, `SC_EASE_OUT_QUAD`, `SC_EASE_INOUT`, `SC_EASE_SPRING` (damped oscillation).

Animatable properties: `SC_ANIM_X`, `SC_ANIM_Y`, `SC_ANIM_W`, `SC_ANIM_H`, `SC_ANIM_ALPHA`, `SC_ANIM_RED`, `SC_ANIM_GREEN`, `SC_ANIM_BLUE`.

#### Event dispatch

```c
SCEvent ev = {
    .type    = SC_EVENT_MOUSE_DOWN,
    .mouse_x = 320.0f,
    .mouse_y = 240.0f,
};
sc_scene_dispatch_event(&scene, &ev);
```

Hit-testing is a linear scan through all interactive widgets — sufficient for UI workloads. For 10 000+ hit-test targets, add a spatial index.

#### Custom paint canvas

```c
void my_paint(SCScene *s, i32 wid, SCGfxContext *gfx, SCRect2f bounds) {
    sc_gfx_draw_line(gfx, sc_v2(bounds.x, bounds.y),
                          sc_v2(bounds.x + bounds.w, bounds.y + bounds.h),
                          2.0f, SC_WHITE);
}

i32 canvas = sc_widget_canvas(&scene, parent_layout, my_paint, style);
```

#### Limits

| Constant | Value | Where |
|---|---|---|
| `SC_SCENE_MAX_WIDGETS` | 8 192 | `sc_widget.h` |
| `SC_SCENE_MAX_ANIMS` | 1 024 | `sc_widget.h` |
| `SC_WIDGET_MAX_TEXT` | 128 bytes | `sc_widget.h` |

---

### 4.7 `sc_runtime.h` — Runtime & Event Loop

Replaces the JavaScript event loop + fiber scheduler with a zero-allocation C equivalent.

**Activate:** `#define SC_RUNTIME_IMPLEMENTATION` in one TU.

#### SCFiber — Cooperative coroutines

Stackful coroutines using `ucontext_t` (POSIX) or `CreateFiber` (Win32). Each logical "component" can be a fiber — it yields control back to the scheduler without blocking the entire thread.

```c
void my_component(void *ud) {
    Dashboard *d = (Dashboard*)ud;
    while (1) {
        // do some work
        sc_fiber_yield(loop);   // suspend, return to scheduler
    }
}

i32 fid = sc_fiber_spawn(loop, my_component, &dashboard, "dashboard");
```

**Stack size:** `SC_FIBER_STACK_SIZE = 64 KB` per fiber. Stacks are heap-allocated once at spawn, freed at `sc_loop_shutdown`.

**Max fibers:** `SC_RUNTIME_MAX_FIBERS = 256`

#### SCTask — Thread-safe work items

Tasks are fired from any thread (e.g. a network thread that received price data) and consumed on the main thread during `sc_loop_tick`. The queue is a lock-free MPSC (multiple-producer, single-consumer) intrusive linked list.

```c
typedef struct { u32 ticker_id; f32 new_price; } PriceUpdate;

// On any thread:
PriceUpdate payload = {42, 123.45f};
sc_task_post(loop, my_price_handler, &payload, sizeof(payload));

// Handler runs on main thread during sc_loop_tick:
void my_price_handler(void *payload) {
    PriceUpdate *p = (PriceUpdate*)payload;
    g_tickers[p->ticker_id].price = p->new_price;
}
```

**Max payload:** `SC_TASK_PAYLOAD_SIZE = 128 bytes` (inline in the task struct — no extra alloc).

#### SCTimer — Min-heap timer

```c
// One-shot after 500 ms
sc_timer_set(loop, 500 * 1000000ULL, my_callback, userdata);

// Repeating every 1 s
i32 tid = sc_timer_repeat(loop, 1000 * 1000000ULL, tick_callback, userdata);
sc_timer_clear(loop, tid);   // cancel
```

The heap is sorted by `fire_at_ns`. On each `sc_loop_tick`, timers with `fire_at_ns <= now` fire in order.

#### SCEventLoop — The 60 Hz pump

```c
SCArena task_arena;
u8 task_buf[SC_MB(1)];
sc_arena_init(&task_arena, task_buf, sizeof(task_buf));

SCEventLoop loop;
sc_loop_init(&loop, &task_arena);

// Blocking (calls nanosleep for remainder of each 16.67 ms budget):
sc_loop_run(&loop);

// Or integrate manually:
while (app_running) {
    u64 now = sc_clock_ns();
    sc_loop_tick(&loop, now);
    // your render code here
}
```

**Platform clock:** `sc_clock_ns()` returns monotonic nanoseconds via `clock_gettime(CLOCK_MONOTONIC)` on POSIX and `QueryPerformanceCounter` on Windows.

---

## 5. Backend Porting Guide

Each backend stub lives in `backends/`. To implement a real backend:

### Step 1 — Define the backend macro before including `sc_gfx.h`

```c
#define SC_GFX_BACKEND_VULKAN
#define SC_GFX_IMPLEMENTATION
#include "sc_gfx.h"
#include "backends/sc_backend_vulkan.h"
#define SC_BACKEND_VULKAN_IMPLEMENTATION
#include "backends/sc_backend_vulkan.h"
```

### Step 2 — Fill in the backend-specific init function

```c
// In sc_backend_vulkan.h (implementation section)
SCResult sc_vulkan_init(SCGfxContext *ctx, const SCGfxDesc *desc,
                        const SCVulkanDesc *vk_desc) {
    // vkCreateInstance(...)
    // vkEnumeratePhysicalDevices(...)
    // vkCreateDevice(...)
    // vkCreateSwapchainKHR(...)
    // vkCreateRenderPass(...)
    // Allocate per-frame command buffers
    return SC_OK;
}
```

### Step 3 — Hook into `sc_gfx_init`

Inside `sc_gfx.h`'s `sc_gfx_init`:

```c
#ifdef SC_GFX_BACKEND_VULKAN
    return sc_vulkan_init(ctx, desc, NULL);
#endif
```

### Step 4 — Implement the three per-frame functions

```c
sc_vulkan_begin_frame(ctx, clear);   // acquire swapchain image, begin cmd buffer
sc_vulkan_submit(ctx, cmds, count);  // vkCmdBindPipeline + vkCmdDraw per cmd
sc_vulkan_end_frame(ctx);            // end + submit cmd buffer, vkQueuePresentKHR
```

### Minimal Vulkan sequence (reference)

```
vkCreateInstance
  → vkCreateDebugUtilsMessengerEXT  (validation, debug only)
  → vkEnumeratePhysicalDevices → pick
  → vkCreateDevice (graphics queue, present queue)
  → vkCreateSwapchainKHR
  → vkGetSwapchainImagesKHR
  → vkCreateImageView × swapchain_image_count
  → vkCreateRenderPass
  → vkCreateFramebuffer × swapchain_image_count
  → vkCreateCommandPool
  → vkAllocateCommandBuffers × FRAMES_IN_FLIGHT
  → vkCreateSemaphore × 2 × FRAMES_IN_FLIGHT (image-available, render-done)
  → vkCreateFence × FRAMES_IN_FLIGHT (in-flight)
```

---

## 6. Python Bindings

`bindings/python/silvercore.py` is a two-mode library:

- **Native mode:** Loads `libsc_kernel.so` via `ctypes.CDLL` and calls C functions directly.
- **Simulation mode:** All calls are Python no-ops returning dummy values. Activated automatically when the native library is absent — useful in CI and headless tests.

### Supported search paths (native mode)

The loader tries these paths in order:

1. Explicit path passed to `SilverCore(lib_path=...)`
2. `./build/libsc_kernel.so`, `./build/Release/libsc_kernel.so`, `./build/Debug/libsc_kernel.so`
3. `./libsc_kernel.so` (current dir)
4. Same with `.dylib` and `.dll` extensions

### Quick start

```python
from bindings.python.silvercore import SilverCore, Scene, LayoutStyle, Color, FlexDir, Justify

sc    = SilverCore()                          # loads native lib or sim mode
gfx   = sc.gfx_init(width=1280, height=720)
scene = Scene(sc, gfx, 1280, 720)

root  = scene.widget_rect(-1, Color.from_hex("#0d1117"),
                           LayoutStyle(flex_dir=FlexDir.COLUMN,
                                       width=1280, height=720))
hdr   = scene.widget_rect(root, Color.from_hex("#161b22"),
                            LayoutStyle(height=48))
title = scene.widget_text(hdr, "My SilverCore App",
                           Color.WHITE, font_size=16)

while True:
    sc.gfx_begin_frame(gfx, Color(0.05, 0.05, 0.08))
    scene.update(dt=0.016)
    scene.render()
    sc.gfx_end_frame(gfx)
```

### Color helpers

```python
Color.from_hex("#ff6600")          # parse HTML hex
Color.BLACK / .WHITE / .RED / ...  # presets
Color(r=0.1, g=0.2, b=0.9, a=1.0) # direct float construction
```

### EdgeInsets helpers

```python
EdgeInsets.all(8)                  # all four sides = 8
EdgeInsets.symmetric(v=4, h=12)    # vertical=4, horizontal=12
EdgeInsets(top=8, right=16, bottom=8, left=16)
```

### Running the self-test

```bash
python3 bindings/python/silvercore.py
# Expected output:
# [silvercore] simulation mode (native library not found)
# Widgets created: 3
# [silvercore] self-test passed
```

### Adding a new Python-callable C function

1. Add the function to the appropriate C header.
2. Rebuild the shared library.
3. In `SilverCore._bind_functions()`, add:

```python
lib.sc_my_function.restype  = ctypes.c_int
lib.sc_my_function.argtypes = [ctypes.c_void_p, ctypes.c_float]
```

4. Wrap it in a Python method:

```python
def my_function(self, ctx, value: float) -> int:
    if self._sim:
        return 0
    return self._lib.sc_my_function(ctx, ctypes.c_float(value))
```

---

## 7. WebAssembly / Emscripten

### Build

```bash
source /path/to/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-wasm -DWASM=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm
```

`tools/wasm/CMakeLists.wasm.cmake` adds these Emscripten linker flags:

- `EXPORTED_FUNCTIONS`: all public `sc_*` functions
- `EXPORTED_RUNTIME_METHODS`: `cwrap`, `getValue`, `setValue`, `_malloc`, `_free`
- `ALLOW_MEMORY_GROWTH=1`
- `ASYNCIFY=1` (for fiber/coroutine emulation)

### Browser integration

```html
<canvas id="silvercore-canvas" width="1280" height="720"></canvas>
<script type="module">
  import { createSilverCore } from "./tools/wasm/silvercore.js";
  const sc = await createSilverCore({
      canvas: document.getElementById("silvercore-canvas"),
      wasmPath: "./silvercore.wasm",
      targetHz: 60
  });
  await sc.init(1280, 720);
  sc.start();
</script>
```

The JS glue (`silvercore.js`) handles:

- Loading `silvercore.wasm` via Emscripten's factory function
- Wrapping all C exports via `cwrap`
- Calling `requestAnimationFrame` for the render loop
- Blitting the software framebuffer to Canvas2D (for the software backend)

### WebGPU backend (future)

When targeting WebGPU, set `SC_GFX_BACKEND_WGPU` and route `sc_gfx_begin_frame` / `sc_gfx_submit` / `sc_gfx_end_frame` through the WebGPU C API (available in Emscripten 3.1.32+).

---

## 8. Writing a New App

The stock dashboard (`apps/stock_dashboard/stock_dashboard.c`) is the canonical reference. Here's the minimal template:

### Step 1 — Define implementations at the top of your main `.c` file

```c
#define SC_GFX_IMPLEMENTATION
#define SC_LAYOUT_IMPLEMENTATION
#define SC_WIDGET_IMPLEMENTATION
#define SC_RUNTIME_IMPLEMENTATION
#define SC_GFX_BACKEND_SOFTWARE    // change to VULKAN / METAL / D3D12 later

#include "sc_types.h"
#include "sc_math.h"
#include "sc_arena.h"
#include "sc_layout.h"
#include "sc_gfx.h"
#include "sc_widget.h"
#include "sc_runtime.h"
```

### Step 2 — Declare your app state with arena-backed memory

```c
typedef struct {
    u8          frame_buf[SC_MB(8)];
    SCArena     frame_arena;
    u8          task_buf[SC_MB(1)];
    SCArena     task_arena;
    SCGfxContext *gfx;
    SCScene      scene;
    SCEventLoop  loop;
} MyApp;

static MyApp g_app;
```

### Step 3 — Init

```c
int main(void) {
    MyApp *a = &g_app;
    sc_arena_init(&a->frame_arena, a->frame_buf, sizeof(a->frame_buf));
    sc_arena_init(&a->task_arena,  a->task_buf,  sizeof(a->task_buf));

    SCGfxDesc desc = {
        .backend     = SC_BACKEND_SOFTWARE,
        .width       = 1280, .height = 720,
        .frame_arena = &a->frame_arena,
    };
    sc_gfx_init(&desc, &a->gfx);
    sc_scene_init(&a->scene, a->gfx, 1280, 720);
    sc_loop_init(&a->loop, &a->task_arena);

    build_ui(a);      // your widget-tree construction function
    run_loop(a);      // your frame loop
    sc_gfx_shutdown(a->gfx);
    return 0;
}
```

### Step 4 — Build UI (once)

```c
static void build_ui(MyApp *a) {
    SCScene *s = &a->scene;

    i32 root = sc_widget_rect(s, -1, sc_rgba8(15,15,20,255), (SCLayoutStyle){
        .flex_dir      = SC_FLEX_COLUMN,
        .is_container  = true,
        .width         = 1280, .height = 720,
    });

    i32 card = sc_widget_rect(s, s->widgets[root].layout_node,
                               sc_rgba8(30,40,60,255), (SCLayoutStyle){
        .width = 300, .height = 200,
        .margin = {20, 20, 20, 20},
    });

    sc_widget_text(s, s->widgets[card].layout_node,
                   "SilverCore", 24.0f, SC_WHITE, (SCLayoutStyle){
        .width = 280, .height = 40,
    });
}
```

### Step 5 — Frame loop

```c
static void run_loop(MyApp *a) {
    const u64 FRAME_NS = 1000000000ULL / 60;
    for (;;) {
        u64 t0 = sc_clock_ns();

        sc_gfx_begin_frame(a->gfx, sc_rgba(0.05f, 0.05f, 0.08f, 1.0f));
        sc_scene_update(&a->scene, 1.0f / 60.0f);
        sc_scene_render(&a->scene);
        sc_gfx_end_frame(a->gfx);
        sc_loop_tick(&a->loop, t0);
        sc_arena_reset(&a->frame_arena);

        u64 elapsed = sc_clock_ns() - t0;
        if (elapsed < FRAME_NS) {
            struct timespec ts = { 0, (long)(FRAME_NS - elapsed) };
            nanosleep(&ts, NULL);
        }
    }
}
```

### Step 6 — Add to CMakeLists.txt

```cmake
add_executable(my_app apps/my_app/my_app.c)
target_include_directories(my_app PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(my_app PRIVATE m)
```

---

## 9. Test Suite

Tests live in `tests/`. Each test file is a standalone C program (defines its own `_IMPLEMENTATION` macros, links nothing except libm).

### Run all tests

```bash
cd build && ctest --output-on-failure -V
```

### Test files

| File | What it covers |
|---|---|
| `test_arena.c` | `SCArena` basic push/reset/overflow, typed push, temp save/restore; `SCPool` alloc/free/OOM |
| `test_math.c` | `SCVec2/3/4` arithmetic, normalisation, dot/cross; `SCMat4` identity, multiply, ortho |
| `test_layout.c` | Root fill, row equal flex-grow, justify-center offset, column stacking order |
| `test_gfx.c` | `sc_gfx_init` (software), buffer/texture/shader/pipeline lifecycle, frame begin/end, stats |
| `test_runtime.c` | `sc_loop_init`, task post/drain, timer fire, `sc_clock_ns` monotonicity |

### Adding a test

1. Create `tests/test_myfeature.c`.
2. Add `#define MY_IMPLEMENTATION` + `#include` as needed.
3. Write `main()` using the `ASSERT / PASS / FAIL` macros:

```c
#define PASS(name)  printf("  [PASS] %s\n", name)
#define FAIL(name)  do { printf("  [FAIL] %s\n", name); return 1; } while(0)
#define ASSERT(cond, name) do { if (!(cond)) FAIL(name); } while(0)
```

4. In `tests/CMakeLists.txt` add:

```cmake
add_executable(test_myfeature test_myfeature.c)
target_include_directories(test_myfeature PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_myfeature PRIVATE m)
add_test(NAME myfeature COMMAND test_myfeature)
```

---

## 10. Performance Model

### Why it beats the bridge

| Bottleneck | React Native / Flutter | SilverCore |
|---|---|---|
| Layout compute | JS VM → bridge → Yoga (C++) | Direct C call, SoA arrays |
| Draw call submission | JS VM → bridge → native | Inline, same address space |
| Per-frame allocation | GC-managed heap | Arena reset (1 write) |
| Memory footprint | 100–400 MB (V8/Hermes + native) | ~5 MB scene + ~8 MB arenas |
| GC pauses | 5–50 ms spikes | Zero (no GC) |

### Profiling tips

1. **Enable frame stats:**
   ```c
   SCGfxFrameStats s = sc_gfx_frame_stats(ctx);
   ```

2. **Check arena pressure:**
   ```c
   SCMemStats ms = sc_arena_stats(&frame_arena);
   // if ms.arena_used > 0 after reset → something is leaking into the frame arena
   ```

3. **Count dirty widgets:** In the PoC, only widgets with `ticker.dirty == true` update text buffers per frame (~5% of 1 024 = ~51 updates). This pattern (dirty flag + in-place mutation) is the key to <1% CPU.

4. **Layout re-compute cost:** `sc_scene_update` calls `sc_layout_compute` every frame. For a fixed-position grid that never changes size, you can call `sc_layout_compute` only on resize events and sync widget rects manually — cutting layout cost to zero on the hot path.

### Recommended memory budget (per app)

| Arena | Size | Contents |
|---|---|---|
| `frame_arena` | 8–16 MB | Per-frame scratch: temp strings, dynamic command lists |
| `task_arena` | 1–2 MB | MPSC task structs |
| `scene._arena_buf` | 2 MB (built-in) | Widget string labels |
| Widget pool (optional) | `SC_SCENE_MAX_WIDGETS × sizeof(SCWidget)` | Pre-allocated |

---

## 11. Coding Conventions

### Types

Always use the `sc_types.h` aliases — never raw `int`, `float`, `unsigned int`:

```c
// Good
u32 i = 0;
f32 x = 3.14f;

// Avoid
int i = 0;
float x = 3.14f;
```

### Function naming

```
sc_<module>_<verb>     e.g.  sc_arena_push, sc_widget_set_text, sc_gfx_begin_frame
_sc_<module>_<verb>    e.g.  _sc_layout_intrinsic  (internal / static)
```

### Result codes

All functions that can fail return `SCResult`. Check with `sc_ok()`:

```c
SCResult r = sc_gfx_init(&desc, &ctx);
if (!sc_ok(r)) { /* handle */ }
```

### No silent ignoring of NULL

Every pointer parameter that must be non-NULL is guarded with `SC_ASSERT`. If you add a new function, add the assert at the top.

### Header guard pattern

```c
#ifndef SC_MYMODULE_H
#define SC_MYMODULE_H

// declarations

#ifdef SC_MYMODULE_IMPLEMENTATION
// implementation
#endif

#endif /* SC_MYMODULE_H */
```

### Zero initialization

Use `memset(ptr, 0, sizeof(*ptr))` or let `sc_arena_push*` / `sc_pool_alloc` do it (they both zero memory before returning).

---

## 12. Roadmap & Extension Points

### Near-term

| Item | Where to add |
|---|---|
| Font rasteriser (stb_truetype) | Replace placeholder in `sc_scene_render` `SC_WIDGET_TEXT` case |
| Rounded rect rendering | `sc_gfx_draw_rect_rounded` — expand quad to SDF shader |
| Flex-wrap support | `_sc_layout_distribute` in `sc_layout.h` |
| Input text widget | New `SC_WIDGET_INPUT` type + key event handler |
| Scrollable container | Clip rect in `SCGfxDrawCmd.scissor` + scroll offset state |

### Medium-term

| Item | Notes |
|---|---|
| Real Vulkan backend | Fill `sc_backend_vulkan.h` per the porting guide |
| Real Metal backend | Fill `sc_backend_metal.h` with Metal Objective-C |
| Real D3D12 backend | Fill `sc_backend_d3d12.h` |
| Rust scripting layer | `extern "C"` bindings + Rust wrapper crate |
| Hot-reload | Watch file changes, re-run `build_scene`, diff widget IDs |
| Multi-threaded layout | Partition tree into independent subtrees, run per-thread |

### Long-term

| Item | Notes |
|---|---|
| 3-D widget layer | `SC_WIDGET_MESH` type + depth buffer, perspective projection |
| GPU-driven layout | Compute shader pass for Flexbox distribution |
| Accessibility tree | Mirror widget tree to platform a11y APIs |
| Platform window integration | Replace headless with native window on Win32/Cocoa/X11/Wayland |

---

## 13. Glossary

| Term | Definition |
|---|---|
| **Arena allocator** | A bump pointer allocator where all memory is freed at once by resetting a cursor. O(1) alloc and O(1) free-all. |
| **Backend** | A GPU-API-specific implementation of `sc_gfx.h`'s rendering calls (Vulkan, Metal, D3D12, Software, WebGPU). |
| **Bridge** | In React Native/Flutter: the serialized inter-thread channel between the JS VM and native code. SilverCore has none. |
| **Dirty flag** | A boolean on each ticker/widget that marks "data changed, update GPU state". Only dirty items do work per frame. |
| **Fiber** | A stackful coroutine — a unit of execution with its own stack that can yield and resume without blocking a thread. |
| **Flex-grow** | A CSS Flexbox property. Determines how a child grows proportionally when free space is available in the container. |
| **GC pause** | A stop-the-world stall caused by a garbage collector scanning and reclaiming memory. SilverCore has no GC. |
| **Handle** | A `{ u32 id }` struct used instead of raw pointers for GPU resources. Prevents dangling pointers and enables generation checking. |
| **MPSC queue** | Multiple-producer, single-consumer lock-free queue. Used to marshal tasks from network/IO threads to the main thread. |
| **SoA** | Structure-of-Arrays. Storing fields of many objects in separate arrays (e.g., all `x` values together, all `y` values together) for better cache utilisation than Array-of-Structures. |
| **Unity build** | Compiling an entire project as a single translation unit by `#include`-ing all `.c` files from one root file. Enables full cross-module inlining. |
| **Widget** | A leaf or container node in the retained scene graph. Has a type (rect, text, image, canvas), visual properties, layout style, and optional event callbacks. |
