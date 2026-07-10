#include "arena.h"
#include "error.h"
#include "parser.h"
#include "sema.h"
#include "codegen.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <ctype.h>
#include <dirent.h>

/* explicit POSIX declaration for readlink (required under -std=c11 -Wpedantic) */
extern ssize_t readlink(const char *path, char *buf, size_t bufsiz);

/* ── Utilities ────────────────────────────────────────────────────────────── */

static char *read_file_or_null(const char *path, char *err, size_t errsz) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, errsz, "%s", strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        snprintf(err, errsz, "%s", strerror(errno));
        fclose(f);
        return NULL;
    }
    long sz = ftell(f);
    if (sz < 0) {
        snprintf(err, errsz, "%s", strerror(errno));
        fclose(f);
        return NULL;
    }
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        snprintf(err, errsz, "out of memory");
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    if (got != (size_t)sz && ferror(f)) {
        snprintf(err, errsz, "%s", strerror(errno));
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[got] = '\0';
    fclose(f);
    return buf;
}

/* ── Module system ─────────────────────────────────────────────────────────── */

/* stdlib root — resolved once at startup */
static char g_stdlib_root[1024] = "";

/* Every source file (entry + transitively imported) touched by the current
   compile_file() call — recorded so `przp run` can later check whether all
   of them are older than an already-built binary and skip recompiling. */
#define MAX_DEP_FILES 256
static char g_dep_files[MAX_DEP_FILES][1024];
static int  g_n_dep_files = 0;

static void record_dep_file(const char *path) {
    for (int i = 0; i < g_n_dep_files; i++)
        if (!strcmp(g_dep_files[i], path)) return; /* dedupe */
    if (g_n_dep_files >= MAX_DEP_FILES) return;
    snprintf(g_dep_files[g_n_dep_files], sizeof(g_dep_files[0]), "%s", path);
    g_n_dep_files++;
}

/* The parser has no notion of a file path (only source text + a file_id
   used solely for span reporting), so `test "name" { ... }` blocks can't
   record their own origin file at parse time — stamp it here, right after
   each parse() call, for `przp test <file>` filtering later. `path` is
   arena-copied since several call sites pass a stack-local buffer (e.g.
   load_imports's per-iteration `full[1024]`) that doesn't outlive the loop
   iteration it was built in. */
static void stamp_test_files(Module *mod, const char *path, Arena *arena) {
    const char *durable = arena_strdup(arena, path);
    for (size_t i = 0; i < mod->items.len; i++) {
        Item *item = mod->items.data[i];
        if (item->kind == ITEM_FN && item->fn.is_test)
            item->fn.test_file = durable;
    }
}

static void stdlib_root_init(const char *argv0) {
    (void)argv0;
    const char *env = getenv("PRZP_STDLIB");
    if (env) {
        snprintf(g_stdlib_root, sizeof(g_stdlib_root), "%s", env);
        return;
    }
    /* try /proc/self/exe (Linux) to find binary dir */
    char exe[1024];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        const char *slash = strrchr(exe, '/');
        if (slash) {
            size_t len = (size_t)(slash - exe);
            if (len + 5 < sizeof(g_stdlib_root)) {
                memcpy(g_stdlib_root, exe, len);
                memcpy(g_stdlib_root + len, "/std", 5);
                return;
            }
        }
    }
    /* fallback: relative to cwd */
    snprintf(g_stdlib_root, sizeof(g_stdlib_root), "./std");
}

static void src_dir_of(const char *path, char *out, size_t outsz) {
    const char *slash = strrchr(path, '/');
    if (slash && slash > path) {
        size_t n = (size_t)(slash - path);
        if (n >= outsz) n = outsz - 1;
        memcpy(out, path, n);
        out[n] = '\0';
    } else {
        snprintf(out, outsz, ".");
    }
}

static void path_join(char *out, size_t outsz, const char *a, const char *b) {
    if (!strcmp(a, "."))
        snprintf(out, outsz, "%s", b);
    else
        snprintf(out, outsz, "%s/%s", a, b);
}

/* Does `name` refer to `orig_name` — either exactly, or as a generic
   instantiation of it ("Box" itself, or "Box__T" / "Box__i32", but not an
   unrelated name like "BoxOfBoxes" that merely starts with the same
   letters)? A generic type's own methods and internal uses still reference
   it by its pre-mangling, template-shaped name ("Box__T") even from deep
   inside its own impl block, so matching only the bare name here would
   leave those un-prefixed after import-mangling — silently pointing at a
   type that was never actually defined under that name. */
static int names_generic_match(const char *name, const char *orig_name) {
    size_t olen = strlen(orig_name);
    if (!strcmp(name, orig_name)) return 1;
    return !strncmp(name, orig_name, olen) && name[olen] == '_' && name[olen + 1] == '_';
}

/* Rewrite TY_NAMED references that match any of orig_names → alias__name */
static void rw_type(Type *ty, const char **orig, size_t n, const char *alias, Arena *a) {
    if (!ty) return;
    switch (ty->kind) {
        case TY_NAMED:
            for (size_t i = 0; i < n; i++) {
                if (names_generic_match(ty->named.name, orig[i])) {
                    char *buf = arena_alloc(a, strlen(alias) + 2 + strlen(ty->named.name) + 1);
                    sprintf(buf, "%s__%s", alias, ty->named.name);
                    ty->named.name = buf;
                    break;
                }
            }
            break;
        case TY_PTR: case TY_SMART_PTR: case TY_SLICE: case TY_FAILABLE:
            rw_type(ty->ptr.inner, orig, n, alias, a);
            break;
        case TY_ARRAY:
            /* was missing entirely — a struct field typed "[N]Task" never
               got its element type rewritten to "alias__Task" at all,
               leaving array-typed fields pointing at a type that was never
               actually defined under that name once the module got mangled */
            rw_type(ty->array.inner, orig, n, alias, a);
            break;
        case TY_FN:
            for (size_t i = 0; i < ty->fn.params.len; i++)
                rw_type(ty->fn.params.data[i], orig, n, alias, a);
            rw_type(ty->fn.ret, orig, n, alias, a);
            break;
        case TY_TUPLE:
            for (size_t i = 0; i < ty->tuple.elems.len; i++)
                rw_type(ty->tuple.elems.data[i], orig, n, alias, a);
            break;
        default: break;
    }
}

static void rw_types_in_stmts(StmtList sl, const char **orig, size_t n, const char *alias, Arena *a);

static void rw_types_in_stmts(StmtList sl, const char **orig, size_t n, const char *alias, Arena *a) {
    for (size_t i = 0; i < sl.len; i++) {
        Stmt *s = sl.data[i];
        if (!s) continue;
        if (s->kind == STMT_LET) rw_type(s->let.ty, orig, n, alias, a);
        /* recurse into nested stmts */
        if (s->kind == STMT_IF) {
            for (size_t j = 0; j < s->if_.branches.len; j++)
                rw_types_in_stmts(s->if_.branches.data[j].body, orig, n, alias, a);
            rw_types_in_stmts(s->if_.else_body, orig, n, alias, a);
        } else if (s->kind == STMT_WHILE) {
            rw_types_in_stmts(s->while_.body, orig, n, alias, a);
        } else if (s->kind == STMT_FOR) {
            rw_types_in_stmts(s->for_.body, orig, n, alias, a);
        } else if (s->kind == STMT_WHEN) {
            for (size_t j = 0; j < s->when.arms.len; j++) {
                Stmt *body = s->when.arms.data[j].body;
                if (body) rw_types_in_stmts((StmtList){.data=&body,.len=1}, orig, n, alias, a);
            }
        } else if (s->kind == STMT_BLOCK) {
            rw_types_in_stmts(s->block, orig, n, alias, a);
        }
    }
}

/* Rewrite EXPR_IDENT references inside function bodies: if the ident name
   matches one of the module's own (non-extern) items, prefix it with alias__.
   This handles intra-module calls like `slice(...)` inside `trim`'s body. */
