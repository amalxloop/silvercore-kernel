/**
 * silvercore.js  --  SilverCore WebAssembly JS glue layer
 *
 * This file bridges the browser Canvas2D / WebGPU surface to the C kernel
 * exported from silvercore.wasm.
 *
 * Integration
 * -----------
 * <script type="module">
 *   import { createSilverCore } from "./silvercore.js";
 *   const sc = await createSilverCore({ canvas: document.getElementById("c") });
 *   sc.startApp("stock_dashboard");
 * </script>
 *
 * The WASM module exposes every exported C function via the `_funcName`
 * convention (Emscripten default).  We wrap them in a friendlier API.
 */

// ---------------------------------------------------------------------------
// WASM loader
// ---------------------------------------------------------------------------

/**
 * @param {Object} opts
 * @param {HTMLCanvasElement} opts.canvas
 * @param {string} [opts.wasmPath="./silvercore.wasm"]
 * @param {number} [opts.targetHz=60]
 * @returns {Promise<SilverCoreApp>}
 */
export async function createSilverCore(opts = {}) {
    const canvas   = opts.canvas   ?? document.getElementById("silvercore-canvas");
    const wasmPath = opts.wasmPath ?? "./silvercore.wasm";
    const targetHz = opts.targetHz ?? 60;

    // Load WASM module (Emscripten-generated)
    let Module;
    try {
        const { default: factory } = await import(wasmPath.replace(".wasm", ".js"));
        Module = await factory({
            locateFile: (f) => f.endsWith(".wasm") ? wasmPath : f,
        });
    } catch (e) {
        console.warn("[silvercore] WASM not available, running in stub mode:", e.message);
        Module = _stubModule();
    }

    return new SilverCoreApp(Module, canvas, targetHz);
}

// ---------------------------------------------------------------------------
// Main application class
// ---------------------------------------------------------------------------

class SilverCoreApp {
    constructor(module, canvas, targetHz) {
        this._mod        = module;
        this._canvas     = canvas;
        this._ctx2d      = canvas.getContext("2d");
        this._targetHz   = targetHz;
        this._frameNs    = Math.round(1e9 / targetHz);
        this._gfxCtx     = 0;    // C pointer
        this._sceneCtx   = 0;    // C pointer
        this._running    = false;
        this._lastTs     = 0;
        this._frameId    = 0;

        this._bindApis();
    }

    // -----------------------------------------------------------------------
    // Bind C exports to convenient JS methods
    // -----------------------------------------------------------------------
    _bindApis() {
        const m = this._mod;
        this.c = {
            gfx_init:           m.cwrap("sc_gfx_init",          "number", ["number","number"]),
            gfx_shutdown:       m.cwrap("sc_gfx_shutdown",       null,     ["number"]),
            gfx_begin_frame:    m.cwrap("sc_gfx_begin_frame",    null,     ["number","number","number","number","number"]),
            gfx_end_frame:      m.cwrap("sc_gfx_end_frame",      null,     ["number"]),
            gfx_resize:         m.cwrap("sc_gfx_resize",         "number", ["number","number","number"]),
            gfx_draw_rect:      m.cwrap("sc_gfx_draw_rect",      null,     ["number","number","number","number","number","number","number","number","number"]),
            gfx_draw_line:      m.cwrap("sc_gfx_draw_line",      null,     ["number","number","number","number","number","number","number","number","number"]),
            scene_init:         m.cwrap("sc_scene_init",         null,     ["number","number","number","number"]),
            scene_update:       m.cwrap("sc_scene_update",       null,     ["number","number"]),
            scene_render:       m.cwrap("sc_scene_render",       null,     ["number"]),
            scene_dispatch:     m.cwrap("sc_scene_dispatch_event",null,    ["number","number"]),
            widget_text:        m.cwrap("sc_widget_text",        "number", ["number","number","string","number","number","number","number","number","number"]),
            widget_set_text:    m.cwrap("sc_widget_set_text",    null,     ["number","number","string"]),
            widget_set_color:   m.cwrap("sc_widget_set_color",   null,     ["number","number","number","number","number","number"]),
            clock_ns:           m.cwrap("sc_clock_ns",           "number", []),
        };
    }

    // -----------------------------------------------------------------------
    // Lifecycle
    // -----------------------------------------------------------------------

