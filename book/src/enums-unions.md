# Enums & Unions

## Enums

Enums come in two forms: plain enums (named integer constants) and payload enums (sum types, where variants carry values).

### Plain Enums

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

### Payload Enums (Sum Types)

Variants can carry values. Write the payload types in parentheses after the variant name:

```
enum Shape {
    Circle(f64),
    Rect(f64, f64),
    Point,
}
```

Construct a variant by calling it; unit variants are named directly:

```
a: Shape = Shape.Circle(2.0)
b: Shape = Shape.Rect(3.0, 4.0)
c: Shape = Shape.Point
```

Match with `when` and bind the payload values by position:

```
area: f64 = when s {
    Shape.Circle(r)  => 3.14159 * r * r,
    Shape.Rect(w, h) => w * h,
    Shape.Point      => 0.0,
}
```

A single binder on a multi-value variant receives the whole payload as a [tuple](./variables-types.md#tuples):

```
when b {
    Shape.Rect(t) => @pf("{t.0} x {t.1}\n"),
    _             => {},
}
```

A multi-value payload is stored as a tuple internally; the representation is a discriminant plus payload storage sized to the largest variant (the same layout as a [tagged union](#tagged-unions)). A payload enum cannot also have a backing integer type.

## Integer-Backed Enum

Add `=> TYPE` after the name to specify a backing integer. Variants can have explicit values:

```
enum Color => u8 {
    Red   = 0,
    Green = 1,
    Blue  = 2,
}

c: Color = Color.Red
n: u8    = @u8(c)       # 0
```

## `impl` on Enums

```
enum Status {
    Ok,
    Err,
    Pending,
}

impl Status {
    fn label(self: Status) -> str {
        ret when self {
            Status.Ok      => "ok",
            Status.Err     => "error",
            Status.Pending => "pending",
        }
    }
}

s: Status = Status.Ok
@pf("{s.label()}\n")
```

## Unions

`unn` overlays all fields at the same memory address (unsafe):

```
unn FloatBits {
    f: f32,
    u: u32,
}

x: FloatBits = FloatBits{.f=1.0}
@pf("{x.u:#x}\n")   # 3f800000
```

Reading the field that wasn't most recently written is defined bit-level behavior in Perzephxne.

## Tagged Unions

Tagged unions store a discriminant plus enough payload space for the largest variant. Construct them with exactly one variant field, and match them with `when`.

```
unn Shape => enum {
    circle: i32,
    point,
}

fn describe(s: Shape) {
    when s {
        .circle r => @pf("circle {r}\n")
        .point    => @pf("point\n")
    }
}

a: Shape = Shape{.circle = 9}
b: Shape = Shape{.point}

describe(a)
describe(b)
```

Variant patterns use `.name`. If the variant carries a payload, add a binding name after the pattern.

The [payload enum](#payload-enums-sum-types) syntax is sugar for exactly this representation — `enum Shape { Circle(f64), Point }` and `unn Shape => enum { Circle: f64, Point }` produce the same type, and the two pattern forms (`Shape.Circle(r)` and `.Circle r`) are interchangeable.