static void rw_ident_expr(Expr *e, const char **orig, size_t n_orig,
                          const char *alias, Arena *a);
static void rw_ident_stmts(StmtList sl, const char **orig, size_t n_orig,
                            const char *alias, Arena *a);
static void rw_ident_stmt(Stmt *s, const char **orig, size_t n_orig,
                           const char *alias, Arena *a);

static void rw_ident_stmts(StmtList sl, const char **orig, size_t n_orig,
                            const char *alias, Arena *a) {
    for (size_t i = 0; i < sl.len; i++) {
        Stmt *s = sl.data[i];
        if (!s) continue;
        switch (s->kind) {
            case STMT_EXPR:   rw_ident_expr(s->expr, orig, n_orig, alias, a); break;
            case STMT_LET:    rw_ident_expr(s->let.init, orig, n_orig, alias, a); break;
            case STMT_ASSIGN:
                rw_ident_expr(s->assign.target, orig, n_orig, alias, a);
                rw_ident_expr(s->assign.val, orig, n_orig, alias, a);
                break;
            case STMT_RET:    rw_ident_expr(s->ret.val, orig, n_orig, alias, a); break;
            case STMT_IF:
                for (size_t j = 0; j < s->if_.branches.len; j++) {
                    rw_ident_expr(s->if_.branches.data[j].cond, orig, n_orig, alias, a);
                    rw_ident_stmts(s->if_.branches.data[j].body, orig, n_orig, alias, a);
                }
                rw_ident_stmts(s->if_.else_body, orig, n_orig, alias, a);
                break;
            case STMT_WHILE:
                rw_ident_expr(s->while_.cond, orig, n_orig, alias, a);
                rw_ident_stmts(s->while_.body, orig, n_orig, alias, a);
                break;
            case STMT_FOR:
                rw_ident_expr(s->for_.clause.iter, orig, n_orig, alias, a);
                rw_ident_expr(s->for_.clause.range_end, orig, n_orig, alias, a);
                rw_ident_expr(s->for_.clause.cond, orig, n_orig, alias, a);
                rw_ident_stmt(s->for_.clause.init, orig, n_orig, alias, a);
                rw_ident_stmt(s->for_.clause.step, orig, n_orig, alias, a);
                rw_ident_stmts(s->for_.body, orig, n_orig, alias, a);
                break;
            case STMT_BLOCK:  rw_ident_stmts(s->block, orig, n_orig, alias, a); break;
            case STMT_DEFER:  rw_ident_stmts(s->defer, orig, n_orig, alias, a); break;
            case STMT_WHEN:
                rw_ident_expr(s->when.val, orig, n_orig, alias, a);
                for (size_t j = 0; j < s->when.arms.len; j++) {
                    WhenArm *arm = &s->when.arms.data[j];
                    for (size_t k = 0; k < arm->pats.len; k++)
                        rw_ident_expr(arm->pats.data[k], orig, n_orig, alias, a);
                    rw_ident_stmt(arm->body, orig, n_orig, alias, a);
                }
                break;
            default: break;
        }
    }
}

static void rw_ident_stmt(Stmt *s, const char **orig, size_t n_orig,
                           const char *alias, Arena *a) {
    if (!s) return;
    StmtList sl = {.data = &s, .len = 1};
    rw_ident_stmts(sl, orig, n_orig, alias, a);
}

static void rw_ident_expr(Expr *e, const char **orig, size_t n_orig,
                          const char *alias, Arena *a) {
    if (!e) return;
    switch (e->kind) {
        case EXPR_IDENT: {
            for (size_t i = 0; i < n_orig; i++) {
                if (names_generic_match(e->ident.name, orig[i])) {
                    char buf[512];
                    snprintf(buf, sizeof(buf), "%s__%s", alias, e->ident.name);
                    e->ident.name = arena_strdup(a, buf);
                    break;
                }
            }
            break;
        }
        case EXPR_CALL:
            rw_ident_expr(e->call.callee, orig, n_orig, alias, a);
            for (size_t i = 0; i < e->call.args.len; i++)
                rw_ident_expr(e->call.args.data[i], orig, n_orig, alias, a);
            break;
        case EXPR_BINOP:
            rw_ident_expr(e->binop.l, orig, n_orig, alias, a);
            rw_ident_expr(e->binop.r, orig, n_orig, alias, a);
            break;
        case EXPR_UNOP:
            rw_ident_expr(e->unop.operand, orig, n_orig, alias, a);
            break;
        case EXPR_INDEX:
            rw_ident_expr(e->index.arr, orig, n_orig, alias, a);
            rw_ident_expr(e->index.idx, orig, n_orig, alias, a);
            break;
        case EXPR_DEREF:
        case EXPR_SMARTDEREF:
            rw_ident_expr(e->deref.operand, orig, n_orig, alias, a);
            break;
        case EXPR_CAST:
            rw_ident_expr(e->cast.val, orig, n_orig, alias, a);
            break;
        case EXPR_BUILTIN:
            for (size_t i = 0; i < e->builtin.args.len; i++)
                rw_ident_expr(e->builtin.args.data[i], orig, n_orig, alias, a);
            break;
        case EXPR_FIELD:
            rw_ident_expr(e->field.obj, orig, n_orig, alias, a);
            break;
        case EXPR_STRUCT_LIT:
            for (size_t i = 0; i < n_orig; i++) {
                if (names_generic_match(e->struct_lit.ty_name, orig[i])) {
                    char buf[512];
                    snprintf(buf, sizeof(buf), "%s__%s", alias, e->struct_lit.ty_name);
                    e->struct_lit.ty_name = arena_strdup(a, buf);
                    break;
                }
            }
            for (size_t i = 0; i < e->struct_lit.fields.len; i++)
                rw_ident_expr(e->struct_lit.fields.data[i].val, orig, n_orig, alias, a);
            break;
        case EXPR_ARRAY_LIT:
        case EXPR_TUPLE: /* EXPR_TUPLE reuses the array_lit field (parser.c) */
            for (size_t i = 0; i < e->array_lit.len; i++)
                rw_ident_expr(e->array_lit.data[i], orig, n_orig, alias, a);
            break;
        case EXPR_IF:
            rw_ident_expr(e->if_expr.cond, orig, n_orig, alias, a);
            rw_ident_stmt(e->if_expr.then_, orig, n_orig, alias, a);
            rw_ident_stmt(e->if_expr.else_, orig, n_orig, alias, a);
            break;
        case EXPR_WHEN:
            rw_ident_expr(e->when.cond, orig, n_orig, alias, a);
            for (size_t i = 0; i < e->when.arms.len; i++) {
                WhenArm *arm = &e->when.arms.data[i];
                for (size_t j = 0; j < arm->pats.len; j++)
                    rw_ident_expr(arm->pats.data[j], orig, n_orig, alias, a);
                rw_ident_stmt(arm->body, orig, n_orig, alias, a);
            }
            break;
        default: break;
    }
}

