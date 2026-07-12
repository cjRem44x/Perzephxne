# Operator Precedence

Operators are listed from highest to lowest precedence.

| Precedence | Operators | Associativity | Notes |
|---|---|---|---|
| 13 | `()` `[]` `.` `.*` | left | call, index, field, deref |
| 12 | unary `-` `!` `~` `&` `*` | right | negate, logical-not, bitwise-not, address-of, deref |
| 11 | `*` `/` `%` | left | multiplication, division, modulo |
| 10 | `+` `-` | left | addition, subtraction |
| 9 | `<<` `>>` | left | bit shift |
| 8 | `&` | left | bitwise AND |
| 7 | `^` | left | bitwise XOR |
| 6 | `\|` | left | bitwise OR |
| 5 | `==` `!=` `<` `>` `<=` `>=` | left | comparison |
| 4 | `&&` | left | logical AND |
| 3 | `\|\|` | left | logical OR |
| 2 | `..` `..=` | left | range |
| 1 | `=` `+=` `-=` `*=` `/=` `%=` `&=` `\|=` `^=` `<<=` `>>=` | right | assignment (statement only) |

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

# range
r: range = 0..10

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
