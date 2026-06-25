# Naming Conventions

These are conventions, not compiler-enforced rules. Following them makes code easier to read alongside the standard library.

## Summary Table

| Item | Convention | Example |
|---|---|---|
| Variables | `snake_case` | `byte_count`, `is_valid` |
| Functions | `snake_case` | `parse_int`, `read_line` |
| Constants | `UPPER_SNAKE_CASE` | `MAX_SIZE`, `PI` |
| Struct types | `PascalCase` | `Vec2`, `HttpRequest` |
| Enum types | `PascalCase` | `Direction`, `IoError` |
| Enum variants | `PascalCase` | `Direction.North` |
| Type aliases | `PascalCase` | `type Byte = u8` |
| Type parameters | single uppercase letter or `PascalCase` | `T`, `Key`, `Val` |
| Module names | `snake_case` | `import "std/io"` |
| File names | `snake_case.przp` | `http_client.przp` |

## Variables and Functions

```
item_count: usize = 0
is_running: bool  = true

fn calculate_area(width: f64, height: f64) -> f64 {
    ret width * height
}
```

## Constants

Constants use `UPPER_SNAKE` to distinguish them from mutable state at a glance:

```
MAX_CONNECTIONS: usize : 1024
DEFAULT_TIMEOUT: f64   : 30.0
```

## Types

```
struct RequestBody { ... }
enum ParseError { ... }
type Callback = fn(i32) -> bool
```

## Booleans

Boolean variables and functions returning `bool` should read as assertions:

```
is_valid: bool     = true
has_error: bool    = false
can_retry: bool    = true

fn is_empty(s: str) -> bool { ret s.len == 0 }
fn has_prefix(s: str, p: str) -> bool { ... }
```

## Acronyms

Treat acronyms as single words — do not ALL-CAPS them inside names:

```
# preferred
http_client: HttpClient
parse_url: fn(str) -> Url

# avoid
http_client: HTTPClient
parse_URL: fn(str) -> URL
```

Exception: a name that *is* the acronym stands alone and uses UPPER:

```
PI: f64 : 3.14159
URL: str = "..."
```

## Module Aliases

When a module name is long or conflicts with a local name, alias it with a short, obvious prefix:

```
import "mylib/geometry" as geo
import "std/fmt" as fmt

p: geo.Point = geo.Point{.x=0.0, .y=0.0}
s: str = fmt.sprintf("{p.x}, {p.y}")
```