/* Prefix all item names in mod with "alias__", and rewrite type references */
static void mangle_items(Module *mod, const char *alias, Arena *arena) {
    /* Collect all item names — both extern fns and regular fns get prefixed.
       Extern fn names in function bodies also need rewriting to call the wrapper. */
    const char *orig[256];
    size_t n_orig = 0;
    for (size_t i = 0; i < mod->items.len && n_orig < 256; i++) {
        Item *item = mod->items.data[i];
        if (item->name)
            orig[n_orig++] = item->name;
    }

    /* collect ALL original names for type-name rewriting (structs, enums, etc.) */
    const char *all_orig[256];
    size_t n_all_orig = 0;
    for (size_t i = 0; i < mod->items.len && n_all_orig < 256; i++) {
        Item *item = mod->items.data[i];
        if (item->name) all_orig[n_all_orig++] = item->name;
    }

    /* gen_insts recorded while parsing this module reference its own
       generic types by their pre-mangling names ("Box", "Box__i32") —
       rewrite those the same way item names below get rewritten, or sema
       looks for a template/instantiation under a name that no longer
       exists once this module's items are all prefixed with alias__. */
    for (size_t i = 0; i < mod->gen_insts.len; i++) {
        GenInst *gi = &mod->gen_insts.data[i];
        for (size_t j = 0; j < n_all_orig; j++) {
            if (names_generic_match(gi->base, all_orig[j])) {
                char b[512];
                snprintf(b, sizeof(b), "%s__%s", alias, gi->base);
                gi->base = arena_strdup(arena, b);
                break;
            }
        }
        for (size_t j = 0; j < n_all_orig; j++) {
            if (names_generic_match(gi->mangled, all_orig[j])) {
                char m[512];
                snprintf(m, sizeof(m), "%s__%s", alias, gi->mangled);
                gi->mangled = arena_strdup(arena, m);
                break;
            }
        }
    }

    char buf[512];
    for (size_t i = 0; i < mod->items.len; i++) {
        Item *item = mod->items.data[i];
        if (!item->name) continue;
        if (item->kind == ITEM_EXTERN_FN) {
            /* Save the original C symbol name, then mangle item->name so
               alias.fn rewrites work — but only capture c_name on the first
               mangling pass. A transitively-imported extern fn (e.g.
               std/collections re-exported through a module that itself gets
               imported) goes through mangle_items more than once as each
               importer's alias gets applied in turn; capturing c_name again
               on a later pass would overwrite the real C symbol ("malloc")
               with the previous pass's already-mangled item->name
               ("collections__malloc"), and the linker would then look for a
               C symbol that was never actually exported under that name. */
            if (!item->extern_fn.c_name) item->extern_fn.c_name = item->name;
            snprintf(buf, sizeof(buf), "%s__%s", alias, item->name);
            item->name = arena_strdup(arena, buf);
            continue;
        }
        snprintf(buf, sizeof(buf), "%s__%s", alias, item->name);
        item->name = arena_strdup(arena, buf);
        /* For impl blocks, also mangle the target type name */
        if (item->kind == ITEM_IMPL) {
            snprintf(buf, sizeof(buf), "%s__%s", alias, item->impl.ty_name);
            item->impl.ty_name = arena_strdup(arena, buf);
        }
    }

    /* rewrite TY_NAMED annotations and intra-module EXPR_IDENT calls in bodies */
    for (size_t i = 0; i < mod->items.len; i++) {
        Item *item = mod->items.data[i];
        if (item->kind == ITEM_FN) {
            for (size_t j = 0; j < item->fn.params.len; j++)
                rw_type(item->fn.params.data[j].ty, all_orig, n_all_orig, alias, arena);
            rw_type(item->fn.ret, all_orig, n_all_orig, alias, arena);
            rw_types_in_stmts(item->fn.body, all_orig, n_all_orig, alias, arena);
            /* rewrite direct calls to other module functions */
            rw_ident_stmts(item->fn.body, orig, n_orig, alias, arena);
        } else if (item->kind == ITEM_EXTERN_FN) {
            /* extern fn: only rewrite type annotations (param/return types) */
            for (size_t j = 0; j < item->extern_fn.params.len; j++)
                rw_type(item->extern_fn.params.data[j].ty, all_orig, n_all_orig, alias, arena);
            rw_type(item->extern_fn.ret, all_orig, n_all_orig, alias, arena);
        } else if (item->kind == ITEM_STRUCT) {
            for (size_t j = 0; j < item->struct_.fields.len; j++)
                rw_type(item->struct_.fields.data[j].ty, all_orig, n_all_orig, alias, arena);
        } else if (item->kind == ITEM_GLOBAL) {
            rw_type(item->global.ty, all_orig, n_all_orig, alias, arena);
            /* a global's initializer can itself reference other items in this
               module — a struct-literal type name, a bare function reference
               for a fn-pointer field, ... — and needs the same alias__
               rewriting a function body gets, or a re-imported module's own
               re-mangled global would reference stale, single-mangled names
               (e.g. "be__Backend" instead of "gl__be__Backend" once gl.przp
               is itself imported under the alias "gl"). */
            rw_ident_expr(item->global.init, orig, n_orig, alias, arena);
        } else if (item->kind == ITEM_IMPL) {
            for (size_t j = 0; j < item->impl.methods.len; j++) {
                Item *m = item->impl.methods.data[j];
                for (size_t k = 0; k < m->fn.params.len; k++)
                    rw_type(m->fn.params.data[k].ty, all_orig, n_all_orig, alias, arena);
                rw_type(m->fn.ret, all_orig, n_all_orig, alias, arena);
                rw_types_in_stmts(m->fn.body, all_orig, n_all_orig, alias, arena);
                rw_ident_stmts(m->fn.body, orig, n_orig, alias, arena);
            }
        }
    }
}

static void rw_item(Item *item, const char **al, size_t n, Arena *a);

/* Flatten every top-level `mod Name { ...items... }` block in mod->items:
   mangle the block's own items (and any of its own internal generic uses,
   since gen_insts recorded while parsing the block live in the same
   Module-wide list) with alias "Name" — exactly what mangle_items already
   does for an imported file — then splice the (now Name__-prefixed) items
   into mod->items in the block's place. After this, "mod X { fn a() {} }"
   and "import(x = <a file containing fn a() {}>)" leave the same shape:
   an item named "x__a" in the module's flat item list. Does not recurse
   into nested mod-in-mod (not needed yet, and this keeps it simple).

   Also rewrites the rest of this module's own code (the code outside the
   mod block, in this same file) so that qualified accesses like X=>A or
   X.A — which parse to EXPR_FIELD(EXPR_IDENT("X"), "A") the same as an
   import alias would — resolve to the flattened item "X__A", using the
   same rw_item machinery load_imports uses for import aliases. */
static void expand_mod_items(Module *mod, Arena *arena) {
    int any_mod = 0;
    size_t total = 0;
    for (size_t i = 0; i < mod->items.len; i++) {
        Item *it = mod->items.data[i];
        if (it->kind == ITEM_MOD) { any_mod = 1; total += it->mod_.items.len; }
        else total += 1;
    }
    if (!any_mod) return;

    const char *mod_names[64];
    size_t      n_mod_names = 0;

    Item **out = arena_alloc(arena, total * sizeof(Item *));
    size_t n = 0;
    for (size_t i = 0; i < mod->items.len; i++) {
        Item *it = mod->items.data[i];
        if (it->kind != ITEM_MOD) { out[n++] = it; continue; }

        if (n_mod_names < 64) mod_names[n_mod_names++] = it->name;

        Module pseudo = {0};
        pseudo.items     = it->mod_.items;
        pseudo.gen_insts = mod->gen_insts; /* same underlying array — mangle_items
                                               mutates matching entries in place */
        pseudo.arena     = arena;
        mangle_items(&pseudo, it->name, arena);
        for (size_t j = 0; j < pseudo.items.len; j++)
            out[n++] = pseudo.items.data[j];
    }
    mod->items.data = out;
    mod->items.len  = n;

    if (n_mod_names > 0)
        for (size_t i = 0; i < mod->items.len; i++)
            rw_item(mod->items.data[i], mod_names, n_mod_names, arena);
}

/* ── AST rewrite: EXPR_FIELD(EXPR_IDENT("alias"), "x") → EXPR_IDENT("alias__x") ── */

static int is_alias(const char **aliases, size_t n, const char *name) {
    for (size_t i = 0; i < n; i++)
        if (!strcmp(aliases[i], name)) return 1;
    return 0;
}

static void rw_stmt(Stmt *s, const char **al, size_t n, Arena *a);

static void rw_stmts(StmtList sl, const char **al, size_t n, Arena *a) {
    for (size_t i = 0; i < sl.len; i++) rw_stmt(sl.data[i], al, n, a);
}

