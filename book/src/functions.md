# Functions

## Basic Syntax

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

## Failable Functions

Use `!T` to indicate the function might return an error:

```
fn parse_int(s: str) -> !i32 {
    # returns an error value on failure
}

n: !i32 = parse_int("abc")
if n == @err { @pf("parse failed\n") }
```

Use the `?` operator to propagate errors up:

```
fn load_config() -> !Config {
    text: str    = read_file("config.toml")?
    parsed: Config = parse_toml(text)?
    ret parsed
}
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

## Function Pointers vs Closures

Bare function references (like `double` above) are plain pointers — zero overhead.

Closures that capture the environment are not yet supported (planned for a future version).

## Extern Functions

To call C functions, declare them with `extern`:

```
extern fn printf(fmt: *u8, ...) -> i32
extern fn malloc(size: usize) -> *u8
extern fn free(p: *u8)
```

Variadic `...` is allowed in extern declarations only.

## Inline and No-Inline Hints

```
@inline fn fast_path(x: i32) -> i32 { ret x * 2 }
@noinline fn slow_path(x: i32) -> i32 { ... }
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
