# Builtins

Builtins are compiler-provided functions and constants. They are always in scope and begin with `@`.

## I/O

| Builtin | Signature | Description |
|---|---|---|
| `@pf(fmt, ...)` | `fn(str, ...) -> void` | print formatted to stdout |
| `@epf(fmt, ...)` | `fn(str, ...) -> void` | print formatted to stderr |
| `@pf` uses `{expr}` inside strings for interpolation | | |

```
name: str = "World"
@pf("Hello, {name}!\n")
@pf("sum = {1 + 2}\n")
```

## Type Casts

`@T(val)` where T is any primitive type:

`@i8` `@i16` `@i32` `@i64` `@u8` `@u16` `@u32` `@u64` `@f16` `@f32` `@f64` `@usize` `@bool` `@char` `@str`

```
x: i32 = 300
y: u8  = @u8(x)     # 44
```

## Memory

| Builtin | Description |
|---|---|
| `@sizeof(T)` | byte size of type `T` at compile time |
| `@alignof(T)` | alignment of type `T` at compile time |
| `@bitcast(T, val)` | reinterpret bits — same size required |
| `@ptrof(name)` | function pointer from name |
| `@addrof(name)` | address as `usize` |

## Collections

| Builtin | Description |
|---|---|
| `@len(arr_or_slice)` | length at compile time (arrays) or runtime (slices) |
| `@cap(slice)` | allocated capacity of a slice |

## Math

| Builtin | Description |
|---|---|
| `@sqrt(x)` | square root |
| `@abs(x)` | absolute value |
| `@min(a, b)` | minimum |
| `@max(a, b)` | maximum |
| `@clamp(v, lo, hi)` | clamp to range |
| `@pow(base, exp)` | power |
| `@log(x)` | natural log |

## Diagnostics

| Builtin | Description |
|---|---|
| `@assert(cond, msg)` | panic if false (debug only) |
| `@panic(msg)` | unconditional panic with message and stack trace |
| `@unreachable()` | mark code path as unreachable |

## Compile-Time Info

| Builtin | Type | Description |
|---|---|---|
| `@debug` | `bool` | true in debug builds |
| `@release` | `bool` | true in release builds |
| `@os.linux` | `bool` | target is Linux |
| `@os.macos` | `bool` | target is macOS |
| `@os.windows` | `bool` | target is Windows |
| `@arch.x86_64` | `bool` | target CPU |
| `@arch.aarch64` | `bool` | target CPU |
| `@endian.little` | `bool` | little-endian target |
| `@ptr_width` | `usize` | pointer size in bits |

## Process / Args

| Builtin | Signature | Description |
|---|---|---|
| `@args` | `-> []str` | command-line arguments |
| `@argc` | `-> usize` | argument count |
| `@exit(code)` | `fn(i32)` | exit process immediately |
| `@env(name)` | `fn(str) -> !str` | read environment variable |

## Checked Arithmetic

| Builtin | Description |
|---|---|
| `@checked_add(a, b)` | returns `!T` — error on overflow |
| `@checked_sub(a, b)` | returns `!T` — error on overflow |
| `@checked_mul(a, b)` | returns `!T` — error on overflow |
| `@wrapping_add(a, b)` | always wraps (no panic in debug) |
| `@saturating_add(a, b)` | clamps to type max/min |

## Random Numbers

| Builtin | Signature | Description |
|---|---|---|
| `@rng(T, min, max)` | `fn(type, T, T) -> T` | random integer or float in `[min, max]` inclusive |
| `@rng_seed(n)` | `fn(i32)` | seed the RNG (default: unseeded / stdlib `rand`) |

`T` must be an integer or float type. Pass the type name as the first argument:

```
x: i32  = @rng(i32, 1, 6)       # roll a die
y: f64  = @rng(f64, 0.0, 1.0)   # uniform float
z: u64  = @rng(u64, 0, 100)     # unsigned range

@rng_seed(42)                    # reproducible sequence
```

## Assembly

| Builtin | Description |
|---|---|
| `@asm(inst, constr, ...)` | inline assembly |
| `@asm_volatile(...)` | inline assembly with volatile semantics |

## String Interpolation

`@pf` and `@epf` support `{expr}` inside the format string:

```
x: i32 = 42
@pf("x is {x} and doubled is {x * 2}\n")
```

Use `{{` and `}}` to emit literal braces.
