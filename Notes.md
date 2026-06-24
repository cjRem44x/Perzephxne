# Notes

A programming language influenced the by the modern schemes of things. No GC baggage, no OOP principles, just pure C flavored programming.

Source compiles into LLVM IR (llvm backend) as it represents a more flexible format then hardware assembly.

## Compiling

Using the Perzephxne build structure.
`przp init`
`przp build`
`przp run`

Producing the structure.
```
/project
    .git # fresh git repo
    /bin
        # compiled binaries
    /src
        /main
            /przp # source code
                main.przp
    przp.toml     # manage project deps
    .gitignore    # default for przp project
```

Down the road, like cargo, I would like simple `przp add <pkg>` to link deps.

Using Stand-Alone-Compiler.
`przp sac <files> -o=Name`
