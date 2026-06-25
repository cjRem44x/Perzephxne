# Perzephxne Language Specification

A programming language influenced by the modern schemes of things. No GC baggage, no OOP
principles, just pure C-flavored programming.

Source compiles into LLVM IR (LLVM backend), which represents a more flexible format than
hardware assembly and allows cross-platform targeting.

---

## Build System (`przp`)

`przp` is the Perzephxne build tool, similar to Cargo for Rust.

### Commands

| Command | Description |
|---|---|
| `przp init` | Initialize a new project in the current directory |
| `przp build` | Compile the project (debug by default) |
| `przp build --release` | Optimized release build |
| `przp build --debug` | Explicit debug build (default) |
| `przp run` | Build and run (debug) |
| `przp run --release` | Build and run (release) |
| `przp sac <files> -o=Name` | Stand-Alone Compiler — compile files outside a project |
| `przp add <pkg>` | Add a package dependency (future) |

### Build Modes

**Debug** (default): no optimization, bounds checking, overflow traps, `@assert` active.

**Release** (`--release`): full optimization, bounds checking off, overflow wraps, `@assert` stripped.

Query the current mode at compile time:

```
if @debug   { @pf("running in debug mode\n") }
if @release { @pf("running in release mode\n") }
```

### Project Structure

```
/MyProject
    .git/
    /bin/
        MyProject          # compiled binary
    /src/
        /main/
            /przp/
                main.przp  # entry point
    przp.toml              # project manifest and deps
    .gitignore
```

### `przp.toml`

```toml
[project]
name    = "MyProject"
version = "0.1.0"
target  = "x86_64-linux"  # optional, defaults to host

[deps]
# przp add <pkg> writes here
# mylib = "1.2.0"
```

### Quick Start

```
mkdir MyProject
cd MyProject
przp init
przp run
```

---

## Comments

```
# single line comment

##
multi
line
comment
##
```

---

## Hello World

```
fn main() {
    @pf("Hello World\n")
}
```

Outside a project structure:

```
przp sac main.przp -o=main
```

---

## Literals

### Integer Literals

```
42           # decimal — default type i32
0xFF         # hex
0b1010_1010  # binary (underscore separator allowed anywhere)
0o17         # octal
1_000_000    # decimal with separators
```

Type suffixes when the context cannot infer the type:

```
42u8    42u16    42u32    42u64
42i8    42i16    42i32    42i64
42usize
```

### Float Literals

```
3.14         # default type f64
1.5e-3       # scientific notation
1_000.0      # separator allowed
```

Type suffixes:

```
3.14f32    3.14f64
```

### Character Literals

```
c: char = 'A'
nl: char = '\n'
```

### Boolean Literals

```
true    false
```

### String Literals

```
"hello world"
```

Escape sequences:

| Sequence | Meaning |
|---|---|
| `\n` | newline |
| `\t` | tab |
| `\r` | carriage return |
| `\0` | null byte |
| `\\` | backslash |
| `\"` | double quote |
| `\'` | single quote |
| `\xHH` | hex byte |
| `\uHHHH` | Unicode code point (UTF-8 encoded) |

### Default Literal Types

| Literal form | Default type |
|---|---|
| `42` | `i32` |
| `3.14` | `f64` |
| `'A'` | `char` |
| `true` / `false` | `bool` |
| `"..."` | `str` |

---

## Functions

### Declaration

```
fn foo(param1: type, param2: type) -> retType {
    ...
    ret value
}
```

### Void Functions

No `->` needed. Empty `ret` is allowed for early exit.

```
fn greet(name: str) {
    if name.len == 0 {
        ret
    }
    @pf("Hello, {name}\n")
}
```

### Multiple Return Values

Functions return a single value. Use a struct for multiple returns.

```
struct divResult { quot: i32, rem: i32 }

fn divide(a: i32, b: i32) -> divResult {
    ret divResult{.quot = a/b, .rem = a%b}
}
```

### Failable Return (`!`)

Prefix the return type with `!` to signal the function can fail. The caller destructures
the result into a value and an error.

```
fn safe_div(a: i32, b: i32) -> !i32 {
    if b == 0 { ret @err.div_zero }
    ret a / b
}
```

- `ret value` — success; `err` is `@err.ok` at the call site
- `ret @err.X` — failure; value is `undef` at the call site

Call site:

```
val, err: !i32 = safe_div(10, 2)
if err != @err.ok {
    @pf("failed: {err}\n")
}
```

### Inline Functions

Hint to the compiler to inline the function at call sites.

```
inline fn square(x: i32) -> i32 {
    ret x * x
}
```

### Variadic Functions

Accept a variable number of arguments of any type. The variadic parameter is typed as a
slice of `any`.

