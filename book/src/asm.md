# Inline Assembly

Inline assembly lets you embed target-specific instructions directly in a function body. The syntax mirrors LLVM's inline assembly format.

## Basic Syntax

```
@asm("instruction", constraints, operands)
```

## Example — Read the Cycle Counter

```
fn rdtsc() -> u64 {
    lo: u32 = undef
    hi: u32 = undef
    @asm("rdtsc", "=a,=d", &lo, &hi)
    ret @u64(lo) | (@u64(hi) << 32)
}
```

## Example — Atomic Fence

```
fn memory_fence() {
    @asm("mfence", "", )
}
```

## Constraints

Constraints follow AT&T/GCC notation:

| Constraint | Meaning |
|---|---|
| `=r` | write-only register output |
| `=m` | write-only memory output |
| `r` | register input |
| `m` | memory input |
| `i` | immediate integer |
| `a` | `eax`/`rax` |
| `d` | `edx`/`rdx` |

Separate multiple constraints with `,`.

## Volatile Assembly

Mark assembly that has side effects (memory, I/O) as volatile so the compiler doesn't move or remove it:

```
@asm_volatile("out %0, %1", "r,r", port, value)
```

## Clobbers

Specify registers the assembly modifies beyond the outputs:

```
@asm("cpuid", "=a,=b,=c,=d:a:~{rbx},~{rcx},~{rdx}", &eax, &ebx, &ecx, &edx, leaf)
```

## Use in Platform Blocks

```
fn yield_cpu() {
    when @arch {
        .x86_64  => @asm("pause", "", ),
        .aarch64 => @asm("yield", "", ),
        _        => {},   # no-op
    }
}
```

## Safety

Inline assembly is inherently unsafe. The compiler:
- Does not validate the instruction string
- Does not verify that constraint/operand counts match
- Cannot reason about side effects unless they are declared

Use sparingly, prefer intrinsics from `std/simd` or `std/atomic` when available.
