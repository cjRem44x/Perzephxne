# Graphics — Design Sketch (Unimplemented)

> **Nothing in this chapter exists yet.** `std/graphics` is not built, not shipped, and not tested. This is an architecture sketch — written to guide implementation — for a future media library in the spirit of [raylib](https://www.raylib.com/): simple enough for a weekend game, capable enough for a native GUI app. Every other chapter in this book documents the compiler as it is; this one documents a plan.

## Goals

- **Fast.** A thin binding layer with no hidden allocation or indirection in the hot path — draw calls should compile down to near-C-speed FFI calls.
- **Versatile.** The same primitives should support 2D and 3D games, and — layered with a small immediate-mode widget set — native GUI applications, editor tools, and visualizers.
- **Idiomatic.** `snake_case` functions, `struct` + `impl` for resources, enums for key/button constants, tuples for multi-value returns (`get_mouse_position() -> (f32, f32)`), and `!T` failable returns for anything that can fail to load (a missing texture file should be a caught error, not a null-pointer crash).

## Why bind an existing library instead of writing a renderer from scratch

Perzephxne has no windowing, no GPU access, and no image/audio codecs of its own — building all of that from zero is a multi-year project, not a stdlib module. raylib already solves it: a flat, dependency-light C API with no macros, no templates, and no OOP — which happens to be an unusually good match for Perzephxne's `extern fn` FFI, since there's no C++ name mangling or preprocessor magic to work around. The plan is to **bind raylib**, then build idiomatic Perzephxne conventions on top of the binding.

## Layered architecture

```
┌─────────────────────────────────────────────┐
│ std/gui        immediate-mode widgets        │  ← pure Perzephxne, built on std/graphics
│                (button, slider, textbox)     │
├─────────────────────────────────────────────┤
│ std/graphics   idiomatic wrapper             │  ← snake_case fns, impl methods, enums,
│                                               │     failable loads, tuple returns
├─────────────────────────────────────────────┤
│ std/graphics/raylib   raw binding layer      │  ← extern fn declarations, 1:1 with the
│                                               │     C API, plus POD struct definitions
├─────────────────────────────────────────────┤
│ libraylib.so / .a    (C, not shipped)        │  ← the actual renderer, window, audio mixer
└─────────────────────────────────────────────┘
```

### Layer 1 — the binding layer

Most of raylib's public types are **plain-old-data structs** (not opaque handles) — `Vector2`, `Vector3`, `Color`, `Rectangle`, `Camera2D`, `Camera3D`, and even `Texture2D` are just fields, no internal state hidden behind a pointer. These map directly onto ordinary Perzephxne `struct` declarations, field-for-field, since Perzephxne structs already follow C ABI layout:

```
struct Vector2  { x: f32, y: f32 }
struct Vector3  { x: f32, y: f32, z: f32 }
struct Color    { r: u8, g: u8, b: u8, a: u8 }
struct Rectangle { x: f32, y: f32, width: f32, height: f32 }

struct Texture2D {
    id:      u32,
    width:   i32,
    height:  i32,
    mipmaps: i32,
    format:  i32,
}
```

Functions are declared with `extern fn`, matching raylib's C signatures one-to-one:

```
extern fn InitWindow(width: i32, height: i32, title: *u8)
extern fn WindowShouldClose() -> bool
extern fn CloseWindow()
extern fn BeginDrawing()
extern fn EndDrawing()
extern fn ClearBackground(color: Color)
extern fn DrawRectangle(x: i32, y: i32, w: i32, h: i32, color: Color)
extern fn DrawCircleV(center: Vector2, radius: f32, color: Color)
extern fn GetMousePosition() -> Vector2
extern fn LoadTexture(path: *u8) -> Texture2D
extern fn IsKeyPressed(key: i32) -> bool
```

This layer is deliberately dumb — it exists so the idiomatic layer above it has something honest to build on, and so advanced users can always drop to the raw C names when the wrapper doesn't cover something yet.

### Layer 2 — the idiomatic wrapper

`std/graphics` turns the raw bindings into something that reads like the rest of the standard library: `snake_case` names, `enum` constants instead of bare integers, tuples instead of output parameters, and `!T` for anything that can fail.

```
enum Key {
    Space, Enter, Escape, Up, Down, Left, Right,
    A, B, C, # ... rest of the alphabet
}

enum MouseButton { Left, Right, Middle }

struct Window { title: str, w: i32, h: i32 }

impl Window {
    fn open(title: str, w: i32, h: i32) -> Window {
        InitWindow(w, h, title.data)
        SetTargetFPS(60)
        ret Window{.title=title, .w=w, .h=h}
    }
    fn should_close(self: Window) -> bool { ret WindowShouldClose() }
    fn close(self: Window) { CloseWindow() }
}

fn is_key_pressed(k: Key) -> bool { ret IsKeyPressed(key_code(k)) }
fn mouse_position() -> (f32, f32) {
    v: Vector2 = GetMousePosition()
    ret (v.x, v.y)
}

struct Texture { native: Texture2D }
impl Texture {
    fn load(path: str) -> !Texture {
        t: Texture2D = LoadTexture(path.data)
        if t.id == 0 { ret @err(1) }          # raylib sets id=0 on failed load
        ret @ok(Texture{.native=t})
    }
    fn unload(self: Texture) { UnloadTexture(self.native) }
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

Because it's built entirely from `std/graphics` primitives (rectangles, text, mouse position, click state), it needs no compiler support beyond what a game already needs — the "GUI toolkit" is just a library, same as the "game engine" is.

## Open engineering prerequisites

These aren't polish items — they're things that must be resolved before layer 1 can be written for real. Found by testing directly against the current compiler while drafting this chapter (none of this is raylib-specific; both are general compiler defects surfaced by designing around them):

| # | Prerequisite | Status |
|---|---|---|
| 1 | **`!StructName` failable returns are broken for every struct, no FFI involved.** Tested: `fn make() -> !Point { ret @ok(Point{...}) }` fails to compile — codegen builds the `{ StructTy, i32 }` failable wrapper via `insertvalue`, but passes the struct's alloca *pointer* where the raw aggregate *value* is expected (`'%tN' defined with type 'ptr' but expected '%Point = type {...}'`). This is pure Perzephxne, unrelated to graphics — it blocks the `Texture.load(path) -> !Texture` pattern shown above, and blocks the same pattern for any fallible constructor returning a struct. This is the cheaper of the two fixes and has value independent of this library — worth fixing first, before any raylib work starts. |
| 2 | **Struct-by-value FFI ABI.** raylib passes and returns small structs by value constantly (`Vector2 GetMousePosition()`, `DrawCircleV(Vector2, ...)`). Tested empirically: a Perzephxne `extern fn` that takes/returns a `struct Vector2 { x: f32, y: f32 }` by value currently emits a raw LLVM aggregate call without System V ABI classification. Linked against real C code, the fields come back wrong (verified: `a + b` on two `Vector2` values returned incorrect results through the FFI boundary). This needs either (a) fixing codegen to classify small aggregates into the correct registers per the x86-64 SysV ABI for calls to `extern fn`, or (b) generating thin C shim functions with pointer-based signatures for every raylib entry point that takes/returns a struct by value, and binding to the shims instead of raylib directly. |
| 3 | **Linker flags / native library dependencies.** There is currently no way for a `przp.toml` project to declare "link against `-lraylib`" (or platform GL/window system libs). The `[deps]` manifest section is reserved but unimplemented (see [Status & Next Work](./status-next.md)). Binding any native library — not just raylib — needs this solved generally. |
| 4 | **Variadic/macro helpers.** A few raylib conveniences (`TextFormat`, which is a `printf`-style helper returning a static buffer) aren't representable as a clean `extern fn`. These get reimplemented on top of `@fmt` instead of bound directly. |
| 5 | **Callback-based APIs.** Functions like `SetTraceLogCallback` take a C function pointer. Perzephxne's bare function pointers (no closures) are sufficient here since raylib's own callbacks are plain C function pointers with no captured state — this is a non-issue, listed for completeness. |
| 6 | **Threading model.** raylib expects single-threaded use of its main loop (window, input, and drawing calls all from one thread). The wrapper should document this constraint rather than attempt to paper over it. |

## Suggested build order

1. Resolve prerequisite #1 (`!Struct` returns) — cheap, self-contained, no FFI needed to reproduce or fix, and useful to every other struct-returning failable function in the language, not just this library.
2. Resolve prerequisite #2 (struct-by-value FFI ABI) with a minimal repro test in the regression suite — this is the single riskiest item, since every layer above it depends on it working.
3. Resolve prerequisite #3 (linker flags) with the smallest workable manifest addition — even a flat `link = ["raylib"]` key under `[build]` would unblock this.
4. Write layer 1 (`std/graphics/raylib`) for a deliberately small surface first: window lifecycle, 2D shapes, keyboard/mouse input, and texture loading. Resist binding the entire raylib API up front.
5. Write layer 2 (`std/graphics`) over that same small surface, with a regression test per wrapped function group (window, shapes, input, textures) following the existing `tests/run/std_*.przp` convention.
6. Only after 2D is solid, extend to 3D (`Camera3D`, `DrawCube`, `DrawGrid`) and audio (`Sound`, `Music`).
7. Layer 3 (`std/gui`) can start as soon as layer 2 has rectangles, text, and mouse state — it doesn't need 3D or audio at all.
