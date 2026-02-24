"""
silvercore.py  --  SilverCore Python Scripting Layer
======================================================
Thin ctypes wrapper over the compiled sc_kernel shared library.

Usage
-----
from silvercore import SilverCore, Scene, LayoutStyle, Color, FlexDir

sc = SilverCore("./build/libsc_kernel.so")   # or .dylib / .dll
gfx = sc.gfx_init(width=1280, height=720)
scene = Scene(sc, gfx, 1280, 720)

root = scene.widget_rect(parent=-1, color=Color(0.1, 0.1, 0.15),
                          layout=LayoutStyle(flex_dir=FlexDir.ROW))
...
while True:
    sc.gfx_begin_frame(gfx, Color(0.05, 0.05, 0.1))
    scene.update(dt=0.016)
    scene.render()
    sc.gfx_end_frame(gfx)

The module can also be used without the native library for a pure-Python
"simulation" mode (useful in CI / headless testing).
"""

import ctypes
import ctypes.util
import os
import struct
from dataclasses import dataclass, field
from typing import Callable, Optional, List

# ---------------------------------------------------------------------------
# Python-side enum mirrors  (must match sc_types.h / sc_layout.h)
# ---------------------------------------------------------------------------

class FlexDir:
    ROW    = 0
    COLUMN = 1

class Justify:
    START         = 0
    CENTER        = 1
    END           = 2
    SPACE_BETWEEN = 3
    SPACE_AROUND  = 4

class AlignItems:
    START   = 0
    CENTER  = 1
    END     = 2
    STRETCH = 3

class Backend:
    AUTO     = 0
    VULKAN   = 1
    METAL    = 2
    D3D12    = 3
    SOFTWARE = 4
    WGPU     = 5

class AnimProp:
    X     = 0
    Y     = 1
    W     = 2
    H     = 3
    ALPHA = 4
    RED   = 5
    GREEN = 6
    BLUE  = 7

class Easing:
    LINEAR   = 0
    IN_QUAD  = 1
    OUT_QUAD = 2
    INOUT    = 3
    SPRING   = 4

# ---------------------------------------------------------------------------
# Plain-data types (no native struct needed – we pack on the fly)
# ---------------------------------------------------------------------------

@dataclass
class Color:
    r: float = 0.0
    g: float = 0.0
    b: float = 0.0
    a: float = 1.0

    def pack(self) -> bytes:
        return struct.pack("4f", self.r, self.g, self.b, self.a)

    @classmethod
    def from_hex(cls, hexstr: str) -> "Color":
        # Bug #15: validate input length and hex characters before parsing
        hexstr = hexstr.lstrip("#")
        if len(hexstr) not in (6, 8):
            raise ValueError(f"Color.from_hex: expected 6 or 8 hex digits, got {len(hexstr)!r}")
        try:
            r = int(hexstr[0:2], 16)
            g = int(hexstr[2:4], 16)
            b = int(hexstr[4:6], 16)
            a = int(hexstr[6:8], 16) if len(hexstr) == 8 else 255
        except ValueError:
            raise ValueError(f"Color.from_hex: invalid hex string {hexstr!r}")
        return cls(r / 255.0, g / 255.0, b / 255.0, a / 255.0)

    # Preset palette
    BLACK   = None  # filled below
    WHITE   = None
    RED     = None
    GREEN   = None
    BLUE    = None
    YELLOW  = None

Color.BLACK  = Color(0,0,0)
Color.WHITE  = Color(1,1,1)
Color.RED    = Color(1,0,0)
Color.GREEN  = Color(0,1,0)
Color.BLUE   = Color(0,0,1)
Color.YELLOW = Color(1,1,0)


@dataclass
class EdgeInsets:
    top: float    = 0.0
    right: float  = 0.0
    bottom: float = 0.0
    left: float   = 0.0

    def pack(self) -> bytes:
        return struct.pack("4f", self.top, self.right, self.bottom, self.left)

    @classmethod
    def all(cls, v: float) -> "EdgeInsets":
        return cls(v, v, v, v)

    @classmethod
    def symmetric(cls, v: float = 0, h: float = 0) -> "EdgeInsets":
        return cls(v, h, v, h)


SC_LAYOUT_UNDEFINED = -1.0
SC_LAYOUT_AUTO      = -2.0


