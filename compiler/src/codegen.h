#pragma once
#include "ast.h"
#include <stdio.h>

/* Emit LLVM IR text for the module to the given file.
   Returns 0 on error. */
int codegen(Module *mod, FILE *out);
