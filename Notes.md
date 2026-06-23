# Notes

There the is only one Type, T. Which is determined at compile time the reps need in asm.

```
import other_file_module

dat Foo {x, y, z} # handled as a struct

enum ENUM {ONE, TWO, THREE}

enum Mapped {ONE=3.145, TWO="Lol"}

myEnum = ENUM.ONE

foo = Foo
foo.x = ...

foo2 = Foo {x=0, y="LOL", z=-3.45}

# THIS COMPILES TO A SMART PTR,
# where the pointer it tracked and freed automatically.
*p = alo(Foo) # allocate mem to heap
p.* = Foo {...}
```
