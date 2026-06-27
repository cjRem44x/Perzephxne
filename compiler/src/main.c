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

/* explicit POSIX declaration for readlink (required under -std=c11 -Wpedantic) */
extern ssize_t readlink(const char *path, char *buf, size_t bufsiz);

/* ── Utilities ────────────────────────────────────────────────────────────── */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "przp: cannot open '%s': %s\n", path, strerror(errno)); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)sz + 1);
    if (!buf) { fprintf(stderr, "przp: out of memory\n"); exit(1); }
    fread(buf, 1, (size_t)sz, f);
    buf[sz] = '\0';
    fclose(f);
    return buf;
}

/* ── Module system ─────────────────────────────────────────────────────────── */

/* stdlib root — resolved once at startup */
static char g_stdlib_root[1024] = "";

static void stdlib_root_init(const char *argv0) {
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

/* Rewrite TY_NAMED references that match any of orig_names → alias__name */
static void rw_type(Type *ty, const char **orig, size_t n, const char *alias, Arena *a) {
    if (!ty) return;
    switch (ty->kind) {
        case TY_NAMED:
            for (size_t i = 0; i < n; i++) {
                if (!strcmp(ty->named.name, orig[i])) {
                    char *buf = arena_alloc(a, strlen(alias) + 2 + strlen(orig[i]) + 1);
                    sprintf(buf, "%s__%s", alias, orig[i]);
                    ty->named.name = buf;
                    break;
                }
            }
            break;
        case TY_PTR: case TY_SLICE:
            rw_type(ty->ptr.inner, orig, n, alias, a);
            break;
        case TY_FN:
            for (size_t i = 0; i < ty->fn.params.len; i++)
                rw_type(ty->fn.params.data[i], orig, n, alias, a);
            rw_type(ty->fn.ret, orig, n, alias, a);
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
                if (!strcmp(e->ident.name, orig[i])) {
                    char buf[512];
                    snprintf(buf, sizeof(buf), "%s__%s", alias, orig[i]);
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
            for (size_t i = 0; i < e->struct_lit.fields.len; i++)
                rw_ident_expr(e->struct_lit.fields.data[i].val, orig, n_orig, alias, a);
            break;
        case EXPR_ARRAY_LIT:
            for (size_t i = 0; i < e->array_lit.len; i++)
                rw_ident_expr(e->array_lit.data[i], orig, n_orig, alias, a);
            break;
        case EXPR_IF:
            rw_ident_expr(e->if_expr.cond, orig, n_orig, alias, a);
            rw_ident_stmt(e->if_expr.then_, orig, n_orig, alias, a);
            rw_ident_stmt(e->if_expr.else_, orig, n_orig, alias, a);
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

    char buf[512];
    for (size_t i = 0; i < mod->items.len; i++) {
        Item *item = mod->items.data[i];
        if (!item->name) continue;
        if (item->kind == ITEM_EXTERN_FN) {
            /* save original C name, then mangle so alias.fn rewrites work */
            item->extern_fn.c_name = item->name;
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
static void load_imports(Module *mod, const char *src_path,
                         Arena *arena, const char **loading, size_t n_loading) {
    /* collect (alias, resolved-path) pairs from ITEM_IMPORT items */
    const char *aliases[64];
    size_t      n_aliases = 0;
    char        src_dir[1024];
    src_dir_of(src_path, src_dir, sizeof(src_dir));

    for (size_t i = 0; i < mod->items.len && n_aliases < 64; i++) {
        Item *item = mod->items.data[i];
        if (item->kind != ITEM_IMPORT) continue;

        for (size_t j = 0; j < item->imports.len && n_aliases < 64; j++) {
            const char *alias     = item->imports.data[j].alias;
            const char *imp_path  = item->imports.data[j].path;

            /* resolve path: append .przp */
            char full[1024];
            if (imp_path[0] == '/') {
                snprintf(full, sizeof(full), "%s.przp", imp_path);
            } else if (!strncmp(imp_path, "std/", 4) && g_stdlib_root[0]) {
                /* stdlib path: resolve against stdlib root */
                snprintf(full, sizeof(full), "%s/%s.przp", g_stdlib_root, imp_path + 4);
            } else {
                snprintf(full, sizeof(full), "%s/%s.przp", src_dir, imp_path);
            }

            /* cycle detection */
            int cycle = 0;
            for (size_t k = 0; k < n_loading; k++)
                if (!strcmp(loading[k], full)) { cycle = 1; break; }
            if (cycle) {
                fprintf(stderr, "przp: import cycle: '%s'\n", full);
                continue;
            }

            /* read + parse imported file */
            char *imp_src = read_file(full);
            error_init(full, imp_src);
            Module *imp = parse(imp_src, (uint32_t)(n_loading + 1), arena);

            /* recurse: handle imports inside the imported module */
            const char *new_loading[64];
            memcpy(new_loading, loading, n_loading * sizeof(char*));
            new_loading[n_loading] = full;
            load_imports(imp, full, arena, new_loading, n_loading + 1);

            /* mangle imported items, merge into main module */
            mangle_items(imp, alias, arena);
            merge_items(mod, imp);

            aliases[n_aliases++] = alias;
        }
    }

    /* rewrite module accesses in the calling module */
    if (n_aliases > 0) {
        for (size_t i = 0; i < mod->items.len; i++)
            rw_item(mod->items.data[i], aliases, n_aliases, arena);
    }
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
static int compile_file(const char *src_path, const char *out_path, int release) {
    char *src = read_file(src_path);

    Arena arena;
    arena_init(&arena);

    error_init(src_path, src);
    Module *mod = parse(src, 0, &arena);

    /* load and merge imported modules before sema */
    const char *loading[1] = { src_path };
    load_imports(mod, src_path, &arena, loading, 1);
    error_init(src_path, src); /* restore main file context for sema/codegen errors */

    if (!sema_check(mod)) { arena_free(&arena); free(src); return 1; }

    /* write .ll to a temp file next to the source */
    char ll_path[1024];
    snprintf(ll_path, sizeof(ll_path), "/tmp/przp_%d.ll", (int)getpid());

    FILE *ll_f = fopen(ll_path, "w");
    if (!ll_f) { fprintf(stderr, "przp: cannot write '%s'\n", ll_path); return 1; }
    int ok = codegen(mod, ll_f, release);
    fclose(ll_f);

    arena_free(&arena);
    free(src);

    if (!ok) { remove(ll_path); return 1; }

    /* invoke clang to produce the binary */
    char cmd[2048];
    const char *opt = release ? "-O2" : "-O0 -g";
    snprintf(cmd, sizeof(cmd), "clang %s %s -o %s -lm", opt, ll_path, out_path);
    int ret = system(cmd);
    if (!getenv("PRZP_KEEP_IR")) remove(ll_path);
    return (ret == 0) ? 0 : 1;
}

/* ── Sub-commands ─────────────────────────────────────────────────────────── */

static void cmd_sac(int argc, char **argv) {
    /* przp sac <files...> [-o=Name] [--release] */
    const char *out_name = "out";
    int release = 0;
    const char **files = malloc(sizeof(char*) * (size_t)argc);
    int nfiles = 0;

    for (int i = 0; i < argc; i++) {
        if (!strncmp(argv[i], "-o=", 3)) { out_name = argv[i] + 3; continue; }
        if (!strcmp(argv[i], "--release")) { release = 1; continue; }
        files[nfiles++] = argv[i];
    }

    if (nfiles == 0) { fprintf(stderr, "przp sac: no input files\n"); free(files); exit(1); }

    /* For now: single-file only.  Multi-file linking is future work. */
    int rc = compile_file(files[0], out_name, release);
    free(files);
    exit(rc);
}

static void cmd_init(int argc, char **argv) {
    const char *name = (argc > 0) ? argv[0] : "myproject";

    /* create directory layout */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s/src", name);
    system(cmd);

    /* przp.toml */
    char toml_path[256];
    snprintf(toml_path, sizeof(toml_path), "%s/przp.toml", name);
    FILE *f = fopen(toml_path, "w");
    if (f) {
        fprintf(f, "[package]\nname = \"%s\"\nversion = \"0.1.0\"\n\n[build]\nentry = \"src/main.przp\"\n", name);
        fclose(f);
    }

    /* src/main.przp */
    char main_path[256];
    snprintf(main_path, sizeof(main_path), "%s/src/main.przp", name);
    f = fopen(main_path, "w");
    if (f) {
        fprintf(f, "fn main() -> i32 {\n    @pf(\"Hello from %s!\\n\")\n    ret 0\n}\n", name);
        fclose(f);
    }

    printf("Created project '%s'\n", name);
    exit(0);
}

static const char *find_entry(void) {
    /* look for przp.toml, parse entry = "..." */
    FILE *f = fopen("przp.toml", "r");
    if (!f) return NULL;
    static char entry[256];
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, " entry = \"%255[^\"]\"", entry) == 1) {
            fclose(f);
            return entry;
        }
    }
    fclose(f);
    return "src/main.przp";
}

static void cmd_build(int argc, char **argv) {
    int release = 0;
    const char *out_name = NULL;
    for (int i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--release")) release = 1;
        if (!strncmp(argv[i], "-o=", 3))  out_name = argv[i] + 3;
    }

    const char *entry = find_entry();
    if (!entry) { fprintf(stderr, "przp build: no przp.toml or entry point found\n"); exit(1); }

    char out_buf[256] = "out";
    if (out_name) {
        snprintf(out_buf, sizeof(out_buf), "%s", out_name);
    } else {
        /* derive output name from entry filename */
        basename_no_ext(entry, out_buf, sizeof(out_buf));
    }

    int rc = compile_file(entry, out_buf, release);
    exit(rc);
}

static void cmd_run(int argc, char **argv) {
    int release = 0;
    for (int i = 0; i < argc; i++)
        if (!strcmp(argv[i], "--release")) release = 1;

    const char *entry = find_entry();
    if (!entry) { fprintf(stderr, "przp run: no przp.toml or entry point found\n"); exit(1); }

    char out_buf[256];
    basename_no_ext(entry, out_buf, sizeof(out_buf));

    if (compile_file(entry, out_buf, release) != 0) exit(1);

    char run_cmd[512];
    snprintf(run_cmd, sizeof(run_cmd), "./%s", out_buf);
    int rc = system(run_cmd);
    exit(WEXITSTATUS(rc));
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
    if (!strcmp(cmd, "sac"))   cmd_sac(argc, argv);

    fprintf(stderr, "przp: unknown command '%s'\n", cmd);
    usage();
}
