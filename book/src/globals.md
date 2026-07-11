# Global Variables

Global variables are declared at the top level of a file (outside any function).

## Syntax

```
# mutable global — zero-initialized
count: i32 = 0

# immutable global (constant)
MAX_SIZE: usize : 1024

# initialized with an expression
PI: f64 : 3.14159265358979
```

The same mutability rules apply as for locals: `:` is immutable, `=` is mutable.

## Zero Initialization

Mutable globals not explicitly initialized are zero-initialized:

```
flag: bool    # false
ptr: *i32     # null
buf: [256]u8  # all zeros
```

## Immutable Globals (Constants)

Immutable globals must have a compile-time-constant initializer:

```
BUFSIZE: usize : 4096
MASK:    u32   : 0xFF00FF00
PREFIX:  str   : "api/v2/"
```

A constant initializer isn't limited to a single literal — struct and array literals count too, built recursively from other constants, and a bare function name is a valid constant for a fn-pointer-typed field (handy for building a fixed table of operations, like `std/graphics/gl`'s `GL_BACKEND`):

```
struct Vec2 { x: f32, y: f32 }
ORIGIN: Vec2 : Vec2{.x=0.0, .y=0.0}

struct Ops { double: fn(i32) -> i32 }
fn doubler(x: i32) -> i32 { ret x * 2 }
MY_OPS: Ops : Ops{.double = doubler}
```

Anything else — a function call, a reference to another variable, an arithmetic expression on a non-constant — isn't a compile-time constant and is a compile error rather than a silently zero-initialized global.

## Mutable Globals

Mutable globals persist for the lifetime of the process:

```
request_count: u64 = 0

fn handle_request() {
    request_count += 1
    @pf("request #{request_count}\n")
}
```

## Thread Safety

Global variables are **not** automatically thread-safe. Concurrent access to mutable globals requires external synchronization.

## Extern Globals

To reference a global defined in C or another object file:

```
extern environ: **u8    # char **environ from libc

first: *u8 = environ.*
```

The symbol is declared but not defined — the linker resolves it. Extern globals are always mutable.

## `@args`

The program's command-line arguments are available via a builtin — no global declaration needed:

```
fn main() {
    args: []str = @args      # slice of all arguments including argv[0]
    @pf("program: {args[0]}\n")
}
```
