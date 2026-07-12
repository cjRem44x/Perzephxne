# Standard Library

The standard library ships with the compiler under `compiler/std`. Import modules with `import(alias = "std/module")`.

There is no external package resolver yet. `[deps]` in `przp.toml` is reserved for future packages; the modules below are the standard library that currently ships with the language.

Every module below has a runnable usage snippet. For complete projects using several modules together, see the `examples/` gallery at the repo root (also pointed to from [Getting Started](./getting-started.md#next-steps)).

## `std/io`

File I/O via libc. Open files are raw `*u8` handles (`FILE *`).

| Symbol | Description |
|---|---|
| `SEEK_SET`, `SEEK_CUR`, `SEEK_END` | seek constants |
| `open(path, mode)` | open a file; returns `null` on failure |
| `close(f)` | close file handle |
| `write_str(f, s)` | write a `str` to a file |
| `read_line(f)` | read one line into a heap string |
| `file_size(f)` | byte size of an open file |
| `print(s)` | print a string |
| `println(s)` | print a string plus newline |

```
import(io = "std/io")

f: *u8 = io.open("greeting.txt", "w")
io.write_str(f, "hello, przp\n")
io.close(f)

r: *u8 = io.open("greeting.txt", "r")
line: str = io.read_line(r)
io.print(line)   # "hello, przp" — read_line keeps the trailing newline
io.close(r)
```

## `std/str`

String helpers. Functions that produce new strings allocate.

| Function | Description |
|---|---|
| `len(s)` | string length |
| `eq(a, b)` | equality check |
| `concat(a, b)` | concatenate two strings |
| `slice(s, start, end)` | substring with exclusive end |
| `find(s, sub)` | first byte offset, or `s.len` if not found |
| `contains(s, sub)` | true if `sub` appears |
| `starts_with(s, prefix)` | prefix check |
| `ends_with(s, suffix)` | suffix check |
| `to_upper(s)` | ASCII uppercase copy |
| `to_lower(s)` | ASCII lowercase copy |
| `trim(s)` | strip leading/trailing whitespace |
| `repeat(s, n)` | repeat string `n` times |
| `index_of_char(s, ch)` | first byte offset for `ch`, or `s.len` |
| `replace(s, old, new_)` | replace first occurrence |
| `replace_all(s, old, new_)` | replace all non-overlapping occurrences |
| `split_count(s, delim)` | count split tokens |
| `split_next(s, delim, pos)` | read next token and advance `*pos` |
| `is_digit_str(s)` | all ASCII digits |
| `is_alpha_str(s)` | all ASCII letters |

```
import(str = "std/str")

s: str = "  Hello, World!  "
t: str = str.trim(s)
u: str = str.to_lower(t)
@pf("{u}\n")                             # "hello, world!"
@assert(str.starts_with(u, "hello"))
n: usize = str.split_count(u, ", ")      # 2 — "hello" and "world!"
```

## `std/math`

Math wrappers and constants.

| Symbol | Description |
|---|---|
| `PI`, `E` | mathematical constants |
| `sqrt(x)`, `sqrtf(x)` | square root |
| `pow(base, exp)`, `powf(base, exp)` | power |
| `fabs(x)`, `fabsf(x)` | floating absolute value |
| `floor(x)`, `ceil(x)`, `round(x)` | rounding |
| `fmod(x, y)` | floating remainder |
| `log(x)`, `log2(x)`, `log10(x)` | logarithms |
| `exp(x)` | exponential |
| `sin(x)`, `cos(x)`, `tan(x)` | trigonometry |
| `atan2(y, x)` | arc tangent |
| `abs(x)`, `absf(x)` | user-friendly float absolute value wrappers |
| `abs_i32(x)`, `abs_i64(x)` | integer absolute value |
| `min_i32(a, b)`, `max_i32(a, b)` | i32 min/max |
| `min_i64(a, b)`, `max_i64(a, b)` | i64 min/max |
| `clamp_i32(x, lo, hi)`, `clamp_f64(x, lo, hi)` | clamp |

```
import(m = "std/math")

d: f64 = m.sqrt(2.0) * m.sqrt(2.0)   # 2.0 (up to floating-point error)
c: i32 = m.clamp_i32(15, 0, 10)      # 10
@pf("{d} {c}\n")
```

## `std/os`

Process, environment, and directory helpers.

| Function | Description |
|---|---|
| `quit(code)` | exit the process |
| `getenv_str(name)` | read environment variable; empty string if unset |
| `run(cmd)` | run shell command and return status |
| `cwd()` | current working directory |
| `mkdir_dir(path)` | create a directory with mode `755` |
| `chdir_to(path)` | change current directory |
| `rmdir_dir(path)` | remove an empty directory |

```
import(os = "std/os")

dir: str = os.cwd()
os.mkdir_dir("scratch")
os.rmdir_dir("scratch")
@pf("running in {dir}\n")
```

## `std/file`

Path-based file utilities.

| Function | Description |
|---|---|
| `exists(path)` | true if path is accessible |
| `size(path)` | byte size, or `-1` on error |
| `read_all(path)` | read entire file into a heap string |
| `write_all(path, content)` | create/overwrite file |
| `append(path, content)` | append to file, creating if missing |
| `remove_file(path)` | delete file |
| `rename_file(old, new_path)` | rename/move file |
| `is_file(path)` | true if path exists and is a regular file |
| `is_dir(path)` | true if path exists and is a directory |
| `list(dir)` | `![]str` — dir-joined paths of `dir`'s entries (`.`/`..` skipped), ready to pass to `is_file`/`is_dir`/`read_all` |
| `mkdir_all(dir_path)` | create `dir_path` and any missing parent directories (`mkdir -p`); true if the directory exists afterward either way |
| `make(path)` | create `path`: a trailing `/` makes a directory (plus missing parents); otherwise an empty file (plus missing parent directories) — truncates an existing file, leaves an existing directory alone |
| `delete(path)` | remove the file or directory at `path`; a directory is removed recursively (contents deleted first) even if non-empty |

`list` builds its result with the [`@slice`](./builtins.md#memory) builtin, since a directory's entry count is only known at runtime, and a slice otherwise only ever comes from an array (whose size is a compile-time constant) decaying or being range-indexed.

```
import(file = "std/file")

file.write_all("notes.txt", "line one\n")
file.append("notes.txt", "line two\n")
content: str = file.read_all("notes.txt")
@pf("{content}")

entries, err: ![]str = file.list(".")
if err == 0 {
    is_f: bool = file.is_file("notes.txt")
    @pf("{@len(entries)} entries here, notes.txt is_file={is_f}\n")
}
```

```
import(file = "std/file")

file.make("build/assets/")           # mkdir -p — creates build/ and build/assets/
file.make("build/assets/readme.txt") # an empty file, parent dirs already there
file.delete("build")                 # gone — recursively, even though non-empty
```

## `std/fmt`

String formatting helpers. For interpolation-style formatting, use the `@fmt` builtin directly with named expressions, for example `@fmt("x={x}")`.

| Function | Description |
|---|---|
| `from_int(n)` | `i64` to decimal string |
| `from_uint(n)` | `u64` to decimal string |
| `from_float(f)` | `f64` to decimal string |
| `from_bool(b)` | `"true"` or `"false"` |
| `hex(n)` | lowercase hexadecimal |
| `hex_upper(n)` | uppercase hexadecimal |
| `fixed(f, decimals)` | fixed decimal places |
| `pad_left(s, width, ch)` | prepend `ch` to reach `width` |
| `pad_right(s, width, ch)` | append `ch` to reach `width` |
| `zero_pad(n, width)` | left-pad integer with `0` |

```
import(fmt = "std/fmt")

h: str = fmt.hex(255u64)          # "ff"
p: str = fmt.zero_pad(7, 3)       # "007"
f: str = fmt.fixed(3.14159, 2)    # "3.14"
@pf("{h} {p} {f}\n")
```

## `std/collections`

The current collection module provides a growable `Vec` of `i64` values.

| Symbol | Description |
|---|---|
| `Vec` | growable i64 array |
| `Vec.new()` | create empty vector |
| `Vec.free_vec(self)` | release heap memory |
| `Vec.push(self, val)` | append value |
| `Vec.get(self, i)` | get value at index; no bounds check |
| `Vec.set(self, i, val)` | overwrite value at index |
| `Vec.len_of(self)` | number of elements |
| `Vec.is_empty(self)` | true if empty |
| `Vec.pop(self)` | remove and return last value; `0` if empty |
| `Vec.clear(self)` | set length to `0` without freeing capacity |

```
import(vec = "std/collections")

v: vec.Vec = vec.Vec.new()
v.push(10)
v.push(20)
v.push(30)
@pf("{v.len_of()} {v.get(1)}\n")   # 3 20
v.free_vec()
```

## `std/atomic`

Atomic operations on `i64` values. Operations use sequentially consistent ordering.

| Function | Description |
|---|---|
| `load(ptr)` | atomic read |
| `store(ptr, val)` | atomic write |
| `add(ptr, val)` | add, return old value |
| `sub(ptr, val)` | subtract, return old value |
| `bit_and(ptr, val)` | bitwise AND, return old value |
| `bit_or(ptr, val)` | bitwise OR, return old value |
| `xor(ptr, val)` | bitwise XOR, return old value |
| `swap(ptr, val)` | exchange, return old value |
| `cas(ptr, expected, desired)` | compare-and-swap; true if swapped |
| `inc(ptr)` | add 1, return new value |
| `dec(ptr)` | subtract 1, return new value |

```
import(atomic = "std/atomic")

counter: i64 = 0
old: i64 = atomic.add(&counter, 5)
v: i64 = atomic.load(&counter)
@pf("{old} {v}\n")   # 0 5
```

## `std/sync`

Mutex and read-write lock wrappers over pthread types. The compiler links generated binaries with pthread support, so programs can import this module without extra linker flags.

| Symbol | Description |
|---|---|
| `Mutex` | mutex storage wrapper |
| `mutex_new()` | create initialized mutex |
| `lock(m)` | acquire mutex |
| `trylock(m)` | try to acquire mutex |
| `unlock(m)` | release mutex |
| `mutex_destroy(m)` | destroy mutex |
| `RwLock` | read-write lock storage wrapper |
| `rwlock_new()` | create initialized read-write lock |
| `rlock(rw)` | acquire reader lock |
| `tryrlock(rw)` | try reader lock |
| `wlock(rw)` | acquire writer lock |
| `trywlock(rw)` | try writer lock |
| `rwunlock(rw)` | release reader or writer lock |
| `rwlock_destroy(rw)` | destroy read-write lock |

```
import(sync = "std/sync")

mu: sync.Mutex = sync.mutex_new()
sync.lock(&mu)
defer sync.unlock(&mu)
# ... critical section ...
```

## `std/crypto`

Hashing and simple byte-buffer crypto helpers.

| Function | Description |
|---|---|
| `hash_djb2(s)` | djb2 non-cryptographic hash |
| `hash_fnv1a(s)` | FNV-1a 64-bit non-cryptographic hash |
| `xor_encrypt(data, len, key)` | XOR stream cipher in place |
| `sha256(data, len)` | SHA-256 digest as a 32-byte heap buffer |
| `sha256_hex(data, len)` | SHA-256 digest as lowercase hex string |

### High-Level Helpers

Pick an algorithm with the `HashKind` / `CipherKind` enums — today each has one member (`Sha256`, `XorSha`), leaving room to add more without changing call sites.

| Function | Description |
|---|---|
| `hash_pk(kind, plain)` | hash a password/key → lowercase hex digest string |
| `auth_hash(kind, plain, hashed)` | `true` if hashing `plain` reproduces `hashed` |
| `enc_file(kind, path, key)` | encrypt the file at `path` in place |
| `dec_file(kind, path, key)` | decrypt the file at `path` in place (symmetric with `enc_file`) |

```
import(crypto = "std/crypto")

# password hashing
h: str = crypto.hash_pk(crypto.HashKind.Sha256, "hunter2")
ok: bool = crypto.auth_hash(crypto.HashKind.Sha256, "hunter2", h)   # true

# file encryption — enc_file/dec_file use the same transform, so
# calling either one twice with the same key restores the original bytes
crypto.enc_file(crypto.CipherKind.XorSha, "secret.txt", "correct horse battery staple")
crypto.dec_file(crypto.CipherKind.XorSha, "secret.txt", "correct horse battery staple")
```

`CipherKind.XorSha` derives a keystream from `sha256(key || block_counter)` and XORs it against the file's bytes 32 bytes at a time — unlike a repeating-key XOR, the keystream never repeats within a file. It is not authenticated (no tamper detection) and has not been audited; treat it as a starting point for the standard library rather than a production cipher.
