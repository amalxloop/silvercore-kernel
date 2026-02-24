# SilverCore-UI

**No bridge. No GC. No compromise.**

A cross-platform rendering kernel written in pure C that eliminates the JavaScript bridge entirely. Instead of running app logic through an interpreter and translating it across a JS-to-native boundary, SilverCore compiles directly to a native binary — or to WebAssembly — and drives the GPU without a runtime middleman.

---

## The Problem It Solves

Every major cross-platform UI framework (React Native, Flutter, Electron, Expo) has the same fundamental bottleneck: app logic lives in a managed runtime (V8, Hermes, Dart VM) and must cross a serialized bridge to reach the platform's native drawing APIs. That crossing costs CPU cycles, introduces latency spikes from garbage collection, and burns battery waiting for round-trips.

SilverCore removes the bridge. App logic, layout, animation, and rendering are all C code compiled into one binary. There is no interpreter, no GC pause, no bridge.

---

## What Is Built

| Subsystem | File | What It Does |
|---|---|---|
| Types & primitives | `include/sc_types.h` | Fixed-width integers, platform detection, compiler macros, result codes, RGBA color, 2-D rects |
| Math | `include/sc_math.h` | vec2/vec3/vec4/mat4 with SSE2 and NEON hot paths |
| Memory | `include/sc_arena.h` | Linear bump allocator + typed slab pool — zero `malloc` on the hot path |
| Layout engine | `include/sc_layout.h` | Flexbox subset in C: row/column, flex-grow, justify-content, align-items, margin, padding |
| Graphics layer | `include/sc_gfx.h` | Backend-agnostic 2-D/3-D API (Vulkan / Metal / D3D12 / Software) |
| Widget/scene | `include/sc_widget.h` | Retained widget tree with animations, events, and layout sync |
| Runtime | `include/sc_runtime.h` | Cooperative fiber scheduler, MPSC task queue, min-heap timer, 60 Hz loop |
| Vulkan backend | `backends/sc_backend_vulkan.h` | Vulkan surface stub — replace bodies with real `vk*` calls |
| Metal backend | `backends/sc_backend_metal.h` | Metal surface stub |
| D3D12 backend | `backends/sc_backend_d3d12.h` | D3D12 surface stub |
| Python bindings | `bindings/python/silvercore.py` | ctypes wrapper + simulation mode (no native lib required) |
| WASM glue | `tools/wasm/silvercore.js` | Emscripten JS bridge, Canvas2D blit, requestAnimationFrame loop |
| PoC app | `apps/stock_dashboard/` | 1 024-ticker stock dashboard, 60 Hz, zero heap allocs per frame |

---

## Proof of Concept Results

The stock dashboard PoC renders **1 024 live-updating ticker cards** (each with symbol, price, and a green/red delta bar) inside a 1 280 × 720 viewport, headless, for 5 seconds:

```
Tickers    : 1024
Viewport   : 1280x720
Target     : 60 Hz
Cell size  : 34x18 px

--- Benchmark results (300 frames) ---
  Total time    : 5.034 s
  Actual fps    : 59.5
  Draw calls/fr : 4103
  Verts/fr      : 24618
  Scene memory  : 5.19 MB
  Arena used    : 0.00 KB / 8192.00 KB   ← zero heap allocs per frame
```

---

## Architecture at a Glance

```
┌────────────────────────────────────────────────────────────┐
│                        Your App (C / Python / Rust)         │
└───────────────────────┬────────────────────────────────────┘
                        │  SilverCore Public API
┌───────────────────────▼────────────────────────────────────┐
│  SCScene  (widget tree + animation + layout sync)           │
│      ↓                          ↓                           │
│  SCLayoutTree (Flexbox)   SCWidgetAnim (easing tweens)      │
│      ↓                          ↓                           │
│  SCGfxContext  ──── draw_rect / draw_line / draw_sprite ───▶│
│      ↓                                                      │
│  Backend: Vulkan | Metal | D3D12 | Software | WebGPU        │
└────────────────────────────────────────────────────────────┘
                        │
┌───────────────────────▼────────────────────────────────────┐
│  SCEventLoop  (fibers + MPSC tasks + min-heap timers)       │
└────────────────────────────────────────────────────────────┘
```

