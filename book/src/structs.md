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
