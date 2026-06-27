# Standard Library

The standard library lives under `std/`. Import with `import(alias = "std/module")`.

## `std/io`

File I/O via libc. Open files are raw `*u8` handles (libc `FILE *`).

```
import(io = "std/io")

f: *u8 = io.open("data.txt", "r")
if f == null { @panic("open failed") }
defer io.close(f)

line: str = io.read_line(f)
@pf("{line}\n")

io.write_str(f, "hello\n")
sz: i64 = io.file_size(f)
```

| Function | Description |
|---|---|
| `io.open(path, mode)` | open a file (`"r"`, `"w"`, `"a"`, etc.) — `null` on failure |
| `io.close(f)` | close the file handle |
| `io.write_str(f, s)` | write a `str` to a file |
| `io.read_line(f)` | read one line (heap-allocated) |
| `io.file_size(f)` | byte size of an open file |

Constants: `io.SEEK_SET`, `io.SEEK_CUR`, `io.SEEK_END`

## `std/str`

String operations. Functions that return new strings allocate on the heap.

```
import(s = "std/str")

greeting: str = "Hello, World!"
upper: str  = s.to_upper(greeting)
lower: str  = s.to_lower(greeting)
trimmed: str = s.trim("  hi  ")
idx: usize   = s.find(greeting, "World")  # returns s.len if not found
ok: bool     = s.contains(greeting, "Hello")
pfx: bool    = s.starts_with(greeting, "Hello")
sfx: bool    = s.ends_with(greeting, "!")
cat: str     = s.concat("foo", "bar")
sub: str     = s.slice(greeting, 7, 12)   # "World"
rep: str     = s.repeat("ab", 3)          # "ababab"
eq:  bool    = s.eq("abc", "abc")
```

| Function | Description |
|---|---|
| `s.len(s)` | string length |
| `s.eq(a, b)` | equality check |
| `s.concat(a, b)` | concatenate two strings |
| `s.slice(s, start, end)` | substring view (exclusive end) |
| `s.find(s, sub)` | byte offset of first match, or `s.len` |
| `s.contains(s, sub)` | true if sub appears anywhere |
| `s.starts_with(s, pfx)` | prefix check |
| `s.ends_with(s, sfx)` | suffix check |
| `s.to_upper(s)` | ASCII uppercase copy |
| `s.to_lower(s)` | ASCII lowercase copy |
| `s.trim(s)` | strip leading/trailing whitespace |
| `s.repeat(s, n)` | repeat string n times |

## `std/math`

Mathematical functions via libm.

```
import(math = "std/math")

y: f64 = math.sin(math.PI / 2.0)    # 1.0
z: f64 = math.sqrt(2.0)
r: f64 = math.pow(2.0, 10.0)        # 1024.0
n: i32 = math.abs_i32(-5)           # 5
c: f64 = math.clamp_f64(1.5, 0.0, 1.0)  # 1.0
```

Constants: `math.PI`, `math.E`

| Function | Description |
|---|---|
| `math.sqrt(x)` | square root |
| `math.pow(base, exp)` | power |
| `math.log(x)` | natural logarithm |
| `math.sin(x)`, `math.cos(x)`, `math.tan(x)` | trig |
| `math.atan2(y, x)` | arc tangent |
| `math.floor(x)`, `math.ceil(x)`, `math.round(x)` | rounding |
| `math.fmod(x, y)` | floating-point remainder |
| `math.abs_i32(x)`, `math.abs_i64(x)` | integer absolute value |
| `math.min_i32(a, b)`, `math.max_i32(a, b)` | integer min/max |
| `math.min_i64(a, b)`, `math.max_i64(a, b)` | i64 min/max |
| `math.clamp_i32(x, lo, hi)`, `math.clamp_f64(x, lo, hi)` | clamp |

## `std/os`

Process and environment operations.

```
import(os = "std/os")

home: str = os.getenv_str("HOME")
os.run("ls -la")
os.quit(0)
```

| Function | Description |
|---|---|
| `os.getenv_str(name)` | read environment variable (empty str if unset) |
| `os.run(cmd)` | run a shell command, return exit status |
| `os.quit(code)` | exit the process |

## `std/collections`

A growable array of `i64` values.

```
import(c = "std/collections")

v: c.Vec = c.Vec.new()
c.Vec.push(&v, 10)
c.Vec.push(&v, 20)
c.Vec.push(&v, 30)

@pf("len=%d\n", v.len)
c.Vec.free_vec(&v)
```

| Method | Description |
|---|---|
| `Vec.new()` | create empty Vec |
| `Vec.push(self, val)` | append an i64 value |
| `Vec.get(self, i)` | get element at index |
| `Vec.free_vec(self)` | release heap memory |
