# Notes

A programming language influenced the by the modern schemes of things. No GC baggage, no OOP principles, just pure C flavored programming.

Source compiles into LLVM IR (llvm backend) as it represents a more flexible format then hardware assembly.

## Compiling

Using the Perzephxne build structure.
`przp init`
`przp build`
`przp run`

Producing the structure.
```
/project
    .git # fresh git repo
    /bin
        # compiled binaries
    /src
        /main
            /przp # source code
                main.przp
    przp.toml     # manage project deps
    .gitignore    # default for przp project
```

Down the road, like cargo, I would like simple `przp add <pkg>` to link deps.

To make a new Perzephxne project.

```
mkdir MyProject
cd MyProject
przp init
```

To build binaries.

`przp build`

To run binaries.

`przp run`

Using Stand-Alone-Compiler.
`przp sac <files> -o=Name`


## Hello World

The driver function in Perzephxne.

```
fn main() {
    @pf("Hello World\n") # buitlin for PrintF
}
```

`przp build` is simple enough. However, if this code was outside the Perzephxne project structure, then I might run something like `przp sac main.przp -o=main`

## Functions

The general structure of a function in Perzephxne.
```
fn foo(param1: type, param2: type, ...) -> retType {
    ...
    ret value
}
```

Though, in many cases void funcs are common.
```
fn voidFunc() {
}
```
No `->` return pointer is needed. Also empty returns are allowed.
```
fn foo() {
    if thing {
        ...
    } else {
        ret
    }
}
```


## Types

There are only two types in Perzephxne, mutbale `mut` and immutable `imu`.
```
VAR: TYPE = VALUE # mutable type

VAR: TYPE : VALUE # immutable type
```

Of the available types there are:

Integers
```
u8/i8
u16/i16
u32/i32
u64/i64
```

Floats
```
f16
f32
f64
```

And
```
usize # special int for array lengths
bool
str
```

For instance,
```
fn main() {
    x: i32 = 0
    y: i32 = 0

    ans: i64 = x+y # is allowed since i32 can fit inside of i64

    ans2: i16 = x+y # [X] WRONG, will fail since 16-bit is too small
    
    #################

    PI: f32 : 3.145

    r: f64 = PI*5.46

    #################

    name: str = "John" # Perzephxne uses modern String handle
    len :usize: name.len # internally str is an array of u8's
}
```

## Statements
```
if X {
    ...
} elif Y {
    ...
else {
    ...
}

when X {
    A => ...,
    B => {
        ...
    },
    ...
    _ => ... # else clause
}
```

For using `and or != ...`

```
if X and Y or Z != 0 and W >= 0 {}
elif X != 55 {}
```

## Loops
```
while <condition> {
    ...
}

while <condition> => foo() {} # point to do function
```

```
for e => arr {
    # e in array ...
}

for e, i => arr {} # elemant and index

for e => arr, 0..N # or 0..=N {} element in array until N or including N

for i := 0, i<=arr, i++ {} # traditional array

for 0..N # or 0..=N {} # basic counting loop
```

## Structs

```
struct Foo {
    x: i32, y: str, z: f64
}

foo: Foo = Foo
foo.x = ...

foo2: Foo = Foo{.x=12, ...} # one field or all
```

## Implements
Add functions to a struct
```
impl Foo {
    fn bar() {
        @slf.x *= 5 # self pointer to structu instance

        @pf(@imuslf.x) # immutable self pointer like, void foo() const {} in C++
        
        type this = @slf # I can alias too
    }
}
```

## Type Alias
`type T = ...`

Such as `type int = i32`

## Enums

```
enum X {
    ONE, TWO, THREE
}

x: X = X.ONE

enum Y => i32 {
    ONE=13, TWO=456,
}
y: Y = Y.ONE
```

## Unions
```
unn X {
    a: i32, b: f32, ...
}

x: X = X{.a=11}
x.a = ...
```

Tagged unions
```
unn Y => enum {
    a: i32, b: str, c: MyStruct
}

y: Y = Y{.b="john"}

when y {
    .a => ...,
    .b => ...,
    .c => ...,
    - => ...
}
```

## Pointers

Perzephxne offers both Raw-Fat Pointers ans Smart Pointers. Where raw pointers also store there size to prevent indexing issues. And smart pointers track themselves and don't require to be freed.

```
x: i32 = 12
p_x: *i32 = &x # raw pointer to the stack

# how manual mem alloc works
heap_var: *type = @alo(T) # compiler determines size in usize
@free(heap_var)

size: usize = @size(heap_var) # we can do it but compiler does it internally with alo

# to acces the value
heap_var.* = ... # just like Zig
```

Then we have smart pointers reps with `^`
```
smrt_ptr: ^i32 = @alo(i32)
smrt_ptr.^ = 100
# no free, it handles itself
```

Pass as params,
```
fn foo(rfp: *type, smrtp: ^type)
```


## Module System

NO HEADERS, we use modules.

```
# big import block
import(
    alias = "path/to/file",
    lib = ...,
)

fn main() {
    ...
}
```

## Std/Libs API Naming

I prefer lowcaps underscores with camelcased type names.

`func_foo_bar()`

And I like the name minimal and not long.

`myType`

Combined

`get_myType()`
