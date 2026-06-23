# Notes

```
ls Foos: Foo # define a List

enum X :
| ONE | TWO | THREE

# mapping to specific vals
enum Y: | ONE=3.145 | TWO="lol"

dat Foo:
| x | y | z

lbl:
| ...

proc Foo(x, y, z) -> value:
| ...

# driver
START:
| pf "Hello"

| go lbl

| f = Foo
| f.x = ...
| f2 = Foo: x=0, y="word", z=11 # init vals
| foos = Foos | foos.add(f) | foos.add(f2)

| x = foo(...)

| var = X.ONE
```
