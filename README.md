# Perzephxne

A compiled, statically typed systems language with a C-flavored syntax and an LLVM backend.

```
fn main() {
    @pf("Hello, World!\n")
}
```

**No garbage collector. No OOP. No header files.**  
Manual memory, explicit error handling, and a build tool that gets out of your way.

---

## Requirements

- `clang` on your `PATH` (LLVM 14+)

## Quick Start

```sh
# compile the compiler
cd compiler && make

# create a new project
./przp init hello
cd hello
../przp run
```

Or compile a single file without a project:

```sh
przp sac main.przp -o=hello
./hello
```

## Language Overview

### Variables

```
x: i32 = 42          # mutable
MAX: usize : 1000    # immutable constant
```

### Functions

```
fn add(a: i32, b: i32) -> i32 {
    ret a + b
}
```

### Failable Returns

Errors are values. `!T` is either a success value or an error code:

```
fn parse(s: str) -> !i32 {
    val, err: !i32 = @i32(s)
    if err != 0 { ret @err(1) }
    ret @ok(val)
}
```

### Imports

```
import(io = "std/io", math = "std/math")

fn main() {
    io.println("pi ≈ {math.PI}")
}
```

### Control Flow

```
for i => 0..10 {
    if i % 2 == 0 { @pf("{i} is even\n") }
}

items: [3]str = ["a", "b", "c"]
for item => items { @pf("{item}\n") }
```

### Structs

```
struct Vec2 { x: f64, y: f64 }

impl Vec2 {
    fn len(self: *Vec2) -> f64 {
        ret @sqrt(self.x * self.x + self.y * self.y)
    }
}

v: Vec2 = Vec2{.x = 3.0, .y = 4.0}
@pf("len = {v.len()}\n")
```

### Generic Structs

```
struct Box[T] { value: T }

impl Box[T] {
    fn get(self: *Box[T]) -> T { ret self.value }
    fn set(self: *Box[T], v: T) { self.value = v }
}

b: Box[i32] = Box[i32]{.value = 42}
@pf("value = {b.get()}\n")
```

### Unions

Plain (untagged) unions share memory across all fields:

```
unn Data { i: i32, f: f64 }

d: Data = Data{.i = 42}
d.f = 3.14    # reinterprets the same memory
```

Tagged unions carry a discriminant for safe pattern matching:

```
unn Shape => enum {
    circle: f64,
    rect: Vec2,
    point,
}

s: Shape = Shape{.circle = 5.0}

when s {
    .circle r => @pf("circle r={r}\n")
    .rect   v => @pf("rect {v.x}x{v.y}\n")
    .point    => @pf("point\n")
}
```

### Pointers and Heap Allocation

```
x: i32  = 12
p: *i32 = &x           # pointer to stack variable
p.* = 99               # write through pointer
val: i32 = p.*         # read through pointer

heap: *i32 = @alo(i32)     # allocate one i32 on the heap
heap.* = 42
@free(heap)

arr: *i32 = @alo(i32, 10)  # allocate array of 10 i32
arr[0] = 1
arr[9] = 99
@free(arr)
```

### Type Casts

```
n: i32  = @i32("42")          # str → int, 0 on failure
s: str  = @str(99)            # int → "99"
f: f32  = @f32(3.14)
bits: u32 = @bitcast(u32, f)  # raw bit reinterpret
```

### String Formatting

```
name: str = "world"
@pf("Hello, {name}!\n")

greeting: str = @fmt("Hello, {name}!")
```

---

## Project Structure

```
MyProject/
  przp.toml        # project manifest
  src/
    main.przp      # entry point
```

**`przp.toml`**

```toml
[package]
name    = "MyProject"
version = "0.1.0"
```

## Build System

| Command | Description |
|---|---|
| `przp init [name]` | create a new project |
| `przp build` | debug build |
| `przp build --release` | optimized release build |
| `przp run` | build and run |
| `przp sac <files> -o=Name` | compile files without a project |

---

## Standard Library

| Module | Description |
|---|---|
| `std/io` | print, file I/O, stdin |
| `std/str` | string operations |
| `std/math` | sqrt, trig, constants |
| `std/os` | env, paths, processes |

---

## Documentation

The full language reference lives in [`book/`](book/). Build it with [mdBook](https://rust-lang.github.io/mdBook/):

```sh
mdbook serve book/
```

---

## Status

Early development. The compiler (`compiler/przp`) targets x86-64 Linux. Core language features are implemented; the standard library is minimal and growing.
