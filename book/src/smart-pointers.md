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

Use `.^` to read or write through the pointer:

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