---

## Building

### Prerequisites

- CMake 3.20+
- GCC 10+ or Clang 12+ (or MSVC 19.28+)
- `libm` (standard on all platforms)
- Optional for WASM: [Emscripten SDK](https://emscripten.org/docs/getting_started/)

### Native build (Linux / macOS / Windows)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

This produces:

| Output | Path |
|---|---|
| Static library | `build/libsc_kernel.a` |
| Shared library | `build/libsc_kernel.so` (`.dylib` / `.dll`) |
| PoC executable | `build/stock_dashboard` |
| Test binaries  | `build/test_arena`, `build/test_math`, etc. |

### Run the PoC

```bash
./build/stock_dashboard
```

### Run tests

```bash
cd build && ctest --output-on-failure
```

### WebAssembly (Emscripten)

```bash
emcmake cmake -S . -B build-wasm -DWASM=ON
cmake --build build-wasm -j$(nproc)
```

### CMake options

| Option | Default | Description |
|---|---|---|
| `SC_TESTS` | `ON` | Build test suite |
| `SC_SHARED` | `ON` | Build shared library for Python ctypes |
| `SC_ASAN` | `OFF` | Enable AddressSanitizer |
| `SC_UBSAN` | `OFF` | Enable UndefinedBehaviourSanitizer |
| `WASM` | `OFF` | Emscripten WASM target |

---

## Python Scripting Layer

```bash
python3 bindings/python/silvercore.py   # self-test in simulation mode
```

```python
from bindings.python.silvercore import SilverCore, Scene, LayoutStyle, Color, FlexDir

sc    = SilverCore("./build/libsc_kernel.so")
gfx   = sc.gfx_init(width=1280, height=720)
scene = Scene(sc, gfx, 1280, 720)

root  = scene.widget_rect(-1, Color.from_hex("#1a1a2e"),
                           LayoutStyle(flex_dir=FlexDir.COLUMN,
                                       width=1280, height=720))
title = scene.widget_text(root, "Hello SilverCore", Color.WHITE, font_size=18)

while True:
    sc.gfx_begin_frame(gfx, Color(0.05, 0.05, 0.1))
    scene.update(dt=0.016)
    scene.render()
    sc.gfx_end_frame(gfx)
```

When the native library is absent the module runs in **simulation mode** — all calls succeed as no-ops. This lets Python tests and CI run without a compiled binary.

---

## Project Structure

```
silvercore-kernel/
├── include/
│   ├── sc_types.h        Core types, macros, result codes
│   ├── sc_math.h         vec2/3/4, mat4, SIMD paths
│   ├── sc_arena.h        Arena + pool allocators
│   ├── sc_layout.h       Flexbox layout engine
│   ├── sc_gfx.h          Graphics abstraction layer
│   ├── sc_widget.h       Widget tree, scene, animations
│   └── sc_runtime.h      Fibers, tasks, timers, event loop
├── backends/
│   ├── sc_backend_vulkan.h
│   ├── sc_backend_metal.h
│   └── sc_backend_d3d12.h
├── bindings/
│   └── python/silvercore.py
├── tools/
│   └── wasm/
│       ├── CMakeLists.wasm.cmake
│       └── silvercore.js
├── apps/
│   └── stock_dashboard/
│       └── stock_dashboard.c
├── tests/
│   ├── test_arena.c
│   ├── test_math.c
│   ├── test_layout.c
│   ├── test_gfx.c
│   └── test_runtime.c
└── CMakeLists.txt
```

---

## License

Licensed under the [Apache License, Version 2.0](LICENSE).

```
Copyright 2026 SilverCore Contributors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```
