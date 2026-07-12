# Getting Started

## Installation

You need:

- `clang` (any recent version, 14+)
- The `przp` compiler binary (build from source)

```sh
git clone https://github.com/cjRem44x/Perzephxne
cd Perzephxne
./deploy.sh              # builds compiler/przp, installs it + std/ to /usr/local (needs sudo)
# or, no sudo needed:
PREFIX="$HOME/.local" ./deploy.sh
```

Don't just `cp compiler/przp /usr/local/bin/` by hand — `przp` resolves every `import(x = "std/...")` against a `std/` directory it finds relative to its own binary path (or `$PRZP_STDLIB`, if set), so a bare binary with no `std/` deployed anywhere near it fails to import anything from the standard library. `deploy.sh` installs both, in the layout `przp` already knows how to find on its own (`$PREFIX/bin/przp` + `$PREFIX/lib/przp/std/`) — see `deploy.sh --help`, or `make deploy` / `make uninstall` from the repo root.

## Hello World

Create `main.przp`:

```
fn main() {
    @pf("Hello, World!\n")
}
```

Compile and run outside a project with the stand-alone compiler:

```sh
przp sac main.przp -o=hello
./hello
```

Or inside a project:

```sh
mkdir myproject && cd myproject
przp init
przp run
```

## Comments

```
# single line comment

##
multi
line
comment
##
```

## Next Steps

The `examples/` directory at the repo root is a gallery of complete, runnable projects — each its own `przp.toml` + `src/` + `tests/`, buildable with `przp run` / `przp test` from inside the project's own directory:

- `examples/core/` — language fundamentals: control flow, error handling, pointers/memory, structs & generics
- `examples/dsa/` — data structures: linked list, binary search tree, stack/queue, sorting
- `examples/use_cases/` — small complete programs: a calculator, a to-do list, a word counter
- `examples/graphics/` — windowing and 2D drawing via `std/graphics` (needs a real or virtual X display — see [Graphics](./graphics.md))

The [Standard Library](./stdlib.md) reference page also has a runnable code snippet for every module.
