# Literals

## Integer Literals

```
42           # decimal — default type i32
0xFF         # hex
0b1010_1010  # binary (underscore separators allowed anywhere)
0o17         # octal
1_000_000    # decimal with separators
```

Type suffixes (when context can't infer):

```
42u8    42u16    42u32    42u64
42i8    42i16    42i32    42i64
42usize
```

## Float Literals

```
3.14         # default type f64
1.5e-3       # scientific notation
1_000.0      # underscores allowed
```

Type suffixes:

```
3.14f16    3.14f32    3.14f64
```

A suffix pins the literal's type — `x := 3.14f32` infers `f32` instead of the default `f64`.

## Character Literals

```
c: char = 'A'
nl: char = '\n'
```

## Boolean Literals

```
true    false
```

## String Literals

```
"hello world"
```

Escape sequences:

| Sequence | Meaning |
|---|---|
| `\n` | newline |
| `\t` | tab |
| `\r` | carriage return |
| `\0` | null byte |
| `\\` | backslash |
| `\"` | double quote |
| `\'` | single quote |
| `\xHH` | hex byte |
| `\uHHHH` | Unicode code point (UTF-8) |

## Default Literal Types

| Literal | Default type |
|---|---|
| `42` | `i32` |
| `3.14` | `f64` |
| `'A'` | `char` |
| `true` / `false` | `bool` |
| `"..."` | `str` |
