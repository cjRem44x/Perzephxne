# Operator Precedence

Operators are listed from highest to lowest precedence.

| Precedence | Operators | Associativity | Notes |
|---|---|---|---|
| 14 | `()` `[]` `.` `.*` | left | call, index, field, deref |
| 13 | unary `-` `!` `~` `&` `*` | right | negate, logical-not, bitwise-not, address-of, deref |
| 12 | `*` `/` `%` | left | multiplication, division, modulo |
| 11 | `+` `-` | left | addition, subtraction |
| 10 | `..` `..=` | left | range |
| 9 | `<<` `>>` | left | bit shift |
| 8 | `<` `>` `<=` `>=` | left | relational comparison |
| 7 | `==` `!=` | left | equality comparison |
| 6 | `&` | left | bitwise AND |
| 5 | `^` | left | bitwise XOR |
| 4 | `\|` | left | bitwise OR |
| 3 | `&&` | left | logical AND |
| 2 | `\|\|` | left | logical OR |
| 1 | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | right | assignment (statement only) |

This table is the C family's own precedence ordering, not a cleaned-up
one — bitwise `&`/`^`/`|` bind *looser* than every comparison, and
relational (`<` `>` `<=` `>=`) binds tighter than equality (`==` `!=`),
both of which read as backwards on first glance if you're used to a
language that fixed this (Python, Rust, Zig all bind bitwise ops
tighter than comparisons; C, and this table, don't). Concretely:
`flags & MASK != 0` parses as `flags & (MASK != 0)` — a `u32`/`bool`
type mismatch, not the "did this bit come out nonzero" check it looks
like — and `a < b == c < d` parses as `(a < b) == (c < d)`, not
left-to-right as four terms in one chain. Always parenthesize a bitwise
op the moment it sits next to a comparison: `(flags & MASK) != 0`.

Range (`..`/`..=`) binds far tighter than its position in a mental
model built from "loosest, right above assignment" would suggest —
tighter than shift, comparison, bitwise, and logical, only looser than
arithmetic. `0..n + 1` is `0..(n + 1)`, matching the common case
(a range endpoint computed from an expression) without parens, but
`0..n & mask` is `(0..n) & mask`, not `0..(n & mask)`.

## Notes

- Assignment operators are **statements** — they do not produce values and cannot appear in expressions.
- Dereference `.*` is postfix: `p.*` not `*p`.
- Address-of `&` is prefix: `&x`.
- `!` is logical NOT for booleans; `~` is bitwise NOT for integers.

## Examples

```
# parsed as (a + b) * c
x: i32 = (a + b) * c

# parsed as a + (b * c)
y: i32 = a + b * c

# comparison before logical
z: bool = a < b && c > d

# bitwise vs. comparison — the C-style gotcha above; parenthesize
ok:  bool = (flags & MASK) != 0
# bad: bool = flags & MASK != 0   # flags & (MASK != 0) -- type error

# range binds tighter than +/- would suggest from its "near assignment"
# position further down this table -- looser than arithmetic, tighter
# than everything else
r: []i32 = arr[0..n + 1]

# deref and field
v: f64 = p.*.x        # deref p, then access field x
```

## No Operator Overloading

Operators cannot be overloaded. Custom types use named methods instead:

```
impl Vec2 {
    fn add(a: Vec2, b: Vec2) -> Vec2 { ... }
}

c: Vec2 = Vec2.add(a, b)    # not: a + b
```

## `str` Concatenation

`str` is the one built-in exception: `+` and `+=` are wired into the
compiler for concatenation (not user overloading, and not extendable to
other types). `+` builds a fresh string; `+=` appends onto an existing
binding in place. Every other operator — `-`, `*`, `-=`, etc. — is
rejected with a compile error when either side is a `str`.

```
a: str = "hello"
b: str = " world"

c: str = a + b   # "hello world" — a and b are unchanged
a += b            # a becomes "hello world"
a += "!"          # a becomes "hello world!"
```

`==`/`!=` on `str` are also built in (see [Variables & Types](./variables-types.md)) — they compare contents, not pointers.
