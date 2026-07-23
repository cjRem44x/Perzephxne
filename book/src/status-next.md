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

## Bugs Found and Fixed Along the Way

- **Nested-import local-variable collision** — a 3+-level-deep import chain (file A imports B, B imports C) could mis-rewrite an unrelated local variable inside C's own function body if its name happened to match a top-level item name declared in B. `mangle_items` merges an already-mangled imported module's items into the importer's own item list *before* the importer's own alias-rewrite pass runs, so that pass's name-match set ended up including names with no relation to the code it was walking, and the rewrite itself had no notion of lexical scope to tell a local apart from an item reference. Fixed by collecting each function's own locals (`LocalNames` in `main.c`) and always letting a local name shadow an outer item for that whole function body. Covered by `tests/run/local_name_collision.przp`.
- **Zip Slip (path traversal) in `std/zip.unzip_path`** (CWE-22) — extraction joined an archive entry's name onto `dest_dir` with zero validation, so an entry named e.g. `../../etc/whatever` could write outside `dest_dir` entirely. Confirmed exploitable with a crafted `.zip`. Fixed by rejecting (skipping, not extracting) any entry that's an absolute path or contains a `..` path segment — see `is_safe_entry_name` in `zip.przp`. Covered by `tests/zip/traversal`.
- **Shell command injection via `przp.toml`'s `[build].link`** (CWE-78) — each `link = [...]` entry was spliced verbatim into a `-l<name>` clang flag, itself part of a `system()` call, with no character validation — a manifest entry like `"m; rm -rf ~"` executed arbitrary shell commands the moment `przp build`/`run`/`test` ran. Confirmed exploitable. Fixed by rejecting any `link` entry containing a character outside `[A-Za-z0-9_.-]` before it's ever used. Covered by `tests/project-fail/malicious_manifest_link`.

## Documentation Rule

Any compiler feature that is added or changed should update both the book source and the generated `book/book` output in the same change. The book should remain the user-facing source of truth for the current language.