static void rw_expr(Expr *e, const char **al, size_t n, Arena *a) {
    if (!e) return;
    switch (e->kind) {
        case EXPR_FIELD:
            if (e->field.obj && e->field.obj->kind == EXPR_IDENT
                    && is_alias(al, n, e->field.obj->ident.name)) {
                /* rewrite in-place: EXPR_FIELD → EXPR_IDENT("alias__name") */
                char buf[512];
                snprintf(buf, sizeof(buf), "%s__%s",
                         e->field.obj->ident.name, e->field.field);
                e->kind       = EXPR_IDENT;
                e->ident.name = arena_strdup(a, buf);
            } else {
                rw_expr(e->field.obj, al, n, a);
            }
            break;
        case EXPR_CALL:
            rw_expr(e->call.callee, al, n, a);
            for (size_t i = 0; i < e->call.args.len; i++)
                rw_expr(e->call.args.data[i], al, n, a);
            break;
        case EXPR_BINOP:
            rw_expr(e->binop.l, al, n, a);
            rw_expr(e->binop.r, al, n, a);
            break;
        case EXPR_UNOP:
            rw_expr(e->unop.operand, al, n, a);
            break;
        case EXPR_INDEX:
            rw_expr(e->index.arr, al, n, a);
            rw_expr(e->index.idx, al, n, a);
            break;
        case EXPR_DEREF:
        case EXPR_SMARTDEREF:
            rw_expr(e->deref.operand, al, n, a);
            break;
        case EXPR_CAST:
            rw_expr(e->cast.val, al, n, a);
            break;
        case EXPR_BUILTIN:
            for (size_t i = 0; i < e->builtin.args.len; i++)
                rw_expr(e->builtin.args.data[i], al, n, a);
            break;
        case EXPR_STRUCT_LIT:
            for (size_t i = 0; i < e->struct_lit.fields.len; i++)
                rw_expr(e->struct_lit.fields.data[i].val, al, n, a);
            break;
        case EXPR_ARRAY_LIT:
        case EXPR_TUPLE: /* EXPR_TUPLE reuses the array_lit field (parser.c) */
            for (size_t i = 0; i < e->array_lit.len; i++)
                rw_expr(e->array_lit.data[i], al, n, a);
            break;
        case EXPR_IF:
            rw_expr(e->if_expr.cond, al, n, a);
            rw_stmt(e->if_expr.then_, al, n, a);
            rw_stmt(e->if_expr.else_, al, n, a);
            break;
        case EXPR_WHEN:
            rw_expr(e->when.cond, al, n, a);
            for (size_t i = 0; i < e->when.arms.len; i++) {
                WhenArm *arm = &e->when.arms.data[i];
                for (size_t j = 0; j < arm->pats.len; j++)
                    rw_expr(arm->pats.data[j], al, n, a);
                rw_stmt(arm->body, al, n, a);
            }
            break;
        default: break; /* leaf nodes */
    }
}

static void rw_stmt(Stmt *s, const char **al, size_t n, Arena *a) {
    if (!s) return;
    switch (s->kind) {
        case STMT_EXPR:
            rw_expr(s->expr, al, n, a);
            break;
        case STMT_LET:
            rw_expr(s->let.init, al, n, a);
            break;
        case STMT_ASSIGN:
            rw_expr(s->assign.target, al, n, a);
            rw_expr(s->assign.val, al, n, a);
            break;
        case STMT_RET:
            rw_expr(s->ret.val, al, n, a);
            break;
        case STMT_IF:
            for (size_t i = 0; i < s->if_.branches.len; i++) {
                rw_expr(s->if_.branches.data[i].cond, al, n, a);
                rw_stmts(s->if_.branches.data[i].body, al, n, a);
            }
            rw_stmts(s->if_.else_body, al, n, a);
            break;
        case STMT_WHILE:
            rw_expr(s->while_.cond, al, n, a);
            rw_stmts(s->while_.body, al, n, a);
            break;
        case STMT_FOR:
            rw_expr(s->for_.clause.iter, al, n, a);
            rw_expr(s->for_.clause.range_end, al, n, a);
            rw_expr(s->for_.clause.cond, al, n, a);
            rw_stmt(s->for_.clause.init, al, n, a);
            rw_stmt(s->for_.clause.step, al, n, a);
            rw_stmts(s->for_.body, al, n, a);
            break;
        case STMT_WHEN:
            rw_expr(s->when.val, al, n, a);
            for (size_t i = 0; i < s->when.arms.len; i++) {
                WhenArm *arm = &s->when.arms.data[i];
                for (size_t j = 0; j < arm->pats.len; j++)
                    rw_expr(arm->pats.data[j], al, n, a);
                rw_stmt(arm->body, al, n, a);
            }
            break;
        case STMT_DEFER:  rw_stmts(s->defer, al, n, a); break;
        case STMT_BLOCK:  rw_stmts(s->block, al, n, a); break;
        default: break;
    }
}

static void rw_item(Item *item, const char **al, size_t n, Arena *a) {
    if (!item) return;
    switch (item->kind) {
        case ITEM_FN:
            for (size_t i = 0; i < item->fn.body.len; i++)
                rw_stmt(item->fn.body.data[i], al, n, a);
            break;
        case ITEM_IMPL:
            for (size_t i = 0; i < item->impl.methods.len; i++)
                rw_item(item->impl.methods.data[i], al, n, a);
            break;
        case ITEM_GLOBAL:
            rw_expr(item->global.init, al, n, a);
            break;
        case ITEM_ENUM:
            for (size_t i = 0; i < item->enum_.variants.len; i++)
                rw_expr(item->enum_.variants.data[i].val, al, n, a);
            break;
        default: break;
    }
}

/* Append items from imp into mod, skipping ITEM_IMPORT entries */
static void merge_items(Module *mod, Module *imp) {
    /* imp's own gen_insts (generic instantiations it recorded while being
       parsed — its own internal use of a generic type, or a caller's
       qualified alias.Generic<T> use recorded against imp's own parser)
       never reach sema unless they end up in the same Module sema actually
       checks. Without this, any imported module that uses a generic type
       — even purely internally, with no cross-module qualification at all —
       breaks the moment something else imports it, since the concrete
       instantiation is never requested in the merged module sema sees. */
    if (imp->gen_insts.len) {
        size_t new_gi_len = mod->gen_insts.len + imp->gen_insts.len;
        GenInst *new_gi = arena_alloc(mod->arena, new_gi_len * sizeof(GenInst));
        memcpy(new_gi, mod->gen_insts.data, mod->gen_insts.len * sizeof(GenInst));
        memcpy(new_gi + mod->gen_insts.len, imp->gen_insts.data,
               imp->gen_insts.len * sizeof(GenInst));
        mod->gen_insts.data = new_gi;
        mod->gen_insts.len  = new_gi_len;
    }

    size_t extra = 0;
    for (size_t i = 0; i < imp->items.len; i++)
        if (imp->items.data[i]->kind != ITEM_IMPORT) extra++;
    if (!extra) return;

    size_t new_len = mod->items.len + extra;
    Item **new_data = arena_alloc(mod->arena, new_len * sizeof(Item*));
    memcpy(new_data, mod->items.data, mod->items.len * sizeof(Item*));
    size_t dst = mod->items.len;
    for (size_t i = 0; i < imp->items.len; i++)
        if (imp->items.data[i]->kind != ITEM_IMPORT)
            new_data[dst++] = imp->items.data[i];
    mod->items.data = new_data;
    mod->items.len  = new_len;
}

/*
 * Load all imports into mod.  loading[] is the set of paths currently being
 * processed (for cycle detection).  The main file's error context is restored
 * by the caller after this returns.
 */
