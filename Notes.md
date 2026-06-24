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
```
