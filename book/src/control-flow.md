# Control Flow

## Statement Separation

Statements are separated by newlines — no semicolons required. This is not newline-*sensitive* parsing, though: a statement's expression keeps extending across the newline for as long as the next token is a valid postfix continuation (`.`, `(`, `[`, `.*`, `.^`, `->`). That matters when a statement happens to end in something callable/indexable and the *next* statement starts with `(`, `[`, or `.` — the two merge into one expression instead of parsing as two statements, the same pitfall JavaScript's automatic-semicolon-insertion has:

```
buf: *u8 = malloc(4)
(buf + 1).* = 200u8     # WRONG: parses as malloc(4)(buf + 1).* — a call on malloc's result,
                        # which then hits the '=' unexpectedly and fails to compile
```

The fix is the same idiom used throughout this book and the standard library: name the pointer expression first, then dereference the name, so the statement starts with a plain identifier instead of `(`:

```
buf: *u8 = malloc(4)
p: *u8 = buf + 1
p.* = 200u8              # fine — starts with an identifier, nothing to merge with
```

This only bites when a statement *starts* with `(`, `[`, or a bare `.` right after one that ends in a value — the overwhelmingly common case (a statement starting with an identifier or keyword) is never affected.

## if, elif, else

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

## `if` as an Expression

`if` can produce a value. The last expression in each block is the result:

```
x: i32 = 7

label: str = if x > 10 { "big" } else { "small" }

# elif chains work too
grade := if x >= 90 {
    "A"
} else if x >= 70 {
    "B"
} else {
    "C"
}

# multi-statement blocks — last expression is the value
n := if x > 0 {
    a: i32 = x * 2
    a + 1        # result
} else {
    0
}
```

The type is inferred from the `then` branch. Both branches must produce the same type.

## Loops

### `loop`

Infinite loop:

```
i: i32 = 0
loop {
    if i >= 10 { break }
    @pf("{i}\n")
    i += 1
}
```

### `while`

```
i: i32 = 0
while i < 10 {
    @pf("{i}\n")
    i += 1
}
```

### `for` — Range

```
for i => 0..10 {      # 0, 1, ..., 9
    @pf("{i}\n")
}

for i => 0..=10 {     # 0, 1, ..., 10
    @pf("{i}\n")
}
```

### `for` — Iterator (slice / array)

```
nums: [5]i32 = [10, 20, 30, 40, 50]

for v => nums {
    @pf("{v}\n")
}
```

With index:

```
for v, i => nums {
    @pf("[{i}] = {v}\n")
}
```

### `break` and `continue`

```
for i => 0..100 {
    if i == 5  { continue }   # skip 5
    if i == 10 { break }      # stop at 10
    @pf("{i}\n")
}
```

## when

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

Multi-value cases (`|` separates alternative patterns in one arm):

```
when x {
    1 | 2 | 3 => @pf("small\n"),
    _         => @pf("large\n"),
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

`when` also matches on `str` subjects, comparing by content (not pointer identity):

```
cmd: str = @cin("enter: ")

when cmd {
    "a"       => @pf("a!\n"),
    "b" | "c" => @pf("b or c!\n"),
    _         => @pf("invalid!\n"),
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

## Labels and `goto`

Labels mark a position within a function. `goto` jumps to a label. Both are **function-internal** — a label cannot be reached from outside the function it is declared in.

```
fn process(skip: bool) {
    if skip { goto done }

    @pf("doing work\n")
    # ... more statements ...

    done:
    @pf("finished\n")
}
```

Labels can appear before any statement, including in the middle of a block. Forward and backward jumps are both allowed:

```
fn retry_loop() {
    attempts: i64 = 0

    try_again:
    attempts += 1
    if attempts < 3 { goto try_again }

    @pf("gave up after {attempts} tries\n")
}
```

Jumping over a variable declaration is allowed. Code between the `goto` and the label is simply skipped.

`goto` is intentionally restricted to the current function to avoid the classic C pitfall of jumping into another function's stack frame.

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
