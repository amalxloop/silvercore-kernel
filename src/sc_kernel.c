/*
 * sc_kernel.c  --  Unity-build translation unit for the SilverCore kernel
 *
 * By defining all SC_*_IMPLEMENTATION macros here and #including every
 * kernel header in a single TU, the compiler can inline aggressively
 * across subsystem boundaries without LTO.
 *
 * To select a graphics backend, define SC_GFX_BACKEND_VULKAN before
 * including this file (e.g. via -D or #define at the top), or let the
 * default software backend apply.
 */
#define SC_GFX_IMPLEMENTATION
#define SC_LAYOUT_IMPLEMENTATION
#define SC_WIDGET_IMPLEMENTATION
#define SC_RUNTIME_IMPLEMENTATION
#define SC_FONT_IMPLEMENTATION

#include "sc_types.h"
#include "sc_math.h"
#include "sc_arena.h"
#include "sc_layout.h"
#include "sc_gfx.h"
#include "sc_font.h"
#include "sc_widget.h"
#include "sc_runtime.h"

#ifdef SC_GFX_BACKEND_VULKAN
#define SC_BACKEND_VULKAN_IMPLEMENTATION
#include "sc_backend_vulkan.h"
#endif
