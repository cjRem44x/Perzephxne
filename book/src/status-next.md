# Status & Next Work

Perzephxne is in beta. The book is intended to be the complete language reference for the current compiler and shipped standard library.

The reference currently covers:

- source layout, project manifests, and `przp` commands
- literals, variables, primitive types, arrays, slices, strings, tuples, and failable types
- functions, first-class function pointers, extern declarations, inline functions, and return inference
- control flow, `when`, loops, labels, `break`, `continue`, `goto`, and `defer`
- structs, `impl` methods, generic functions, generic structs, enums, plain unions, and tagged unions
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
| Package dependencies | `[deps]` is reserved. There is no external resolver yet; Core/Stdlib ships with the language. |
| Release UX | A future version command should be `przp version`, but versioning is intentionally deferred while beta work is moving quickly. |
| `any` type | Reserved keyword. Runtime-tagged values (boxing, type IDs, `when` dispatch on types) are designed but not implemented; removed from the reference until they exist. |
| printf-style `%s` | `@pf("%s", s)` passes the fat pointer raw and crashes; use `{s}` interpolation. A fix should extract the data pointer (and handle non-NUL-terminated slices). |
| `!StructName` failable returns | `fn f() -> !Point { ret @ok(Point{...}) }` fails to compile for any struct — codegen passes the struct's alloca pointer where the failable wrapper's `insertvalue` expects the raw aggregate value. Found while drafting the [Graphics](./graphics.md) design sketch; blocks any fallible constructor pattern (`Thing.load(path) -> !Thing`), not just graphics. |
| Struct-by-value FFI ABI | An `extern fn` that takes or returns a small struct by value (e.g. `Vector2 { f32, f32 }`) does not follow the x86-64 System V ABI — linked against real C code, field values come back wrong. Also found while drafting [Graphics](./graphics.md); blocks binding any C library whose API passes small structs by value, which is common (raylib, math libraries, many system APIs). |

## Documentation Rule

Any compiler feature that is added or changed should update both the book source and the generated `book/book` output in the same change. The book should remain the user-facing source of truth for the current language.
