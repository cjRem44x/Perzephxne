# Error Handling

Errors in Perzephxne are values — not exceptions. There are no `try/catch` blocks.

## Compiler Diagnostics

Compiler diagnostics include the source file, line, column, the offending source line, and a caret. Import failures are reported at the `import(...)` declaration and include the resolved path that the compiler attempted to load.

## Failable Return Type `!T`

A function that can fail returns `!T`. The caller receives a struct of `{ value: T, err: i32 }`. A zero error code means success; non-zero means failure.

```
fn divide(a: i32, b: i32) -> !i32 {
    if b == 0 { ret @err(1) }
    ret @ok(a / b)
}
```

## Returning Success or Failure

Use `@ok(val)` to return a successful value and `@err(code)` to return an error:

```
fn parse_port(s: str) -> !i32 {
    val, err: !i32 = @i32(s)
    if err != 0 { ret @err(1) }          # not a number
    if val < 1 || val > 65535 { ret @err(2) }
    ret @ok(val)
}
```

## Checking the Result

Use failable destructuring to separate the value and error flag:

```
port, err: !i32 = parse_port("8080")
if err != 0 {
    @pf("bad port (err=%d)\n", err)
} else {
    @pf("listening on %d\n", port)
}
```

When you only care about the value and want to ignore errors, assign to a plain type — the value field is extracted automatically (zero on failure):

```
n: i32 = parse_port("8080")   # err flag discarded
```

## Checked Arithmetic

`@checked_add`, `@checked_sub`, and `@checked_mul` return `!T` with the overflow flag as the error:

```
val, err: !i32 = @checked_add(x, y)
if err != 0 { @panic("integer overflow") }
```

## String-to-Number Casts

All `@T(str)` casts return `!T` — error is 1 if the string isn't a valid number:

```
val, err: !i64 = @i64("9876543210")    # val=9876543210 err=0
bad, err2: !f64 = @f64("hello")        # bad=0.0        err2=1
```

## Panics

For unrecoverable situations, use `@panic`:

```
fn get(s: []i32, i: usize) -> i32 {
    if i >= @len(s) { @panic("index out of bounds") }
    ret s[i]
}
```

`@panic` prints the message and terminates the process immediately. Panics are **not** for expected error conditions — use `!T` for those.

## `@assert`

```
@assert(x > 0, "x must be positive")
```

Active in debug builds; stripped in release. Use for invariants you know hold but want verified during development.

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

In debug builds `@unreachable()` panics if reached. In release it is a hint to the optimizer.
