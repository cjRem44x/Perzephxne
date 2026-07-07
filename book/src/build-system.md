# Build System (`przp`)

`przp` is the Perzephxne build tool, similar to Cargo for Rust.

## Commands

| Command | Description |
|---|---|
| `przp init [name]` | Initialize a new project |
| `przp build` | Compile (debug by default) |
| `przp build -o=Name` | Compile and write a custom output binary |
| `przp build --release` | Optimized release build |
| `przp run` | Build (if needed) and run — see [Incremental `run`](#incremental-run) |
| `przp run --release` | Build (if needed) and run with optimizations |
| `przp test [<file>] [<name>]` | Run `test "..." { }` blocks — see [Testing](./testing.md) |
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
  tests/
    example_test.przp   # przp test discovers every *.przp file placed here
```

`przp init` scaffolds all of the above, including a `tests/example_test.przp` example. `tests/` is discovered by `przp test` independently of what `src/main.przp` imports — see [Testing § Test Discovery](./testing.md#test-discovery).

## `przp.toml`

The manifest is intentionally small while the language ships only its core compiler and standard library.

```toml
[package]
name    = "MyProject"
version = "0.1.0"

[build]
entry = "src/main.przp"
link  = ["X11", "GL"]

[deps]
# reserved for future package dependencies
```

Supported fields:

| Field | Required | Description |
|---|---:|---|
| `[package].name` | yes | Package name. Used as the default `przp build`/`przp run` binary name. |
| `[package].version` | no | Package version metadata. |
| `[build].entry` | no | Entry source file. Defaults to `src/main.przp`. |
| `[build].link` | no | Array of system library names to link against — each entry becomes a `-l<name>` flag on the final link step (`link = ["X11", "GL"]` links `-lX11 -lGL`). For linking against `extern fn` declarations backed by libraries the OS already ships (no bundled `.so`/`.a` of your own). |

`[deps]` is accepted as a reserved section, but the current toolchain does not download or resolve packages. Standard library modules are shipped with the compiler and imported with paths such as `"std/io"`.

Only `[package]`, `[build]`, and `[deps]` are recognized. Manifest values are quoted strings (or, for `[build].link`, an array of quoted strings on one line); malformed assignments, unknown sections, and invalid string/array values are reported as `przp.toml:line: error: ...`.

## Outputs

Project builds write the binary to the current project directory:

```
przp build          # ./MyProject, using [package].name
przp build -o=app   # ./app
przp run            # builds ./MyProject, then runs it
```

Stand-alone compilation defaults to `./out` unless `-o=Name` is provided.

## Incremental `run`

`przp run` skips recompiling when the existing binary is already newer than every file it depends on (the entry file and everything it transitively `import()`s), and only rebuilds when something is actually stale:

- editing any source file that's part of the build (entry or an import) forces a rebuild
- switching between `przp run` and `przp run --release` forces a rebuild, even with no source changes, since the two modes produce different binaries
- deleting the binary forces a rebuild

This tracking lives in a `<binary>.d` sidecar file written next to the binary after each successful build (a plain list of the files that build depended on) — safe to delete at any time, since its absence just means the next `run` rebuilds unconditionally. `przp build` always compiles unconditionally; the skip-if-fresh behavior is `run`-only, matching its "get me a running program" purpose rather than `build`'s "give me a fresh binary" one.

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
