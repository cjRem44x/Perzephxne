# Graphics — Design Sketch (Layer 1 Implemented, Layer 2 In Progress)

> **Most of this chapter is still a plan, but layer 1 and a first slice of layer 2 are now real.** This is an architecture sketch — written to guide implementation — for a future media stack in the spirit of [raylib](https://www.raylib.com/): simple enough for a weekend game, capable enough for a native GUI app. **This is not a raylib binding.** The plan is to write Perzephxne's own renderer and windowing layer from scratch, on raw platform APIs, and shape its call-level ergonomics after raylib's — because raylib's API is a genuinely good design, not because the library itself is a dependency worth taking on. Every other chapter in this book documents the compiler as it is; this one documents a plan, with a growing exception: `std/graphics/window` (open/close, resize, keyboard/mouse input), the backend contract, `std/graphics/gl` (context setup, clear/present, and 2D immediate-mode drawing), and a first slice of `std/graphics` itself (window lifecycle, input queries, `Color`, and `draw_rectangle`/`draw_circle`/`draw_line`) are real, shipped, and regression-tested — see their sections below. `gdev` and `guix` remain unbuilt.

The stack splits into two purpose-specific libraries built on one shared foundation:

- **`gdev`** — game-dev primitives: sprites, 3D cameras, audio, animation.
- **`guix`** — native GUI widgets: buttons, sliders, text editing, window chrome, DPI scaling.
- **`std/graphics`** — the shared foundation both depend on: window lifecycle, input, the raw GPU backend, and the 2D/3D draw primitives neither `gdev` nor `guix` needs to reimplement.

A game doesn't need `guix`'s text-editing and clipboard code any more than a settings panel needs `gdev`'s 3D camera and audio mixer — splitting them means each stays as small as the thing it's actually for, without either depending on the other.

## Goals

- **Fast, with nothing between the call and the GPU.** Draw calls should reach the GPU backend directly — no bound C library's abstractions, no extra indirection layer, no per-frame hidden allocation. Perzephxne already has no GC and manual/RC memory control; the graphics layer should spend that advantage on latency, especially input-to-frame latency for mouse-driven interaction, not give it back to a middleman.
- **Simple by default, advanced the deeper you go.** The raylib-level call (`draw_circle(x, y, r, color)`, `Window.open(...)`) is the front door and stays trivially learnable — closer to Python's "simple things are simple" than to a AAA engine's config-first onboarding. Depth is opt-in, not mandatory: the same layering that lets `gdev`/`guix` sit above the backend contract also lets an advanced user drop down a level (raw `std/graphics` primitives, or `std/graphics/gl`'s own functions) exactly when the high-level call doesn't cover what they need, never before.
- **Versatile, `gdev` first.** `gdev` (2D and 3D games) and `guix` (native GUI applications, editor tools, visualizers) both sit on `std/graphics`, the same way Qt or GTK cover custom-rendered content and native-feeling widgets from one toolkit family — but `gdev` is the priority once layer 1 (windowing + backend contract + GL) is solid; `guix` starts once `gdev`'s 2D surface is in good shape, not in lockstep with it (see the build order below). Within `gdev` itself, 2D ships before 3D — a raylib-shaped 2D core is the actual near-term target, not a checkbox on the way to `Camera3D`.
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

- **`std/graphics/window`** — open a window and pump its event queue via the platform's native windowing API (X11 first, since that's universally available even under Wayland's XWayland compatibility layer; a native Wayland backend and, eventually, Win32/Cocoa backends follow the same shape behind one Perzephxne-facing surface). **Implemented, including resize and keyboard/mouse input** — see below.
- **The backend contract** — not a library binding at all, but a fixed set of Perzephxne function signatures (or a struct of function pointers) that any GPU backend must implement: init, clear, present, shutdown today, with submit/upload-texture/bind-shader named for later. **Implemented, at the scope today's capabilities can back for real** — see `std/graphics/backend` below. Perzephxne has no trait/interface mechanism, so the contract is a concrete `struct` of function pointers (`Backend`) rather than an abstract type — see [Functions § First-Class Functions](./functions.md#first-class-functions), which is what makes this possible with no new compiler feature.
- **`std/graphics/gl`** — the OpenGL implementation of that contract, plus a small set of 2D immediate-mode draw primitives. **Partially implemented** — see below. GL functions beyond 1.1 are resolved at runtime (`glXGetProcAddress` on Linux, not link-time symbols); that loader is still needed the moment shader-based (GL 2.0+) rendering is attempted — legacy immediate mode (`glBegin`/`glVertex2f`/`glColor4f`), which the 2D primitives below use, is real GL 1.1 and doesn't need it.

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

`XEvent` is a 192-byte C union with no Perzephxne struct declared for it (yet) — events are read directly out of a raw `[192]u8` buffer via pointer arithmetic and type-punning (`p: *u64 = buf + 32` reads the `window`/`event` field every event variant used here shares at that byte offset), with the offsets checked against a real `offsetof(...)` compile against each event's real struct (`XClientMessageEvent`, `XKeyEvent`, `XButtonEvent`, `XMotionEvent`, `XConfigureEvent`) rather than guessed:

```
fn event_type(buf: *u8) -> i32 { p: *i32 = buf; ret p.* }              # offset 0
fn event_window(buf: *u8) -> u64 { p: *u64 = buf + 32; ret p.* }        # offset 32
fn event_client_message_type(buf: *u8) -> u64 { p: *u64 = buf + 40; ret p.* }  # offset 40
fn event_client_data_l0(buf: *u8) -> i64 { p: *i64 = buf + 56; ret p.* }       # offset 56
fn event_keycode(buf: *u8) -> u32 { p: *u32 = buf + 84; ret p.* }              # offset 84
fn event_button(buf: *u8) -> u32 { p: *u32 = buf + 84; ret p.* }               # offset 84 (same union slot as keycode)
fn event_x(buf: *u8) -> i32 { p: *i32 = buf + 64; ret p.* }                    # offset 64
fn event_y(buf: *u8) -> i32 { p: *i32 = buf + 68; ret p.* }                    # offset 68
fn event_width(buf: *u8) -> i32 { p: *i32 = buf + 56; ret p.* }                # offset 56 (ConfigureNotify)
fn event_height(buf: *u8) -> i32 { p: *i32 = buf + 60; ret p.* }               # offset 60 (ConfigureNotify)
```

Close detection uses the standard `WM_PROTOCOLS`/`WM_DELETE_WINDOW` handshake — a window manager sends a `ClientMessage` on a close-button click rather than the connection just dying, so a well-behaved window has to register for it and watch for it in the event loop, the same way a C/Xlib program would. `tests/x11/window_close` verifies this by sending itself that exact `ClientMessage` via `XSendEvent` (a real round trip through the X server, not a mocked event) and confirming the event loop reads it correctly and exits — since the test's virtual display has no window manager to click a close button in the first place. That test only runs when `Xvfb` and `libX11` are available; `tests/run.sh` skips it with a message otherwise rather than failing the whole suite on machines without a virtual display set up.

Resize (`ConfigureNotify`) and keyboard/mouse (`KeyPress`/`KeyRelease`/`ButtonPress`/`ButtonRelease`/`MotionNotify`) events use the same raw-buffer approach — `INPUT_MASK` is the `XSelectInput` mask covering all of them, and `XKeycodeToKeysym(display, keycode, 0)` turns a `KeyPress`/`KeyRelease` event's raw hardware keycode into a portable `KeySym` (an X11 concept — the actual `Key`-enum mapping from that `KeySym` lives in `std/graphics`, layer 2, not here; this layer stays thin and dumb). `tests/x11/input_and_resize` verifies the byte-offset decoding itself (not `XKeycodeToKeysym`, whose result depends on the running system's keyboard layout and isn't something a portable test can pin down) the same way `window_close` does: synthesize each event kind with `XSendEvent`, known field values by hand, and confirm the readers pull back exactly what was set.

Plain-old-data types (`Vector2`, `Vector3`, `Color`, `Rectangle`) are ordinary Perzephxne `struct` declarations, field-for-field, since Perzephxne structs already follow C ABI layout — this part of the original raylib-binding sketch carries over unchanged, since it's just describing data, not an API:

```
struct Vector2   { x: f32, y: f32 }
struct Vector3   { x: f32, y: f32, z: f32 }
struct Color     { r: u8, g: u8, b: u8, a: u8 }
struct Rectangle { x: f32, y: f32, width: f32, height: f32 }
```

### The backend contract — implemented (`std/graphics/backend`)

`std/graphics/backend` defines one `Backend` struct: four function-pointer fields, populated once by each backend module and then called through uniformly, without the caller needing to know which backend produced the value:

```
struct Backend {
    init:     fn(*u8, u64) -> *u8,   # (display, window) -> opaque ctx, or null on failure
    clear:    fn(*u8, f32, f32, f32, f32),  # (ctx, r, g, b, a)
    present:  fn(*u8),                # (ctx)
    shutdown: fn(*u8),                # (ctx)
}
```

`init` returns an opaque, backend-owned context handle; every other field takes that handle back, so backend-specific setup (GLX visual selection, a future Vulkan surface/device, ...) happens entirely inside `init` and is never exposed above it — this is the seam that lets a Vulkan/Metal/D3D12 backend satisfy the same contract later without `std/graphics`, `gdev`, or `guix` changing at all, which is the whole reason this chapter insists on designing the contract before writing more GL code against it.

Deliberately out of scope for now: `submit`/`upload_texture`/`bind_shader` from the original design sketch aren't struct fields yet — a real implementation needs the runtime GL-function loader (prerequisite #2, still not built), and a field pointing at nothing callable would be worse than not having the field. Adding them once they mean something is an ordinary, non-breaking struct extension this early in the language's life.

`init`'s `(display: *u8, window: u64)` shape mirrors `std/graphics/window`'s current X11-only handles — it'll need revisiting the day a second windowing backend (Wayland, Win32, ...) exists behind a portable window handle instead, since neither of those concepts is Vulkan/Win32-shaped either.

### `std/graphics/gl` — partially implemented

GLX context creation plus a handful of core GL 1.1 entry points, linked via `[build].link = ["X11", "GL"]`. Real `extern fn` bindings, not a sketch — see `compiler/std/graphics/gl.przp` and the regression test `tests/gl/clear_and_read_pixel`:

```
extern fn glXChooseVisual(display: *u8, screen: i32, attribs: *i32) -> *u8
extern fn glXCreateContext(display: *u8, vis: *u8, share: *u8, direct: i32) -> *u8
extern fn glXMakeCurrent(display: *u8, drawable: u64, ctx: *u8) -> i32
extern fn glXSwapBuffers(display: *u8, drawable: u64)
extern fn glXDestroyContext(display: *u8, ctx: *u8)

extern fn glClearColor(r: f32, g: f32, b: f32, a: f32)
extern fn glClear(mask: u32)
extern fn glGetError() -> u32
extern fn glReadPixels(x: i32, y: i32, w: i32, h: i32, format: u32, ty: u32, data: *u8)
```

`choose_visual`/`create_context`/`clear`/`swap`/`destroy_context` wrap these into the same shape `window.przp` uses for X11 — a thin, dumb layer with no safety wrapping, there for anyone who wants to drop below the contract. One notable finding from building this: `create_context`'s `glXMakeCurrent` is called against the window `window.przp`'s `XCreateSimpleWindow` returns — which carries the *default* X visual, not necessarily the one `glXChooseVisual` picked. A stricter GLX implementation might reject that mismatch (requiring the heavier `XCreateWindow` attributes-struct API and a matching colormap instead), but Mesa's GLX accepted it in testing under Xvfb, so the existing simple window-creation path is reused as-is rather than adding that complexity preemptively. If a target GLX implementation ever rejects the mismatch, that's the fallback.

On top of those raw calls, `gl.przp` also builds `GL_BACKEND: backend.Backend` — the module's actual conformance to the contract above, wiring `gl_backend_init`/`gl_backend_clear`/`gl_backend_present`/`gl_backend_shutdown` into a `Backend` value. `gl_backend_init` bundles the GLX context together with the display/window it was created against into one internal `GLBackendCtx`, so every other field only ever needs the opaque handle `init` returns — no X11 type leaks into a caller going through `GL_BACKEND` rather than the raw functions:

```
import(win = "std/graphics/window")
import(gl  = "std/graphics/gl")

# ... open display/window via win.* as usual ...
ctx: *u8 = gl.GL_BACKEND.init(display, window)
gl.GL_BACKEND.clear(ctx, 0.1, 0.2, 0.3, 1.0)
# ... draw ...
gl.GL_BACKEND.present(ctx)
gl.GL_BACKEND.shutdown(ctx)
```

`tests/gl/backend_contract` verifies this path end to end (clear through `GL_BACKEND`, then read the pixel back), alongside `tests/gl/clear_and_read_pixel` for the raw functions. Getting an arbitrary mesh rasterized via vertex buffers (`glDrawArrays`/`glDrawElements`) and anything past GL 1.1 (shaders, VBOs) both still need the runtime `glXGetProcAddress` loader from prerequisite #2, which doesn't exist yet — so `Backend.clear`/`present` are real today, and there's still no generic `submit` to call. What *is* built without that loader: `gl.przp` also has `set_ortho_2d`/`draw_rect`/`draw_circle`/`draw_line`, using GL 1.1's legacy immediate-mode calls (`glBegin`/`glVertex2f`/`glColor4f`/`glOrtho`) — real, exported, link-time symbols on every desktop GL implementation (including Mesa's compat profile), needing none of the runtime loading GL 2.0+ would:

```
extern fn glMatrixMode(mode: u32)
extern fn glLoadIdentity()
extern fn glOrtho(left: f64, right: f64, bottom: f64, top: f64, near: f64, far: f64)
extern fn glBegin(mode: u32)
extern fn glEnd()
extern fn glVertex2f(x: f32, y: f32)
extern fn glColor4f(r: f32, g: f32, b: f32, a: f32)

fn set_ortho_2d(w: i32, h: i32)   # top-left-origin, Y-down pixel space sized w x h
fn draw_rect(x: f32, y: f32, w: f32, h: f32, r: f32, g: f32, b: f32, a: f32)
fn draw_circle(cx: f32, cy: f32, radius: f32, r: f32, g: f32, b: f32, a: f32)  # CIRCLE_SEGMENTS-sided polygon
fn draw_line(x1: f32, y1: f32, x2: f32, y2: f32, r: f32, g: f32, b: f32, a: f32)
```

These call directly through GL, not through `Backend` — `Backend` has no submit-geometry field yet (see its scope note above), so `std/graphics` (layer 2, below) calls `gl.przp`'s draw functions directly rather than through the contract for now. That's a deliberate, temporary coupling: there's only one backend today, so there's nothing to be backend-agnostic about for drawing specifically yet; it goes away once `Backend` grows a real `submit` entry point and a second backend exists to justify one. `tests/gl/graphics_layer2` verifies these end to end (rectangle, circle, and untouched background all sampled back at the right pixels).

### Layer 2 — `std/graphics`, the shared foundation

`std/graphics` turns the backend contract and windowing layer into something that reads like the rest of the standard library and, deliberately, like raylib's call-level ergonomics: `snake_case` names, `enum` constants instead of bare integers, tuples instead of output parameters, and `!T` for anything that can fail. This is the layer both `gdev` and `guix` import — window lifecycle, input, and 2D drawing primitives live here exactly once, not duplicated in each consumer. **A first slice is implemented**: window lifecycle, keyboard/mouse input queries, and the 2D draw primitives below (`Texture`/`draw_texture`/`draw_text` are still sketch — no image codec or text layout exists yet, see prerequisites #4/#5). See `compiler/std/graphics.przp` and `tests/gl/graphics_layer2`:

```
struct Color { r: u8, g: u8, b: u8, a: u8 }
RAYWHITE: Color : Color{.r=245, .g=245, .b=245, .a=255}   # + BLACK, WHITE, RED, GREEN, BLUE, YELLOW

enum Key => i32 { Space = 0, Enter = 1, Escape = 2, Up = 3, Down = 4, Left = 5, Right = 6, A = 7, /* ...Z = 32 */ }
enum MouseButton => i32 { Left = 0, Right = 1, Middle = 2 }

struct Window { w: i32, h: i32 }
impl Window {
    fn open(title: str, w: i32, h: i32) -> Window   # opens the X11 window, GL context, and Backend in one call
    fn should_close(self) -> bool
    fn close(self)
}

fn is_key_down(k: Key) -> bool
fn is_mouse_button_down(b: MouseButton) -> bool
fn mouse_position() -> (f32, f32)

fn begin_drawing()
fn end_drawing()               # presents the frame, then polls input for the next one
fn clear_background(c: Color)
fn draw_rectangle(x: f32, y: f32, w: f32, h: f32, c: Color)
fn draw_circle(x: f32, y: f32, radius: f32, c: Color)
fn draw_line(x1: f32, y1: f32, x2: f32, y2: f32, c: Color)
```

Single-window model, deliberately (like raylib): `Window.open` sets one module-level "current window" state; every other function operates on it implicitly, with no handle threaded through every call. Input is sampled once per frame inside `end_drawing` (see `poll_events` in `compiler/std/graphics.przp`) — key/mouse state read anywhere in a frame reflects exactly what was true when that frame's events were drained, not a live read racing the event queue. A `KeyPress`/`KeyRelease` event's raw X11 `KeySym` is mapped to a `Key` by `keysym_to_key`, tested directly against hardcoded `KeySym` constants from `X11/keysymdef.h` (`tests/gl/graphics_layer2`) rather than through a real key event, since the actual `KeySym` a keycode produces depends on the test machine's keyboard layout.

A full loop, in the shape `gdev`'s own loop (see its section below) will look identical to once it exists:

```
import(gfx = "std/graphics")

fn main() -> i32 {
    win: gfx.Window = gfx.Window.open("demo", 800, 450)
    pos: (f32, f32) = (400.0, 225.0)

    while !win.should_close() {
        if gfx.is_key_down(gfx.Key.Right) { pos.0 += 5.0 }
        gfx.begin_drawing()
        gfx.clear_background(gfx.RAYWHITE)
        gfx.draw_circle(pos.0, pos.1, 20.0, gfx.RED)
        gfx.end_drawing()
    }
    win.close()
    ret 0
}
```

Still a sketch — no image codec (prerequisite #4) or text layout (prerequisite #5) exists yet:

```
struct Texture { id: u32, w: i32, h: i32 }
impl Texture {
    fn load(path: str) -> !Texture {
        # decode (PNG to start), upload via backend_upload_texture, wrap the id
        ret gfx_load_texture(path)
    }
    fn unload(self: Texture) { gfx_delete_texture(self.id) }
}

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

The compiler defects this chapter has surfaced have all since been fixed and regression-tested — they're recorded here because they were found while building this design, not because they're still blocking it:

- **`!StructName` failable returns.** `fn make() -> !Point { ret @ok(Point{...}) }` now works for any struct, tagged union, or array — see `tests/run/failable_struct.przp`.
- **Struct-by-value FFI ABI.** An `extern fn` reached via `import()` that takes or returns a plain struct by value (e.g. `Vector2 { f32, f32 }`) now follows the x86-64 System V ABI, verified by linking against independently-compiled C code — see [Functions § Struct-by-Value Parameters and Returns](./functions.md#struct-by-value-parameters-and-returns) and `tests/ffi/struct_abi`. The one scoped limitation: this only works when the `extern fn` is declared in a module reached through `import()` — a struct-by-value `extern fn` declared directly in a file with no import fails to compile with a clear diagnostic rather than silently miscompiling.
- **Linker flags / native library dependencies.** `[build].link` in `przp.toml` now declares system libraries to link against (`link = ["X11", "GL"]` → `-lX11 -lGL` on the final link step, for both `przp build` and `przp run`) — see [Build System](./build-system.md) and `tests/project/link_libs`. This is not a package resolver (`[deps]` is still reserved and unimplemented, see [Status & Next Work](./status-next.md)) — it only covers linking against libraries the OS already ships, which is exactly what the windowing (`libX11`) and GPU (`libGL`) layers below need.
- **Global constant initializers ignored struct/array literals and function references.** Found while wiring `std/graphics/gl`'s `GL_BACKEND` constant: a global declared with a struct-literal initializer (`Backend{...}`) compiled silently but always came back zero-initialized — codegen's constant emitter didn't recognize the expression kind and fell back to `zeroinitializer` with no diagnostic. Fixed: global initializers now support struct/array literals (recursively) and bare function references as compile-time constants, and anything genuinely non-constant is a compile error instead of silent zeroing — see [Global Variables § Immutable Globals](./globals.md#immutable-globals-constants) and `tests/run/global_const_struct`.
- **`p.* = <struct or array literal>` stored the wrong value.** Also found while wiring `GL_BACKEND`'s `init`: assigning a struct or array literal through a pointer dereference stored the literal's alloca address itself rather than its bytes, corrupting the target in a way that happened to segfault reliably rather than silently. Fixed — see [Pointers & Memory § Allocation](./pointers.md#allocation) and `tests/run/derefassign_struct_array`.
- **A module importing another module's types in a global initializer broke under re-import.** `std/graphics/gl` importing `std/graphics/backend` and using `backend.Backend` in its own global (`GL_BACKEND: backend.Backend : backend.Backend{...}`) compiled fine on its own, but failed once something else imported `gl` itself — the re-mangling pass that rewrites a module's own names when it's imported under a new alias rewrote a global's declared type but not its initializer expression, so the two ended up with mismatched (single- vs. double-mangled) type names. Fixed in the import-mangling pass — this is what makes `std/graphics/gl` importing `std/graphics/backend` (the first time one `std/graphics` module has imported a sibling module) actually work.
- **A tuple literal referencing an imported symbol never got its reference re-mangled.** Found while wiring `std/graphics`'s `mouse_position() -> (f32, f32) { ret (STATE.mouse_x, STATE.mouse_y) }`: the import-mangling pass that rewrites a module's own bare identifiers when it's spliced under an alias (so `STATE` becomes `gfx__STATE` wherever the module refers to itself) walks every expression kind recursively — except tuple literals had no case at all, so any identifier reference inside one was silently skipped, leaving a now-nonexistent bare name and an "undefined identifier" error. Fixed by giving `EXPR_TUPLE` the same handling `EXPR_ARRAY_LIT` already had (it reuses the same underlying field) in both of `main.c`'s identifier-rewrite passes.

This means the FFI mechanics and linking layer 1 depends on (struct-by-value calls, failable returns for loaders, linking against system libraries) are no longer blocked on compiler/toolchain work — the remaining prerequisites below are about what actually has to be written (a renderer, a windowing backend), not missing tooling.

## Open engineering prerequisites

| # | Prerequisite | Status |
|---|---|---|
| 1 | **Backend contract design.** The fixed interface `std/graphics` calls through needs to be designed and settled *before* the GL backend is written against it — this is what makes future Vulkan/Metal/D3D12 backends additive instead of a rewrite. **Done, at today's scope**: `std/graphics/backend`'s `Backend` struct fixes `init`/`clear`/`present`/`shutdown`; `submit`/`upload_texture`/`bind_shader` are deliberately not fields yet (see `std/graphics/backend` above) until prerequisite #2 makes a real implementation possible. |
| 2 | **GL function loading.** Nothing beyond OpenGL 1.1 is available as a link-time symbol — every modern GL entry point (shaders, buffers, textures beyond the basics) has to be resolved at runtime via `glXGetProcAddress` and called through a function pointer. This needs a small runtime loader written once, not per-project. **Not started** — `std/graphics/gl` so far only uses real link-time GL 1.1 symbols (`glClear`, `glClearColor`, `glGetError`, `glReadPixels`), which don't need this loader. |
| 3 | **Windowing backend.** X11 first (works everywhere on Linux, including under Wayland via XWayland); a native Wayland backend, then Win32/Cocoa, come later behind the same `std/graphics/window` surface so `std/graphics`, `gdev`, and `guix` never have to change per platform. **Done, for X11**: open/title/close (including the `WM_DELETE_WINDOW` handshake), resize reporting, and keyboard/mouse input events are all implemented and tested. Still open: a second (Wayland/Win32/Cocoa) backend, which is intentionally not started until a real cross-platform need shows up. |
| 4 | **Image and audio codecs.** No bundled renderer means no bundled codecs either — PNG/JPEG decoding (for `guix` icons and `gdev` textures alike) and audio file loading (for `gdev`) need their own (likely also hand-written or minimally-bound) implementations, deferred until the 2D primitives above them are solid. |
| 5 | **Text layout.** `guix` needs real text shaping/layout (cursor positioning, line wrapping, at minimum) to be a credible native-app toolkit; `gdev` only needs simple bitmap-font text draw calls. This is a `guix`-specific investment, not a `std/graphics` one. |
| 6 | **Variadic/macro helpers.** Small conveniences like a `printf`-style text-formatting helper for on-screen debug text are built on `@fmt` rather than needing any new compiler feature — a non-issue, listed for completeness. |
| 7 | **Threading model.** GPU contexts are tied to the thread that created them; the wrapper should document a single-threaded main loop (window, input, and drawing calls all from one thread) as the initial supported model rather than attempt general multi-threaded rendering from the start. |

## Suggested build order

1. Design the backend contract (prerequisite #1) before writing any GL code — settle the smallest set of operations `std/graphics` needs so the GL backend is written *against* a fixed interface, not the interface being reverse-engineered out of GL calls after the fact. **Done, at today's scope**: see `std/graphics/backend` above.
2. Write the windowing backend (`std/graphics/window`, X11) for the smallest usable slice: open a window, pump events, report close/resize. **Done**: open/title/close, resize, and keyboard/mouse input events are all implemented and tested.
3. Write `std/graphics/gl` as the first implementation of the backend contract, and get a single triangle or cleared background on screen — this is the point where "a window exists" becomes "a frame can be drawn." **Done, for 2D**: `GL_BACKEND`'s `init`/`clear`/`present`/`shutdown` conform to the contract, and `set_ortho_2d`/`draw_rect`/`draw_circle`/`draw_line` get real filled shapes on screen via GL 1.1 immediate mode (verified via pixel readback, see `tests/gl/graphics_layer2`). Rasterizing an arbitrary mesh via vertex buffers, and anything past GL 1.1 (shaders, VBOs), still needs the loader (prerequisite #2).
4. Write layer 2 (`std/graphics`) over that same small surface: 2D shapes, keyboard/mouse input, then texture loading once an image codec exists. Resist building out the full raylib-equivalent surface up front — this is also where the "simple by default" goal gets tested for real: a first-time caller should get a circle on screen in a handful of raylib-shaped lines, with 3D, shaders, and custom batching all staying opt-in layers underneath, not something the 2D path has to route around. **Done for 2D shapes and input**: `Window` lifecycle, `is_key_down`/`is_mouse_button_down`/`mouse_position`, and `draw_rectangle`/`draw_circle`/`draw_line` are all real (see the layer 2 section above). Texture loading is still open, blocked on prerequisite #4 (no image codec yet).
5. Write a regression test per wrapped function group (window, shapes, input, textures), following the existing `tests/run/std_*.przp` convention, to the extent a windowing/GPU-dependent test can run headlessly in CI. **Done for window/shapes/input**: `tests/x11/window_close`, `tests/x11/input_and_resize`, `tests/gl/backend_contract`, `tests/gl/clear_and_read_pixel`, `tests/gl/graphics_layer2`. Textures still need their test once loading exists.
6. Start `gdev` once `std/graphics`'s 2D primitives, input, and texture loading are solid, and finish its 2D surface (sprites, 2D camera/scrolling, basic audio) before touching 3D or starting `guix`. `gdev` is the priority once layer 1 is done — `guix` is deliberately sequenced after, not developed in lockstep with it, so effort doesn't split across both toolkits while neither is finished.
7. `gdev`: only once the 2D surface above is solid, extend to 3D (`Camera3D`-equivalent, cube/grid primitives).
8. `guix`: start once `gdev`'s 2D surface is solid (does not need to wait for `gdev`'s 3D work). Extend to DPI scaling, text editing/cursor handling, focus/tab order, and clipboard. Prioritize mouse-interaction latency here specifically, since that's the axis this design most wants to win on: input state should be sampled once per frame with nothing between the OS event and code reading it.
