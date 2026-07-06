#pragma once
#include "ast.h"
#include <stdio.h>

/* Emit LLVM IR text for the module to the given file.
   release=1 sets @release=true/@debug=false. If test_mode is set, any
   user `fn main()` is ignored and a test-dispatch main is generated
   instead (see `przp test`); out_path (the eventual binary path, not
   `out`) is used to write "<out_path>.tests", a sidecar listing each
   discovered test's index/name/source file for the test runner in
   main.c to read back without re-parsing the program itself. Returns 0
   on error. */
int codegen(Module *mod, FILE *out, int release, int test_mode, const char *out_path);
