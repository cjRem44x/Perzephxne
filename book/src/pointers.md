# Pointers & Memory

Perzephxne has no garbage collector. Memory is managed manually or with smart pointers from the standard library.

## Raw Pointers

```
x: i32  = 42
p: *i32 = &x      # address-of
v: i32  = p.*     # dereference
p.* = 100         # write through pointer
```

Pointer arithmetic:

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

```
import "std/mem"

p: *i32  = mem.alloc(i32)
p.* = 42
# ...
mem.free(p)
```

Array allocation:

```
buf: *u8 = mem.alloc_n(u8, 1024)
defer mem.free(buf)
```

## Smart Pointers

The standard library provides smart pointers that free automatically:

| Type | Description |
|---|---|
| `Box[T]` | unique ownership — frees on scope exit |
| `Rc[T]` | shared ownership — frees when last reference drops |
| `Weak[T]` | non-owning reference to `Rc[T]` |

```
import "std/box"

b: Box[i32] = Box.new(42)
@pf("{b.get().*}\n")
# freed at end of scope
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

## `@sizeof` and `@alignof`

```
sz: usize  = @sizeof(i32)    # 4
al: usize  = @alignof(f64)   # 8
```

## `@ptrof` and `@addrof`

```
fn my_fn() {}
fp: fn() = @ptrof(my_fn)    # function pointer from name
addr: usize = @addrof(my_fn)
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