@dataclass
class LayoutStyle:
    flex_dir:        int        = FlexDir.ROW
    justify_content: int        = Justify.START
    align_items:     int        = AlignItems.STRETCH
    is_container:    bool       = False
    width:           float      = SC_LAYOUT_UNDEFINED
    height:          float      = SC_LAYOUT_UNDEFINED
    min_width:       float      = SC_LAYOUT_UNDEFINED
    min_height:      float      = SC_LAYOUT_UNDEFINED
    max_width:       float      = SC_LAYOUT_UNDEFINED
    max_height:      float      = SC_LAYOUT_UNDEFINED
    flex_grow:       float      = 0.0
    flex_shrink:     float      = 1.0
    flex_basis:      float      = SC_LAYOUT_UNDEFINED
    margin:          EdgeInsets = field(default_factory=EdgeInsets)
    padding:         EdgeInsets = field(default_factory=EdgeInsets)

    def pack(self) -> bytes:
        """Pack to match SCLayoutStyle C struct layout."""
        return struct.pack(
            "iiiBffffffffffff16s16s",
            self.flex_dir,
            self.justify_content,
            self.align_items,
            1 if self.is_container else 0,
            self.width, self.height,
            self.min_width, self.min_height,
            self.max_width, self.max_height,
            self.flex_grow, self.flex_shrink, self.flex_basis,
            0.0,  # padding float to struct alignment
            self.margin.pack(),
            self.padding.pack(),
        )

# ---------------------------------------------------------------------------
# Native library bridge
# ---------------------------------------------------------------------------

class SilverCore:
    """
    Loads libsc_kernel and exposes a Pythonic API.

    When lib_path is None or the library is not found, the class falls back
    to "simulation mode" (all calls are no-ops that return dummy values).
    This is intentional: Python unit tests and CI should not require a
    compiled binary.
    """

    def __init__(self, lib_path: Optional[str] = None):
        self._lib = None
        self._sim = True

        paths_to_try = []
        if lib_path:
            paths_to_try.append(lib_path)
        # Common build output locations
        for name in ("sc_kernel", "silvercore"):
            for pfx in ("./build/", "./build/Release/", "./build/Debug/", "./"):
                for ext in (".so", ".dylib", ".dll"):
                    paths_to_try.append(f"{pfx}lib{name}{ext}")

        for path in paths_to_try:
            if os.path.exists(path):
                try:
                    self._lib = ctypes.CDLL(path)
                    self._sim = False
                    self._bind_functions()
                    print(f"[silvercore] loaded native library: {path}")
                    break
                except OSError:
                    pass

        if self._sim:
            print("[silvercore] simulation mode (native library not found)")

    # -----------------------------------------------------------------------
    # ctypes function binding
    # -----------------------------------------------------------------------

    def _bind_functions(self):
        lib = self._lib
        c_void_p = ctypes.c_void_p
        c_int    = ctypes.c_int
        c_uint   = ctypes.c_uint
        c_float  = ctypes.c_float
        c_char_p = ctypes.c_char_p
        c_bool   = ctypes.c_bool

        # sc_gfx_init(desc, **out_ctx) -> SCResult
        lib.sc_gfx_init.restype  = c_int
        lib.sc_gfx_init.argtypes = [c_void_p, ctypes.POINTER(c_void_p)]

        lib.sc_gfx_shutdown.restype  = None
        lib.sc_gfx_shutdown.argtypes = [c_void_p]

        lib.sc_gfx_begin_frame.restype  = None
        lib.sc_gfx_begin_frame.argtypes = [c_void_p, c_float, c_float, c_float, c_float]

        lib.sc_gfx_end_frame.restype  = None
        lib.sc_gfx_end_frame.argtypes = [c_void_p]

        lib.sc_gfx_draw_rect.restype  = None
        lib.sc_gfx_draw_rect.argtypes = [c_void_p,
            c_float, c_float, c_float, c_float,   # rect xywh
            c_float, c_float, c_float, c_float]   # color rgba

        lib.sc_scene_update.restype  = None
        lib.sc_scene_update.argtypes = [c_void_p, c_float]

        lib.sc_scene_render.restype  = None
        lib.sc_scene_render.argtypes = [c_void_p]

    # -----------------------------------------------------------------------
    # Graphics context
    # -----------------------------------------------------------------------

    def gfx_init(self, width: int = 1280, height: int = 720,
                  backend: int = Backend.SOFTWARE) -> ctypes.c_void_p:
        if self._sim:
            return ctypes.c_void_p(0x1)  # dummy handle
        ctx_ptr = ctypes.c_void_p(0)
        # Build a minimal SCGfxDesc: backend(u32) w(u32) h(u32) vsync(bool)
        desc_buf = struct.pack("IIIb", backend, width, height, 0)
        desc_arr = (ctypes.c_uint8 * len(desc_buf))(*desc_buf)
        ret = self._lib.sc_gfx_init(ctypes.byref(desc_arr), ctypes.byref(ctx_ptr))
        if ret != 0:
            raise RuntimeError(f"sc_gfx_init failed: {ret}")
        return ctx_ptr

    def gfx_begin_frame(self, ctx, color: Color = None):
        if color is None:
            color = Color(0.05, 0.05, 0.1)
        if not self._sim:
            self._lib.sc_gfx_begin_frame(ctx, color.r, color.g, color.b, color.a)

    def gfx_end_frame(self, ctx):
        if not self._sim:
            self._lib.sc_gfx_end_frame(ctx)

    def gfx_shutdown(self, ctx):
        if not self._sim:
            self._lib.sc_gfx_shutdown(ctx)

# ---------------------------------------------------------------------------
# High-level Scene (pure-Python retained mode for scripting)
# ---------------------------------------------------------------------------

