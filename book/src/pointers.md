# Pointers & Memory

Perzephxne has no garbage collector. Memory is managed manually or with smart pointers from the standard library.

## Raw Pointers

```
x: i32  = 42
p: *i32 = &x      # address-of
v: i32  = p.*     # dereference
p.* = 100         # write through pointer
```

## Struct Field Access Through a Pointer

Two equivalent syntaxes work for accessing fields of a struct through a pointer:

```
p: *Foo = &my_foo

# dot — auto-derefs *Foo automatically
x: i32 = p.x

# arrow — C-style, identical to dot on pointer types
x: i32 = p->x
```

For explicit dereference followed by field access:

```
x: i32 = p.*.x     # .* dereferences, then .x accesses the field
```

Assignment works the same way:

```
p->x  = 10
p.*.x = 10     # identical
p.x   = 10     # identical
```

> `->` and `.` on a `*T` are interchangeable — both auto-deref the pointer.

## Pointer Arithmetic

```
arr: [5]i32 = [1, 2, 3, 4, 5]
p: *i32 = &arr[0]
q: *i32 = p + 2    # points to arr[2]
@pf("{q.*}\n")     # 3
```

## Nullable Pointers

Raw pointers can be `null`:

```
p: *i32 = null
if p != null {
    @pf("{p.*}\n")
}
```

## Allocation

Use `@alo(T)` to heap-allocate a single value (raw pointer, manual free):

```
p: *i32 = @alo(i32)
p.* = 42
# ...
@free(p)
```

Use `@new(T)` for reference-counted smart pointer allocation:

```
p: ^i32 = @new(i32)
p.* = 42
@release(p)    # RC drop; frees when count reaches 0
```

For raw byte buffers use `@alo` (allocate) and `@free`:

```
buf: *u8 = @alo(1024)
defer @free(buf)
```

## Stack Allocation

Prefer stack allocation when the lifetime is bounded to a scope:

```
buf: [1024]u8 = undef      # on stack
p: *u8 = &buf[0]
```

## Pointer Types Summary

| Syntax | Meaning |
|---|---|
| `*T` | raw single pointer |
| `**T` | pointer to pointer |
| `*u8` | typical byte pointer / C `char *` |
| `[]T` | slice (fat pointer: `{*T, usize}`) |
| `*[N]T` | pointer to fixed array |

## Pointer Casts

```
p: *i32 = &x
pb: *u8 = @bitcast(*u8, p)   # reinterpret pointer type
```

## `@size` and `@align`

```
sz: usize  = @size(i32)    # 4
al: usize  = @align(f64)   # 8
```

## Common Patterns

**Out-parameter:**

```
fn parse(s: str, out: *i32) -> bool {
    # write result into *out, return success
}

n: i32  = undef
ok: bool = parse("42", &n)
```

**Linked list node:**

```
struct Node {
    value: i32,
    next:  *Node,     # pointer to next (or null)
}
```

**Opaque handle (C FFI):**

```
extern struct SDL_Window   # declared but not defined
type WindowHandle = *SDL_Window
```
