# Builtins

Builtins are compiler-provided functions and constants. They are always in scope and begin with `@`.

## I/O

| Builtin | Signature | Description |
|---|---|---|
| `@pf(fmt, ...)` | `fn(str, ...) -> void` | print formatted to stdout |
| `@epf(fmt, ...)` | `fn(str, ...) -> void` | print formatted to stderr |
| `@fmt(fmt, ...)` | `fn(str, ...) -> str` | format to a heap-allocated string |
| `@cin(prompt?)` | `fn(str?) -> str` | read a line from stdin |

`@pf`, `@epf`, and `@fmt` all support `{expr}` interpolation inside the format string:

```
name: str = "World"
@pf("Hello, {name}!\n")
@pf("sum = {1 + 2}\n")

greeting: str = @fmt("Hello, {name}!")
```

Interpolated `bool` values are printed as C-style integers (`1` or `0`). Use
`@str(value)` when you need `"true"` or `"false"` text.

Use `{{` and `}}` to emit literal braces.

## Type Casts

`@T(val)` converts `val` to type `T`:

`@i8` `@i16` `@i32` `@i64` `@u8` `@u16` `@u32` `@u64` `@f32` `@f64` `@usize` `@bool` `@char` `@str`

```
x: i32 = 300
y: u8  = @u8(x)      # truncates to 44

a: f64 = 3.7
b: i32 = @i32(a)     # truncates to 3

s: str = @str(99)    # → "99"
c: char = @char(65)  # → 'A'
```

### String to Number

Casting a `str` to any numeric type parses the string. On failure (non-numeric input, empty string, or partial match like `"12abc"`) the plain form returns zero:

```
n: i32 = @i32("42")      # 42
z: i32 = @i32("hello")   # 0  — fallback to zero
```

Use the failable form to distinguish parse success from failure:

```
val, err: !i32 = @i32("42")
@pf("val=%d err=%d\n", val, err)   # val=42 err=0

bad, err2: !i32 = @i32("hello")
@pf("val=%d err=%d\n", bad, err2)  # val=0  err=1
```

### `@bool` from String

`@bool(s)` returns `true` if `s` is `"true"` or `"1"`, `false` for anything else.

```
b1: bool = @bool("true")   # true
b2: bool = @bool("1")      # true
b3: bool = @bool("yes")    # false
```

## Memory

| Builtin | Description |
|---|---|
| `@size(T)` | byte size of type `T` |
| `@align(T)` | alignment of type `T` |
| `@offsetof(T, field)` | byte offset of a struct field |
| `@bitcast(T, val)` | reinterpret bits — same size required |
| `@zeroed(T)` | zero value of type `T` |
| `@alo(T)` | heap-allocate one `T`, return `*T` (raw pointer, manual `@free`) |
| `@new(T)` | heap-allocate one `T`, return `^T` (RC-managed) |
| `@clone(val)` | increment RC of a `^T`, return shared `^T` |
| `@free(ptr)` | free raw `*T` heap memory |
| `@memcpy(dst, src, n)` | copy `n` bytes from src to dst |
| `@memmove(dst, src, n)` | copy `n` bytes, handles overlap |
| `@memset(dst, byte, n)` | fill `n` bytes with `byte` |
| `@addr(expr)` | address of a variable, field, or index expression — like `&` but usable in more contexts |
| `@str_raw(ptr, len)` | construct a `str` fat pointer from a raw `*u8` and a `usize` length |

```
f: f32    = 1.0
bits: u32 = @bitcast(u32, f)   # raw bit pattern — 0x3F800000
```

## Collections

| Builtin | Description |
|---|---|
| `@len(arr_or_slice)` | element count — compile time for arrays, runtime for slices |

## Math

| Builtin | Description |
|---|---|
| `@sqrt(x)` | square root |
| `@abs(x)` | absolute value |
| `@min(a, b)` | minimum |
| `@max(a, b)` | maximum |

## Bit Operations

