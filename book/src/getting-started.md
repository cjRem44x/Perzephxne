# Getting Started

## Installation

You need:

- `clang` (any recent version, 14+)
- The `przp` compiler binary (build from source)

```sh
git clone https://github.com/cjRem44x/Perzephxne
cd Perzephxne/compiler
make
sudo cp przp /usr/local/bin/
```

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
