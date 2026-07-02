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

## `impl` on Generic Structs

`impl Name<T>` attaches methods to a generic struct. `T` is usable in every signature and body. Methods with no `self` are static and are called on the instantiated type:

```
struct Box<T> {
    value: T,
}

impl Box<T> {
    fn new(v: T) -> Box<T> {           # static constructor
        ret Box<T>{.value = v}
    }

    fn get(self: *Box<T>) -> T {
        ret self.value
    }

    fn set(self: *Box<T>, v: T) {
        self.value = v
    }
}

b: Box<i32> = Box<i32>.new(42)   # Type<T>.method() calls a static method
@pf("{b.get()}\n")               # 42
b.set(99)
```

## Generic Data Structures

Generic structs can refer to themselves through pointers, which is all a linked data structure needs. `@alo(Node<T>)` heap-allocates a node of the concrete instantiation:

```
struct Node<T> {
    value: T,
    next:  *Node<T>,     # self-referential through a pointer
}

struct List<T> {
    head: *Node<T>,
    len:  usize,
}

impl List<T> {
    fn new() -> List<T> {
        ret List<T>{.head = null, .len = 0}
    }

    fn push(self: *List<T>, v: T) {
        n: *Node<T> = @alo(Node<T>)
        n.value = v
        n.next  = self.head
        self.head = n
        self.len += 1
    }

    fn sum(self: *List<T>) -> T {
        total: T = self.head.value
        cur: *Node<T> = self.head.next
        while cur != null {
            total = total + cur.value
            cur = cur.next
        }
        ret total
    }
}

l: List<i32> = List<i32>.new()
l.push(10)
l.push(20)
l.push(30)
@pf("len={l.len} sum={l.sum()}\n")   # len=3 sum=60
```

The standard library's `std/collections` dynamic array is built the same way.

## Monomorphization

Generics are compiled to concrete, specialized versions at the call site — the same approach as C++ templates, with no runtime overhead.