static int load_imports(Module *mod, const char *src_path, const char *src,
                        Arena *arena, const char **loading, size_t n_loading) {
    /* collect (alias, resolved-path) pairs from ITEM_IMPORT items */
    const char *aliases[64];
    size_t      n_aliases = 0;
    char        src_dir[1024];
    src_dir_of(src_path, src_dir, sizeof(src_dir));
    /* remember how many items this module originally has so rw_item only
       touches the caller's own code, not the imported (already-mangled) items */
    size_t n_orig_items = mod->items.len;

    for (size_t i = 0; i < mod->items.len && n_aliases < 64; i++) {
        Item *item = mod->items.data[i];
        if (item->kind != ITEM_IMPORT) continue;

        for (size_t j = 0; j < item->imports.len && n_aliases < 64; j++) {
            const char *alias     = item->imports.data[j].alias;
            const char *imp_path  = item->imports.data[j].path;

            /* resolve path: append .przp */
            char full[1024];
            int full_n;
            if (imp_path[0] == '/') {
                full_n = snprintf(full, sizeof(full), "%s.przp", imp_path);
            } else if (!strncmp(imp_path, "std/", 4) && g_stdlib_root[0]) {
                /* stdlib path: resolve against stdlib root */
                full_n = snprintf(full, sizeof(full), "%s/%s.przp", g_stdlib_root, imp_path + 4);
            } else {
                full_n = snprintf(full, sizeof(full), "%s/%s.przp", src_dir, imp_path);
            }
            if (full_n < 0 || (size_t)full_n >= sizeof(full)) {
                error_init(src_path, src);
                error_at(item->span, "import path too long: '%s'", imp_path);
                return 0;
            }

            /* cycle detection */
            int cycle = 0;
            for (size_t k = 0; k < n_loading; k++)
                if (!strcmp(loading[k], full)) { cycle = 1; break; }
            if (cycle) {
                error_init(src_path, src);
                error_at(item->span, "import cycle involving '%s'", full);
                return 0;
            }

            /* read + parse imported file */
            char err[256];
            char *imp_src = read_file_or_null(full, err, sizeof(err));
            if (!imp_src) {
                error_init(src_path, src);
                error_at(item->span, "cannot import '%s' (resolved to '%s'): %s", imp_path, full, err);
                return 0;
            }
            record_dep_file(full);
            error_init(full, imp_src);
            Module *imp = parse(imp_src, (uint32_t)(n_loading + 1), arena);
            stamp_test_files(imp, full, arena);
            expand_mod_items(imp, arena);

            /* recurse: handle imports inside the imported module */
            const char *new_loading[64];
            memcpy(new_loading, loading, n_loading * sizeof(char*));
            new_loading[n_loading] = full;
            if (!load_imports(imp, full, imp_src, arena, new_loading, n_loading + 1))
                return 0;

            /* mangle imported items, merge into main module */
            mangle_items(imp, alias, arena);
            merge_items(mod, imp);

            aliases[n_aliases++] = alias;
        }
    }

    /* rewrite module accesses in the calling module only (not imported items) */
    if (n_aliases > 0) {
        for (size_t i = 0; i < n_orig_items; i++)
            rw_item(mod->items.data[i], aliases, n_aliases, arena);
    }
    return 1;
}

/* strip extension, return basename without it */
static void basename_no_ext(const char *path, char *out, size_t outsz) {
    const char *slash = strrchr(path, '/');
    const char *base  = slash ? slash + 1 : path;
    const char *dot   = strrchr(base, '.');
    size_t len = dot ? (size_t)(dot - base) : strlen(base);
    if (len >= outsz) len = outsz - 1;
    memcpy(out, base, len);
    out[len] = '\0';
}

/* compile one .przp file → .ll → binary via clang */
/* True if `path` does not end in ".przp" — used by `sac` to pass through
   already-compiled native objects/archives to the final link step, e.g. a
   C shim linked alongside a struct-by-value `extern fn` test. */
static int has_przp_ext(const char *path) {
    size_t n = strlen(path);
    return n >= 5 && !strcmp(path + n - 5, ".przp");
}

/* Write the list of files this build depended on (entry + every resolved
   import) to "<out_path>.d" — a "#release=0|1" header line followed by one
   dependency path per line — read back by binary_is_fresh() to decide
   whether `przp run` needs to recompile. */
static void write_dep_file(const char *out_path, int release) {
    char dep_path[1100];
    snprintf(dep_path, sizeof(dep_path), "%s.d", out_path);
    FILE *f = fopen(dep_path, "w");
    if (!f) return; /* best-effort — worst case, run always rebuilds */
    fprintf(f, "#release=%d\n", release);
    for (int i = 0; i < g_n_dep_files; i++)
        fprintf(f, "%s\n", g_dep_files[i]);
    fclose(f);
}

/* True if `out_path` exists, has a matching "<out_path>.d" dependency list
   from a previous successful build in the same (release/debug) mode, and
   every file in that list is no newer than the binary — i.e. `przp run`
   can skip recompiling. Missing binary, missing/unreadable dep file, a
   mode mismatch, or any stale/missing dependency all conservatively return
   false (rebuild). */
static int binary_is_fresh(const char *out_path, int release) {
    struct stat bin_st;
    if (stat(out_path, &bin_st) != 0) return 0;

    char dep_path[1100];
    snprintf(dep_path, sizeof(dep_path), "%s.d", out_path);
    FILE *f = fopen(dep_path, "r");
    if (!f) return 0;

    int fresh = 1;
    int first = 1;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = '\0';
        if (first) {
            first = 0;
            char want[16];
            snprintf(want, sizeof(want), "#release=%d", release);
            if (strcmp(line, want) != 0) { fresh = 0; break; }
            continue;
        }
        if (n == 0) continue;
        struct stat dep_st;
        if (stat(line, &dep_st) != 0 || dep_st.st_mtime > bin_st.st_mtime) {
            fresh = 0;
            break;
        }
    }
    fclose(f);
    return fresh;
}

/* Merge every "*.przp" file directly under `tests_dir` into `mod`, even if
   nothing imports them — this is what lets `przp test` find tests placed
   in ./tests/ independently of whatever the entry file's own import graph
   reaches. Each file gets its own load_imports pass (so a tests/ file can
   import a module under test), then merges in via merge_items. Missing or
   unreadable tests_dir is not an error — it's just an empty test set. */
static int merge_tests_dir(Module *mod, const char *tests_dir, Arena *arena) {
    DIR *td = opendir(tests_dir);
    if (!td) return 1;

    struct dirent *de;
    while ((de = readdir(td))) {
        if (!has_przp_ext(de->d_name)) continue;
        char full[600];
        path_join(full, sizeof(full), tests_dir, de->d_name);

        char terr[256];
        char *tsrc = read_file_or_null(full, terr, sizeof(terr));
        if (!tsrc) {
            fprintf(stderr, "przp: cannot open '%s': %s\n", full, terr);
            closedir(td);
            return 0;
        }
        record_dep_file(full);
        error_init(full, tsrc);
        Module *extra = parse(tsrc, 0, arena);
        stamp_test_files(extra, full, arena);
        expand_mod_items(extra, arena);

        const char *extra_loading[1] = { full };
        if (!load_imports(extra, full, tsrc, arena, extra_loading, 1)) {
            free(tsrc);
            closedir(td);
            return 0;
        }
        merge_items(mod, extra);
        free(tsrc);
    }
    closedir(td);
    return 1;
}