    async init(width, height) {
        this._canvas.width  = width;
        this._canvas.height = height;

        // Allocate SCGfxDesc on the WASM heap
        const descSize = 32;
        const descPtr  = this._mod._malloc(descSize);
        this._mod.setValue(descPtr,      4,  "i32"); // backend = SOFTWARE
        this._mod.setValue(descPtr + 4,  width,  "i32");
        this._mod.setValue(descPtr + 8,  height, "i32");
        this._mod.setValue(descPtr + 12, 0, "i32"); // vsync = false

        const ctxPtrPtr = this._mod._malloc(4);
        this.c.gfx_init(descPtr, ctxPtrPtr);
        this._gfxCtx = this._mod.getValue(ctxPtrPtr, "i32");
        this._mod._free(descPtr);
        this._mod._free(ctxPtrPtr);

        // Allocate SCScene on the WASM heap
        const sceneSize = 256 * 1024;  // 256 KB for demo
        this._scenePtr = this._mod._malloc(sceneSize);
        this.c.scene_init(this._scenePtr, this._gfxCtx, width, height);
    }

    start() {
        this._running = true;
        this._lastTs  = performance.now();
        this._loop();
    }

    stop() {
        this._running = false;
        cancelAnimationFrame(this._frameId);
    }

    // -----------------------------------------------------------------------
    // Frame loop
    // -----------------------------------------------------------------------
    _loop() {
        if (!this._running) return;
        this._frameId = requestAnimationFrame((ts) => {
            const dt = Math.min((ts - this._lastTs) / 1000.0, 0.1);
            this._lastTs = ts;
            this._tick(dt);
            this._loop();
        });
    }

    _tick(dt) {
        // Update scene (C side)
        this.c.gfx_begin_frame(this._gfxCtx, 0.05, 0.05, 0.1, 1.0);
        if (this._scenePtr) {
            this.c.scene_update(this._scenePtr, dt);
            this.c.scene_render(this._scenePtr);
        }
        this.c.gfx_end_frame(this._gfxCtx);

        // Blit the software framebuffer to Canvas2D
        this._blit();
    }

    _blit() {
        // When using the software rasteriser, the framebuffer lives at
        // gfx_ctx->framebuffer (offset 32 in the struct for now).
        // A real integration exposes sc_gfx_framebuffer_ptr().
        // For now we just clear the canvas and let the C-side 2D batch
        // commands drive the visible output via WebGL / WebGPU in a real build.
        const ctx = this._ctx2d;
        ctx.clearRect(0, 0, this._canvas.width, this._canvas.height);
    }

    // -----------------------------------------------------------------------
    // App launcher
    // -----------------------------------------------------------------------

    // Bug #18: dynamic import() with a user-controlled string allows
    // path traversal and arbitrary module loading.
    // Fix: use a strict allowlist; never interpolate appName into an import path.
    static get _KNOWN_APPS() {
        return Object.freeze({
            stock_dashboard: () => import("./stock_dashboard_wasm.js"),
        });
    }

    startApp(appName) {
        const loader = SilverCoreApp._KNOWN_APPS[appName];
        if (!loader) {
            throw new Error(`Unknown app: ${JSON.stringify(appName)}`);
        }
        return loader().then((m) => m.launch(this));
    }
}

// ---------------------------------------------------------------------------
// Stub module (no WASM)
// ---------------------------------------------------------------------------

// Bug #17: returning pointer=1 from _malloc causes _free(1) twice and
// getValue(1) to be used as a real gfx context handle in stub mode.
// Fix: use a proper simulated heap so alloc/free/getValue are self-consistent.
function _stubModule() {
    let _nextPtr = 0x1000;  // start above NULL
    const _heap  = new Map(); // ptr -> value

    return {
        _malloc: (n) => {
            const ptr = _nextPtr;
            _nextPtr += Math.max(n, 4);
            _heap.set(ptr, 0);
            return ptr;
        },
        _free: (ptr) => { _heap.delete(ptr); },
        getValue: (ptr) => _heap.get(ptr) ?? 0,
        setValue: (ptr, val) => { _heap.set(ptr, val); },
        cwrap: (name) => (...args) => {
            if (name === "sc_clock_ns") return Date.now() * 1e6;
            return 0;
        },
    };
}
