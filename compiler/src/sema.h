#pragma once
#include "ast.h"

/* Walk the module, resolve names, annotate Expr->ty.
   Returns 0 on error. */
int sema_check(Module *mod);

/* Render a Type as a Perzephxne source-syntax string (e.g. "^vec2", "[]i32",
   "!f64"), recursing into composite kinds. Used for diagnostics; also used
   by codegen's @typeof to avoid a second, drift-prone type-name renderer. */
const char *ty_str(Type *t);
