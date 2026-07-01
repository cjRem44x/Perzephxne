# Build System (`przp`)

`przp` is the Perzephxne build tool, similar to Cargo for Rust.

## Commands

| Command | Description |
|---|---|
| `przp init [name]` | Initialize a new project |
| `przp build` | Compile (debug by default) |
| `przp build -o=Name` | Compile and write a custom output binary |
| `przp build --release` | Optimized release build |
| `przp run` | Build and run |
| `przp run --release` | Build and run with optimizations |
| `przp sac <files> -o=Name` | Stand-Alone Compiler — compile one or more files without a project |
| `przp sac <files> --release -o=Name` | Stand-alone optimized build |

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
  przp.toml        # project manifest
  src/
    main.przp      # default entry point
```

## `przp.toml`

The manifest is intentionally small while the language ships only its core compiler and standard library.

```toml
[package]
name    = "MyProject"
version = "0.1.0"

[build]
entry = "src/main.przp"

[deps]
# reserved for future package dependencies
```

Supported fields:

| Field | Required | Description |
|---|---:|---|
| `[package].name` | yes | Package name. Used as the default `przp build`/`przp run` binary name. |
| `[package].version` | no | Package version metadata. |
| `[build].entry` | no | Entry source file. Defaults to `src/main.przp`. |

`[deps]` is accepted as a reserved section, but the current toolchain does not download or resolve packages. Standard library modules are shipped with the compiler and imported with paths such as `"std/io"`.

Only `[package]`, `[build]`, and `[deps]` are recognized. Manifest values are quoted strings; malformed assignments, unknown sections, and invalid string values are reported as `przp.toml:line: error: ...`.

## Outputs

Project builds write the binary to the current project directory:

```
przp build          # ./MyProject, using [package].name
przp build -o=app   # ./app
przp run            # builds ./MyProject, then runs it
```

Stand-alone compilation defaults to `./out` unless `-o=Name` is provided.

## Quick Start

```sh
przp init myproject
cd myproject
przp run
```

To initialize the current directory instead:

```sh
mkdir myproject
cd myproject
przp init
```