static int compile_file(const char *src_path, const char *out_path, int release,
                         const char **extra_links, int n_extra, int test_mode,
                         const char *tests_dir) {
    g_n_dep_files = 0;
    record_dep_file(src_path);

    char err[256];
    char *src = read_file_or_null(src_path, err, sizeof(err));
    if (!src) {
        fprintf(stderr, "przp: cannot open '%s': %s\n", src_path, err);
        return 1;
    }

    Arena arena;
    arena_init(&arena);

    error_init(src_path, src);
    Module *mod = parse(src, 0, &arena);
    stamp_test_files(mod, src_path, &arena);
    expand_mod_items(mod, &arena);

    /* load and merge imported modules before sema */
    const char *loading[1] = { src_path };
    if (!load_imports(mod, src_path, src, &arena, loading, 1)) {
        arena_free(&arena);
        free(src);
        return 1;
    }
    if (tests_dir && !merge_tests_dir(mod, tests_dir, &arena)) {
        arena_free(&arena);
        free(src);
        return 1;
    }
    error_init(src_path, src); /* restore main file context for sema/codegen errors */

    if (!sema_check(mod)) { arena_free(&arena); free(src); return 1; }

    /* write .ll to a temp file next to the source */
    char ll_path[1024];
    snprintf(ll_path, sizeof(ll_path), "/tmp/przp_%d.ll", (int)getpid());

    FILE *ll_f = fopen(ll_path, "w");
    if (!ll_f) { fprintf(stderr, "przp: cannot write '%s'\n", ll_path); return 1; }
    int ok = codegen(mod, ll_f, release, test_mode, out_path);
    fclose(ll_f);

    arena_free(&arena);
    free(src);

    if (!ok) { remove(ll_path); return 1; }

    /* invoke clang to produce the binary */
    char cmd[2048];
    const char *opt = release ? "-O2" : "-O0 -g";
    int pos = snprintf(cmd, sizeof(cmd), "clang %s %s", opt, ll_path);
    for (int i = 0; i < n_extra && pos < (int)sizeof(cmd); i++)
        pos += snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, " %s", extra_links[i]);
    snprintf(cmd + pos, sizeof(cmd) - (size_t)pos, " -o %s -lm -pthread", out_path);
    int ret = system(cmd);
    if (!getenv("PRZP_KEEP_IR")) remove(ll_path);
    if (ret != 0) return 1;

    /* record the dependency list so a later `przp run` can tell whether
       this binary is still fresh without recompiling to find out */
    write_dep_file(out_path, release);
    return 0;
}

/* ── Sub-commands ─────────────────────────────────────────────────────────── */

static void cmd_sac(int argc, char **argv) {
    /* przp sac <files...> [-o=Name] [--release]
       Non-.przp file arguments (e.g. a .o built from a small C shim) pass
       straight through to the final link step — useful for exercising an
       `extern fn` against real native code in a regression test. */
    const char *out_name = "out";
    int release = 0;
    const char **files = malloc(sizeof(char*) * (size_t)argc);
    int nfiles = 0;
    const char **extra_links = malloc(sizeof(char*) * (size_t)argc);
    int n_extra = 0;

    for (int i = 0; i < argc; i++) {
        if (!strncmp(argv[i], "-o=", 3)) { out_name = argv[i] + 3; continue; }
        if (!strcmp(argv[i], "--release")) { release = 1; continue; }
        if (has_przp_ext(argv[i])) files[nfiles++] = argv[i];
        else                       extra_links[n_extra++] = argv[i];
    }

    if (nfiles == 0) { fprintf(stderr, "przp sac: no input files\n"); free(files); free(extra_links); exit(1); }

    if (nfiles == 1) {
        int rc = compile_file(files[0], out_name, release, extra_links, n_extra, 0, NULL);
        free(files);
        free(extra_links);
        exit(rc);
    }

    /* Multi-file: parse all files, merge into one module, then compile. */
    Arena arena;
    arena_init(&arena);

    char *srcs[256];
    char err[256];
    srcs[0] = read_file_or_null(files[0], err, sizeof(err));
    if (!srcs[0]) {
        fprintf(stderr, "przp: cannot open '%s': %s\n", files[0], err);
        free(files);
        exit(1);
    }
    error_init(files[0], srcs[0]);
    Module *mod = parse(srcs[0], 0, &arena);
    stamp_test_files(mod, files[0], &arena);
    expand_mod_items(mod, &arena);
    const char *loading[1] = { files[0] };
    if (!load_imports(mod, files[0], srcs[0], &arena, loading, 1)) {
        arena_free(&arena);
        free(srcs[0]);
        free(files);
        exit(1);
    }

    for (int fi = 1; fi < nfiles && fi < 256; fi++) {
        srcs[fi] = read_file_or_null(files[fi], err, sizeof(err));
        if (!srcs[fi]) {
            fprintf(stderr, "przp: cannot open '%s': %s\n", files[fi], err);
            arena_free(&arena);
            for (int j = 0; j < fi; j++) free(srcs[j]);
            free(files);
            exit(1);
        }
        error_init(files[fi], srcs[fi]);
        Module *extra = parse(srcs[fi], 0, &arena);
        stamp_test_files(extra, files[fi], &arena);
        expand_mod_items(extra, &arena);
        const char *extra_loading[1] = { files[fi] };
        if (!load_imports(extra, files[fi], srcs[fi], &arena, extra_loading, 1)) {
            arena_free(&arena);
            for (int j = 0; j <= fi; j++) free(srcs[j]);
            free(files);
            exit(1);
        }
        merge_items(mod, extra);
    }
    error_init(files[0], srcs[0]);

    if (!sema_check(mod)) {
        arena_free(&arena);
        for (int fi = 0; fi < nfiles && fi < 256; fi++) free(srcs[fi]);
        free(files);
        exit(1);
    }

    char ll_path[1024];
    snprintf(ll_path, sizeof(ll_path), "/tmp/przp_%d.ll", (int)getpid());
    FILE *ll_f = fopen(ll_path, "w");
    if (!ll_f) { fprintf(stderr, "przp: cannot write '%s'\n", ll_path); exit(1); }
    int ok = codegen(mod, ll_f, release, 0, out_name);
    fclose(ll_f);
    arena_free(&arena);
    for (int fi = 0; fi < nfiles && fi < 256; fi++) free(srcs[fi]);
    free(files);

    if (!ok) { remove(ll_path); exit(1); }
    char cmd2[2048];
    const char *opt2 = release ? "-O2" : "-O0 -g";
    int pos2 = snprintf(cmd2, sizeof(cmd2), "clang %s %s", opt2, ll_path);
    for (int i = 0; i < n_extra && pos2 < (int)sizeof(cmd2); i++)
        pos2 += snprintf(cmd2 + pos2, sizeof(cmd2) - (size_t)pos2, " %s", extra_links[i]);
    snprintf(cmd2 + pos2, sizeof(cmd2) - (size_t)pos2, " -o %s -lm -pthread", out_name);
    int ret2 = system(cmd2);
    if (!getenv("PRZP_KEEP_IR")) remove(ll_path);
    free(extra_links);
    exit((ret2 == 0) ? 0 : 1);
}

