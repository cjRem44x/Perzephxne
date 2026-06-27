#pragma once
#include "ast.h"
#include <stdio.h>

/* Emit LLVM IR text for the module to the given file.
   release=1 sets @release=true/@debug=false. Returns 0 on error. */
int codegen(Module *mod, FILE *out, int release);
