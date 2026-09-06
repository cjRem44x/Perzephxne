# Security Policy

Perzephxne is beta software (see [Status](README.md#status)). `przp` compiles
and runs code, invokes `clang`, and shells out to `git` for dependencies —
treat `przp.toml`/`przp.lock` and `.przp` source files from anyone you don't
trust the same way you'd treat an untrusted `Makefile` or `build.rs`.

## Reporting a Vulnerability

Please report suspected vulnerabilities privately rather than opening a
public issue: use GitHub's
[private vulnerability reporting](https://github.com/cjRem44x/Perzephxne/security/advisories/new)
for this repository.

Include:

- the affected version/commit
- reproduction steps or a minimal `przp.toml`/`.przp` file that triggers it
- the impact you'd expect (e.g. arbitrary command execution, path traversal)

We'll acknowledge reports and aim to ship a fix before any public disclosure.
There is no bug bounty program.

## Supported Versions

Only the latest commit on `main` is supported. There are no maintained
release branches yet.

## Scope

In scope: the `przp` compiler/CLI (`compiler/src`), the standard library
(`compiler/std`), and the build tool's handling of `przp.toml`/`przp.lock`
and dependency fetching.

Known, already-mitigated classes worth knowing about if you're auditing this
codebase (see `book/src/status-next.md` for the full history): manifest
fields (`[package].name`, `[build].link`, `[deps]` refs) are validated
against strict character allowlists before being used in any `system()`
call, since they're spliced into shell commands during `build`/`run`/`test`;
and archive extraction (`std/zip`) rejects entry paths that would escape the
destination directory (Zip Slip).
