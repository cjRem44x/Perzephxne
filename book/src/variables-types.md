# Variables & Types

## Mutability

Every variable is either **mutable** (declared with `=`) or **immutable** (declared with `:`).

```
x: i32 = 5      # mutable — can be reassigned
PI: f64 : 3.14  # immutable — assignment after this is a compile error
```

Attempting to assign to an immutable binding is a compile-time error:

```
N: usize : 10
N = 20           # ERROR: cannot assign to immutable binding 'N'
```

## Primitive Types

### Integers

```
u8   i8
u16  i16
u32  i32
u64  i64
usize        # platform-width (64-bit on 64-bit targets) — use for sizes and indices
```

### Floats

```
f16
f32
f64
```

### Other Primitives

```
char         # single byte character — alias for u8
bool         # true / false
```

## Strings and Arrays

### `str`

`str` is a fat pointer `{*u8 data, usize len}`. String literals are immutable.

```
name: str   = "Alice"
len: usize  = name.len
first: char = name[0]    # indexing returns char
```

Concatenation and mutation require the standard library (`std/str`).

### Fixed Arrays

Fixed-size arrays live on the stack. The size must be a compile-time constant.

```
N: usize : 5
arr: [N]i32 = [1, 2, 3, 4, 5]
arr[0] = 10

count: usize = @len(arr)     # 5
```

Multidimensional arrays:

```
matrix: [3][3]f32 = [[1.0, 0.0, 0.0],
                     [0.0, 1.0, 0.0],
                     [0.0, 0.0, 1.0]]
val: f32 = matrix[1][2]
```

## Slices

A slice is a fat pointer `{*T data, usize len}` — a non-owning view into an array or allocation.

```
arr: [5]i32 = [10, 20, 30, 40, 50]
sl:  []i32  = arr[1..3]    # [20, 30] — exclusive end
sl2: []i32  = arr[1..=3]   # [20, 30, 40] — inclusive end

len: usize = @len(sl)      # 2
```

Passing a slice to a function:

```
fn sum(s: []i32) -> i32 {
    total: i32 = 0
    for v => s { total += v }
    ret total
}
```

## `any`

`any` holds a value of any type alongside a runtime type tag. Use `when` to inspect:

```
x: any = 42
x = "hello"

when x {
    i32 n  => @pf("int: {n}\n"),
    str s  => @pf("str: {s}\n"),
    bool b => @pf("bool: {b}\n"),
    _      => @pf("unknown\n"),
}
```

## Value Semantics

Structs and arrays are **copied by value** on assignment and when passed to functions — the same as C.

```
a: vec2 = vec2{.x=1.0, .y=2.0}
b: vec2 = a           # independent copy
b.x = 99.0
@pf("{a.x}\n")        # still 1.0

fn zero(v: vec2) {
    v.x = 0.0         # modifies local copy only
}
```

To mutate the caller's value, pass a pointer:

```
fn zero_ptr(v: *vec2) {
    v.*.x = 0.0
}
```

## Implicit Widening

Smaller integers widen to larger ones automatically. Narrowing is a compile error.

```
x: i32 = 10
y: i64 = x        # ok — i32 fits in i64
z: i16 = x        # ERROR — use @i16(x) instead
n: i32 = 5
f: f64 = n        # ok — int widens to float
```

## Overflow Behavior

Both signed and unsigned integers use defined 2's-complement wrapping — overflow is never undefined behavior.

In **debug builds** overflow triggers a runtime panic. In **release builds** it wraps silently.

```
x: u8 = 255
x = x + 1    # wraps to 0 — defined
```

Use `@checked_*` to detect overflow in any build mode:

```
result, err: !i32 = @checked_add(a, b)
if err != @err.ok { @panic("overflow") }
```

## Special Values

| Value | Valid on | Meaning |
|---|---|---|
| `undef` | any type | explicitly uninitialized |
| `null` | raw pointers (`*T`) only | zero pointer |

```
x: i32  = undef
p: *i32 = null

x = 5
if p != null { p.* = x }
```

## Type Casting

Use `@T(val)` where `T` is the target type:

```
x: i32  = 300
y: u8   = @u8(x)         # truncates to 44

a: f64  = 3.7
b: i32  = @i32(a)        # truncates to 3

n: i32  = @i32("42")     # parse string → int
s: str  = @str(99)       # int → "99"
c: char = @char(65)      # → 'A'
```

All cast builtins: `@i8` `@i16` `@i32` `@i64` `@u8` `@u16` `@u32` `@u64`
`@f16` `@f32` `@f64` `@usize` `@bool` `@char` `@str`

Raw bit reinterpretation (same size, no conversion):

```
f: f32    = 1.0
bits: u32 = @bitcast(u32, f)
```

## Type Aliases

```
type int  = i32
type uint = u32
type byte = u8
type cstr = *u8    # null-terminated C string
```
