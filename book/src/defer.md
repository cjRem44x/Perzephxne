# Defer

`defer` pushes a statement onto a LIFO stack that runs when the enclosing scope exits — whether by `ret`, `break`, `continue`, or falling off the end.

## Basic Usage

```
fn read_file(path: str) -> !str {
    fd: i32 = open(path)?
    defer close(fd)           # runs when the function returns

    buf: [4096]u8 = undef
    n: isize = read(fd, &buf[0], @sizeof(buf))
    ret str{.data=&buf[0], .len=@usize(n)}
}
```

`close(fd)` runs after the `ret` — the file is always closed.

## Ordering

Multiple defers run in **reverse** (last-in, first-out) order:

```
defer @pf("1\n")
defer @pf("2\n")
defer @pf("3\n")
# prints: 3, 2, 1
```

## Defer in Loops

Each loop iteration has its own defer stack:

```
for i in 0..3 {
    defer @pf("end {i}\n")
    @pf("start {i}\n")
}
# start 0, end 0, start 1, end 1, start 2, end 2
```

## Resource Cleanup Pattern

```
fn process() -> !void {
    conn: *Conn = db_connect("...")?
    defer db_close(conn)

    tx: *Tx = db_begin(conn)?
    defer db_rollback(tx)      # safety net

    do_work(tx)?
    db_commit(tx)              # commit first; rollback defer still runs but is a no-op on committed tx
}
```

## Defer with Blocks

```
defer {
    cleanup_a()
    cleanup_b()
}
```

## Notes

- The expression in `defer` is evaluated **when the defer is reached** (except for function calls whose arguments are evaluated immediately)
- `defer` cannot appear at the top level — only inside function bodies
- The deferred statements run *before* any stack variables are released, so pointers to local variables in deferred calls are safe