```
fn log(fmt: str, args: ..any) {
    # args is []any — iterate like a slice
    for a => args {
        when a {
            i32 n  => @pf("{n} "),
            f64 f  => @pf("{f:.2} "),
            str s  => @pf("{s} "),
            bool b => @pf("{b} "),
            _      => @pf("? "),
        }
    }
    @pf("\n")
}

log("vals:", 42, 3.14, "hi", true)
```

### Function Pointers

```
fn add(a: i32, b: i32) -> i32 { ret a + b }

fp: fn(i32, i32) -> i32 = &add
result: i32 = fp(1, 2)
```

### No Overloading

Function overloading is not supported. Every function name in a scope must be unique.
Use distinct names or generic type parameters instead.

```
fn add_i32(a: i32, b: i32) -> i32 { ret a + b }
fn add_f64(a: f64, b: f64) -> f64 { ret a + b }

fn add(T: type, a: T, b: T) -> T { ret a + b }   # generic alternative
```

### Extern / FFI

Declare external C functions. Variadic C functions use `...`.

```
extern fn printf(fmt: *u8, ...) -> i32
extern fn malloc(size: usize) -> *u8
extern fn free(ptr: *u8)
```

---

## Types

### Mutability

Every variable is either mutable (`=`) or immutable (`:`).

```
VAR: TYPE = VALUE   # mutable
VAR: TYPE : VALUE   # immutable (compile-time constant if value is a literal)
```

### Primitive Types

**Integers**
```
u8   i8
u16  i16
u32  i32
u64  i64
usize        # platform-width (64-bit on 64-bit targets) — use for sizes and indices
```

**Floats**
```
f16
f32
f64
```

**Other**
```
char         # single byte character — alias for u8
bool         # true / false
str          # managed string — fat pointer {*u8 data, usize len}
any          # runtime type-erased value — fat pointer {*u8 data, typeId tag}
```

### Value Semantics

Structs and arrays are **copied by value** on assignment and when passed to functions —
the same as C. Pointers copy the pointer address, not the data they point to.

```
a: vec2 = vec2{.x=1.0, .y=2.0}
b: vec2 = a        # b is an independent copy — modifying b does not affect a
b.x = 99.0
@pf("{a.x}\n")     # still 1.0

fn zero(v: vec2) {
    v.x = 0.0      # modifies the local copy — caller's value unchanged
}
```

To mutate the caller's value, pass a pointer:

```
fn zero_ptr(v: *vec2) {
    v.*.x = 0.0    # modifies caller's struct through the pointer
}
```

### Implicit Widening

A value of a smaller integer type can be assigned to a larger one. Narrowing is a
compile error — use an explicit cast.

```
x: i32 = 10
y: i64 = x        # ok — i32 fits in i64
z: i16 = x        # ERROR — narrowing, use @i16(x)
```

Integer to float widening is also allowed:

```
n: i32 = 5
f: f64 = n        # ok
```

### Overflow Behavior

