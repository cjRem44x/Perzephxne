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

## Test Discovery

`przp test` finds tests two ways:

- **Inline**, following the entry file's own `import()` graph — any `test "..." { }` block in the entry file or anything it (transitively) imports, exactly like the example above.
- **`tests/`** — every `*.przp` file placed directly under the project's `tests/` directory is compiled in too, *whether or not anything imports it*. This is the place for tests that don't belong next to any particular module, or that exercise a module without wanting that module to declare its own test-only imports. A `tests/` file can still `import()` the module it's testing:

```
import(math = "src/math")

test "add negative numbers" {
    @assert(math.add(-2, -3) == -5)
}
```

`przp init` scaffolds `tests/example_test.przp` as a starting point.

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

## Test Artifacts

`przp test` writes its compiled binary and manifest sidecar into `tests/` rather than the project root — `tests/<package>_test` and `tests/<package>_test.tests`, plus a `tests/<package>_test.d` dependency sidecar (see [Build System § Incremental `run`](./build-system.md#incremental-run) for what `.d` files are; `test` doesn't currently use it to skip rebuilds, it's just written for consistency). All three are gitignored.

Before compiling, `przp test` sweeps `tests/` and deletes any leftover `*_test`/`*_test.tests`/`*_test.d` files that don't match the binary it's about to (re)write — so renaming `[package].name`, or accumulating artifacts across many `sac`/manual runs, doesn't leave dead binaries behind. This sweep never touches `*.przp` source files, even ones named like `login_test.przp` that happen to contain `_test` themselves.
