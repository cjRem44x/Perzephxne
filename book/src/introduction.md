# Introduction

Perzephxne is a compiled, statically typed, systems programming language with a C-flavored syntax.

**Core design goals:**

- No garbage collector — memory is manual or scope-tracked via smart pointers
- No OOP — data and behavior are separated; `impl` blocks are namespace organization, not inheritance
- No header files — modules are source files, all declarations are visible across files in a project
- LLVM backend — compiles to LLVM IR for portable, optimized code generation
- Explicit is better than implicit — errors are values, casts are explicit, lifetimes are visible

```
fn main() {
    @pf("Hello, World!\n")
}
```

## What Perzephxne Is

A language for people who want the directness of C with a saner type system: proper enums, slices, failable return types, and generic functions without templates.

If you've written C, Odin, or Zig you'll feel at home. If you're coming from Rust, the main difference is that Perzephxne has no borrow checker — memory safety is the programmer's responsibility, aided by smart pointers and `defer`.

## What Perzephxne Is Not

- A safe language — there is no borrow checker, and raw pointers are fully exposed
- An object-oriented language — there is no inheritance, no virtual dispatch, no `self` keyword
- A scripting language — every program is compiled to a native binary

## Source File Extension

Perzephxne source files use the `.przp` extension.

## Compiler

The compiler (`przp`) emits LLVM IR text (`.ll` files) and invokes `clang` to produce the final native binary. You need `clang` on your `PATH`.
