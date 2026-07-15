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
| Two-level `mod` + `import` access in a type annotation | `x: lib.Y.Circle = ...` (a struct/enum inside a `mod` block that itself lives inside an *imported* file, named directly in a type annotation) doesn't parse — `parse_type` only resolves one level of import-alias-qualified type name. The equivalent *expression* access (`lib.Y.Circle.new(...)`, `lib.Y.someFreeFn()`, `lib.Y.Direction.North`, and matching that in a `when` pattern) all resolve correctly; work around the annotation gap with `:=`/`::` to infer the type from an expression instead (`x := lib.Y.Circle.new(...)`); see [Modules](./modules.md). |
| A smart-pointer field access (`p.^field`) interpolated directly in `@pf`/`@epf`/`@fmt` fails to compile when the field is itself a struct | `@pf("{p.^field}\n")` for a struct-typed `field` tries to pass the struct by value to `printf`'s varargs, but produces a mismatched-type LLVM IR error (`defined with type 'ptr' but expected '%Struct = type {...}'`) instead of compiling — found while testing the `@bitcast` pointer-type fix above, on code with no `@bitcast` involved at all. Assigning `p.^field` to a local first, then interpolating the local, works around it. |

## Documentation Rule

Any compiler feature that is added or changed should update both the book source and the generated `book/book` output in the same change. The book should remain the user-facing source of truth for the current language.
