# Platform Detection

Perzephxne provides compile-time boolean builtins for conditional compilation based on the target platform. Dead branches are removed by the compiler — no runtime overhead.

## OS Detection

```
if @os.linux   { @pf("linux\n") }
if @os.mac     { @pf("macOS\n") }
if @os.windows { @pf("windows\n") }
```

## Architecture Detection

```
if @arch.x86_64 { @pf("64-bit x86\n") }
if @arch.arm64  { @pf("AArch64\n") }
if @arch.x86    { @pf("32-bit x86\n") }
if @arch.arm    { @pf("32-bit ARM\n") }
```

## Build Mode

```
if @debug   { validate_all() }
if @release { fast_path() }
```

## Combining Conditions

```
fn init_network() {
    if @os.linux {
        setup_epoll()
    }
    if @os.mac {
        setup_kqueue()
    }
    if @os.windows {
        setup_iocp()
    }
}
```

## Compile-Time Constants Reference

| Builtin | Type | True when |
|---|---|---|
| `@os.linux` | `bool` | target is Linux |
| `@os.mac` | `bool` | target is macOS |
| `@os.windows` | `bool` | target is Windows |
| `@arch.x86_64` | `bool` | target is x86-64 |
| `@arch.arm64` | `bool` | target is AArch64 |
| `@arch.x86` | `bool` | target is 32-bit x86 |
| `@arch.arm` | `bool` | target is 32-bit ARM |
| `@debug` | `bool` | compiled without `--release` |
| `@release` | `bool` | compiled with `--release` |
