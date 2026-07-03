# Graphics — Design Sketch (Unimplemented)

> **Nothing in this chapter exists yet.** `std/graphics` is not built, not shipped, and not tested. This is an architecture sketch — written to guide implementation — for a future media library in the spirit of [raylib](https://www.raylib.com/): simple enough for a weekend game, capable enough for a native GUI app. **This is not a raylib binding.** The plan is to write Perzephxne's own renderer and windowing layer from scratch, on raw platform APIs, and shape its call-level ergonomics after raylib's — because raylib's API is a genuinely good design, not because the library itself is a dependency worth taking on. Every other chapter in this book documents the compiler as it is; this one documents a plan.

## Goals

- **Fast, with nothing between the call and the GPU.** Draw calls should reach OpenGL (or whatever backend a given platform uses) directly — no bound C library's abstractions, no extra indirection layer, no per-frame hidden allocation. Perzephxne already has no GC and manual/RC memory control; the graphics layer should spend that advantage on latency, especially input-to-frame latency for mouse-driven interaction, not give it back to a middleman.
- **Versatile.** The same primitives should support 2D and 3D games, and — layered with a small immediate-mode widget set — native GUI applications, editor tools, and visualizers, the same way Qt or GTK cover both custom-rendered content and native-feeling widgets from one toolkit.
- **Idiomatic.** `snake_case` functions, `struct` + `impl` for resources, enums for key/button constants, tuples for multi-value returns (`get_mouse_position() -> (f32, f32)`), and `!T` failable returns for anything that can fail to load (a missing texture file should be a caught error, not a null-pointer crash).

## Why build our own renderer instead of binding raylib

Binding raylib was the original plan, and it's a much smaller amount of work — but it means every Perzephxne program built on it inherits raylib's own constraints: a single prebuilt `.so`/`.a` to link against on every platform, raylib's own threading model, and a hard ceiling on performance and control set by whatever raylib's C implementation chooses to do internally. Writing the renderer directly against the platform (OpenGL for the GPU, native windowing/input APIs — X11 or Wayland on Linux first, matching this project's current target) means:

- **No layer between a draw call and the driver.** A wrapped `draw_rectangle()` can compile down to appending to a vertex buffer that's already bound, with no intermediate library deciding how that happens.
- **Full control over the frame loop and threading**, instead of inheriting raylib's single-threaded-main-loop assumption.
- **The widget layer (layer 3) can be built to genuinely competing with Qt/GTK** for native-app use, rather than staying a thin wrapper over a game library's optional extra (`raygui`) — real native apps need things like text layout, DPI awareness, and window-manager integration that a game-first library treats as secondary.
- **One dependency story instead of two.** The library only ever needs what the OS already ships (`libGL`, `libX11`/Wayland client libs) — no separately-versioned third-party renderer to build, vendor, or link against on every target platform.

The tradeoff is honest: this is a much bigger undertaking than binding an existing library. Window creation, GL context setup, function-pointer loading, an immediate-mode 2D/3D draw API, image decoding, and audio all have to be built, not bound. The sections below sketch that as a layered architecture so it can be built incrementally, starting from the smallest usable slice (open a window, clear it, draw a rectangle) rather than attempting the whole surface at once.

## Layered architecture

```
┌─────────────────────────────────────────────┐
│ std/gui        immediate-mode widgets        │  ← pure Perzephxne, built on std/graphics
│                (button, slider, textbox)     │
├─────────────────────────────────────────────┤
│ std/graphics   idiomatic wrapper             │  ← snake_case fns, impl methods, enums,
│                                               │     failable loads, tuple returns
├─────────────────────────────────────────────┤
│ std/graphics/gl       raw OpenGL bindings    │  ← extern fn per GL entry point, loaded at
│                                               │     runtime (no libGL.so build-time symbols
│                                               │     beyond the loader itself)
│ std/graphics/window   platform windowing     │  ← extern fn to X11/Wayland + input events;
│                       and input              │     one backend per platform, same surface
├─────────────────────────────────────────────┤
│ libGL.so, libX11.so / Wayland client libs    │  ← already present on the OS; no bundled
│ (system, not shipped)                        │     third-party renderer dependency
└─────────────────────────────────────────────┘
```

### Layer 1 — platform bindings (windowing + raw GL)

Two thin `extern fn` layers, each a direct, dumb mapping onto a system API — no idiomatic naming, no safety wrapping, so advanced users can always drop down to the raw calls when a higher layer doesn't cover something yet:

- **`std/graphics/window`** — open a window and pump its event queue via the platform's native windowing API (X11 first, since that's universally available even under Wayland's XWayland compatibility layer; a native Wayland backend and, eventually, Win32/Cocoa backends follow the same shape behind one Perzephxne-facing surface).
- **`std/graphics/gl`** — one `extern fn` per OpenGL entry point actually used. Since GL functions beyond 1.1 are resolved at runtime (`glXGetProcAddress` on Linux, not link-time symbols), this layer is a loader plus a table of function pointers, not a flat `extern fn` list against a link-time library the way the windowing layer is.

```
# std/graphics/window (sketch)
extern fn XOpenDisplay(name: *u8) -> *u8
extern fn XCreateSimpleWindow(display: *u8, parent: u64, x: i32, y: i32,
                               w: u32, h: u32, border_w: u32, border: u64, bg: u64) -> u64
extern fn XMapWindow(display: *u8, window: u64)
extern fn XNextEvent(display: *u8, event_out: *u8) -> i32
extern fn XPending(display: *u8) -> i32

# std/graphics/gl (sketch) — resolved at runtime, not linked at build time
fn gl_get_proc_address(name: str) -> *void   # wraps glXGetProcAddress
# each wrapped GL call is a function-pointer field on a loaded-functions struct,
# populated once at startup by gl_get_proc_address("glClear"), etc.
```

Plain-old-data types (`Vector2`, `Vector3`, `Color`, `Rectangle`) are ordinary Perzephxne `struct` declarations, field-for-field, since Perzephxne structs already follow C ABI layout — this part of the original raylib-binding sketch carries over unchanged, since it's just describing data, not an API:

```
struct Vector2   { x: f32, y: f32 }
struct Vector3   { x: f32, y: f32, z: f32 }
struct Color     { r: u8, g: u8, b: u8, a: u8 }
struct Rectangle { x: f32, y: f32, width: f32, height: f32 }
```

### Layer 2 — the idiomatic wrapper

`std/graphics` turns the raw platform + GL bindings into something that reads like the rest of the standard library and, deliberately, like raylib's call-level ergonomics: `snake_case` names, `enum` constants instead of bare integers, tuples instead of output parameters, and `!T` for anything that can fail. The difference from the original binding plan is entirely underneath this layer — `Window.open()` now creates an X11 window and a GL context instead of calling into raylib's `InitWindow`, and `draw_rectangle()` now issues raw GL draw calls (batched into a vertex buffer, flushed on `end_drawing()`) instead of forwarding to raylib's immediate 2D renderer. The surface Perzephxne code sees stays the same:

```
enum Key {
    Space, Enter, Escape, Up, Down, Left, Right,
    A, B, C, # ... rest of the alphabet
}

enum MouseButton { Left, Right, Middle }

struct Window { title: str, w: i32, h: i32 }

impl Window {
    fn open(title: str, w: i32, h: i32) -> Window {
        # opens an X11 window, creates a GL context, loads GL entry points
        ret gfx_open_window(title, w, h)
    }
    fn should_close(self: Window) -> bool { ret gfx_should_close() }
    fn close(self: Window) { gfx_close_window() }
}

fn is_key_pressed(k: Key) -> bool { ret gfx_key_pressed(key_code(k)) }
fn mouse_position() -> (f32, f32) { ret gfx_mouse_position() }

struct Texture { id: u32, w: i32, h: i32 }
impl Texture {
    fn load(path: str) -> !Texture {
        # decode (PNG to start), upload via glTexImage2D, wrap the GL texture id
        ret gfx_load_texture(path)
    }
    fn unload(self: Texture) { gfx_delete_texture(self.id) }
}
```

A full program, in the style the rest of the book uses:

```
import(gfx = "std/graphics")

fn main() -> i32 {
    win: gfx.Window = gfx.Window.open("demo", 800, 450)
    pos: (f32, f32) = (400.0, 225.0)

    while !win.should_close() {
        if gfx.is_key_pressed(gfx.Key.Right) { pos.0 += 5.0 }
        gfx.begin_drawing()
        gfx.clear_background(gfx.Color.RayWhite)
        gfx.draw_circle(pos.0, pos.1, 20.0, gfx.Color.Red)
        gfx.end_drawing()
    }
    win.close()
    ret 0
}
```

### Layer 3 — `std/gui`, an immediate-mode widget set

This is the piece that makes the library useful for **native GUI apps**, not just games — the same immediate-mode loop that draws a frame of a game can draw a frame of a settings panel. Modeled on raylib's companion library `raygui`: no retained widget tree, no event callbacks — each widget function draws itself and returns whether it was interacted with *this frame*, decided entirely from the current mouse/keyboard state:

```
fn button(bounds: Rectangle, label: str) -> bool   # true the frame it's clicked
fn slider(bounds: Rectangle, value: f32, min: f32, max: f32) -> f32  # returns updated value
fn checkbox(bounds: Rectangle, label: str, checked: bool) -> bool
fn text_box(bounds: Rectangle, text: str) -> str
```

Because it's built entirely from `std/graphics` primitives (rectangles, text, mouse position, click state), it needs no compiler support beyond what a game already needs — the "GUI toolkit" is just a library, same as the "game engine" is. Matching Qt/GTK's *reach* (real desktop apps, not just game overlays) is a layer-3 goal, not a layer-1/2 one: it comes from investing in text layout, DPI scaling, and window-manager integration (resizable/native-chrome windows, clipboard, drag-and-drop) once the primitives underneath are solid, not from anything the renderer or windowing layer needs to do differently.

## Resolved prerequisites

Both of the compiler defects this chapter originally surfaced have since been fixed and regression-tested — they're recorded here because they were found while drafting this design, not because they're still blocking it:

- **`!StructName` failable returns.** `fn make() -> !Point { ret @ok(Point{...}) }` now works for any struct, tagged union, or array — see `tests/run/failable_struct.przp`.
- **Struct-by-value FFI ABI.** An `extern fn` reached via `import()` that takes or returns a plain struct by value (e.g. `Vector2 { f32, f32 }`) now follows the x86-64 System V ABI, verified by linking against independently-compiled C code — see [Functions § Struct-by-Value Parameters and Returns](./functions.md#struct-by-value-parameters-and-returns) and `tests/ffi/struct_abi`. The one scoped limitation: this only works when the `extern fn` is declared in a module reached through `import()` — a struct-by-value `extern fn` declared directly in a file with no import fails to compile with a clear diagnostic rather than silently miscompiling.

This means the FFI mechanics layer 1 depends on (struct-by-value calls, failable returns for loaders) are no longer blocked on compiler work — the remaining prerequisites below are about what actually has to be written (a renderer, a windowing backend), not compiler correctness.

## Open engineering prerequisites

| # | Prerequisite | Status |
|---|---|---|
| 1 | **Linker flags / native library dependencies.** There is currently no way for a `przp.toml` project to declare "link against `-lX11 -lGL`" or similar. The `[deps]` manifest section is reserved but unimplemented (see [Status & Next Work](./status-next.md)). As a stopgap for a proof of concept, `przp sac` does accept extra native object/archive files on its command line, passed straight through to the link step (used by the ABI regression test above) — but that's a `sac`-only escape hatch, not a real answer for a `przp build`/`przp run` project. |
| 2 | **GL function loading.** Nothing beyond OpenGL 1.1 is available as a link-time symbol — every modern GL entry point (shaders, buffers, textures beyond the basics) has to be resolved at runtime via `glXGetProcAddress` and called through a function pointer. This needs a small runtime loader written once, not per-project. |
| 3 | **Windowing backend.** X11 first (works everywhere on Linux, including under Wayland via XWayland); a native Wayland backend, then Win32/Cocoa, come later behind the same `std/graphics/window` surface so `std/graphics` and everything above it never has to change per platform. |
| 4 | **Image and audio codecs.** No bundled renderer means no bundled codecs either — PNG/JPEG decoding and audio file loading need their own (likely also hand-written or minimally-bound) implementations, deferred until the 2D primitives above them are solid. |
| 5 | **Variadic/macro helpers.** Small conveniences like a `printf`-style text-formatting helper for on-screen debug text are built on `@fmt` rather than needing any new compiler feature — a non-issue, listed for completeness. |
| 6 | **Threading model.** GL contexts are tied to the thread that created them; the wrapper should document a single-threaded main loop (window, input, and drawing calls all from one thread) as the initial supported model rather than attempt general multi-threaded rendering from the start. |

## Suggested build order

1. Resolve prerequisite #1 (linker flags) with the smallest workable manifest addition — even a flat `link = ["X11", "GL"]` key under `[build]` would unblock this for real projects, not just `sac`-based experiments.
2. Write the windowing backend (`std/graphics/window`, X11) for the smallest usable slice: open a window, pump events, report close/resize.
3. Write the GL loader (`std/graphics/gl`) and get a single triangle or cleared background on screen — this is the point where "a window exists" becomes "a frame can be drawn."
4. Write layer 2 (`std/graphics`) over that same small surface: 2D shapes, keyboard/mouse input, then texture loading once an image codec exists. Resist building out the full raylib-equivalent surface up front.
5. Write a regression test per wrapped function group (window, shapes, input, textures), following the existing `tests/run/std_*.przp` convention, to the extent a windowing/GL-dependent test can run headlessly in CI.
6. Only after 2D is solid, extend to 3D (`Camera3D`-equivalent, cube/grid primitives) and audio.
7. Layer 3 (`std/gui`) can start as soon as layer 2 has rectangles, text, and mouse state — it doesn't need 3D or audio at all. Prioritize mouse-interaction latency here specifically, since that's the axis this design most wants to win on: input state should be sampled once per frame with nothing between the OS event and code reading it.
