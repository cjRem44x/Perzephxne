# Graphics — Design Sketch (Mostly Unimplemented)

> **Almost nothing in this chapter is built yet.** This is an architecture sketch — written to guide implementation — for a future media stack in the spirit of [raylib](https://www.raylib.com/): simple enough for a weekend game, capable enough for a native GUI app. **This is not a raylib binding.** The plan is to write Perzephxne's own renderer and windowing layer from scratch, on raw platform APIs, and shape its call-level ergonomics after raylib's — because raylib's API is a genuinely good design, not because the library itself is a dependency worth taking on. Every other chapter in this book documents the compiler as it is; this one documents a plan, with one exception: `std/graphics/window`'s open/close path (below) is real, shipped, and regression-tested. Everything else — the backend contract, the GL backend, `std/graphics`, `gdev`, and `guix` — remains unbuilt.

The stack splits into two purpose-specific libraries built on one shared foundation:

- **`gdev`** — game-dev primitives: sprites, 3D cameras, audio, animation.
- **`guix`** — native GUI widgets: buttons, sliders, text editing, window chrome, DPI scaling.
- **`std/graphics`** — the shared foundation both depend on: window lifecycle, input, the raw GPU backend, and the 2D/3D draw primitives neither `gdev` nor `guix` needs to reimplement.

A game doesn't need `guix`'s text-editing and clipboard code any more than a settings panel needs `gdev`'s 3D camera and audio mixer — splitting them means each stays as small as the thing it's actually for, without either depending on the other.

## Goals

- **Fast, with nothing between the call and the GPU.** Draw calls should reach the GPU backend directly — no bound C library's abstractions, no extra indirection layer, no per-frame hidden allocation. Perzephxne already has no GC and manual/RC memory control; the graphics layer should spend that advantage on latency, especially input-to-frame latency for mouse-driven interaction, not give it back to a middleman.
- **Versatile.** `gdev` covers 2D and 3D games; `guix` covers native GUI applications, editor tools, and visualizers — the same way Qt or GTK cover both custom-rendered content and native-feeling widgets from one toolkit family, without forcing a game to carry GUI weight or a GUI app to carry a 3D renderer.
- **Idiomatic.** `snake_case` functions, `struct` + `impl` for resources, enums for key/button constants, tuples for multi-value returns (`get_mouse_position() -> (f32, f32)`), and `!T` failable returns for anything that can fail to load (a missing texture file should be a caught error, not a null-pointer crash).

## Why build our own renderer instead of binding raylib

Binding raylib was the original plan, and it's a much smaller amount of work — but it means every Perzephxne program built on it inherits raylib's own constraints: a single prebuilt `.so`/`.a` to link against on every platform, raylib's own threading model, and a hard ceiling on performance and control set by whatever raylib's C implementation chooses to do internally. Writing the renderer directly against the platform means:

- **No layer between a draw call and the driver.** A wrapped `draw_rectangle()` can compile down to appending to a vertex buffer that's already bound, with no intermediate library deciding how that happens.
- **Full control over the frame loop and threading**, instead of inheriting a game library's single-threaded-main-loop assumption.
- **`guix` can genuinely compete with Qt/GTK** for native-app use, rather than staying a thin wrapper over a game library's optional extra (`raygui`) — real native apps need things like text layout, DPI awareness, and window-manager integration that a game-first library treats as secondary.
- **One dependency story instead of two.** The library only ever needs what the OS already ships (`libGL`, `libX11`/Wayland client libs) — no separately-versioned third-party renderer to build, vendor, or link against on every target platform.

### GPU backend: OpenGL first, behind a swap boundary

OpenGL is the initial GPU backend — it's mature, well-documented, and (via GLX) gets a first triangle on screen with the least amount of platform-specific plumbing, which matches this project's current Linux-first target. It is not, long-term, the obviously-correct cross-platform answer: it's effectively frozen on macOS at OpenGL 4.1 (Apple has been steering everyone toward Metal for years), and it doesn't expose the explicit, modern control that Vulkan/Metal/D3D12 do. Committing to it as *the* answer now would mean a full rewrite the day real macOS or high-end-performance support becomes a goal.

The mitigation is architectural, not a bet on OpenGL forever: `std/graphics` never calls GL functions directly. It calls through a small, fixed **backend contract** — init, clear, submit a batch of triangles/quads, upload a texture, bind a shader — and `std/graphics/gl` is just the first (and, for now, only) implementation of that contract. Adding Vulkan, Metal, or D3D12 later means writing a new backend that satisfies the same contract, not touching `std/graphics`, `gdev`, or `guix` at all. Getting this seam right *before* writing the GL backend — not retrofitting it once GL calls are already scattered through the wrapper — is listed explicitly in the build order below, because retrofitting it is exactly the kind of rewrite this boundary exists to avoid.

The tradeoff is honest either way: this is a much bigger undertaking than binding an existing library. Window creation, GL context setup, function-pointer loading, a backend contract, an immediate-mode 2D/3D draw API, image decoding, and audio all have to be built, not bound. The sections below sketch that as a layered architecture so it can be built incrementally, starting from the smallest usable slice (open a window, clear it, draw a rectangle) rather than attempting the whole surface at once.

## Layered architecture

```
┌────────────────────────────┐   ┌────────────────────────────┐
│ guix   native GUI widgets  │   │ gdev   game-dev primitives │
│ (button, slider, text      │   │ (sprites, 3D camera,       │
│  editing, window chrome,   │   │  batching, audio,          │
│  DPI scaling, clipboard)   │   │  animation helpers)        │
└──────────────┬─────────────┘   └──────────────┬─────────────┘
               └────────────────┬────────────────┘
                     std/graphics — shared foundation
             (2D/3D draw primitives, plain-data types, window
              lifecycle, input, snake_case idiomatic wrapper)
                    ┌────────────┴────────────┐
                    │   backend contract        │  ← fixed interface: init, clear,
                    │   (fixed interface)       │     submit batch, upload texture,
                    └────────────┬────────────┘     bind shader, ...
        ┌───────────────────────┼───────────────────────┐
 std/graphics/gl (OpenGL,        │           (future: Vulkan/Metal/D3D12
  first backend, via GLX)        │            backends satisfying the same
 std/graphics/window (X11,       │            contract — no changes needed
  first backend)                 │            above this line)
        └───────────────────────┴───────────────────────┘
      libGL.so, libX11.so / Wayland client libs (system, not shipped)
```

### Layer 1 — platform bindings and the backend contract

Three pieces, each a thin, dumb layer — no idiomatic naming, no safety wrapping, so advanced users can always drop to the raw calls when a higher layer doesn't cover something yet:

- **`std/graphics/window`** — open a window and pump its event queue via the platform's native windowing API (X11 first, since that's universally available even under Wayland's XWayland compatibility layer; a native Wayland backend and, eventually, Win32/Cocoa backends follow the same shape behind one Perzephxne-facing surface). **Implemented** — see below.
- **The backend contract** — not a library binding at all, but a fixed set of Perzephxne function signatures (or a struct of function pointers) that any GPU backend must implement: `backend_init()`, `backend_clear(color)`, `backend_submit(vertices, indices)`, `backend_upload_texture(pixels, w, h) -> u32`, `backend_bind_shader(id)`, and a small, deliberately minimal set beyond that. This contract is designed once, before any backend implements it. Not yet designed.
- **`std/graphics/gl`** — the OpenGL implementation of that contract. Since GL functions beyond 1.1 are resolved at runtime (`glXGetProcAddress` on Linux, not link-time symbols), this layer is a loader plus a table of function pointers, not a flat `extern fn` list against a link-time library the way the windowing layer is. Not yet built.

### `std/graphics/window` — implemented

Open, title, and cleanly close an X11 window, linked via `[build].link = ["X11"]` (see [Build System](./build-system.md)). Real `extern fn` bindings, not a sketch — see `compiler/std/graphics/window.przp` and the regression test `tests/x11/window_close`:

```
extern fn XOpenDisplay(name: *u8) -> *u8
extern fn XCloseDisplay(display: *u8) -> i32
extern fn XDefaultScreen(display: *u8) -> i32
extern fn XRootWindow(display: *u8, screen: i32) -> u64
extern fn XCreateSimpleWindow(display: *u8, parent: u64, x: i32, y: i32,
                               w: u32, h: u32, border_w: u32, border: u64, bg: u64) -> u64
extern fn XDestroyWindow(display: *u8, window: u64) -> i32
extern fn XMapWindow(display: *u8, window: u64) -> i32
extern fn XStoreName(display: *u8, window: u64, name: *u8) -> i32
extern fn XSelectInput(display: *u8, window: u64, mask: i64) -> i32
extern fn XInternAtom(display: *u8, name: *u8, only_if_exists: i32) -> u64
extern fn XSetWMProtocols(display: *u8, window: u64, protocols: *u64, count: i32) -> i32
extern fn XNextEvent(display: *u8, event_out: *u8) -> i32
extern fn XPending(display: *u8) -> i32
extern fn XFlush(display: *u8) -> i32
extern fn XSendEvent(display: *u8, window: u64, propagate: i32, mask: i64, event: *u8) -> i32
```

`XEvent` is a 192-byte C union with no Perzephxne struct declared for it (yet) — events are read directly out of a raw `[192]u8` buffer via pointer arithmetic and type-punning (`p: *u64 = buf + 32` reads the `window` field every event variant shares at that byte offset), with the offsets checked against a real `offsetof(XClientMessageEvent, ...)` compile rather than guessed:

```
fn event_type(buf: *u8) -> i32 { p: *i32 = buf; ret p.* }              # offset 0
fn event_window(buf: *u8) -> u64 { p: *u64 = buf + 32; ret p.* }        # offset 32
fn event_client_message_type(buf: *u8) -> u64 { p: *u64 = buf + 40; ret p.* }  # offset 40
fn event_client_data_l0(buf: *u8) -> i64 { p: *i64 = buf + 56; ret p.* }       # offset 56
```

Close detection uses the standard `WM_PROTOCOLS`/`WM_DELETE_WINDOW` handshake — a window manager sends a `ClientMessage` on a close-button click rather than the connection just dying, so a well-behaved window has to register for it and watch for it in the event loop, the same way a C/Xlib program would. `tests/x11/window_close` verifies this by sending itself that exact `ClientMessage` via `XSendEvent` (a real round trip through the X server, not a mocked event) and confirming the event loop reads it correctly and exits — since the test's virtual display has no window manager to click a close button in the first place. That test only runs when `Xvfb` and `libX11` are available; `tests/run.sh` skips it with a message otherwise rather than failing the whole suite on machines without a virtual display set up.

Plain-old-data types (`Vector2`, `Vector3`, `Color`, `Rectangle`) are ordinary Perzephxne `struct` declarations, field-for-field, since Perzephxne structs already follow C ABI layout — this part of the original raylib-binding sketch carries over unchanged, since it's just describing data, not an API:

```
struct Vector2   { x: f32, y: f32 }
struct Vector3   { x: f32, y: f32, z: f32 }
struct Color     { r: u8, g: u8, b: u8, a: u8 }
struct Rectangle { x: f32, y: f32, width: f32, height: f32 }
```

### Layer 2 — `std/graphics`, the shared foundation

`std/graphics` turns the backend contract and windowing layer into something that reads like the rest of the standard library and, deliberately, like raylib's call-level ergonomics: `snake_case` names, `enum` constants instead of bare integers, tuples instead of output parameters, and `!T` for anything that can fail. This is the layer both `gdev` and `guix` import — window lifecycle, input, and 2D drawing primitives live here exactly once, not duplicated in each consumer:

```
enum Key {
    Space, Enter, Escape, Up, Down, Left, Right,
    A, B, C, # ... rest of the alphabet
}

enum MouseButton { Left, Right, Middle }

struct Window { title: str, w: i32, h: i32 }

impl Window {
    fn open(title: str, w: i32, h: i32) -> Window {
        # opens an X11 window, creates a GL context, initializes the backend
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
        # decode (PNG to start), upload via backend_upload_texture, wrap the id
        ret gfx_load_texture(path)
    }
    fn unload(self: Texture) { gfx_delete_texture(self.id) }
}

fn begin_drawing()
fn end_drawing()               # flushes the frame's batched draws to the backend
fn clear_background(c: Color)
fn draw_rectangle(x: f32, y: f32, w: f32, h: f32, c: Color)
fn draw_circle(x: f32, y: f32, r: f32, c: Color)
fn draw_texture(t: Texture, x: f32, y: f32)
fn draw_text(s: str, x: f32, y: f32, size: i32, c: Color)
```

### Layer 3a — `gdev`, game-dev primitives

Built on `std/graphics`, adding the pieces a game needs that a GUI app doesn't:

```
struct Camera3D { position: Vector3, target: Vector3, up: Vector3, fovy: f32 }
fn begin_mode_3d(cam: Camera3D)
fn end_mode_3d()
fn draw_cube(pos: Vector3, w: f32, h: f32, d: f32, c: Color)
fn draw_grid(slices: i32, spacing: f32)

struct Sprite { texture: Texture, frame: Rectangle }
impl Sprite {
    fn draw(self: Sprite, pos: Vector2) { draw_texture_rec(self.texture, self.frame, pos) }
}

struct Sound { id: u32 }
impl Sound {
    fn load(path: str) -> !Sound { ret gdev_load_sound(path) }
    fn play(self: Sound) { gdev_play_sound(self.id) }
}
```

A full program, in the style the rest of the book uses:

```
import(gdev = "std/gdev")

fn main() -> i32 {
    win: gdev.Window = gdev.Window.open("demo", 800, 450)
    pos: (f32, f32) = (400.0, 225.0)

    while !win.should_close() {
        if gdev.is_key_pressed(gdev.Key.Right) { pos.0 += 5.0 }
        gdev.begin_drawing()
        gdev.clear_background(gdev.Color.RayWhite)
        gdev.draw_circle(pos.0, pos.1, 20.0, gdev.Color.Red)
        gdev.end_drawing()
    }
    win.close()
    ret 0
}
```

### Layer 3b — `guix`, native GUI widgets

This is the piece aimed squarely at native apps, not games — immediate-mode, raygui-style widgets, plus the concerns a game doesn't have to think about: DPI scaling, text editing, focus/tab order, and clipboard. No retained widget tree, no event callbacks — each widget function draws itself and returns whether it was interacted with *this frame*, decided entirely from the current mouse/keyboard state:

```
fn button(bounds: Rectangle, label: str) -> bool   # true the frame it's clicked
fn slider(bounds: Rectangle, value: f32, min: f32, max: f32) -> f32  # returns updated value
fn checkbox(bounds: Rectangle, label: str, checked: bool) -> bool
fn text_box(bounds: Rectangle, text: str) -> str    # handles cursor, selection, IME later

fn dpi_scale() -> f32                # for crisp rendering on high-DPI displays
fn set_clipboard(s: str)
fn get_clipboard() -> str
fn request_focus(widget_id: i32)     # tab order / keyboard navigation
```

Because it's built entirely from `std/graphics` primitives (rectangles, text, mouse position, click state), `guix` needs no compiler support beyond what `gdev` already needs — the "GUI toolkit" is just a library, same as the "game engine" is. The `Qt`/`GTK`-level reach — resizable native-chrome windows, real text layout, window-manager integration — is a `guix`-specific investment on top of the shared primitives, not something `std/graphics` or the backend contract needs to change to support.

```
import(guix = "std/guix")

fn main() -> i32 {
    win: guix.Window = guix.Window.open("settings", 400, 300)
    volume: f32 = 0.5

    while !win.should_close() {
        guix.begin_drawing()
        guix.clear_background(guix.Color.RayWhite)
        volume = guix.slider(guix.Rectangle{.x=20.0, .y=20.0, .width=360.0, .height=24.0},
                              volume, 0.0, 1.0)
        if guix.button(guix.Rectangle{.x=20.0, .y=60.0, .width=100.0, .height=32.0}, "Apply") {
            @pf("volume: {volume}\n")
        }
        guix.end_drawing()
    }
    win.close()
    ret 0
}
```

## Resolved prerequisites

Both of the compiler defects this chapter originally surfaced have since been fixed and regression-tested — they're recorded here because they were found while drafting this design, not because they're still blocking it:

- **`!StructName` failable returns.** `fn make() -> !Point { ret @ok(Point{...}) }` now works for any struct, tagged union, or array — see `tests/run/failable_struct.przp`.
- **Struct-by-value FFI ABI.** An `extern fn` reached via `import()` that takes or returns a plain struct by value (e.g. `Vector2 { f32, f32 }`) now follows the x86-64 System V ABI, verified by linking against independently-compiled C code — see [Functions § Struct-by-Value Parameters and Returns](./functions.md#struct-by-value-parameters-and-returns) and `tests/ffi/struct_abi`. The one scoped limitation: this only works when the `extern fn` is declared in a module reached through `import()` — a struct-by-value `extern fn` declared directly in a file with no import fails to compile with a clear diagnostic rather than silently miscompiling.
- **Linker flags / native library dependencies.** `[build].link` in `przp.toml` now declares system libraries to link against (`link = ["X11", "GL"]` → `-lX11 -lGL` on the final link step, for both `przp build` and `przp run`) — see [Build System](./build-system.md) and `tests/project/link_libs`. This is not a package resolver (`[deps]` is still reserved and unimplemented, see [Status & Next Work](./status-next.md)) — it only covers linking against libraries the OS already ships, which is exactly what the windowing (`libX11`) and GPU (`libGL`) layers below need.

This means the FFI mechanics and linking layer 1 depends on (struct-by-value calls, failable returns for loaders, linking against system libraries) are no longer blocked on compiler/toolchain work — the remaining prerequisites below are about what actually has to be written (a renderer, a windowing backend, the backend contract), not missing tooling.

## Open engineering prerequisites

| # | Prerequisite | Status |
|---|---|---|
| 1 | **Backend contract design.** The fixed interface `std/graphics` calls through (init, clear, submit, upload texture, bind shader) needs to be designed and settled *before* the GL backend is written against it — this is what makes future Vulkan/Metal/D3D12 backends additive instead of a rewrite. |
| 2 | **GL function loading.** Nothing beyond OpenGL 1.1 is available as a link-time symbol — every modern GL entry point (shaders, buffers, textures beyond the basics) has to be resolved at runtime via `glXGetProcAddress` and called through a function pointer. This needs a small runtime loader written once, not per-project. |
| 3 | **Windowing backend.** X11 first (works everywhere on Linux, including under Wayland via XWayland); a native Wayland backend, then Win32/Cocoa, come later behind the same `std/graphics/window` surface so `std/graphics`, `gdev`, and `guix` never have to change per platform. **Partially done**: open/title/close (including the `WM_DELETE_WINDOW` handshake) is implemented and tested. Still needed: resize reporting, keyboard/mouse input events. |
| 4 | **Image and audio codecs.** No bundled renderer means no bundled codecs either — PNG/JPEG decoding (for `guix` icons and `gdev` textures alike) and audio file loading (for `gdev`) need their own (likely also hand-written or minimally-bound) implementations, deferred until the 2D primitives above them are solid. |
| 5 | **Text layout.** `guix` needs real text shaping/layout (cursor positioning, line wrapping, at minimum) to be a credible native-app toolkit; `gdev` only needs simple bitmap-font text draw calls. This is a `guix`-specific investment, not a `std/graphics` one. |
| 6 | **Variadic/macro helpers.** Small conveniences like a `printf`-style text-formatting helper for on-screen debug text are built on `@fmt` rather than needing any new compiler feature — a non-issue, listed for completeness. |
| 7 | **Threading model.** GPU contexts are tied to the thread that created them; the wrapper should document a single-threaded main loop (window, input, and drawing calls all from one thread) as the initial supported model rather than attempt general multi-threaded rendering from the start. |

## Suggested build order

1. Design the backend contract (prerequisite #1) before writing any GL code — settle the smallest set of operations `std/graphics` needs (clear, submit a batch, upload a texture, bind a shader) so the GL backend is written *against* a fixed interface, not the interface being reverse-engineered out of GL calls after the fact.
2. Write the windowing backend (`std/graphics/window`, X11) for the smallest usable slice: open a window, pump events, report close/resize. **Open + title + close is done**; resize and input events are still open.
3. Write `std/graphics/gl` as the first implementation of the backend contract, and get a single triangle or cleared background on screen — this is the point where "a window exists" becomes "a frame can be drawn."
4. Write layer 2 (`std/graphics`) over that same small surface: 2D shapes, keyboard/mouse input, then texture loading once an image codec exists. Resist building out the full raylib-equivalent surface up front.
5. Write a regression test per wrapped function group (window, shapes, input, textures), following the existing `tests/run/std_*.przp` convention, to the extent a windowing/GPU-dependent test can run headlessly in CI.
6. Split into `gdev` and `guix` once `std/graphics`'s 2D primitives, input, and texture loading are solid — from here the two can proceed independently, since neither depends on the other, only on `std/graphics`.
7. `gdev`: extend to 3D (`Camera3D`-equivalent, cube/grid primitives) and audio.
8. `guix`: extend to DPI scaling, text editing/cursor handling, focus/tab order, and clipboard. Prioritize mouse-interaction latency here specifically, since that's the axis this design most wants to win on: input state should be sampled once per frame with nothing between the OS event and code reading it.
