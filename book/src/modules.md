# Modules

A module is a source file. There are no header files and no separate declaration step.

## Importing

```
import(io = "std/io")
import(io = "std/io", math = "std/math")
```

Each entry is `alias = "path"`. The alias is the namespace prefix used to access the module's symbols. Multiple modules can be imported in a single statement with a comma-separated list.

Paths starting with `"std/"` resolve against the standard library. Everything else resolves relative to the importing file's directory.

Import cycles are rejected. The compiler reports the cycle at the `import(...)` declaration that closes the loop and includes the resolved path involved in the cycle.

## Using Symbols

After importing, use the alias as a namespace prefix:

```
import(io = "std/io")

fn main() {
    io.println("hello")
}
```

## Multiple Imports

```
import(io = "std/io", math = "std/math", os = "std/os")

fn main() {
    r: f64 = math.sqrt(2.0)
    io.println(@str(r))
}
```

## Module Layout

Source files within the same project are visible by default. Layout follows the filesystem:

```
MyProject/
  src/
    main.przp
    util.przp          # import(u = "util") or import(u = "src/util")
    net/
      http.przp        # import(http = "net/http")
  przp.toml
```

## Visibility

All top-level declarations are visible within a project by default.

## Standard Library Modules

| Module | Contents |
|---|---|
| `std/io` | print, file I/O, stdin |
| `std/str` | string operations, split/trim/contains/replace |
| `std/math` | sqrt, trig, pow, log, floor/ceil, constants |
| `std/os` | env, cwd, directory helpers, process execution |
| `std/file` | file read/write, append, exists, delete, size |
| `std/fmt` | string formatting and padding utilities |
| `std/collections` | dynamic array support |
| `std/atomic` | atomic load/store/add/sub/cas/inc/dec on i64 |
| `std/sync` | mutex and read-write lock wrappers |
| `std/crypto` | djb2, fnv1a, sha256 hashing; xor_encrypt |
