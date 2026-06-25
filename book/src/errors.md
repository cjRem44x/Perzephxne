# Error Handling

Errors in Perzephxne are values — not exceptions. There are no `try/catch` blocks.

## Failable Return Type `!T`

A function that can fail returns `!T`. The `!` wrapper holds either a success value of type `T` or an error.

```
fn open_file(path: str) -> !str { ... }
```

## Checking for Errors

```
result: !str = open_file("data.txt")

if result == @err {
    @pf("failed: {result.err_msg}\n")
} else {
    @pf("{result}\n")
}
```

## The `?` Propagation Operator

`?` unwraps a `!T` result — on error it immediately returns the error to the caller.

```
fn load() -> !Config {
    text: str    = read_file("cfg.toml")?
    cfg:  Config = parse_toml(text)?
    ret cfg
}
```

Equivalent to:

```
fn load() -> !Config {
    text_r: !str = read_file("cfg.toml")
    if text_r == @err { ret text_r.err }
    text: str = text_r

    cfg_r: !Config = parse_toml(text)
    if cfg_r == @err { ret cfg_r.err }
    ret cfg_r
}
```

## Returning an Error

```
fn divide(a: i32, b: i32) -> !i32 {
    if b == 0 { ret @err("division by zero") }
    ret a / b
}
```

## Custom Error Types

Define your own error enum and use it as the payload:

```
enum IoError {
    NotFound,
    PermissionDenied,
    UnexpectedEof,
}

fn read(path: str) -> !str | IoError { ... }
```

## Panics

For unrecoverable situations, use `@panic`:

```
fn get(s: []i32, i: usize) -> i32 {
    if i >= @len(s) { @panic("index out of bounds") }
    ret s[i]
}
```

`@panic` prints the message and the call stack, then terminates the process immediately.

Panics are **not** for expected error conditions — use `!T` for those.

## `@assert`

```
@assert(x > 0, "x must be positive")
```

Active in debug builds; stripped in release builds. Use for invariants you know hold but want verified during development.

## `@unreachable`

Mark code paths that should never execute:

```
when color {
    Color.Red   => ret 0,
    Color.Green => ret 1,
    Color.Blue  => ret 2,
    _           => @unreachable(),
}
```

In debug builds `@unreachable()` panics if reached. In release it is a hint to the optimizer (undefined behavior if reached).
