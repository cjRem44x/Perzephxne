# Standard Library

The standard library ships with the compiler under `compiler/std`. Import modules with `import(alias = "std/module")`.

There is no external package resolver yet. `[deps]` in `przp.toml` is reserved for future packages; the modules below are the standard library that currently ships with the language.

Every module below has a runnable usage snippet. For complete projects using several modules together, see the `examples/` gallery at the repo root (also pointed to from [Getting Started](./getting-started.md#next-steps)).

## `std/io`

File I/O via libc. Open files are raw `*u8` handles (`FILE *`). Naming mirrors
C's stdio, just shorter: `r`/`w` picks the direction, the suffix picks what's
being moved — `chr` (one char), `str` (one line), `raw` (n arbitrary bytes),
or a bit width (`8`/`16`/`32`/`64`, with `le`/`be` on the multi-byte ones
since a single byte has no order to pick).

| Symbol | Description |
|---|---|
| `SEEK_SET`, `SEEK_CUR`, `SEEK_END` | seek constants |
| `open(path, mode)` | open a file; returns `null` on failure |
| `close(f)` | close file handle |
| `eof(f)` | true once `f` has been read past its end |
| `err(f)` | true if `f`'s error indicator is set |
| `flush(f)` | push `f`'s buffered writes out now |
| `seek(f, off, whence)` | reposition `f`'s read/write offset |
| `tell(f)` | `f`'s current read/write offset |
| `fsize(f)` | byte size of an open file |
| `rchr(f)` | read one byte as `fgetc` does: 0-255, or `-1` at EOF/error |
| `wchr(f, c)` | write one `char`; returns it, or `-1` on error |
| `rstr(f)` | read one line into a heap string |
| `wstr(f, s)` | write a `str` to a file, no newline added |
| `rraw(f, buf, n)` | read up to `n` bytes into `buf`; returns count read |
| `wraw(f, buf, n)` | write `n` bytes from `buf`; returns count written |
| `r8`/`w8(f, v)` | read/write a single raw byte (`u8`) |
| `r16le`/`w16le`, `r32le`/`w32le`, `r64le`/`w64le` | fixed-width read/write, little-endian |
| `r16be`/`w16be`, `r32be`/`w32be`, `r64be`/`w64be` | fixed-width read/write, big-endian |
| `print(s)` | print a string |
| `println(s)` | print a string plus newline |

The fixed-width readers return `0` on a short read (same sentinel-on-failure
tradeoff `std/file.stat_mode` already makes) — check `eof(f)`/`err(f)`
afterward if a genuine zero-valued byte must be told apart from a failed read.
`rchr` doesn't need that: like C's `fgetc`, it returns `i32` specifically so
`-1` (EOF) can't collide with any real byte value 0-255.

```
import(io = "std/io")

f: *u8 = io.open("greeting.txt", "w")
io.wchr(f, 'h')
io.wstr(f, "ello, przp\n")
io.close(f)

r: *u8 = io.open("greeting.txt", "r")
line: str = io.rstr(r)
io.print(line)   # "hello, przp" — rstr keeps the trailing newline
io.close(r)

# fixed-width binary, either byte order
b: *u8 = io.open("packet.bin", "wb")
io.w32be(b, 0x01020304u32)   # network byte order: bytes 01 02 03 04
io.w32le(b, 0x01020304u32)   # native byte order:   bytes 04 03 02 01
io.close(b)
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
| `monotonic_ms()` | current monotonic clock reading, in ms — only meaningful as a difference between two calls |
| `sleep_ms(ms)` | block the calling thread for at least `ms` milliseconds, yielding the CPU to the scheduler |
| `waste_ms(ms)` | spend at least `ms` milliseconds busy-looping on the calling thread — pegs the CPU instead of yielding |

```
import(os = "std/os")

dir: str = os.cwd()
os.mkdir_dir("scratch")
os.rmdir_dir("scratch")
@pf("running in {dir}\n")

t0: i64 = os.monotonic_ms()
os.sleep_ms(50)             # yields — near-zero CPU time used
@pf("slept {os.monotonic_ms() - t0}ms\n")
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
| `parent(path)` | the portion of `path` before its last `/`, or an empty str if `path` has no `/` |
| `name(path)` | the portion of `path` after its last `/` (its own file/dir name), or the whole `path` if it has no `/` |
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

```
import(file = "std/file")

p: str = file.parent("photos/2024/a.png") # "photos/2024"
n: str = file.name("photos/2024/a.png")   # "a.png"
```

## `std/zip`

Create and extract `.zip` archives, via [libzip](https://libzip.org) — an extern FFI binding, not a hand-rolled format implementation. Needs `-lzip` at link time: add `link = ["zip"]` to `[build]` in your project's `przp.toml` (see [Build System](./build-system.md)); `sac` users pass `-lzip` directly on the command line.

| Function | Description |
|---|---|
| `zip_path(out_zip, src_path)` | create/overwrite the archive at `out_zip` from `src_path` — a single file, or an entire directory tree (recursively) |
| `unzip_path(zip_file, dest_dir)` | extract every entry of `zip_file` into `dest_dir` (created if missing), recreating each entry's own directory structure |

Zipping a directory names its entries relative to the directory's own parent, so zipping `photos` produces entries like `photos/a.png` — the same convention `zip -r archive.zip photos` uses, and what `unzip_path` expects to recreate the tree under the destination.

```
import(zip = "std/zip")

zip.zip_path("backup.zip", "photos")       # a whole directory tree
zip.unzip_path("backup.zip", "restored")   # -> restored/photos/...

zip.zip_path("single.zip", "notes.txt")    # a single file
zip.unzip_path("single.zip", "out")        # -> out/notes.txt
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

Pick an algorithm with the `hshknd` / `cphrknd` enums:

| `hshknd` | Description |
|---|---|
| `Sha256` | fast, unsalted — fine for checksums, **not** for password storage (rainbow-tableable, GPU-crackable) |
| `Argon2id` | [RFC 9106](https://www.rfc-editor.org/rfc/rfc9106) — memory-hard, salted; use this for real passwords |

| `cphrknd` | Description |
|---|---|
| `XorSha` | fast, but unauthenticated and reuses its keystream from byte 0 on every call — not suitable for real confidentiality |
| `ChaCha20Poly1305` | [RFC 8439](https://www.rfc-editor.org/rfc/rfc8439) — authenticated (AEAD), random nonce per call; use this for real encryption |

| Function | Description |
|---|---|
| `hash_pk(kind, plain)` | hash a password/key → a string safe to store directly |
| `auth_hash(kind, plain, hashed)` | `true` if hashing `plain` reproduces `hashed` |
| `enc_file(kind, path, key)` | encrypt the file at `path` in place |
| `dec_file(kind, path, key)` | decrypt the file at `path` in place |

```
import(crypto = "std/crypto")

# password hashing — Argon2id embeds a fresh random salt in its output,
# so two calls with the same password produce two different strings
h1: str = crypto.hash_pk(crypto.hshknd.Argon2id, "hunter2")
h2: str = crypto.hash_pk(crypto.hshknd.Argon2id, "hunter2")
different: bool = h1 != h2                                   # true
ok: bool = crypto.auth_hash(crypto.hshknd.Argon2id, "hunter2", h1)   # true

# authenticated file encryption — dec_file returns false (and leaves the
# file untouched) on a wrong key or a tampered/corrupted ciphertext
crypto.enc_file(crypto.cphrknd.ChaCha20Poly1305, "secret.txt", "correct horse battery staple")
verified: bool = crypto.dec_file(crypto.cphrknd.ChaCha20Poly1305, "secret.txt", "correct horse battery staple")
```

`Sha256`/`XorSha` are the original, toy-grade helpers: `Sha256` hashes the input directly with no salt or work factor, and `XorSha` derives a keystream from `sha256(key || block_counter)` that's never authenticated and always restarts from the same counter on every call (reusing a key across two files leaks the XOR of their plaintexts). Neither has been audited; treat them as a starting point rather than something to build real security on.

`Argon2id`/`ChaCha20Poly1305` are real, standard algorithms, implemented from their RFCs and verified byte-for-byte against the reference C implementation of Argon2 and against RFC 8439's own test vectors — but this implementation itself still hasn't been independently audited, so treat it the way you'd treat any unaudited crypto code: appropriate for learning, prototypes, and low-stakes use, not yet something to bet a production security boundary on without your own review. `Argon2id` runs at fixed production parameters (19 MiB, 2 iterations, 1 lane — OWASP's recommended minimum for interactive logins); `ChaCha20Poly1305`'s nonce is generated via the kernel's CSPRNG (`getrandom(2)`), never `std/math`'s `@rng` (that one is seeded from clock^pid and is not appropriate for anything security-sensitive).
