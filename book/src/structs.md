# Structs & Impl

## Structs

```
struct Vec2 {
    x: f64,
    y: f64,
}
```

## Instantiation

Field names must be specified with `.name=`:

```
v: Vec2 = Vec2{.x=1.0, .y=2.0}
```

Partial initialization (remaining fields are `undef`):

```
v: Vec2 = Vec2{.x=1.0}
```

## Field Access

```
@pf("{v.x} {v.y}\n")
v.x = 5.0
```

## Nested Structs

```
struct Line {
    start: Vec2,
    end:   Vec2,
}

l: Line = Line{
    .start = Vec2{.x=0.0, .y=0.0},
    .end   = Vec2{.x=1.0, .y=1.0},
}

dx: f64 = l.end.x - l.start.x
```

## Impl Blocks

`impl` attaches functions to a struct. They are called with dot syntax. The first parameter conventionally named `self` receives the struct by value; use `*Self` for mutation.

```
impl Vec2 {
    fn len(self: Vec2) -> f64 {
        ret @sqrt(self.x * self.x + self.y * self.y)
    }

    fn scale(self: *Vec2, factor: f64) {
        self.*.x *= factor
        self.*.y *= factor
    }
}

v: Vec2 = Vec2{.x=3.0, .y=4.0}
@pf("{v.len()}\n")     # 5.0

v.scale(2.0)
@pf("{v.x}\n")         # 6.0
```

### Self Parameter Kinds

The first parameter's declared kind decides both what it means and which receivers can call it:

| Declaration | Meaning | Callable on |
|---|---|---|
| `self: T` | by value — a genuine copy; mutations inside the method never affect the caller's value | `T`, `*T`, or `^T` (all copy into the method) |
| `self: *T` | raw pointer — mutations are visible to the caller | `T` (auto-addressed) or `*T` |
| `self: ^T` | smart pointer — mutations are visible to the caller | `^T` only (a plain `T` has no reference-count header to match against) |
| `self: @self` | polymorphic reference — see below | `T`, `*T`, or `^T`, uniformly by reference |

Calling a method through the wrong receiver kind for `*T`/`^T` self params is a compile error, not a silent misread — raw and smart pointers have different memory layouts (`^T` carries an 8-byte reference-count header `*T` doesn't), so the compiler rejects mixing them rather than guessing.

### `@self` — Receiver-Agnostic Self

`self: @self` accepts a value, `*T`, or `^T` receiver interchangeably — one method body instead of writing (or being limited to) one specific kind:

```
struct vec2 { x: i32, y: i32 }

impl vec2 {
    fn new(x: i32, y: i32) -> vec2 { ret vec2{.x=x, .y=y} }
    fn scale(self: @self, k: i32) {
        self.x *= k
        self.y *= k
    }
}

v := vec2.new(1, 2)
v.scale(2)                            # v is now (2, 4) — plain values are auto-referenced

p: *vec2 = @alo(vec2)
p.* = vec2.new(3, 4)
p.scale(10)                           # (30, 40)

q: ^vec2 = @new(vec2.new(5, 6))
q.scale(3)                            # (15, 18)
```

Unlike `self: T`, `@self` is always a reference — calling it on a plain value still mutates that value, the same way `self: *T` would. Use `self: T` when you want a guaranteed copy and `self: @self` when the method should work uniformly no matter how callers happen to hold the struct. `@self` is only valid as an impl method's first (receiver) parameter — using it elsewhere, or past the first parameter, is a compile error.

## Static Methods

Methods with no `self` parameter are static — called on the type directly:

```
impl Vec2 {
    fn add(a: Vec2, b: Vec2) -> Vec2 {
        ret Vec2{.x=a.x+b.x, .y=a.y+b.y}
    }
}

sum: Vec2 = Vec2.add(v, Vec2{.x=1.0, .y=0.0})
```

## Encapsulation: `@opaque` and `pub`

By default every struct field and every `impl` method is public — visible to any code that can see the struct itself. `@opaque` on a struct changes that: its fields (and, inside its `impl` block, its methods) become private outside that struct's own `impl` block. `pub fn` marks the methods meant as the deliberate public API surface:

```
@opaque
struct Counter {
    n: i32,
}

impl Counter {
    pub fn new(start: i32) -> Counter { ret Counter{.n=start} }
    pub fn value(self: Counter) -> i32 { ret self.n }

    pub fn increment(self: *Counter) {
        self.bump()      # impl always has access to its own struct's private members
    }

    fn bump(self: *Counter) {    # no `pub` — private outside this impl block
        self.*.n += 1
    }
}

c: Counter = Counter.new(10)
c.increment()
@pf("{c.value()}\n")     # 11

c.n           # compile error: field 'n' is private on opaque struct 'Counter'
c.bump()      # compile error: method 'bump' is private ... — mark it 'pub' to call it from outside its impl block
Counter{.n=5} # compile error: cannot construct opaque struct 'Counter' with field-literal syntax outside its impl block
```

Rules:

- **Fields** are private outside `@opaque`'s own `impl` block, full stop — there's no per-field `pub` for fields, only for methods.
- **Field-literal construction** (`Counter{.n=5}`) also requires being inside the matching `impl` block, since it names fields directly — this is what makes a `pub fn new(...)` constructor the actual entry point from outside, rather than a bypassable convention.
- **Methods** without `pub` are private outside the `impl` block the same way fields are; `pub fn` methods are callable from anywhere the struct itself is visible, the same as on an ordinary struct.
- **Inside** the matching `impl` block, everything is accessible regardless of `pub` — one method can freely call another private method or read/write private fields on `self` or on another instance of the same struct.
- A **different** struct's `impl` block is still "outside" — `@opaque struct A`'s fields stay private even from `impl B`'s methods.
- On an **ordinary** (non-`@opaque`) struct, `pub` is accepted but has no effect — every field and method there is already public, matching the struct's normal fully-open default.

Use `@opaque` when a struct's internal representation is something callers shouldn't depend on (so it can change later without breaking them) and you want the compiler to enforce that instead of relying on a naming convention.

## Packed Structs

```
@packed
struct Flags {
    active:  bool,
    count:   u8,
    value:   u16,
}
```

`@packed` removes padding — useful for binary protocols.

## Memory Layout

By default structs follow C ABI alignment rules. Use `@packed` for tight packing or `@align(N)` for explicit alignment:

```
@align(64)
struct CacheLine {
    data: [64]u8,
}
```
