# Modules

A module is a source file. There are no header files and no separate declaration step.

## Importing

```
import "std/io"
import "std/mem"
import "mylib/math"
```

Imports are resolved relative to the project root, or to the standard library path for `"std/*"` imports.

## Using Symbols

After importing, use the module's name as a namespace prefix:

```
import "std/io"

io.print("hello\n")
fd: io.File = io.open("data.txt")
```

## Aliasing an Import

```
import "mylib/very_long_name" as vln

vln.do_thing()
```

## Selective Import

```
import "std/math" { sqrt, PI }

r: f64 = sqrt(9.0)
@pf("{PI}\n")
```

## Module Layout

```
MyProject/
  src/
    main.przp       # import "src/util" resolves to this file
    util.przp
    net/
      http.przp     # import "src/net/http"
```

## Visibility

All top-level declarations are visible within a project by default.

Mark symbols `private` to restrict access to the declaring file:

```
private fn internal_helper() { ... }
```

Private symbols are not accessible from other files.

## External Libraries

```
@link("mylib")        # links libmylib.a / libmylib.so
import "mylib/core"
```

Or in `przp.toml`:

```toml
[dependencies]
mylib = { path = "../mylib" }
```

## Standard Library Modules

| Module | Contents |
|---|---|
| `std/io` | print, file I/O, stdin |
| `std/mem` | alloc, free, memcpy, memset |
| `std/str` | string operations |
| `std/math` | sqrt, sin, cos, etc. |
| `std/os` | env, args, exit, paths |
| `std/box` | `Box[T]`, `Rc[T]`, `Weak[T]` |
| `std/fmt` | string formatting |