static void cmd_init(int argc, char **argv) {
    const char *dir = (argc > 0) ? argv[0] : ".";
    char name[256];
    if (argc > 0) {
        snprintf(name, sizeof(name), "%s", argv[0]);
    } else {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd))) basename_no_ext(cwd, name, sizeof(name));
        else snprintf(name, sizeof(name), "myproject");
    }

    if (argc > 0 && mkdir(dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "przp init: cannot create '%s': %s\n", dir, strerror(errno));
        exit(1);
    }

    char src_dir[512];
    path_join(src_dir, sizeof(src_dir), dir, "src");
    if (mkdir(src_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "przp init: cannot create '%s': %s\n", src_dir, strerror(errno));
        exit(1);
    }

    /* przp.toml */
    char toml_path[256];
    path_join(toml_path, sizeof(toml_path), dir, "przp.toml");
    FILE *f = fopen(toml_path, "w");
    if (f) {
        fprintf(f, "[package]\nname = \"%s\"\nversion = \"0.1.0\"\n\n[build]\nentry = \"src/main.przp\"\n", name);
        fclose(f);
    } else {
        fprintf(stderr, "przp init: cannot write '%s': %s\n", toml_path, strerror(errno));
        exit(1);
    }

    /* src/main.przp */
    char main_path[256];
    path_join(main_path, sizeof(main_path), src_dir, "main.przp");
    f = fopen(main_path, "w");
    if (f) {
        fprintf(f, "fn main() -> i32 {\n    @pf(\"Hello from %s!\\n\")\n    ret 0\n}\n", name);
        fclose(f);
    } else {
        fprintf(stderr, "przp init: cannot write '%s': %s\n", main_path, strerror(errno));
        exit(1);
    }

    /* tests/ — `przp test` discovers every *.przp file placed directly here
       on its own, independent of what src/main.przp imports. */
    char tests_dir[512];
    path_join(tests_dir, sizeof(tests_dir), dir, "tests");
    if (mkdir(tests_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "przp init: cannot create '%s': %s\n", tests_dir, strerror(errno));
        exit(1);
    }

    char example_test_path[600];
    path_join(example_test_path, sizeof(example_test_path), tests_dir, "example_test.przp");
    f = fopen(example_test_path, "w");
    if (f) {
        fprintf(f,
            "test \"example\" {\n"
            "    x: i32 = 2 + 2\n"
            "    if x != 4 { @fail(\"math is broken\") }\n"
            "    @pass()\n"
            "}\n");
        fclose(f);
    } else {
        fprintf(stderr, "przp init: cannot write '%s': %s\n", example_test_path, strerror(errno));
        exit(1);
    }

    printf("Created project '%s'\n", name);
    exit(0);
}

#define MANIFEST_MAX_LINK_LIBS 16

typedef struct {
    char package_name[256];
    char version[64];
    char entry[256];
    char link_libs[MANIFEST_MAX_LINK_LIBS][64];
    int  n_link_libs;
} Manifest;

static char *trim_ws(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1])) *--end = '\0';
    return s;
}

static int parse_quoted_value(const char *s, char *out, size_t outsz) {
    const char *q = strchr(s, '"');
    if (!q) return 0;
    q++;
    const char *end = strchr(q, '"');
    if (!end) return 0;
    size_t n = (size_t)(end - q);
    if (n >= outsz) n = outsz - 1;
    memcpy(out, q, n);
    out[n] = '\0';
    return 1;
}

/* Parse a single-line TOML string array, e.g. `["X11", "GL"]`, into `out`
   (up to `max` entries of `elsz` bytes each). Returns the number of entries
   parsed, or -1 on malformed input (missing brackets/quotes). */
static int parse_string_array_value(const char *s, char out[][64], int max) {
    const char *p = strchr(s, '[');
    if (!p) return -1;
    p++;
    const char *close = strchr(p, ']');
    if (!close) return -1;
    int n = 0;
    while (p < close) {
        while (p < close && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (p >= close) break;
        if (*p != '"') return -1;
        p++;
        const char *end = memchr(p, '"', (size_t)(close - p));
        if (!end) return -1;
        if (n >= max) return -1;
        size_t len = (size_t)(end - p);
        if (len >= 64) len = 63;
        memcpy(out[n], p, len);
        out[n][len] = '\0';
        n++;
        p = end + 1;
    }
    return n;
}

static int read_manifest(Manifest *m) {
    memset(m, 0, sizeof(*m));
    snprintf(m->entry, sizeof(m->entry), "src/main.przp");

    FILE *f = fopen("przp.toml", "r");
    if (!f) return 0;

    enum { SEC_NONE, SEC_PACKAGE, SEC_BUILD, SEC_DEPS } section = SEC_NONE;
    char line[512];
    int line_no = 0;
    while (fgets(line, sizeof(line), f)) {
        line_no++;
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *s = trim_ws(line);
        if (!*s) continue;

        if (!strcmp(s, "[package]")) { section = SEC_PACKAGE; continue; }
        if (!strcmp(s, "[build]"))   { section = SEC_BUILD; continue; }
        if (!strcmp(s, "[deps]"))    { section = SEC_DEPS; continue; }
        if (*s == '[') {
            fprintf(stderr, "przp.toml:%d: error: unknown section '%s'\n", line_no, s);
            fclose(f);
            return -1;
        }

        char *eq = strchr(s, '=');
        if (!eq) {
            fprintf(stderr, "przp.toml:%d: error: expected key = \"value\"\n", line_no);
            fclose(f);
            return -1;
        }
        *eq = '\0';
        char *key = trim_ws(s);
        char *val = trim_ws(eq + 1);

        if (section == SEC_PACKAGE && !strcmp(key, "name")) {
            if (!parse_quoted_value(val, m->package_name, sizeof(m->package_name))) {
                fprintf(stderr, "przp.toml:%d: error: [package].name must be a quoted string\n", line_no);
                fclose(f);
                return -1;
            }
        } else if (section == SEC_PACKAGE && !strcmp(key, "version")) {
            if (!parse_quoted_value(val, m->version, sizeof(m->version))) {
                fprintf(stderr, "przp.toml:%d: error: [package].version must be a quoted string\n", line_no);
                fclose(f);
                return -1;
            }
        } else if (section == SEC_BUILD && !strcmp(key, "entry")) {
            if (!parse_quoted_value(val, m->entry, sizeof(m->entry))) {
                fprintf(stderr, "przp.toml:%d: error: [build].entry must be a quoted string\n", line_no);
                fclose(f);
                return -1;
            }
        } else if (section == SEC_BUILD && !strcmp(key, "link")) {
            int n = parse_string_array_value(val, m->link_libs, MANIFEST_MAX_LINK_LIBS);
            if (n < 0) {
                fprintf(stderr, "przp.toml:%d: error: [build].link must be an array of quoted "
                                "strings, e.g. link = [\"X11\", \"GL\"] (max %d)\n",
                        line_no, MANIFEST_MAX_LINK_LIBS);
                fclose(f);
                return -1;
            }
            m->n_link_libs = n;
        } else if (section == SEC_NONE) {
            fprintf(stderr, "przp.toml:%d: error: key '%s' must be inside [package], [build], or [deps]\n", line_no, key);
            fclose(f);
            return -1;
        }
    }
    fclose(f);

    if (!m->package_name[0]) {
        fprintf(stderr, "przp: przp.toml missing [package].name\n");
        return -1;
    }
    if (!m->entry[0]) {
        fprintf(stderr, "przp: przp.toml has empty [build].entry\n");
        return -1;
    }
    return 1;
}

static void cmd_build(int argc, char **argv) {
    int release = 0;
    const char *out_name = NULL;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--release")) release = 1;
        if (!strncmp(argv[i], "-o=", 3))  out_name = argv[i] + 3;
    }

    Manifest manifest;
    int mf = read_manifest(&manifest);
    if (mf == 0) { fprintf(stderr, "przp build: no przp.toml found\n"); exit(1); }
    if (mf < 0) exit(1);

    char out_buf[256] = "out";
    if (out_name) {
        snprintf(out_buf, sizeof(out_buf), "%s", out_name);
    } else {
        snprintf(out_buf, sizeof(out_buf), "%s", manifest.package_name);
    }

    char link_flags[MANIFEST_MAX_LINK_LIBS][68];
    const char *link_argv[MANIFEST_MAX_LINK_LIBS];
    for (int i = 0; i < manifest.n_link_libs; i++) {
        snprintf(link_flags[i], sizeof(link_flags[i]), "-l%s", manifest.link_libs[i]);
        link_argv[i] = link_flags[i];
    }

    int rc = compile_file(manifest.entry, out_buf, release, link_argv, manifest.n_link_libs, 0, NULL);
    exit(rc);
}

