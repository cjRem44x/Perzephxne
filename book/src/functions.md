# Functions

## Declaration

```
fn add(a: i32, b: i32) -> i32 {
    ret a + b
}
```

## No Return Value

Omit `->` when the function returns nothing:

```
fn greet(name: str) {
    @pf("Hello, {name}!\n")
}
```

## Inferred Return Type

Omit `->` with a `ret` statement and the compiler infers the return type from the returned expression. The inferred type is the exact type of the `ret` expression — no widening is applied.

```
fn double(x: i64) {
    ret x * 2      # inferred -> i64
}

fn scale(x: f32, factor: f32) {
    ret x * factor # inferred -> f32
}
```

This is equivalent to annotating the return type explicitly. Use explicit annotations when the function is part of a public API or when there are multiple `ret` paths that might resolve to different types.

## Multiple Return Values

```
fn divmod(a: i32, b: i32) -> (i32, i32) {
    ret (a / b, a % b)
}

q, r: i32 = divmod(17, 5)
@pf("{q} remainder {r}\n")
```

The result can also be kept whole as a [tuple](./variables-types.md#tuples) and accessed by position:

```
t: (i32, i32) = divmod(17, 5)
@pf("{t.0} remainder {t.1}\n")

u := divmod(9, 4)     # inferred tuple type works too
```

## Named Return Values

```
fn stats(s: []f64) -> (mean: f64, max: f64) {
    sum: f64 = 0.0
    mx: f64  = s[0]
    for v => s {
        sum += v
        if v > mx { mx = v }
    }
    ret (sum / @f64(@len(s)), mx)
}
```

## Failable Return

Use `!T` as the return type to indicate the function might return an error. Return `@ok(val)` on success or `@err(code)` on failure:

```
fn parse_port(s: str) -> !i32 {
    val, err: !i32 = @i32(s)
    if err != 0 { ret @err(1) }
    if val < 1 || val > 65535 { ret @err(2) }
    ret @ok(val)
}
```

Callers destructure the result:

```
port, err: !i32 = parse_port("8080")
if err != 0 { @pf("invalid port (err=%d)\n", err) }
```

## Entry Point

```
fn main() {
    # program starts here
}

# or with an exit code
fn main() -> i32 {
    ret 0
}
```

## First-Class Functions

Functions are values. The type of a function is written `fn(params) -> ret`:

```
op: fn(i32, i32) -> i32 = add
result: i32 = op(3, 4)
```

Passing functions to other functions:

```
fn apply(f: fn(i32) -> i32, x: i32) -> i32 {
    ret f(x)
}

fn double(x: i32) -> i32 { ret x * 2 }
y: i32 = apply(double, 5)   # 10
```

## Function Pointers

Bare function references (like `double` above) are plain pointers — zero overhead.

Closures that capture the environment are not yet supported (planned for a future version).

## Extern / FFI

To call C functions, declare them with `extern`:

```
extern fn printf(fmt: *u8, ...) -> i32
extern fn malloc(size: usize) -> *u8
extern fn free(p: *u8)
```

Variadic `...` is allowed in extern declarations only.

C globals and opaque types can be declared the same way:

```
extern environ: **u8       # global defined in libc
extern struct FILE_opaque  # opaque type — use via pointers only
extern fn fopen(path: *u8, mode: *u8) -> *FILE_opaque
```

## Inline

Use `inline fn` to hint that a function should be inlined at call sites:

```
inline fn fast_path(x: i32) -> i32 { ret x * 2 }
```

## Recursion

Recursion works as expected:

```
fn fib(n: i32) -> i32 {
    if n <= 1 { ret n }
    ret fib(n - 1) + fib(n - 2)
}
```

Mutual recursion is supported because `przp` does a two-pass compilation — all top-level names are visible regardless of source order.
