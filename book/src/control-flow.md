# Control Flow

## `if` / `elif` / `else`

```
x: i32 = 42

if x > 100 {
    @pf("big\n")
} elif x > 10 {
    @pf("medium\n")
} else {
    @pf("small\n")
}
```

Braces are required. Conditions do not need parentheses.

## `loop`

Infinite loop:

```
i: i32 = 0
loop {
    if i >= 10 { break }
    @pf("{i}\n")
    i += 1
}
```

## `while`

```
i: i32 = 0
while i < 10 {
    @pf("{i}\n")
    i += 1
}
```

## `for` — Range

```
for i in 0..10 {      # 0, 1, ..., 9
    @pf("{i}\n")
}

for i in 0..=10 {     # 0, 1, ..., 10
    @pf("{i}\n")
}
```

## `for` — Iterator (slice / array)

```
nums: [5]i32 = [10, 20, 30, 40, 50]

for v => nums {
    @pf("{v}\n")
}
```

With index:

```
for i, v => nums {
    @pf("[{i}] = {v}\n")
}
```

## `break` and `continue`

```
for i in 0..100 {
    if i == 5  { continue }   # skip 5
    if i == 10 { break }      # stop at 10
    @pf("{i}\n")
}
```

## `when` — Pattern Match

`when` is exhaustive — the compiler enforces all cases are covered.

```
x: i32 = 3

when x {
    1 => @pf("one\n"),
    2 => @pf("two\n"),
    3 => @pf("three\n"),
    _ => @pf("other\n"),
}
```

Multi-value cases:

```
when x {
    1, 2, 3 => @pf("small\n"),
    _        => @pf("large\n"),
}
```

Range cases:

```
when x {
    0..=9   => @pf("single digit\n"),
    10..=99 => @pf("double digit\n"),
    _       => @pf("big\n"),
}
```

Matching enums:

```
enum Shape { Circle(f64), Rect(f64, f64) }

s: Shape = Shape.Circle(3.0)

when s {
    Shape.Circle(r)    => @pf("circle r={r}\n"),
    Shape.Rect(w, h)   => @pf("rect {w}x{h}\n"),
}
```

## `when` as Expression

```
label: str = when x {
    1 => "one",
    2 => "two",
    _ => "other",
}
```

## `ret`

Return from the current function:

```
fn abs(x: i32) -> i32 {
    if x < 0 { ret -x }
    ret x
}
```

Early return without a value:

```
fn check(x: i32) {
    if x < 0 { ret }
    @pf("ok\n")
}
```
