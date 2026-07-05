# Perzephxne Language Specification (Superseded)

> **This file is the project's original design draft, written in the first commit and never updated since.** It has drifted from the actual language and compiler in a number of places — most notably the `przp init`/`build` project layout (`src/main/przp/main.przp` + `bin/<name>/`, versus the flat `src/main.przp` + project-root binary that's actually implemented), the error-handling model (an `@err.ok`/`@err.div_zero` namespace of sentinels here, versus the real `@ok(val)`/`@err(code)`/`@is_ok`/`@is_err` failable-return builtins), and the `any` type (shown here as implemented; it's a reserved, unimplemented keyword — see [Status & Next Work](../book/src/status-next.md)).
>
> **The current, accurate, test-backed language reference is [`book/`](../book/).** Read it instead of this file — it covers the same ground (build system, literals, functions, types, generics, control flow, structs/enums/unions, pointers, error handling, modules, the standard library, builtins, operators, naming) and is kept in sync with the compiler as it changes, which this file was not.
>
> This file is kept for historical interest only — as a record of the language's original direction before implementation experience changed several of these decisions — not as a reference to build against.

A programming language influenced by the modern schemes of things. No GC baggage, no OOP
principles, just pure C-flavored programming.

Source compiles into LLVM IR (LLVM backend), which represents a more flexible format than
hardware assembly and allows cross-platform targeting.

That framing still holds. Everything below it in this file is the original, now-outdated
draft — see [`book/`](../book/) for what's actually built.
