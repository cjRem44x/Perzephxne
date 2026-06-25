# Standard Library

The standard library is organized into modules under the `std/` prefix.

## `std/io`

File I/O and printing.

```
import "std/io"

io.print("hello\n")

f: !io.File = io.open("data.txt", io.READ)
defer f.close()

buf: [1024]u8 = undef
n: usize = f.read(&buf[0], @sizeof(buf))
```

Key types and functions:

| Name | Description |
|---|---|
| `io.File` | file handle |
| `io.open(path, mode)` | open a file |
| `io.stdin` | standard input |
| `io.stdout` | standard output |
| `io.stderr` | standard error |
| `io.print(s)` | write string to stdout |
| `io.read_line()` | read a line from stdin |

## `std/mem`

Memory allocation.

```
import "std/mem"

p: *i32 = mem.alloc(i32)
p.* = 42
mem.free(p)

buf: []u8 = mem.alloc_slice(u8, 1024)
defer mem.free_slice(buf)

mem.copy(dst, src, n)
mem.set(dst, 0, n)
```

## `std/str`

String operations.

```
import "std/str"

s: str = "hello world"
upper: str    = str.upper(s)
parts: []str  = str.split(s, " ")
joined: str   = str.join(parts, ", ")
idx: !usize   = str.find(s, "world")
has: bool     = str.contains(s, "hello")
trimmed: str  = str.trim(s)
```

## `std/math`

Mathematical functions.

```
import "std/math"

y: f64 = math.sin(math.PI / 2.0)
z: f64 = math.sqrt(2.0)
r: f64 = math.pow(2.0, 10.0)
n: i32 = math.abs(-5)
```

Constants: `math.PI`, `math.E`, `math.TAU`, `math.INF`, `math.NAN`

## `std/os`

Operating system interface.

```
import "std/os"

home: !str = os.env("HOME")
os.exit(0)

cwd: str = os.getcwd()
os.chdir("/tmp")

files: ![]str = os.listdir(".")
exists: bool  = os.path_exists("./data")
```

## `std/box`

Smart pointers.

```
import "std/box"

b: box.Box[i32] = box.Box.new(42)
@pf("{b.get().*}\n")
# freed when b goes out of scope

rc: box.Rc[str] = box.Rc.new("shared")
rc2: box.Rc[str] = rc.clone()
# freed when both rc and rc2 drop
```

## `std/atomic`

Atomic integer types for lock-free programming.

```
import "std/atomic"

counter: atomic.I32 = atomic.I32.new(0)
counter.fetch_add(1)
v: i32 = counter.load()
```

## `std/sync`

Mutex, RwLock, and channels.

```
import "std/sync"

mu: sync.Mutex = sync.Mutex.new()
mu.lock()
defer mu.unlock()
# ... critical section ...
```

## `std/fmt`

String formatting without printing.

```
import "std/fmt"

s: str = fmt.sprintf("x={} y={}", x, y)
```

## `std/net` *(planned)*

TCP/UDP sockets and HTTP client. Not yet implemented.
