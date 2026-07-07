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
| Package dependencies | `[deps]` is reserved. There is no external resolver yet; Core/Stdlib ships with the language. `[build].link` (see [Build System](./build-system.md)) covers linking against system libraries the OS already ships (e.g. `link = ["X11", "GL"]`) — this is not a package resolver, just linker flags. |
| Release UX | A future version command should be `przp version`, but versioning is intentionally deferred while beta work is moving quickly. |
| `any` type | Reserved keyword. Runtime-tagged values (boxing, type IDs, `when` dispatch on types) are designed but not implemented; removed from the reference until they exist. |
| Struct-by-value FFI, non-imported `extern fn` | `extern fn foo(v: Vector2) -> Vector2` declared directly in a file (not reached via `import()`) fails to compile if it needs struct-by-value ABI handling — the wrapper mechanism needs an import alias to redirect call sites to (see [Standard Library](./stdlib.md)). Move the declaration into its own module and `import()` it, which is fully supported. |
| `@bitcast` pointer type arguments | `@bitcast(T, val)`'s type argument only accepts a bare type name (`u32`, `vec2`) — a compound type expression like `*u8` doesn't parse. Not a practical limitation for pointer reinterpretation specifically, since a raw pointer already coerces to any other raw pointer type via plain assignment (see [Pointers & Memory](./pointers.md)), but the parser gap is real for anyone trying to `@bitcast` to e.g. a slice or smart-pointer type. |
| Encapsulation (`@opaque`, `pub`) | Struct field/method visibility is designed but not implemented — see [Future Language Sketches](#future-language-sketches) below. Every struct today is fully public: every field and every `impl` method is visible to any caller that can see the struct itself. |

## Future Language Sketches

Design sketches for features under active consideration, captured here so the intent is on record before implementation starts. **None of the syntax below works in the current compiler** — this is not a reference section.

### `@opaque` structs and `pub`/private `impl` visibility

The idea: an `@opaque` struct hides its fields from anything outside its own `impl` block — the fields still exist (this is encapsulation, not removal), they're just inaccessible to outside code, the same shape as a C++ `private` section or Rust's default field privacy. Inside that `impl` block, methods are private (hidden from outside callers) by default, and `pub fn` marks the ones meant as the struct's public API:

```
@opaque              # hides fields from code outside the impl block
struct Foo {
    ...
}
impl Foo {            # impl always has access to the fields regardless of @opaque
    pub fn bar() {}          # visible outside the impl — the public API surface

    fn baz(slf: @self) {}    # hidden by default — private to the impl
}
```

On an ordinary (non-`@opaque`) struct, every field and method is already public, so `pub` would be meaningless there — a no-op rather than an error, since it doesn't contradict the struct's existing fully-open default:

```
struct X {
    ...
}
impl X {
    fn y() {}   # `pub` doesn't apply here — everything is already public
                # on a standard struct
}
```

## Documentation Rule

Any compiler feature that is added or changed should update both the book source and the generated `book/book` output in the same change. The book should remain the user-facing source of truth for the current language.
