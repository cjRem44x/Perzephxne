# Enums & Unions

## Basic Enum

```
enum Direction {
    North,
    South,
    East,
    West,
}

d: Direction = Direction.North

when d {
    Direction.North => @pf("going north\n"),
    Direction.South => @pf("going south\n"),
    Direction.East  => @pf("going east\n"),
    Direction.West  => @pf("going west\n"),
}
```

## Enum with Data (Tagged Union)

Each variant can carry a value:

```
enum Shape {
    Circle(f64),
    Rect(f64, f64),
    Point,
}

s: Shape = Shape.Circle(5.0)

area: f64 = when s {
    Shape.Circle(r)  => 3.14159 * r * r,
    Shape.Rect(w, h) => w * h,
    Shape.Point      => 0.0,
}
```

Multiple fields per variant use a struct-like syntax:

```
enum Event {
    KeyPress  { key: u32, mods: u8 },
    MouseMove { x: f32, y: f32 },
    Quit,
}

e: Event = Event.MouseMove{.x=10.0, .y=20.0}

when e {
    Event.KeyPress{key, mods} => @pf("key {key} mods {mods}\n"),
    Event.MouseMove{x, y}     => @pf("mouse {x},{y}\n"),
    Event.Quit                => @pf("quit\n"),
}
```

## `impl` on Enums

```
impl Shape {
    fn area(self: Shape) -> f64 {
        ret when self {
            Shape.Circle(r)  => 3.14159 * r * r,
            Shape.Rect(w, h) => w * h,
            Shape.Point      => 0.0,
        }
    }
}

s: Shape = Shape.Rect(3.0, 4.0)
@pf("{s.area()}\n")   # 12.0
```

## Integer-Backed Enum

Add `: TYPE` to specify a backing integer:

```
enum Color: u8 {
    Red   = 0,
    Green = 1,
    Blue  = 2,
}

c: Color = Color.Red
n: u8    = @u8(c)       # 0
c2: Color = @Color(2)   # Color.Blue
```

## C-Compatible Enum

For FFI, use `: c_int`:

```
extern enum errno_t: i32 {
    EPERM  = 1,
    ENOENT = 2,
    EIO    = 5,
}
```

## Raw Unions

`union` overlays all fields at the same memory address (unsafe):

```
union FloatBits {
    f: f32,
    u: u32,
}

x: FloatBits = FloatBits{.f=1.0}
@pf("{x.u:#x}\n")   # 3f800000
```

Reading the field that wasn't most recently written is defined bit-level behavior in Perzephxne.

## Option and Result Patterns

The standard library provides:

```
# generic Option
enum Option[T] { Some(T), None }

# generic Result
enum Result[T, E] { Ok(T), Err(E) }
```

These interact with the `?` propagation operator and `!T` failable return syntax.
