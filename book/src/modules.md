# Modules

A module is a source file. There are no header files and no separate declaration step.

## Importing

```
import(io = "std/io")
import(io = "std/io", math = "std/math")
```

Each entry is `alias = "path"`. The alias is the namespace prefix used to access the module's symbols. Multiple modules can be imported in a single statement with a comma-separated list.

Paths starting with `"std/"` resolve against the standard library. Everything else resolves relative to the importing file's directory.

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
| `std/str` | string operations |
| `std/math` | sqrt, trig, constants |
| `std/os` | env, args, paths |
| `std/collections` | dynamic arrays, maps |
