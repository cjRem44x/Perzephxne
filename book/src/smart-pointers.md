# Smart Pointers

A smart pointer `^T` is a reference-counted heap allocation. The runtime automatically frees the memory when the last reference goes out of scope — no manual `free` needed.

## Allocation

Use `@new` to create a smart pointer:

```
# wrap a value
p: ^i64 = @new(42)
q: ^f64 = @new(3.14)

# allocate a zero-initialized instance by type name
r: ^i32 = @new(i32)

# wrap a struct value
s: ^vec2 = @new(vec2{.x = 1.0, .y = 2.0})
```

Integer and float literals inside `@new` widen the same way `:=` does (`i64` / `f64`), so `@new(42)` returns `^i64`.

## Dereference

Use `.^` to read or write through the pointer — not `.*`, which is for raw `*T` pointers only. The two are not interchangeable: `.^` skips past the 8-byte reference-count header before touching the value, while `.*` doesn't, so using the wrong one on the wrong pointer kind is a compile error rather than a silent wrong-offset read or write.

```
p: ^i64 = @new(10)
p.^ = 20            # write through
val: i64 = p.^      # read through
@pf("{val}\n")      # 20
```

## Reference Counting

The compiler automatically increments and decrements the reference count:

```
fn take_ref(q: ^i64) {
    @pf("inside: {q.^}\n")
}   # RC decremented here; freed if it reaches zero

p: ^i64 = @new(99)
take_ref(p)         # RC = 2 on entry, 1 on exit
```

Use `@clone` when you need to explicitly share ownership (returns the same pointer with RC incremented):

```
a: ^i64 = @new(7)
b: ^i64 = @clone(a)   # a and b share the allocation; RC = 2
```

## Inspecting the Type

`@typeof` returns a `str` describing the type:

```
p: ^i32 = @new(i32)
@pf("{@typeof(p)}\n")   # "^i32"
```

## Field Access on `^Struct`

`.field` and `->field` both auto-deref through a `^Struct`, same as they do for `*Struct` — no need to write `.^.field` unless you want the whole pointee value:

```
struct Point { x: i32, y: i32 }

p: ^Point = @new(Point{.x=1, .y=2})
x: i32 = p.x        # auto-deref
y: i32 = p->y        # identical
z: i32 = p.^.x       # equivalent, more explicit
```

## Mutating Through a Pointer

```
fn increment(p: ^i64) {
    p.^ = p.^ + 1
}

counter: ^i64 = @new(0)
increment(counter)
increment(counter)
@pf("{counter.^}\n")    # 2
```

## Smart Pointer vs Raw Pointer

| | Raw `*T` | Smart `^T` |
|---|---|---|
| Allocation | `@alo(T)` / `malloc` | `@new(val)` |
| Deallocation | manual `@free` / `free` | automatic at scope exit |
| Null | `null` | never null after `@new` |
| Sharing | unsafe aliasing | reference-counted |
| Overhead | none | 8-byte RC header per allocation |

Raw and smart pointers are **not interchangeable** — a `^T` allocation has an 8-byte reference-count header before the data that a `*T` allocation doesn't, so mixing the two doesn't just give a type error, it reads and writes at the wrong memory offset. The compiler catches both directions of this mistake:

```
p: ^vec2 = @alo(vec2)   # ERROR: cannot initialize '^vec2' with value of type '*vec2'
                         # — @alo gives a raw *T with no RC header; use @new(vec2) for ^T
```

The same check applies to method calls: a method's `self`/first-parameter kind (`T`, `*T`, or `^T`) must match how it's actually called:

```
impl vec2 {
    fn print(slf: *vec2) { ... }
}

q: ^vec2 = @new(vec2)
q.print()   # ERROR: method 'print' expects a raw pointer receiver (*vec2),
            #        but was called through a smart pointer (^vec2)
```

A plain value (not a pointer at all) has no RC header either, so it can't be passed to a `^T`-self method — unlike a `*T`-self method, which can validly take the plain value's address on the fly:

```
impl vec2 {
    fn print(self: ^vec2) { ... }
}

v := vec2.new(4, 12)
v.print()   # ERROR: method 'print' expects a smart pointer receiver (^vec2),
            #        but was called on a plain value 'vec2'
```

A `^T` receiver calling a by-value `self: T` method is fine, though — the compiler auto-derefs past the RC header and copies the value, the same way a `*T` receiver can call a by-value method:

```
impl vec2 {
    fn print_val(self: vec2) { @pf("{self.x}, {self.y}\n") }
}

q: ^vec2 = @new(vec2.new(1, 2))
q.print_val()   # OK — dereferences q, copies the struct into self
```

And the same distinction applies to the dereference operators themselves — `.^` requires a `^T` operand, `.*` requires a `*T` operand:

```
p: ^i32 = @new(i32)
p.* = 5     # ERROR: cannot use '.*' on smart pointer '^i32' — use '.^' instead

q: *i32 = @alo(i32)
q.^ = 5     # ERROR: cannot use '.^' on raw pointer '*i32' — use '.*' instead
```

Match the allocator to the declared type (`@alo` with `*T`, `@new` with `^T`), match the dereference operator to the pointer kind (`.*` with `*T`, `.^` with `^T`), and match each method's receiver parameter to how you intend to call it.
