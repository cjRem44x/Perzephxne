# Generics

Generic functions and types are parameterized over one or more type variables written in `<T>` after the name.

## Generic Functions

```
fn max<T>(a: T, b: T) -> T {
    if a > b { ret a }
    ret b
}

x: i32 = max<i32>(10, 20)   # explicit
y: f64 = max(1.5, 0.7)      # inferred from args
```

Multiple type parameters:

```
fn pair<A, B>(a: A, b: B) -> (A, B) {
    ret (a, b)
}

first, second: i32 = pair<i32, i32>(10, 20)
```

## Generic Structs

```
struct Pair<T, U> {
    first:  T,
    second: U,
}

p: Pair<i32, str> = Pair<i32, str>{.first=1, .second="one"}
@pf("{p.second}\n")
```

## Monomorphization

Generics are compiled to concrete, specialized versions at the call site — the same approach as C++ templates, with no runtime overhead.
