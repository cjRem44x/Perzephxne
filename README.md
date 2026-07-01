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
    io.println("Hello, World!")
    @pf("pi ≈ {math.PI}\n")
}
```

### Control Flow

```
# if / elif / else (both "elif" and "else if" are accepted)
score: i32 = 85
if score >= 90 {
    @pf("A\n")
} elif score >= 80 {
    @pf("B\n")
} else if score >= 70 {  # "else if" also works
    @pf("C\n")
} else {
    @pf("F\n")
}

# Boolean operators: 'and'/'or'/'not' or C-style '&&'/'||'/'!'
if score >= 70 && score < 90 { @pf("passing\n") }
if score < 60 || score > 100 { @pf("invalid\n") }
if !false { @pf("ok\n") }

# range for
for i => 0..10 {
    if i % 2 == 0 { @pf("{i} is even\n") }
}

# for-each over array or slice
items: [3]str = ["a", "b", "c"]
for item => items { @pf("{item}\n") }

# for-each over str iterates bytes as char
for ch => "hello" { @pf("{ch}") }

# while loop
i: i32 = 0
while i < 5 {
    @pf("{i} ")
    i = i + 1
}

# break / continue
for j => 0..10 {
    if j == 3 { continue }
    if j == 7 { break }
    @pf("{j} ")
}

# logical not: both '!' and 'not' are accepted
if !false { @pf("ok\n") }
if not false { @pf("ok\n") }

# defer: run statement when the enclosing function returns
fn open_and_read() {
    p: *i32 = @alo(i32)
    defer @free(p)   # runs on every return path
    p.* = 42
}
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
    fn new(v: T) -> Box[T] { ret Box[T]{.value = v} }  # static constructor
    fn get(self: *Box[T]) -> T { ret self.value }
    fn set(self: *Box[T], v: T) { self.value = v }
}

b: Box[i32] = Box[i32].new(42)   # Generic[T].method() calls a static method
@pf("value = {b.get()}\n")
b.set(99)
@pf("value = {b.get()}\n")
```

### Enums

```
enum Direction { North, South, East, West }
enum Status { Ok, Err = 1, Timeout = 2 }

d: Direction = Direction.North

when d {
    Direction.North => @pf("north\n")
    Direction.South => @pf("south\n")
    Direction.East  => @pf("east\n")
    Direction.West  => @pf("west\n")
}

if d == Direction.North { @pf("heading north\n") }
```

### Unions

Plain (untagged) unions share memory across all fields:

```
unn Data { i: i32, f: f32, b: bool }

d: Data = Data{.i = 42}
d.f = 1.5    # reinterprets the same memory
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
b: str  = @str(true)          # bool → "true" or "false"
f: f32  = @f32(3.14)
bits: u32 = @bitcast(u32, f)  # raw bit reinterpret
```

### Memory

```
# Stack
x: i32  = 12
p: *i32 = &x           # pointer to stack variable
p.* = 99               # write through pointer
val: i32 = p.*         # read through pointer

# Heap
heap: *i32 = @alo(i32)        # allocate one i32
heap.* = 42
@free(heap)

arr: *i32 = @alo(i32, 10)     # allocate array of 10 i32
arr = @realo(arr, i32, 20)    # grow to 20 elements
@free(arr)

# Smart (reference-counted) pointer
p: ^Node = @new(Node{.val = 42})
q: ^Node = @clone(p)   # increment RC, shared ownership
@release(p)            # decrement RC; frees when RC hits 0
@release(q)
```

### Bitwise and Numeric Builtins

```
x: i32 = 0xFF
x &= 0x0F              # compound bitwise AND
x |= 0x30              # OR, ^= XOR, <<= SHL, >>= SHR

@clz(1u32)             # count leading zeros → 31
@ctz(8u32)             # count trailing zeros → 3
@popcount(255u32)      # population count → 8
@bswap(0x01020304u32)  # byte-swap → 0x04030201

r: !i32 = @checked_add(a, b)  # overflow-checked arithmetic
```

### Generics with Heap Allocation

```
struct Vec[T] {
    data: *T,
    len: usize,
    cap: usize,
}

impl Vec[T] {
    fn init(self: *Vec[T], cap: usize) {
        self.data = @alo(T, cap)   # T resolves to concrete type
        self.len = 0
        self.cap = cap
    }
    fn push(self: *Vec[T], val: T) {
        self.data[self.len] = val
        self.len = self.len + 1
    }
}

v: Vec[i32] = Vec[i32]{.data = null, .len = 0, .cap = 0}
v.init(8)
v.push(42)
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
| `przp sac <files> -o=Name` | compile one or more files without a project |

---

## Standard Library

| Module | Description |
|---|---|
| `std/io` | print, file I/O, stdin |
| `std/str` | string operations, split/trim/contains/replace |
| `std/math` | sqrt, trig, pow, log, floor/ceil, constants (PI, E, …) |
| `std/os` | env, paths, exit, process execution |
| `std/file` | file read/write, append, exists, delete, size |
| `std/fmt` | string formatting and padding utilities |
| `std/collections` | dynamic array (Vec) |
| `std/atomic` | atomic load/store/add/sub/cas/inc/dec on i64 |
| `std/sync` | mutex and condition-variable primitives |
| `std/crypto` | djb2, fnv1a, sha256 hashing; xor_encrypt |

---

## Documentation

The full language reference lives in [`book/`](book/). Build it with [mdBook](https://rust-lang.github.io/mdBook/):

```sh
mdbook serve book/
```

---

## Status

Early development. The compiler (`compiler/przp`) targets x86-64 Linux. Core language features are implemented; the standard library is minimal and growing.