| Builtin | Description |
|---|---|
| `@clz(x)` | count leading zeros |
| `@ctz(x)` | count trailing zeros |
| `@popcount(x)` | count set bits |
| `@bswap(x)` | reverse byte order |

## Diagnostics

| Builtin | Description |
|---|---|
| `@assert(cond, msg)` | panic if false (debug only) |
| `@panic(msg)` | unconditional panic with message |
| `@unreachable()` | mark code path as unreachable |
| `@todo()` | placeholder — panics at runtime |

## Compile-Time Info

| Builtin | Type | Description |
|---|---|---|
| `@debug` | `bool` | true in debug builds |
| `@release` | `bool` | true in release builds |
| `@os.linux` | `bool` | target is Linux |
| `@os.mac` | `bool` | target is macOS |
| `@os.windows` | `bool` | target is Windows |
| `@arch.x86_64` | `bool` | target is x86-64 |
| `@arch.arm64` | `bool` | target is AArch64 |
| `@arch.x86` | `bool` | target is 32-bit x86 |
| `@arch.arm` | `bool` | target is 32-bit ARM |
| `@typeof(expr)` | `str` | type name as a string constant |

## Process / Args

| Builtin | Signature | Description |
|---|---|---|
| `@args` | `-> []str` | command-line arguments |
| `@exit(code)` | `fn(i32)` | exit process immediately |

## Failable Values

Failable (`!T`) values carry a result and an error flag. These builtins inspect and extract them.

| Builtin | Signature | Description |
|---|---|---|
| `@ok(val)` | `fn(!T) -> T` | extract the value; panics if the error flag is set |
| `@unwrap(val)` | `fn(!T) -> T` | alias for `@ok` |
| `@err(val)` | `fn(!T) -> i32` | extract the error code (0 = success) |
| `@is_ok(val)` | `fn(!T) -> bool` | true if the error flag is not set |

```
result: !i32 = @i32("42")
if @is_ok(result) {
    @pf("parsed: %d\n", @ok(result))
} else {
    @pf("error code: %d\n", @err(result))
}
```

When you only need the error code, use failable destructuring instead:

```
val, err: !i32 = @i32("42")
if err != 0 { @pf("parse failed\n") }
else        { @pf("parsed: %d\n", val) }
```

Plain assignment silently extracts the value (error flag discarded):

```
n: i32 = @i32("42")   # flag ignored
```

## Checked Arithmetic

These return `!T` — use failable destructure to check for overflow:

| Builtin | Description |
|---|---|
| `@checked_add(a, b)` | addition — error on overflow |
| `@checked_sub(a, b)` | subtraction — error on overflow |
| `@checked_mul(a, b)` | multiplication — error on overflow |

```
val, err: !i32 = @checked_add(a, b)
if err != 0 { @panic("integer overflow") }
```

Plain assignment silently extracts the value (overflow still sets the flag, but it is discarded):

```
result: i32 = @checked_add(a, b)   # flag ignored
```

## Random Numbers

| Builtin | Signature | Description |
|---|---|---|
| `@rng(T, min, max)` | `fn(type, T, T) -> T` | random integer or float in `[min, max]` inclusive |
| `@rng_seed(n)` | `fn(i32)` | seed the RNG for a reproducible sequence |

`T` must be an integer or float type. Pass the type name as the first argument:

```
x: i32  = @rng(i32, 1, 6)       # roll a die
y: f64  = @rng(f64, 0.0, 1.0)   # uniform float
z: u64  = @rng(u64, 0, 100)     # unsigned range
```

The RNG is auto-seeded once at process start (from the clock and process ID), so `@rng` produces a different sequence on every run by default. Call `@rng_seed(n)` to pin the sequence for reproducible output — useful for tests and deterministic simulations:

```
@rng_seed(42)                    # same sequence every run
a: i32 = @rng(i32, 1, 100)
b: i32 = @rng(i32, 1, 100)
```

## Assembly

| Builtin | Description |
|---|---|
| `@asm(inst, constr, ...)` | inline assembly |
| `@asm_volatile(...)` | inline assembly with volatile semantics |
