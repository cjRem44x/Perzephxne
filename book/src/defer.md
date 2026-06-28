# Defer

`defer` pushes a statement onto a LIFO stack that runs when the enclosing scope exits — whether by `ret`, `break`, `continue`, or falling off the end.

## Basic Usage

```
import(io = "std/io")

fn process_file(path: str) {
    f: *u8 = io.open(path, "r")
    if f == null { ret }
    defer io.close(f)    # runs when the function returns

    line: str = io.read_line(f)
    @pf("{line}\n")
}
```

`io.close(f)` runs after the `ret` — the file is always closed.

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
for i => 0..3 {
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
