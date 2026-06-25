# Platform Detection

Perzephxne provides compile-time builtins for conditional compilation based on the target platform.

## OS Detection

```
if @os.linux   { @pf("linux\n") }
if @os.macos   { @pf("macos\n") }
if @os.windows { @pf("windows\n") }
if @os.freebsd { @pf("freebsd\n") }
```

These are compile-time constants (`bool`). The dead branch is removed by the compiler — no runtime overhead.

## Architecture Detection

```
if @arch.x86_64  { @pf("64-bit x86\n") }
if @arch.aarch64 { @pf("arm64\n") }
if @arch.riscv64 { @pf("riscv64\n") }
if @arch.wasm32  { @pf("wasm\n") }
```

## Platform-Specific Code Blocks

Large blocks of platform-specific code can use `when` for clarity:

```
when @os {
    .linux   => { use_epoll() },
    .macos   => { use_kqueue() },
    .windows => { use_iocp() },
    _        => { use_select() },
}
```

## Build Mode

```
if @debug   { validate_all() }
if @release { fast_path() }
```

## Endianness

```
if @endian.little { @pf("little-endian\n") }
if @endian.big    { @pf("big-endian\n") }
```

## Pointer Width

```
sz: usize = @ptr_width   # 4 or 8
```

## Conditional Import

```
if @os.linux {
    import "std/linux/epoll"
} else {
    import "std/posix/select"
}
```

## Feature Flags

Define custom feature flags in `przp.toml` and query them at compile time:

```toml
[features]
logging = true
```

```
if @feature.logging {
    log("request received")
}
```