static void cmd_run(int argc, char **argv) {
    int release = 0;
    for (int i = 0; i < argc; i++)
        if (!strcmp(argv[i], "--release")) release = 1;

    Manifest manifest;
    int mf = read_manifest(&manifest);
    if (mf == 0) { fprintf(stderr, "przp run: no przp.toml found\n"); exit(1); }
    if (mf < 0) exit(1);

    char out_buf[256];
    snprintf(out_buf, sizeof(out_buf), "%s", manifest.package_name);

    char link_flags[MANIFEST_MAX_LINK_LIBS][68];
    const char *link_argv[MANIFEST_MAX_LINK_LIBS];
    for (int i = 0; i < manifest.n_link_libs; i++) {
        snprintf(link_flags[i], sizeof(link_flags[i]), "-l%s", manifest.link_libs[i]);
        link_argv[i] = link_flags[i];
    }

    if (!binary_is_fresh(out_buf, release)) {
        if (compile_file(manifest.entry, out_buf, release, link_argv, manifest.n_link_libs, 0, NULL) != 0) exit(1);
    }

    char run_cmd[512];
    snprintf(run_cmd, sizeof(run_cmd), "./%s", out_buf);
    int rc = system(run_cmd);
    exit(WEXITSTATUS(rc));
}

static const char *path_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* one discovered `test "name" { ... }` block, read back from the
   "<binary>.tests" sidecar codegen writes in test mode */
typedef struct {
    int         index;
    char        name[192];
    char        file[512];
} TestCase;

static int read_test_manifest(const char *bin_path, TestCase *out, int max) {
    char path[1100];
    snprintf(path, sizeof(path), "%s.tests", bin_path);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = 0;
    char line[1024];
    while (n < max && fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        char *t1 = strchr(line, '\t');
        if (!t1) continue;
        *t1 = '\0';
        char *t2 = strchr(t1 + 1, '\t');
        if (!t2) continue;
        *t2 = '\0';
        out[n].index = atoi(line);
        snprintf(out[n].name, sizeof(out[n].name), "%s", t1 + 1);
        snprintf(out[n].file, sizeof(out[n].file), "%s", t2 + 1);
        n++;
    }
    fclose(f);
    return n;
}

/* Remove stale test binaries plus their .tests/.d sidecars left in
   `tests_dir` by previous runs or renamed packages — everything except the
   artifacts we're about to (re)write for `keep_bin` (e.g. "tests/foo_test").
   Without this, every renamed package or rebuild would leave dead binaries
   accumulating in tests/ forever. Anything ending in ".przp" is a test
   source file and is never touched, even if its name happens to contain
   "_test" (e.g. "login_test.przp"). */
static void cleanup_stale_test_artifacts(const char *tests_dir, const char *keep_bin) {
    DIR *td = opendir(tests_dir);
    if (!td) return;

    char keep_base[300];
    snprintf(keep_base, sizeof(keep_base), "%s", path_basename(keep_bin));
    char keep_manifest[320], keep_dep[320];
    snprintf(keep_manifest, sizeof(keep_manifest), "%s.tests", keep_base);
    snprintf(keep_dep, sizeof(keep_dep), "%s.d", keep_base);

    struct dirent *de;
    while ((de = readdir(td))) {
        const char *name = de->d_name;
        if (has_przp_ext(name)) continue;

        const char *p = strstr(name, "_test");
        int is_generated = p && (p[5] == '\0' || p[5] == '.');
        if (!is_generated) continue;
        if (!strcmp(name, keep_base) || !strcmp(name, keep_manifest) || !strcmp(name, keep_dep))
            continue;

        char full[600];
        path_join(full, sizeof(full), tests_dir, name);
        remove(full);
    }
    closedir(td);
}

/* przp test [<file>] [<test_name>]
   przp test                    — run every discovered test
   przp test <file>              — run every test declared in <file>
   przp test <file> <test_name>  — run just that one test in <file> */
static void cmd_test(int argc, char **argv) {
    int release = 0;
    const char *file_filter = NULL;
    const char *name_filter = NULL;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--release")) { release = 1; continue; }
        if (!file_filter)      file_filter = argv[i];
        else if (!name_filter) name_filter = argv[i];
    }

    Manifest manifest;
    int mf = read_manifest(&manifest);
    if (mf == 0) { fprintf(stderr, "przp test: no przp.toml found\n"); exit(1); }
    if (mf < 0) exit(1);

    const char *tests_dir = "tests";
    if (mkdir(tests_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "przp test: cannot create '%s': %s\n", tests_dir, strerror(errno));
        exit(1);
    }

    char bin_path[300];
    snprintf(bin_path, sizeof(bin_path), "%s/%s_test", tests_dir, manifest.package_name);
    cleanup_stale_test_artifacts(tests_dir, bin_path);

    char link_flags[MANIFEST_MAX_LINK_LIBS][68];
    const char *link_argv[MANIFEST_MAX_LINK_LIBS];
    for (int i = 0; i < manifest.n_link_libs; i++) {
        snprintf(link_flags[i], sizeof(link_flags[i]), "-l%s", manifest.link_libs[i]);
        link_argv[i] = link_flags[i];
    }

    if (compile_file(manifest.entry, bin_path, release, link_argv, manifest.n_link_libs, 1, tests_dir) != 0)
        exit(1);

    TestCase cases[512];
    int n = read_test_manifest(bin_path, cases, 512);
    if (n < 0) {
        fprintf(stderr, "przp test: could not read '%s.tests'\n", bin_path);
        exit(1);
    }

    int run_idx[512];
    int n_run = 0;
    for (int i = 0; i < n; i++) {
        if (file_filter) {
            int match = !strcmp(cases[i].file, file_filter)
                     || !strcmp(path_basename(cases[i].file), path_basename(file_filter));
            if (!match) continue;
        }
        if (name_filter && strcmp(cases[i].name, name_filter) != 0) continue;
        run_idx[n_run++] = i;
    }

    if (n_run == 0) {
        if (file_filter || name_filter) {
            fprintf(stderr, "przp test: no test matched%s%s%s%s\n",
                    file_filter ? " file '" : "", file_filter ? file_filter : "",
                    file_filter ? "'" : "", name_filter ? " (with that test name)" : "");
            exit(1);
        }
        printf("no tests found\n");
        exit(0);
    }

    int n_pass = 0, n_fail = 0;
    for (int i = 0; i < n_run; i++) {
        TestCase *tc = &cases[run_idx[i]];
        char cmd[1400];
        snprintf(cmd, sizeof(cmd), "./%s %d", bin_path, tc->index);
        int status = system(cmd);
        int ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
        if (ok) {
            n_pass++;
            printf("PASS  %s\n", tc->name);
        } else {
            n_fail++;
            if (WIFSIGNALED(status))
                printf("FAIL  %s (terminated by signal %d)\n", tc->name, WTERMSIG(status));
            else
                printf("FAIL  %s\n", tc->name);
        }
    }
    printf("%d passed, %d failed\n", n_pass, n_fail);
    exit(n_fail > 0 ? 1 : 0);
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

static void usage(void) {
    fprintf(stderr,
        "Usage: przp <command> [options]\n"
        "\n"
        "Commands:\n"
        "  init [name]           Create a new project\n"
        "  build [--release]     Build the project\n"
        "  run   [--release]     Build and run the project\n"
        "  test  [<file>] [<name>] [--release]\n"
        "                        Run test \"...\" { } blocks: all, one file,\n"
        "                        or one named test within one file\n"
        "  sac <files> [-o=Out]  Compile individual files\n"
    );
    exit(1);
}

int main(int argc, char **argv) {
    if (argc < 2) usage();
    stdlib_root_init(argv[0]);

    const char *cmd = argv[1];
    argv += 2;
    argc -= 2;

    if (!strcmp(cmd, "init"))  cmd_init(argc, argv);
    if (!strcmp(cmd, "build")) cmd_build(argc, argv);
    if (!strcmp(cmd, "run"))   cmd_run(argc, argv);
    if (!strcmp(cmd, "test"))  cmd_test(argc, argv);
    if (!strcmp(cmd, "sac"))   cmd_sac(argc, argv);

    fprintf(stderr, "przp: unknown command '%s'\n", cmd);
    usage();
}
