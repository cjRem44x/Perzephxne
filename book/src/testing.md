# Testing

`test "name" { ... }` declares a test block. It's a plain top-level item, written anywhere in a `.przp` file alongside the code it tests — no separate test files or special naming convention required.

```
fn add(a: i32, b: i32) -> i32 {
    ret a + b
}

test "addition works" {
    @assert(add(2, 3) == 5)
}

fn main() -> i32 {
    @pf("real program\n")
    ret 0
}
```

Test blocks are invisible to `przp build`/`przp run`/`przp sac` — they compile (so they never silently bit-rot into something that doesn't parse) but are never called from `fn main()` or anywhere else. Only `przp test` runs them.

## Running Tests

| Command | Runs |
|---|---|
| `przp test` | every test block in the project (entry file and everything it `import()`s) |
| `przp test <file>` | only the test blocks declared in `<file>` |
| `przp test <file> <name>` | the one test named `<name>` in `<file>` |
| `przp test [...] --release` | same, compiled in release mode instead of the default debug mode |

```
przp test                              # everything
przp test src/math.przp                # just src/math.przp's tests
przp test src/math.przp "addition works"  # just that one test
```

Each test runs in its own subprocess — a crash or `@panic` in one test can't take any other test down with it or stop the run. Output looks like:

```
PASS  addition works
FAIL  addition is wrong on purpose
1 passed, 1 failed
```

`przp test` exits `0` if every selected test passed, `1` if any failed — suitable for CI.

## Pass and Fail

A test that runs to completion without panicking has already passed — most tests need nothing beyond `@assert`:

```
test "division works" {
    @assert(10 / 2 == 5)
}
```

Two builtins make the result explicit when that's useful:

| Builtin | Effect |
|---|---|
| `@pass()` | stop the test immediately and mark it passed |
| `@fail(msg?)` | stop the test immediately, print `msg` if given, and mark it failed |

```
test "explicit pass and fail" {
    if some_condition() {
        @pass()   # done here, skip the rest of the test
    }
    @fail("some_condition() was never true")
}
```

`@fail` behaves like `@panic` — same "print and stop" shape — under a name that reads naturally inside a test.

## Debug Mode by Default

`przp test` compiles in debug mode unless `--release` is passed, since `@assert` — the mechanism most tests rely on — is stripped out entirely in release builds (see [Build System § Build Modes](./build-system.md#build-modes)). Pass `--release` explicitly if you specifically want to verify release-mode behavior.
