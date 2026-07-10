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

## Inline Namespaces (`mod`)

For grouping related declarations within a single file, use a `mod` block:

```
mod Shapes {
    fn double(v: i32) -> i32 { ret v * 2 }

    struct Point {
        x: i32,
        y: i32,
    }

    impl Point {
        fn new(x: i32, y: i32) -> Point { ret Point{.x=x, .y=y} }
        fn sum(self) -> i32 { ret self.x + self.y }
    }
}

fn main() -> i32 {
    a := Shapes=>double(21)
    p := Shapes.Point.new(3, 4)
    ret a + p.sum()
}
```

`mod` is purely a same-file grouping/prefixing convenience — it does not introduce real scoping or privacy. A `mod X { fn a() {} }` block is handled exactly like importing a file that declares `fn a() {}` under alias `X`: internally, the compiler mangles the block's own items to `X__a` and splices them into the module's flat item list, so `X=>a()` and `X.a()` resolve identically to an import's `X.a()`.

`=>` is a fully interchangeable spelling of `.` for qualified access — module paths, import aliases, and qualified generics all accept either operator, mixed freely (`Shapes=>Box<i32>.new(...)` and `Shapes.Box<i32>=>new(...)` mean the same thing). `.` continues to work everywhere it always has; `=>` is purely additive.

Generics declared inside a `mod` block use the same qualified-generic syntax as imports:

```
mod Boxes {
    struct Box<T> { val: T }
    impl Box<T> {
        fn new(v: T) -> Box<T> { ret Box<T>{.val=v} }
        fn get(self) -> T { ret self.val }
    }
}

b := Boxes=>Box<i32>.new(7)
```

A `mod` block can also live inside an imported file, and chains with the importer's own alias: given `mod Y { ... }` inside `lib.przp`, an importer that does `import(lib = "lib")` reaches it as `lib.Y=>item` (or `lib.Y.item`) for struct/enum/generic access. Plain free functions declared inside a `mod` that is itself inside an *imported* file are not reachable through a two-level path (`lib.Y=>someFreeFn()`) — only mod access local to the current file, or import access to a top-level (non-mod) function, resolves through a single alias level today. Struct/enum/generic-qualified access (`lib.Y=>Box<i32>.new(...)`, `lib.Y.SomeEnum.Variant`) is unaffected by this and works at any nesting depth.

### `=>` in `when` patterns

Inside a `when` arm's pattern, `=>` is reserved for the arm's own separator (`pattern => body`) and cannot also be used as a qualifier there — use `.` for qualified access within a pattern instead. This only affects the pattern; `=>` works normally as a qualifier in the arm's body and everywhere else.

```
mod Dir {
    enum Direction { North, South, East, West }
}

fn code(d: Dir.Direction) -> i32 {
    ret when d {
        Dir.Direction.North => 1,   # '.' in the pattern, not '=>'
        Dir.Direction.South => 2,
        Dir.Direction.East  => 3,
        Dir.Direction.West  => 4,
    }
}
```

Dot chains of any depth (`Mod.Enum.Variant`, `Mod.Sub.Enum.Variant`, ...) are supported in pattern position, since a pattern is always explicitly terminated by `=>` regardless of how many `.`-qualified segments precede it.

### A lexing note on `>=>`

A generic's closing `>` immediately followed by `=>` (e.g. `Box<i32>=>new(...)`) lexes as `>=` (greater-or-equal) followed by a lone `>`, not as `>` followed by `=>` — the lexer always takes the longest match at each position and has no way to know a generic argument list is closing there. Add a space (`Box<i32> => new(...)`) or just use `.` (`Box<i32>.new(...)`) to avoid it. This is the same class of issue as `>>` needing care after nested generics in other C-like languages.

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
