# Standard Library

The standard library ships with the compiler under `compiler/std`. Import modules with `import(alias = "std/module")`.

There is no external package resolver yet. `[deps]` in `przp.toml` is reserved for future packages; the modules below are the standard library that currently ships with the language.

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

## `std/crypto`

Hashing and simple byte-buffer crypto helpers.

| Function | Description |
|---|---|
| `hash_djb2(s)` | djb2 non-cryptographic hash |
| `hash_fnv1a(s)` | FNV-1a 64-bit non-cryptographic hash |
| `xor_encrypt(data, len, key)` | XOR stream cipher in place |
| `sha256(data, len)` | SHA-256 digest as a 32-byte heap buffer |
| `sha256_hex(data, len)` | SHA-256 digest as lowercase hex string |
