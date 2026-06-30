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

## `std/file`

Path-based file utilities — no open handle needed for common operations.

```
import(file = "std/file")

ok: bool = file.exists("data.txt")
sz: i64  = file.size("data.txt")

content: str = file.read_all("data.txt")
file.write_all("out.txt", content)
file.append("log.txt", "entry\n")

file.remove_file("tmp.txt")
file.rename_file("old.txt", "new.txt")
```

| Function | Description |
|---|---|
| `file.exists(path)` | true if the path exists and is accessible |
| `file.size(path)` | byte size, or -1 on error |
| `file.read_all(path)` | read entire file into a heap-allocated `str` |
| `file.write_all(path, content)` | create/overwrite with `content`; returns `bool` |
| `file.append(path, content)` | append `content` (creates if missing); returns `bool` |
| `file.remove_file(path)` | delete the file; returns `bool` |
| `file.rename_file(old, new)` | rename/move a file; returns `bool` |

## `std/crypto`

Non-cryptographic and cryptographic hashing, XOR stream cipher, and SHA-256.

```
import(crypto = "std/crypto")

# Fast non-cryptographic hashes (good for hash tables)
h1: u64 = crypto.hash_djb2("hello")
h2: u64 = crypto.hash_fnv1a("hello")

# XOR stream cipher — call twice to decrypt
buf: [16]u8 = undef
# ... fill buf ...
crypto.xor_encrypt(&buf[0], 16, "secretkey")

# SHA-256 — returns a 32-byte heap buffer
digest: *u8 = crypto.sha256(data_ptr, data_len)

# SHA-256 as a lowercase hex string (64 characters)
hex: str = crypto.sha256_hex(data_ptr, data_len)
```

| Function | Signature | Description |
|---|---|---|
| `hash_djb2(s)` | `fn(str) -> u64` | djb2 non-crypto hash |
| `hash_fnv1a(s)` | `fn(str) -> u64` | FNV-1a 64-bit non-crypto hash |
| `xor_encrypt(data, len, key)` | `fn(*u8, usize, str)` | XOR stream cipher in-place |
| `sha256(data, len)` | `fn(*u8, usize) -> *u8` | SHA-256 digest (32 bytes, heap) |
| `sha256_hex(data, len)` | `fn(*u8, usize) -> str` | SHA-256 as 64-char hex string |

## `std/collections`

A growable array of `i64` values.

| Method | Description |
|---|---|
| `Vec.new()` | create empty Vec |
| `Vec.push(self, val)` | append an i64 value |
| `Vec.get(self, i)` | get element at index |
| `Vec.free_vec(self)` | release heap memory |

## `std/fmt`

String formatting utilities. For interpolation-style formatting use the `@fmt` builtin directly (`@fmt("x={} y={}", x, y)`). This module provides type-to-string conversion and padding.

```
import(fmt = "std/fmt")

h: str  = fmt.hex(255u64)         # "ff"
hu: str = fmt.hex_upper(255u64)   # "FF"
fi: str = fmt.fixed(3.14159, 2)   # "3.14"
pl: str = fmt.pad_left("hi",  6, 32u8)  # "    hi"
pr: str = fmt.pad_right("hi", 6, 46u8)  # "hi...."
zp: str = fmt.zero_pad(7, 3)      # "007"
```

| Function | Description |
|---|---|
| `fmt.from_int(n)` | `i64` → decimal string |
| `fmt.from_uint(n)` | `u64` → decimal string |
| `fmt.from_float(f)` | `f64` → decimal string |
| `fmt.from_bool(b)` | `"true"` or `"false"` |
| `fmt.hex(n)` | `u64` → lowercase hex (no prefix) |
| `fmt.hex_upper(n)` | `u64` → uppercase hex |
| `fmt.fixed(f, decimals)` | `f64` with fixed decimal places |
| `fmt.pad_left(s, width, ch)` | prepend `ch` to reach `width` |
| `fmt.pad_right(s, width, ch)` | append `ch` to reach `width` |
| `fmt.zero_pad(n, width)` | left-pad integer with `'0'` |

## `std/str` additions

Beyond the core functions, `std/str` also provides:

