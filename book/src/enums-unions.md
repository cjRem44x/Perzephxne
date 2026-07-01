# Enums & Unions

## Enums

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
