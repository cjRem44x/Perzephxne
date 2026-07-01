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

## `@args`

The program's command-line arguments are available via a builtin — no global declaration needed:

```
fn main() {
    args: []str = @args      # slice of all arguments including argv[0]
    @pf("program: {args[0]}\n")
}
```