Both signed and unsigned integers use **defined wrapping** (2's complement) — overflow
is never undefined behavior.

```
x: u8 = 255
x = x + 1    # wraps to 0 — defined
```

In **debug builds** overflow triggers a runtime panic. In **release builds** it wraps
silently. Use `@checked_*` to detect overflow explicitly in any mode.

```
result, err: !i32 = @checked_add(a, b)
if err != @err.ok { @panic("overflow") }
```

Available: `@checked_add`, `@checked_sub`, `@checked_mul`.

### Special Values

| Value | Valid on | Meaning |
|---|---|---|
| `undef` | any type | explicitly uninitialized — using before assignment is a compile error where detectable, UB otherwise |
| `null` | raw pointers (`*T`) only | zero pointer; smart pointers (`^T`) cannot be null |

```
x: i32  = undef
p: *i32 = null

x = 5
if p != null { p.* = x }
```

### `char`

`char` is an alias for `u8` and represents a single ASCII byte. Character literals use
single quotes.

```
c: char = 'A'
digit: char = '0'
nl: char = '\n'

code: u8 = @u8(c)      # char is u8, cast is a no-op
ch: char = @char(65)   # 'A'
```

Indexing a `str` returns `char`:

```
s: str = "hello"
first: char = s[0]     # 'h'
```

### Strings

`str` is a fat pointer `{*u8 data, usize len}`. String literals are immutable.

```
name: str = "Alice"
len: usize  = name.len
first: char = name[0]
```

Concatenation and mutation require the standard library (`std/str`).

### Arrays

Fixed-size arrays on the stack. Size must be a compile-time constant.

```
N: usize : 5
arr: [N]i32 = [1, 2, 3, 4, 5]
arr[0] = 10

len: usize = @len(arr)
```

Multidimensional:

```
matrix: [3][3]f32 = [[1.0, 0.0, 0.0], [0.0, 1.0, 0.0], [0.0, 0.0, 1.0]]
val: f32 = matrix[1][2]
```

### Slices

A slice is a fat pointer `{*T data, usize len}` — a view into an existing array or
allocation. Slices do not own memory.

```
arr: [5]i32 = [10, 20, 30, 40, 50]
sl:  []i32  = arr[1..3]    # [20, 30] — indices 1 and 2, exclusive end
sl2: []i32  = arr[1..=3]   # [20, 30, 40] — inclusive end

len: usize = @len(sl)      # 2
```

Pass slices to functions:

```
fn sum(s: []i32) -> i32 {
    total: i32 = 0
    for v => s { total += v }
    ret total
}
```

### `any`

`any` holds a value of any type alongside a runtime type tag. Use `when` to match on
the held type and bind the value to a name.

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

Use as a function parameter for type-generic behavior:

```
fn print_val(v: any) {
    when v {
        i32 n  => @pf("i32: {n}\n"),
        f64 f  => @pf("f64: {f:.4}\n"),
        str s  => @pf("str: {s}\n"),
        bool b => @pf("bool: {b}\n"),
        _      => @pf("<opaque>\n"),
    }
}
```

### Type Casting

Use `@T(val)` where `T` is the target type. Handles numeric conversions, string parsing,
and value-to-string formatting.

```
x: i32 = 300
y: u8  = @u8(x)          # truncates to 44

a: f64 = 3.7
b: i32 = @i32(a)         # truncates to 3

n: i32 = @i32("42")      # parse string → int
s: str = @str(99)        # int → string "99"
s2: str = @str(3.14)     # float → string "3.14"
c: char = @char(65)      # int → char 'A'
```

All cast builtins:
`@i8` `@i16` `@i32` `@i64` `@u8` `@u16` `@u32` `@u64` `@f16` `@f32` `@f64`
`@usize` `@bool` `@char` `@str`

For raw bit-reinterpretation (same size required, no conversion):

```
f: f32 = 1.0
bits: u32 = @bitcast(u32, f)
```

---

## Type Aliases

```
type T = existingType

type int  = i32
type uint = u32
type byte = u8
type cstr = *u8    # null-terminated C string
```

---

## Generics

Functions and structs can take compile-time type parameters. The compiler generates a
concrete instantiation per unique type combination — similar to C++ templates but
explicit at the call site.

### Generic Functions

Declare a type parameter with `T: type`:

```
fn min(T: type, a: T, b: T) -> T {
    if a < b { ret a }
    ret b
}

x: i32 = min(i32, 3, 7)
y: f64 = min(f64, 1.5, 0.9)
```

Multiple type parameters:

```
fn map(T: type, U: type, arr: []T, f: fn(T) -> U) -> []U {
    ...
}
```

### Generic Structs

```
struct stack(T: type) {
    data: *T,
    len:  usize,
    cap:  usize,
}

impl stack(T: type) {
    fn push(val: T) {
        ...
        @slf.data[@slf.len] = val
        @slf.len += 1
    }

    fn pop() -> !T {
        if @slf.len == 0 { ret @err.invalid }
        @slf.len -= 1
        ret @slf.data[@slf.len]
    }
}

s: stack(i32) = stack(i32)
s.push(10)
val, err: !i32 = s.pop()
```

---

## Global Variables

Top-level declarations outside any function are global. Globals are initialized before
`main` runs.

```
MAX: usize : 1024              # global immutable constant
counter: i32 = 0               # global mutable variable
buf: [256]u8 = undef           # global buffer, explicitly uninitialized
PI: f64 : 3.14159265358979
```

Globals are accessible from any file in the project.

### Compile-time Constant Expressions

`imu` (immutable) values at global or local scope are evaluated at compile time when
their initializer is a constant expression. Constant expressions can use:

- Literal values
- Other `imu` values
- `@size(T)`, `@align(T)`, `@offsetof(T, f)`, `@len(arr)`
- Arithmetic and bitwise operators on the above

```
ITEM_SIZE: usize : @size(myStruct)
BUF_LEN:  usize : ITEM_SIZE * 16
buf: [BUF_LEN]u8 = undef           # array size computed at compile time

MASK: u32 : 0xFF00 & 0x0F0F        # bitwise evaluated at compile time
```

This applies to array sizes, `@align` arguments, and any `imu` binding.

---

## Statements

### if / elif / else

```
if x > 0 {
    ...
} elif x == 0 {
    ...
} else {
    ...
}
```

### Boolean Operators

```
and   or   not   ==   !=   <   >   <=   >=
```

```
if x > 0 and y > 0 or z != 0 {}
if not flag {}
```

### Scope and Shadowing

Each `{}` block introduces a new scope. Variables declared in an inner scope shadow
outer variables with the same name for the duration of that block.

```
x: i32 = 5
{
    x: i32 = 10       # shadows outer x
    @pf("{x}\n")      # 10
}
@pf("{x}\n")          # 5 — outer x unchanged
```

Re-assignment (not shadowing) modifies the existing variable:

```
x: i32 = 5
x = 10                # mutates — x is now 10
```

### `_` Discard

`_` is the discard identifier — it accepts a value and silently drops it. Use it
anywhere a name is required but the value is not needed.

Discarding one side of a failable return:

```
val, _: !i32 = divide(10, 2)    # ignore the error (dangerous — only if you're certain)
_, err: !i32 = divide(10, 2)    # ignore the value, only check err
```

Discarding a loop index:

```
for e, _ => arr {}               # element only, no index
```

Discarding a full return value:

```
_ = some_func()
```

`_` cannot be read — writing to it is always a no-op, reading it is a compile error.

### when (Pattern Match)

`when` is exhaustive — the compiler requires all cases to be covered or a wildcard present.

```
when val {
    A => ...,
    B => {
        ...
    },
    _ => ...
}
```

Multi-pattern arms — match several values to one branch with `|`:

```
when val {
    0 | 1 | 2 => @pf("small\n"),
    3 | 4     => @pf("medium\n"),
    _         => @pf("large\n"),
}

when c {
    'a' | 'e' | 'i' | 'o' | 'u' => @pf("vowel\n"),
    'a'..'z'                     => @pf("consonant\n"),
    _                            => @pf("non-alpha\n"),
}
```

Range matching:

```
when val {
    0       => @pf("zero\n"),
    1..9    => @pf("single digit\n"),
    10..=99 => @pf("two digits\n"),
    _       => @pf("large\n"),
}
```

Tagged union matching:

```
when result {
    .ok  val => @pf("got: {val}\n"),
    .err msg => @pf("err: {msg}\n"),
}
```

`any` type matching:

```
when x {
    i32 n  => @pf("int {n}\n"),
    str s  => @pf("str {s}\n"),
    _      => @pf("other\n"),
}
```

### when as Expression

`when` can produce a value. All branches must resolve to the same type.

```
label: str = when code {
    200 => "ok",
    404 => "not found",
    500 => "server error",
    _   => "unknown",
}

abs_val: i32 = when x < 0 {
    true  => -x,
    false =>  x,
}
```

### if as Expression

`if` can also produce a value. Both the `if` branch and the `else` branch must be
present and resolve to the same type.

```
abs_val: i32 = if x < 0 { -x } else { x }

msg: str = if ok { "success" } else { "failure" }
```

Unlike `when`, an `if` expression does not require exhaustiveness beyond requiring
an `else` branch.

---

## Loops

### while

```
while condition {
    ...
}
```

Call a function each iteration before checking condition:

```
while condition => tick() {}
```

### for — element in collection

```
for e => arr {
    # e is the element
}

for e, i => arr {
    # e is the element, i is the index (usize)
}
```

### for — element with range limit

```
for e => arr, 0..N  {}     # exclusive end
for e => arr, 0..=N {}     # inclusive end
```

### for — traditional C-style

```
for i := 0, i < len, i++ {
    ...
}
```

### for — counting loop

```
for 0..N  {}    # exclusive
for 0..=N {}    # inclusive
```

### break and continue

```
while true {
    if done { break }
    if skip { continue }
}
```

### Loop Labels

Break or continue a specific outer loop by name.

```
outer: while true {
    for 0..N {
        if cond { break outer }
        if skip { continue outer }
    }
}
```

---

## Defer

`defer` schedules a statement or block to execute at the end of the current scope —
function, block, or loop iteration. Multiple defers in the same scope run in **LIFO order**.

Single statement:

```
fn process(path: str) -> !str {
    f: *file = open(path)
    defer close(f)

    buf: *u8 = @alo(u8, 1024)
    defer @free(buf)        # LIFO — runs before close(f)

    ...
    ret @str(buf)
}
```

Block form for multiple statements:

```
fn setup() {
    defer {
        @pf("cleanup step 1\n")
        @pf("cleanup step 2\n")
        flush_logs()
    }
    ...
}
```

In loops, deferred to the end of each iteration:

```
for i := 0, i < N, i++ {
    p: *i32 = @alo(i32)
    defer @free(p)
}
```

Defer captures the current values of variables at the `defer` line, not at execution time.

---

## Structs

```
struct vec2 {
    x: f32,
    y: f32,
}
```

### Instantiation

```
v: vec2 = vec2                          # zero-initialized
v.x = 1.0

v2: vec2 = vec2{.x=1.0, .y=2.0}        # field initializer (any order)
v3: vec2 = vec2{.x=1.0}                # partial — .y zero-initialized
```

### Nested Structs

```
struct rect {
    origin: vec2,
    size:   vec2,
}

r: rect = rect{.origin=vec2{.x=0.0, .y=0.0}, .size=vec2{.x=100.0, .y=50.0}}
w: f32 = r.size.x
```

### Struct Attributes

Apply attributes before the `struct` keyword.

```
@packed
struct header {
    magic:   u32,
    version: u16,
    flags:   u8,
}
# no padding bytes inserted — total size is exactly 7 bytes

@align(16)
struct alignedBuf {
    data: [64]u8,
}
# struct will be aligned to a 16-byte boundary in memory
```

Combining:

```
@packed @align(4)
struct wirePacket {
    len:  u16,
    kind: u8,
    body: [61]u8,
}
```

---

## Implements

Add methods to a struct. This is namespace organization — not OOP. The compiler
resolves `impl` methods as function pointers and injects the instance automatically
under the hood. You never write a `self` parameter.

```
impl vec2 {
    fn len() -> f32 {
        x: f32 = @imuslf.x
        y: f32 = @imuslf.y
        ret @sqrt(x*x + y*y)     # @sqrt from std/math or builtin
    }

    fn scale(s: f32) {
        @slf.x *= s
        @slf.y *= s
    }
}

v: vec2 = vec2{.x=3.0, .y=4.0}
v.scale(2.0)
l: f32 = v.len()
```

Self aliasing:

```
impl vec2 {
    fn normalize() {
        type s = @slf
        l: f32 = @imuslf.len()
        s.x /= l
        s.y /= l
    }
}
```

### Static Methods

Methods with no reference to `@slf` or `@imuslf` are static — called via the type name,
no instance passed.

```
impl vec2 {
    fn zero() -> vec2 {
        ret vec2{.x=0.0, .y=0.0}
    }
    fn one() -> vec2 {
        ret vec2{.x=1.0, .y=1.0}
    }
}

origin: vec2 = vec2.zero()
```

---

## Type Aliases

```
type T = existingType

type int  = i32
type uint = u32
type byte = u8
type cstr = *u8
```

---

## Enums

```
enum direction {
    NORTH, SOUTH, EAST, WEST
}

d: direction = direction.NORTH

when d {
    direction.NORTH => @pf("north\n"),
    direction.SOUTH => @pf("south\n"),
    direction.EAST  => @pf("east\n"),
    direction.WEST  => @pf("west\n"),
}
```

### Backed Enums

Assign an integer backing type and explicit values.

```
enum color => u32 {
    RED   = 0xFF0000,
    GREEN = 0x00FF00,
    BLUE  = 0x0000FF,
}

c: color = color.RED
raw: u32 = @u32(c)
```

---

## Unions

Untagged union — all fields share the same memory. Accessing the wrong field is
undefined behavior.

```
unn data {
    as_i32: i32,
    as_f32: f32,
    as_u32: u32,
}

d: data = data{.as_i32=0x3F800000}
f: f32  = d.as_f32    # reinterpret bits as f32 (= 1.0)
```

### Tagged Unions

Tagged unions carry a runtime discriminant. Use `when` to safely unwrap.

```
unn shape => enum {
    circle: f32,          # radius
    rect:   vec2,         # size
    point,                # no data
}

s: shape = shape{.circle=5.0}

when s {
    .circle r => @pf("circle r={r}\n"),
    .rect   v => @pf("rect {v.x}x{v.y}\n"),
    .point    => @pf("point\n"),
}
```

---

## Pointers

### Raw Fat Pointers (`*`)

Raw pointers store a pointer and the size of the allocation. Manual lifetime — you must
`@free` everything you `@alo`.

```
x: i32  = 12
p: *i32 = &x           # pointer to stack variable

heap: *i32 = @alo(i32)
heap.* = 99             # dereference to write
val: i32 = heap.*       # dereference to read
@free(heap)
```

### Allocating Arrays

```
arr: *i32 = @alo(i32, 10)
arr[0] = 1
arr[9] = 99
@free(arr)
```

### Smart Pointers (`^`)

Smart pointers track their lifetime and free themselves at the end of their scope. No
`@free` needed. Cannot be null.

```
sp: ^i32 = @alo(i32)
sp.^ = 100
# freed automatically when sp goes out of scope
```

### Pointer Arithmetic

```
p: *i32 = &arr[0]
p = p + 1              # advance by sizeof(i32) — steps by element, not byte
p = p - 1
offset: usize = p - &arr[0]
```

### Null Pointers

Only valid on raw pointers. Smart pointers cannot be null.

```
p: *i32 = null
if p != null {
    p.* = 5
}
```

### Passing Pointers

```
fn set(p: *i32, v: i32) {
    p.* = v
}

fn get(p: ^i32) -> i32 {
    ret p.^
}
```

---

## Memory

See `@alo`, `@free`, `@realo`, `@memcpy`, `@memset`, `@memmove` in the Builtins section.

---

## Error Handling

Perzephxne has no exceptions. Errors are values returned alongside the result using the
`!` failable type. See **Failable Return** under Functions for the full call syntax.

### `@err` Namespace

`@err` is the built-in error type. Compare against `@err.ok` to check success.

| Value | Meaning |
|---|---|
| `@err.ok` | no error (success) |
| `@err.fail` | generic failure |
| `@err.div_zero` | division by zero |
| `@err.null_deref` | null pointer dereference |
| `@err.out_of_bounds` | index out of bounds |
| `@err.overflow` | arithmetic overflow |
| `@err.invalid` | invalid argument |
| `@err.not_found` | resource not found |
| `@err.io` | I/O failure |
| `@err.oom` | out of memory |

### Custom Errors

Define your own error enum and return it from `!` functions:

```
enum fileErr { NOT_FOUND, PERMISSION, CORRUPT }

fn open_cfg(path: str) -> !str {
    # ...
    ret fileErr.NOT_FOUND
}

contents, err: !str = open_cfg("/etc/app.conf")
if err != @err.ok {
    when err {
        fileErr.NOT_FOUND  => @pf("file missing\n"),
        fileErr.PERMISSION => @pf("no access\n"),
        _                  => @pf("error: {err}\n"),
    }
}
```

### Panic

For unrecoverable errors — prints message with file/line and aborts.

```
@panic("unreachable state")
```

---

## Platform Detection

Compile-time booleans for conditional platform-specific code. These are constant
expressions — the compiler eliminates dead branches entirely.

```
if @os.linux {
    # linux-specific code
} elif @os.windows {
    # windows-specific code
} else {
    @panic("unsupported platform")
}

fn get_page_size() -> usize {
    if @arch.x86_64 { ret 4096 }
    if @arch.arm64  { ret 16384 }
    ret 4096
}
```

Use with `imu` for platform-specific constants:

```
PATH_SEP: char : if @os.windows { '\\' } else { '/' }
```

Available:

| Builtin | Meaning |
|---|---|
| `@os.linux` | Linux |
| `@os.windows` | Windows |
| `@os.macos` | macOS |
| `@arch.x86_64` | 64-bit x86 |
| `@arch.arm64` | AArch64 / Apple Silicon |
| `@arch.x86` | 32-bit x86 |

---

## Inline Assembly

For direct hardware access. Uses LLVM inline assembly syntax. Inputs and outputs are
bound to Perzephxne variables.

```
result: i64 = undef
asm {
    "syscall"
    out: result = "=r"
    in:  @rax=1, @rdi=1, @rsi=msg_ptr, @rdx=msg_len
    clobber: "memory", "rcx", "r11"
}
```

Simple form for no-output instructions:

```
asm { "nop" }
asm { "cli" }
```

Inline asm is inherently unsafe — the compiler cannot verify correctness of the
instruction string or clobber list.

---

## Module System

No header files. Modules are source files imported by path or package name.

```
import(
    math = "std/math",
    io   = "std/io",
    util = "src/util",
)

fn main() {
    x: f64 = math.sqrt(2.0)
    io.println("done")
}
```

- **Stdlib paths** — start with `std/`
- **Local paths** — relative to the project root `/src/`
- **Package paths** — match names in `przp.toml` `[deps]`

All top-level declarations are accessible to importing modules. There is no explicit
export/visibility system — everything is shared by default.

### Declaration Ordering

Within a `.przp` file, declaration order does not matter. The compiler scans the whole
file before resolving names, so functions, structs, and globals can be declared after
their first use.

```
fn main() {
    greet("world")    # ok even though greet is declared below
}

fn greet(name: str) {
    @pf("Hello, {name}\n")
}
```

### Circular Imports

Circular imports between modules are a compile error. If two modules need to share
types, extract the shared types into a third module that both import.

```
# bad — A imports B and B imports A
# good — both import C which holds the shared types
```

---

## Standard Library

### `std/io`

File I/O and streams.

```
import(io = "std/io")

f, err: !*io.file = io.open("data.txt", io.READ)
defer io.close(f)

line, err2: !str = io.read_line(f)
io.write(f, "hello\n")
io.println("to stdout")
io.eprintln("to stderr")
```

### `std/math`

```
import(math = "std/math")

math.sqrt(x: f64) -> f64
math.pow(base: f64, exp: f64) -> f64
math.abs(x: f64) -> f64
math.floor(x: f64) -> f64
math.ceil(x: f64) -> f64
math.sin(x: f64) -> f64
math.cos(x: f64) -> f64
math.tan(x: f64) -> f64
math.log(x: f64) -> f64
math.log2(x: f64) -> f64
math.PI: f64  # constant
math.E:  f64
```

### `std/str`

String operations. All functions return new strings — original is not modified.

```
import(str = "std/str")

str.len(s: str) -> usize
str.concat(a: str, b: str) -> str
str.slice(s: str, start: usize, end: usize) -> str
str.find(s: str, sub: str) -> !usize
str.contains(s: str, sub: str) -> bool
str.starts_with(s: str, pre: str) -> bool
str.ends_with(s: str, suf: str) -> bool
str.replace(s: str, old: str, new: str) -> str
str.split(s: str, delim: str) -> []str
str.trim(s: str) -> str
str.to_upper(s: str) -> str
str.to_lower(s: str) -> str
str.bytes(s: str) -> []u8
```

### `std/mem`

Memory utilities and allocators.

```
import(mem = "std/mem")

# arena allocator — alloc many, free all at once
arena: mem.arena = mem.arena_new(4096)
defer mem.arena_free(&arena)

ptr: *u8 = mem.arena_alloc(&arena, u8, 256)
# no individual frees needed
```

### `std/collections`

Generic containers.

```
import(col = "std/collections")

# dynamic array
arr: col.dynArr(i32) = col.dynArr_new(i32)
col.push(&arr, 10)
col.push(&arr, 20)
val: i32 = arr[0]
col.dynArr_free(&arr)

# hash map
map: col.hashMap(str, i32) = col.hashMap_new(str, i32)
col.insert(&map, "key", 42)
v, ok: !i32 = col.get(&map, "key")
col.hashMap_free(&map)
```

### `std/fs`

Filesystem operations.

```
import(fs = "std/fs")

contents, err: !str  = fs.read_file("data.txt")
err2: @err           = fs.write_file("out.txt", contents)
exists: bool         = fs.exists("data.txt")
entries, err3: ![]str = fs.read_dir("./src")
```

### `std/os`

Process and environment.

```
import(os = "std/os")

args: []str       = os.args()          # command line args
val, ok: !str     = os.get_env("HOME")
os.set_env("DEBUG", "1")
os.exit(0)
```

### `std/time`

```
import(time = "std/time")

now: u64       = time.now_ms()          # milliseconds since epoch
time.sleep_ms(100)
```

---

## All Builtins

### I/O

| Builtin | Signature | Description |
|---|---|---|
| `@pf` | `@pf(fmt: str, ...)` | print formatted string to stdout |
| `@epf` | `@epf(fmt: str, ...)` | print formatted string to stderr |
| `@fmt` | `@fmt(fmt: str, ...) -> str` | format string and return as `str` |
| `@cin` | `@cin(prompt: str) -> str` | print prompt, read line from stdin |

#### String Interpolation

`@pf`, `@epf`, and `@fmt` use `{varname}` inline interpolation.

```
x: i32 = 42
z: f64 = 3.14159
@pf("value is {x} and {z:.3}\n")    # "value is 42 and 3.142"

s: str = @fmt("result: {x}")
```

Format specifiers:

| Spec | Meaning | Example output |
|---|---|---|
| `{v}` | default | `42`, `3.14`, `true` |
| `{v:d}` | decimal integer | `42` |
| `{v:x}` | hex lowercase | `2a` |
| `{v:X}` | hex uppercase | `2A` |
| `{v:o}` | octal | `52` |
| `{v:b}` | binary | `101010` |
| `{v:.N}` | float N decimal places | `3.142` |
| `{v:Nd}` | min width N, space-padded | `  42` |
| `{v:0Nd}` | min width N, zero-padded | `0042` |
| `{v:e}` | scientific notation | `4.200e+01` |

Use `{{` and `}}` to escape literal braces.

### Memory

| Builtin | Signature | Description |
|---|---|---|
| `@alo` | `@alo(T)` / `@alo(T, N)` | allocate one `T` or array of `N` on the heap |
| `@free` | `@free(p: *T)` | free a raw pointer |
| `@realo` | `@realo(p: *T, N: usize) -> *T` | resize an existing allocation |
| `@memcpy` | `@memcpy(dst: *T, src: *T, n: usize)` | copy `n` bytes, no overlap |
| `@memmove` | `@memmove(dst: *T, src: *T, n: usize)` | copy `n` bytes, overlap safe |
| `@memset` | `@memset(dst: *T, val: u8, n: usize)` | fill `n` bytes with `val` |
| `@zeroed` | `@zeroed(T) -> T` | zero-initialized value of type `T` |

### Type / Compile-time

| Builtin | Signature | Description |
|---|---|---|
| `@size` | `@size(T) -> usize` | size of type `T` in bytes |
| `@align` | `@align(T) -> usize` | alignment requirement of `T` in bytes |
| `@len` | `@len(arr) -> usize` | length of a fixed array or slice |
| `@T` | `@i32(v)`, `@str(v)`, etc. | convert/cast value to type `T` |
| `@bitcast` | `@bitcast(T, val) -> T` | reinterpret raw bits as `T` (same size required) |
| `@offsetof` | `@offsetof(T, field) -> usize` | byte offset of `field` in struct `T` |
| `@typeof` | `@typeof(expr) -> type` | compile-time type of an expression |
| `@debug` | `bool` | true in debug builds |
| `@release` | `bool` | true in release builds |
| `@os.linux` | `bool` | true when targeting Linux |
| `@os.windows` | `bool` | true when targeting Windows |
| `@os.macos` | `bool` | true when targeting macOS |
| `@arch.x86_64` | `bool` | true when targeting x86-64 |
| `@arch.arm64` | `bool` | true when targeting AArch64 |

### Math / Bit

| Builtin | Signature | Description |
|---|---|---|
| `@min` | `@min(a, b) -> T` | minimum of two values |
| `@max` | `@max(a, b) -> T` | maximum of two values |
| `@abs` | `@abs(x) -> T` | absolute value |
| `@clz` | `@clz(x: T) -> u32` | count leading zeros |
| `@ctz` | `@ctz(x: T) -> u32` | count trailing zeros |
| `@popcount` | `@popcount(x: T) -> u32` | count set bits |
| `@bswap` | `@bswap(x: T) -> T` | reverse byte order |
| `@sqrt` | `@sqrt(x: f64) -> f64` | square root |

### Overflow-checked Arithmetic

| Builtin | Signature | Description |
|---|---|---|
| `@checked_add` | `@checked_add(a, b: T) -> !T` | add, return `@err.overflow` on overflow |
| `@checked_sub` | `@checked_sub(a, b: T) -> !T` | subtract, return `@err.overflow` on overflow |
| `@checked_mul` | `@checked_mul(a, b: T) -> !T` | multiply, return `@err.overflow` on overflow |

### Self (impl blocks)

| Builtin | Description |
|---|---|
| `@slf` | mutable self pointer inside an `impl` method |
| `@imuslf` | immutable self pointer inside an `impl` method |

### Error

| Builtin | Description |
|---|---|
| `@err.ok` | success sentinel |
| `@err.fail` | generic failure |
| `@err.div_zero` | division by zero |
| `@err.null_deref` | null pointer dereference |
| `@err.out_of_bounds` | index out of bounds |
| `@err.overflow` | arithmetic overflow |
| `@err.invalid` | invalid argument |
| `@err.not_found` | resource not found |
| `@err.io` | I/O failure |
| `@err.oom` | out of memory |

### Control

| Builtin | Signature | Description |
|---|---|---|
| `@assert` | `@assert(cond: bool)` | debug assertion — panics if false (stripped in release) |
| `@assert` | `@assert(cond: bool, msg: str)` | same, with a message |
| `@panic` | `@panic(msg: str)` | print file/line/message and abort |
| `@exit` | `@exit(code: i32)` | exit process with code |
| `@unreachable` | `@unreachable()` | UB in release, panic in debug |
| `@todo` | `@todo()` | always panics — marks unimplemented code |

---

## Operator Precedence

From highest to lowest. Same-row operators are left-associative unless noted.

| Level | Operators | Notes |
|---|---|---|
| 1 | `()` `[]` `.` `.*` `.^` | postfix / member access |
| 2 | `not` `~` `-` `&` | unary (right-associative) |
| 3 | `*` `/` `%` | multiply, divide, modulo |
| 4 | `+` `-` | add, subtract |
| 5 | `<<` `>>` | bit shift |
| 6 | `<` `>` `<=` `>=` | relational |
| 7 | `==` `!=` | equality |
| 8 | `&` | bitwise AND (binary) |
| 9 | `^` | bitwise XOR (binary) |
| 10 | `\|` | bitwise OR |
| 11 | `and` | logical AND |
| 12 | `or` | logical OR |
| 13 | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | assignment (right-associative) |

**Disambiguation:**
- Unary `&` (address-of) vs binary `&` (bitwise AND) — resolved by position
- Unary `~` is bitwise NOT; `not` is logical NOT (bool only)
- `^` as type sigil (`^i32`) or dereference (`.^`) is not an operator — unambiguous by context

**Shift behavior:**
- `<<` always fills with zeros (logical left shift)
- `>>` on **unsigned** types fills with zeros (logical right shift — `lshr` in LLVM IR)
- `>>` on **signed** types fills with the sign bit (arithmetic right shift — `ashr` in LLVM IR)

---

## Naming Conventions

| Kind | Convention | Example |
|---|---|---|
| Functions | `snake_case` | `get_value()` |
| Types / Structs | `camelCase` | `myType`, `vec2` |
| Enum variants | `UPPER_CASE` | `direction.NORTH` |
| Constants (`imu`) | `UPPER_CASE` | `MAX_SIZE: usize : 1024` |
| Variables | `snake_case` | `my_var` |
| Modules / imports | `snake_case` | `import(math_utils = ...)` |

Combined:

```
get_myType()
parse_myResult()
```
