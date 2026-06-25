# Build System (`przp`)

`przp` is the Perzephxne build tool, similar to Cargo for Rust.

## Commands

| Command | Description |
|---|---|
| `przp init [name]` | Initialize a new project |
| `przp build` | Compile (debug by default) |
| `przp build --release` | Optimized release build |
| `przp run` | Build and run |
| `przp run --release` | Build and run with optimizations |
| `przp sac <files> -o=Name` | Stand-Alone Compiler — compile files without a project |

## Build Modes

**Debug** (default): no optimization, bounds checking, overflow traps, `@assert` active.

**Release** (`--release`): full optimization, bounds checking off, overflow wraps, `@assert` stripped.

Query the current mode at compile time:

```
if @debug   { @pf("debug build\n") }
if @release { @pf("release build\n") }
```

## Project Structure

```
MyProject/
  src/
    main.przp      # entry point
  przp.toml        # project manifest
```

## `przp.toml`

```toml
[package]
name    = "MyProject"
version = "0.1.0"

[build]
entry = "src/main.przp"
```

## Quick Start

```sh
przp init myproject
cd myproject
przp run
```