@dataclass
class _Widget:
    id:       int
    wtype:    str
    visible:  bool     = True
    x: float  = 0.0
    y: float  = 0.0
    w: float  = 100.0
    h: float  = 30.0
    color:    Color    = field(default_factory=Color)
    text:     str      = ""
    font_size: float   = 14.0
    alpha:    float    = 1.0
    children: List[int] = field(default_factory=list)
    parent:   int      = -1
    layout:   Optional[LayoutStyle] = None
    on_press: Optional[Callable] = None
    user_data: object  = None


class Scene:
    """
    Pure-Python retained-mode scene, backed by the native SCScene when the
    shared library is available.
    """

    def __init__(self, sc: SilverCore, gfx, width: float, height: float):
        self._sc       = sc
        self._gfx      = gfx
        self._w        = width
        self._h        = height
        self._widgets: List[_Widget] = []
        self._time     = 0.0
        # Native scene pointer (allocated on C side)
        self._ptr      = None

    # -----------------------------------------------------------------------
    # Widget creation
    # -----------------------------------------------------------------------

    def widget_rect(self, parent: int, color: Color,
                     layout: Optional[LayoutStyle] = None) -> int:
        return self._add("rect", parent, color=color, layout=layout)

    def widget_text(self, parent: int, text: str, color: Color,
                     font_size: float = 14.0,
                     layout: Optional[LayoutStyle] = None) -> int:
        return self._add("text", parent, color=color, text=text,
                          font_size=font_size, layout=layout)

    def widget_canvas(self, parent: int,
                       paint_fn: Callable,
                       layout: Optional[LayoutStyle] = None) -> int:
        wid = self._add("canvas", parent, layout=layout)
        self._widgets[wid].on_press = paint_fn  # reuse slot
        return wid

    def _add(self, wtype: str, parent: int, **kwargs) -> int:
        # Bug #16: validate parent index before accessing self._widgets[parent]
        if parent >= 0 and parent >= len(self._widgets):
            raise IndexError(f"Scene._add: parent widget id {parent} out of range "
                             f"(have {len(self._widgets)} widgets)")
        wid = len(self._widgets)
        w   = _Widget(id=wid, wtype=wtype, parent=parent, **kwargs)
        if layout := kwargs.get("layout"):
            w.layout = layout
        self._widgets.append(w)
        if parent >= 0:
            self._widgets[parent].children.append(wid)
        return wid

    # -----------------------------------------------------------------------
    # Mutation helpers
    # -----------------------------------------------------------------------

    def set_text(self, wid: int, text: str):
        self._widgets[wid].text = text

    def set_color(self, wid: int, color: Color):
        self._widgets[wid].color = color

    def set_visible(self, wid: int, visible: bool):
        self._widgets[wid].visible = visible

    # -----------------------------------------------------------------------
    # Layout (simple fixed-pixel pass for simulation mode)
    # -----------------------------------------------------------------------

    def _layout(self):
        """Extremely simplified layout: assign fixed positions for now."""
        y_cursor = 0.0
        for w in self._widgets:
            if w.parent == -1:
                w.x = 0.0
                w.y = y_cursor
                w.w = w.layout.width if (w.layout and w.layout.width > 0) else self._w
                w.h = w.layout.height if (w.layout and w.layout.height > 0) else 30.0
                y_cursor += w.h

    # -----------------------------------------------------------------------
    # Update / render
    # -----------------------------------------------------------------------

    def update(self, dt: float):
        self._time += dt
        self._layout()
        if not self._sc._sim and self._ptr:
            self._sc._lib.sc_scene_update(self._ptr, ctypes.c_float(dt))

    def render(self):
        if not self._sc._sim and self._ptr:
            self._sc._lib.sc_scene_render(self._ptr)
        # Simulation mode: nothing to draw (headless)

    def resize(self, w: float, h: float):
        self._w = w
        self._h = h


# ---------------------------------------------------------------------------
# CLI self-test
# ---------------------------------------------------------------------------

if __name__ == "__main__":
    sc    = SilverCore()
    gfx   = sc.gfx_init(width=1280, height=720)
    scene = Scene(sc, gfx, 1280, 720)

    root  = scene.widget_rect(-1, Color.from_hex("#1a1a2e"),
                                LayoutStyle(flex_dir=FlexDir.COLUMN,
                                            width=1280, height=720))
    hdr   = scene.widget_rect(root, Color.from_hex("#16213e"),
                                LayoutStyle(width=1280, height=56))
    title = scene.widget_text(hdr, "SilverCore Stock Dashboard",
                               Color.WHITE, font_size=18,
                               layout=LayoutStyle(width=400, height=56))

    print(f"Widgets created: {len(scene._widgets)}")
    scene.update(dt=0.016)
    scene.render()

    sc.gfx_begin_frame(gfx, Color(0.05, 0.05, 0.1))
    sc.gfx_end_frame(gfx)
    sc.gfx_shutdown(gfx)
    print("[silvercore] self-test passed")