| Function | Description |
|---|---|
| `s.replace(s, old, new_)` | replace first occurrence of `old` with `new_` |
| `s.replace_all(s, old, new_)` | replace all non-overlapping occurrences |
| `s.split_count(s, delim)` | count tokens when splitting by `delim` |
| `s.split_next(s, delim, pos)` | extract next token, advance `pos` past delimiter |
| `s.index_of_char(s, ch)` | byte offset of first `ch`, or `s.len` if absent |
| `s.is_digit_str(s)` | true if all characters are ASCII digits |
| `s.is_alpha_str(s)` | true if all characters are ASCII letters |

```
import(s = "std/str")

# split "a,b,c" into tokens
cnt: usize = s.split_count("a,b,c", ",")   # 3
pos: usize = 0
while pos < 5 {
    tok: str = s.split_next("a,b,c", ",", &pos)
    if tok.len == 0 { break }
    @pf("%s\n", tok.data)
}

replaced: str = s.replace_all("aabbaa", "aa", "X")  # "XbbX"
```

## `std/os` additions

| Function | Description |
|---|---|
| `os.cwd()` | current working directory as a `str` |
| `os.mkdir_dir(path)` | create directory (mode 755); `true` on success |
| `os.chdir_to(path)` | change working directory; `true` on success |
| `os.rmdir_dir(path)` | remove empty directory; `true` on success |

## `std/atomic`

Lock-free atomic operations on `i64` values. All operations use sequentially-consistent ordering.

```
import(atomic = "std/atomic")

counter: i64 = 0

atomic.add(&counter, 1)        # add 1, return old value
atomic.sub(&counter, 1)        # subtract 1, return old value
v: i64 = atomic.load(&counter) # atomic read
atomic.store(&counter, 0)      # atomic write
ok: bool = atomic.cas(&counter, 0, 42)  # compare-and-swap

atomic.inc(&counter)   # add 1, return new value
atomic.dec(&counter)   # subtract 1, return new value
```

| Function | Description |
|---|---|
| `atomic.load(ptr)` | atomically read `*ptr` |
| `atomic.store(ptr, val)` | atomically write `val` to `*ptr` |
| `atomic.add(ptr, val)` | add `val`, return **old** value |
| `atomic.sub(ptr, val)` | subtract `val`, return **old** value |
| `atomic.bit_and(ptr, val)` | bitwise AND, return old value |
| `atomic.bit_or(ptr, val)` | bitwise OR, return old value |
| `atomic.xor(ptr, val)` | bitwise XOR, return old value |
| `atomic.swap(ptr, val)` | swap with `val`, return old value |
| `atomic.cas(ptr, expected, desired)` | compare-and-swap; `true` if swapped |
| `atomic.inc(ptr)` | add 1, return **new** value |
| `atomic.dec(ptr)` | subtract 1, return **new** value |

## `std/sync`

Mutex and RwLock via pthreads. Link with `-lpthread`.

```
import(sync = "std/sync")

mu: sync.Mutex = sync.mutex_new()
sync.lock(&mu)
defer sync.unlock(&mu)
# ... critical section ...

rw: sync.RwLock = sync.rwlock_new()
sync.rlock(&rw)    # shared reader lock
sync.rwunlock(&rw)
sync.wlock(&rw)    # exclusive writer lock
sync.rwunlock(&rw)
```

| Function | Description |
|---|---|
| `sync.mutex_new()` | create an initialized `Mutex` |
| `sync.lock(m)` | acquire (blocks until available) |
| `sync.trylock(m)` | try to acquire; `true` if taken |
| `sync.unlock(m)` | release |
| `sync.mutex_destroy(m)` | free pthread resources |
| `sync.rwlock_new()` | create an initialized `RwLock` |
| `sync.rlock(rw)` | acquire shared reader lock |
| `sync.tryrlock(rw)` | try reader lock; `true` if taken |
| `sync.wlock(rw)` | acquire exclusive writer lock |
| `sync.trywlock(rw)` | try writer lock; `true` if taken |
| `sync.rwunlock(rw)` | release reader or writer lock |
| `sync.rwlock_destroy(rw)` | free pthread resources |

## `std/net` *(planned)*

TCP/UDP sockets and HTTP client. Not yet implemented.
