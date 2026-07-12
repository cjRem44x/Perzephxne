# Status & Next Work

Perzephxne is in beta. The book is intended to be the complete language reference for the current compiler and shipped standard library.

The reference currently covers:

- source layout, project manifests, and `przp` commands
- literals, variables, primitive types, arrays, slices, strings, tuples, and failable types
- functions, first-class function pointers, extern declarations, inline functions, and return inference
- control flow, `when`, loops, labels, `break`, `continue`, `goto`, and `defer`
- structs, `impl` methods, generic functions, generic structs, enums, plain unions, and tagged unions
- inline `mod` namespaces and `=>` as a module-access operator interchangeable with `.`
- raw pointers, heap allocation, smart pointers, globals, platform constants, inline assembly, builtins, operators, and naming conventions
- all standard library modules shipped under `compiler/std`

## Product Gaps

The core language surface is represented in the book and regression suite. The remaining work is mostly product hardening:

| Area | Next Work |
|---|---|
| Parser diagnostics | Add focused malformed-syntax regressions and improve messages for missing braces, bad signatures, and invalid generic declarations. |
| Diagnostic consistency | Continue moving command, manifest, import, semantic, and codegen failures toward source-located errors where possible. |
| Regression breadth | Add more edge-case tests for parser recovery, generic instantiation, failable destructuring, and unsafe pointer operations. |
| Build/runtime portability | Keep Linux x86-64 as the current target, then audit assumptions before broadening platform support. |
| Package dependencies | `[deps]` is reserved. There is no external resolver yet; Core/Stdlib ships with the language. `[build].link` (see [Build System](./build-system.md)) covers linking against system libraries the OS already ships (e.g. `link = ["X11", "GL"]`) — this is not a package resolver, just linker flags. |
| Release UX | A future version command should be `przp version`, but versioning is intentionally deferred while beta work is moving quickly. |
| `any` type | Reserved keyword. Runtime-tagged values (boxing, type IDs, `when` dispatch on types) are designed but not implemented; removed from the reference until they exist. |
| Struct-by-value FFI, non-imported `extern fn` | `extern fn foo(v: Vector2) -> Vector2` declared directly in a file (not reached via `import()`) fails to compile if it needs struct-by-value ABI handling — the wrapper mechanism needs an import alias to redirect call sites to (see [Standard Library](./stdlib.md)). Move the declaration into its own module and `import()` it, which is fully supported. |
| `@bitcast` pointer type arguments | `@bitcast(T, val)`'s type argument only accepts a bare type name (`u32`, `vec2`) — a compound type expression like `*u8` doesn't parse. Not a practical limitation for pointer reinterpretation specifically, since a raw pointer already coerces to any other raw pointer type via plain assignment (see [Pointers & Memory](./pointers.md)), but the parser gap is real for anyone trying to `@bitcast` to e.g. a slice or smart-pointer type. |
| Array size via a named constant (`[CAP]T`) | An array type's size is only evaluated as a compile-time constant when it's a literal integer (`[64]T`) — a global constant identifier in that position (`CAP: usize : 64` used as `[CAP]T`) silently computes a size of 0 instead, corrupting the struct's layout (fields after the array overlap it rather than following it). Fixing this means teaching every array-size call site in codegen to evaluate a constant expression generally, not just check for a literal `EXPR_INT`. Use a literal size for now. |
| Two-level non-generic access through `mod` + `import` | A plain free function, a non-generic struct's static method, or an enum variant declared inside a `mod` block that itself lives inside an *imported* file isn't reachable through the full two-level path (`lib.Y.someFreeFn()`, `lib.Y.Circle.new(...)`, `lib.Y.Direction.North`) — only one level of alias resolves for these today (mod access local to the current file, or plain import access to a non-mod item). **Generic** struct/impl access at that same depth (`lib.Y.Box<i32>.new(...)`) is unaffected, since it resolves entirely at parse time rather than through the post-hoc alias-collapsing pass the other cases rely on; see [Modules](./modules.md). |
| `std/os.mkdir_dir` doesn't treat "already exists" as success | Its own doc comment promises `mkdir_dir` "returns true on success or if it already exists," but the implementation only checks `mkdir()`'s return value — a directory that already exists makes `mkdir()` fail (`EEXIST`), so `mkdir_dir` returns `false` for it too, contradicting the comment. Found while adding `std/file.mkdir_all`/`make`, which need exactly that "already exists is fine" behavior — worked around there by checking `is_dir` before ever calling `mkdir_dir`, rather than fixing `mkdir_dir` itself: `std/file` already imports `std/os`, and `std/os` fixing this the same way (checking `is_dir` first) would need importing `std/file` back, which is a cycle (cycles are rejected — see [Modules](./modules.md)). A real fix needs either a small stat-based directory check duplicated directly in `std/os` (avoiding the cross-import), or reading `errno` for `EEXIST` specifically, and there's no established `errno`-reading idiom at the `.przp` level yet (`@perr` reads it internally in the compiler, not exposed as a value). |

## Documentation Rule

Any compiler feature that is added or changed should update both the book source and the generated `book/book` output in the same change. The book should remain the user-facing source of truth for the current language.
