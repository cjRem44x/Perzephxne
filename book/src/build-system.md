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
| `przp run <args>` | Any argument other than `--release` is passed straight through to the program's own `argv` (see `@args` in [Globals](./globals.md)) — e.g. `przp run --no-audio` |
| `przp test [<file>] [<name>]` | Run `test "..." { }` blocks — see [Testing](./testing.md) |
| `przp add <name>[@ref]` | Fetch a dependency and record it in `przp.toml`/`przp.lock` — see [Dependencies](#dependencies) |
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
foo = "1.2.0"
```

Supported fields:

| Field | Required | Description |
|---|---:|---|
| `[package].name` | yes | Package name. Used as the default `przp build`/`przp run` binary name. |
| `[package].version` | no | Package version metadata. |
| `[build].entry` | no | Entry source file. Defaults to `src/main.przp`. |
| `[build].link` | no | Array of system library names to link against — each entry becomes a `-l<name>` flag on the final link step (`link = ["X11", "GL"]` links `-lX11 -lGL`). For linking against `extern fn` declarations backed by libraries the OS already ships (no bundled `.so`/`.a` of your own). |
| `[deps].<name>` | no | A git tag, branch, or commit for the dependency named `<name>` — see [Dependencies](#dependencies). |

Only `[package]`, `[build]`, and `[deps]` are recognized. Manifest values are quoted strings (or, for `[build].link`, an array of quoted strings on one line); malformed assignments, unknown sections, and invalid string/array values are reported as `przp.toml:line: error: ...`. A `[deps]` key must be a plain identifier (letters, digits, underscore, not starting with a digit) and its value a valid git ref (letters, digits, `.`, `_`, `-`, `/`) — both are checked at parse time, not just by `przp add`, since `przp.toml`/`przp.lock` can arrive from someone else's project.

## Dependencies

Dependencies are plain GitHub repos, not a hosted package index: a dependency named `foo` is `github.com/cjRem44x/przp_dep_foo` — a repo whose own root holds `foo.przp` (its root module, named after itself, the same convention `std/audio` follows for `std/audio.przp`). There's no semver range resolution — `[deps]`'s value is a git tag, branch, or commit exactly, resolved to one exact commit the moment `przp add` runs:

```sh
przp add foo           # default branch, pinned to its current HEAD commit
przp add foo@1.2.0      # a tag (or branch, or commit) instead
```

This writes two files:

- **`przp.toml`**'s `[deps]` gets the human-facing ref you asked for (`foo = "1.2.0"`, or the default branch's own name if you didn't give one) — meant to be read and hand-edited like the rest of the manifest.
- **`przp.lock`** gets the exact commit that ref resolved to (`foo = "9f2a1c7e..."`) — generated, never hand-edited; this is what actually gets checked out. Commit it alongside `przp.toml` so anyone else building the project (including CI) gets the identical dependency code, the same role `Cargo.lock` plays for a Cargo binary crate.

`przp build`/`run`/`test` all call the same dependency-resolution step before compiling: if a `[deps]` entry's pinned commit (from `przp.lock`) isn't checked out yet under `.przp/deps/<name>/` — e.g. right after cloning *someone else's* project, which commits `przp.toml`/`przp.lock` but not `.przp/deps/` (see the `.gitignore` `przp init` writes) — it's fetched automatically, the same way `cargo build` fetches from `Cargo.lock` without a separate fetch step. A `[deps]` entry with no matching `przp.lock` entry is a hard error telling you to run `przp add <name>`, not a silent re-resolve — pinning to one exact commit is the whole point of the split between the two files.

Import a dependency with a `dep/` path, parallel to `std/`'s own prefix:

```przp
import(foo = "dep/foo")       # foo's own root module
import(sub = "dep/foo/sub")   # a submodule inside it, the same way "std/graphics/gl" works
```

The explicit `dep/` sigil (rather than a bare `import(foo = "foo")` silently consulting `[deps]`) keeps a dependency import unambiguous with an ordinary project-relative import of a same-named local file or directory.

Explicit non-goals for now: no semver ranges/version resolution (an exact ref only), no global cache (each project gets its own `.przp/deps/`), no authentication (public repos only), and no `przp update` command yet (re-run `przp add <name>` to move to a newer commit on the same ref).

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

`przp run` execs the built binary directly (`fork`+`execv`, no shell in between) rather than shelling out through `system()`, so any pass-through arguments reach the program's `argv` exactly as typed — no shell re-parsing of spaces, quotes, or glob characters along the way.

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
