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

## Type Inference

Use `:=` (mutable) or `::` (immutable) to let the compiler infer the type from the initializer.

When the right-hand side is a bare **literal** (integer, float, string, bool, char), the type is widened to the natural hardware-width so you never accidentally box yourself into a narrow type:

| Literal kind | Inferred binding type |
|---|---|
| integer literal (`0`, `42`, …) | `i64` |
| float literal (`1.5`, `3.14`, …) | `f64` (already the default) |
| string literal | `str` |
| bool / char literal | `bool` / `char` |

```
count  := 0          # i64 — integer literal widens to i64
ratio  := 1.5        # f64
name   :: "Alice"    # str, immutable
active := true       # bool

count = count + 1    # ok — count is mutable i64
name  = "Bob"        # ERROR: cannot assign to immutable binding 'name'
```

When the right-hand side is anything other than a bare literal (a call, variable, expression), the binding takes the **exact** inferred type — no widening:

```
fn get_score() -> i32 { ret 100 }
fn get_ratio() -> f32 { ret 0.5 }

s := get_score()   # i32 — exact return type
r := get_ratio()   # f32 — exact return type
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
if err != 0 { @panic("overflow") }
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

s: str  = @str(99)       # int → "99"
c: char = @char(65)      # → 'A'
```

All cast builtins: `@i8` `@i16` `@i32` `@i64` `@u8` `@u16` `@u32` `@u64`
`@f32` `@f64` `@usize` `@bool` `@char` `@str`

### String to Number

Casting a `str` to a numeric type parses the string. The plain form returns zero on failure; the failable form exposes the error flag:

```
n: i32  = @i32("42")     # 42 — parse string → int
z: i32  = @i32("hello")  # 0  — non-numeric, fallback to zero

val, err: !i32 = @i32("42")    # val=42  err=0
bad, err2: !i32 = @i32("abc")  # bad=0   err2=1
```

### Bit Reinterpretation

`@bitcast(T, val)` reinterprets the raw bits with no numeric conversion. Source and destination must be the same size:

```
f: f32    = 1.0
bits: u32 = @bitcast(u32, f)   # 0x3F800000
```

## Type Aliases

`type` creates a new name for an existing type. The alias is fully interchangeable with the underlying type.

```
type int   = i32
type uint  = u32
type byte  = u8
type cstr  = *u8      # null-terminated C string

type Score   = i64
type Meters  = f64
type Seconds = f64
type NodeId  = u32
type Buffer  = *u8
```

Aliases work everywhere a type is expected — variable declarations, function parameters, and return types:

```
fn speed(dist: Meters, time: Seconds) -> f64 {
    ret dist / time
}

d: Meters  = 100.0
t: Seconds = 9.58
s: f64 = speed(d, t)   # 10.44
```

The built-in aliases `int`, `uint`, `byte`, and `cstr` are pre-defined and available in every file.
