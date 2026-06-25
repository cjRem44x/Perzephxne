#pragma once
#include "ast.h"

/* Walk the module, resolve names, annotate Expr->ty.
   Returns 0 on error. */
int sema_check(Module *mod);
