#include "codegen.h"
#include "error.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>

/* ── Codegen state ────────────────────────────────────────────────────────── */

typedef struct StrConst {
    struct StrConst *next;
    const char      *val;
    int              id;
    size_t           len; /* byte length including null terminator */
} StrConst;

typedef struct StructInfo {
    struct StructInfo *next;
    const char        *name;
    FieldList          fields; /* copy of ITEM_STRUCT's field list */
} StructInfo;

typedef struct { const char *name; int64_t value; } EnumVariantVal;

typedef struct EnumInfo {
    struct EnumInfo *next;
    const char      *name;
    Type            *backing_ty;
    size_t           n_variants;
    EnumVariantVal  *variants;
} EnumInfo;

typedef struct Symbol {
    struct Symbol *next;
    const char    *name;
    const char    *llvm_name; /* @name or %name */
    int            is_global;
    Type          *ty;
} Symbol;

typedef struct DeferEntry {
    struct DeferEntry *next;
    StmtList           stmts;
} DeferEntry;

typedef struct Scope {
    struct Scope  *parent;
    Symbol        *syms;
    DeferEntry    *defers;      /* LIFO — head = most recently deferred */
    int            is_loop;     /* 1 if this scope is the body of a loop */
    int            break_label; /* label to branch to on break */
    int            cont_label;  /* label to branch to on continue */
} Scope;

typedef struct {
    FILE        *out;
    Arena       *arena;
    StrConst    *str_consts;
    StructInfo  *structs;    /* name → field list for struct layout */
    EnumInfo    *enums;      /* name → variant values for enum access */
    int          str_id;
    int          tmp_id;      /* next %t<n> temporary */
    int          label_id;    /* next label suffix     */
    Scope       *scope;
    const char  *cur_fn_ret; /* LLVM type string of current function return */
    int          terminated;  /* 1 = current block already has a terminator */
    int          had_error;
} CG;

/* ── Utilities ────────────────────────────────────────────────────────────── */

static void emit(CG *cg, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(cg->out, fmt, ap);
    va_end(ap);
}

static int new_tmp(CG *cg)   { return cg->tmp_id++; }
static int new_label(CG *cg) { return cg->label_id++; }

/* emit a branch/jump terminator (only if block not already terminated) */
static void emit_br(CG *cg, const char *fmt, ...) {
    if (cg->terminated) return;
    va_list ap; va_start(ap, fmt);
    vfprintf(cg->out, fmt, ap);
    va_end(ap);
    cg->terminated = 1;
}

/* emit a label, which starts a new basic block */
static void emit_label(CG *cg, int id) {
    emit(cg, "l%d:\n", id);
    cg->terminated = 0;
}

static void push_scope(CG *cg) {
    Scope *s = ARENA_NEW(cg->arena, Scope);
    s->parent      = cg->scope;
    s->defers      = NULL;
    s->is_loop     = 0;
    s->break_label = -1;
    s->cont_label  = -1;
    cg->scope = s;
}

static void push_loop_scope(CG *cg, int break_l, int cont_l) {
    Scope *s = ARENA_NEW(cg->arena, Scope);
    s->parent      = cg->scope;
    s->defers      = NULL;
    s->is_loop     = 1;
    s->break_label = break_l;
    s->cont_label  = cont_l;
    cg->scope = s;
}

/* emit deferred stmts for one scope in LIFO order, then clear the list */
static void emit_defers_for_scope(CG *cg, Scope *sc);

static void pop_scope(CG *cg) {
    emit_defers_for_scope(cg, cg->scope);
    cg->scope = cg->scope->parent;
}

static void define_sym(CG *cg, const char *name, const char *llvm, int global, Type *ty) {
    Symbol *s = ARENA_NEW(cg->arena, Symbol);
    s->name      = name;
    s->llvm_name = llvm;
    s->is_global = global;
    s->ty        = ty;
    s->next      = cg->scope->syms;
    cg->scope->syms = s;
}

static Symbol *lookup(CG *cg, const char *name) {
    for (Scope *sc = cg->scope; sc; sc = sc->parent)
        for (Symbol *s = sc->syms; s; s = s->next)
            if (!strcmp(s->name, name)) return s;
    return NULL;
}

/* intern a string constant, return its id */
static int intern_str(CG *cg, const char *val) {
    for (StrConst *sc = cg->str_consts; sc; sc = sc->next)
        if (!strcmp(sc->val, val)) return sc->id;
    StrConst *sc = ARENA_NEW(cg->arena, StrConst);
    sc->val  = val;
    sc->id   = cg->str_id++;
    sc->len  = strlen(val) + 1;
    sc->next = cg->str_consts;
    cg->str_consts = sc;
    return sc->id;
}

/* ── Type → LLVM IR string ───────────────────────────────────────────────── */

static const char *llvm_type(Type *ty) {
    if (!ty) return "void";
    switch (ty->kind) {
        case TY_VOID:      return "void";
        case TY_BOOL:      return "i1";
        case TY_I8:  case TY_U8:  case TY_CHAR: return "i8";
        case TY_I16: case TY_U16: return "i16";
        case TY_I32: case TY_U32: return "i32";
        case TY_I64: case TY_U64: return "i64";
        case TY_USIZE:     return "i64";   /* 64-bit target */
        case TY_F16:       return "half";
        case TY_F32:       return "float";
        case TY_F64:       return "double";
        case TY_STR:       return "{ ptr, i64 }"; /* fat pointer */
        case TY_ANY:       return "{ ptr, i64 }"; /* data + typeId */
        case TY_PTR:       return "ptr";
        case TY_SMART_PTR: return "ptr";
        case TY_SLICE:     return "{ ptr, i64 }";
        case TY_FAILABLE:  return llvm_type(ty->ptr.inner); /* value part */
        case TY_NAMED: {
            /* Round-robin static buffers — safe for up to 8 concurrent uses */
            static char bufs[8][128];
            static int  bi = 0;
            bi = (bi + 1) % 8;
            snprintf(bufs[bi], sizeof(bufs[bi]), "%%%s", ty->named.name);
            return bufs[bi];
        }
        default:           return "ptr";
    }
}

static StructInfo *find_struct(CG *cg, const char *name) {
    for (StructInfo *si = cg->structs; si; si = si->next)
        if (!strcmp(si->name, name)) return si;
    return NULL;
}

static EnumInfo *find_enum(CG *cg, const char *name) {
    for (EnumInfo *ei = cg->enums; ei; ei = ei->next)
        if (!strcmp(ei->name, name)) return ei;
    return NULL;
}

/* For named types: enums use their backing integer type; structs use %Name. */
static const char *effective_llvm_type(CG *cg, Type *ty) {
    if (!ty) return "i32";
    if (ty->kind == TY_NAMED) {
        EnumInfo *ei = find_enum(cg, ty->named.name);
        if (ei) return llvm_type(ei->backing_ty);
    }
    return llvm_type(ty);
}

static int struct_field_index(StructInfo *si, const char *field) {
    for (size_t i = 0; i < si->fields.len; i++)
        if (!strcmp(si->fields.data[i].name, field)) return (int)i;
    return -1;
}

static Type *struct_field_type(StructInfo *si, const char *field) {
    for (size_t i = 0; i < si->fields.len; i++)
        if (!strcmp(si->fields.data[i].name, field)) return si->fields.data[i].ty;
    return NULL;
}

static int type_is_float(Type *ty) {
    if (!ty) return 0;
    return ty->kind == TY_F16 || ty->kind == TY_F32 || ty->kind == TY_F64;
}

static int type_is_signed(Type *ty) {
    if (!ty) return 0;
    return ty->kind == TY_I8 || ty->kind == TY_I16 ||
           ty->kind == TY_I32 || ty->kind == TY_I64;
}

static const char *pf_specifier(Type *ty) {
    if (!ty) return "%d";
    switch (ty->kind) {
        case TY_I8:  case TY_I16: case TY_I32: return "%d";
        case TY_I64: return "%lld";
        case TY_U8:  case TY_U16: case TY_U32: return "%u";
        case TY_U64: case TY_USIZE: return "%llu";
        case TY_CHAR: return "%c";
        case TY_BOOL: return "%d";
        case TY_F16: case TY_F32: case TY_F64: return "%f";
        case TY_STR: return "%s";
        case TY_PTR: return "%p";
        default:     return "%d";
    }
}

/* ── Emit string constants (at module top) ────────────────────────────────── */

static void escape_str_for_ir(FILE *out, const char *s) {
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\' || c < 32 || c > 126)
            fprintf(out, "\\%02X", c);
        else
            fputc(c, out);
    }
    /* null terminator */
    fprintf(out, "\\00");
}

static void emit_str_constants(CG *cg) {
    for (StrConst *sc = cg->str_consts; sc; sc = sc->next) {
        emit(cg, "@.str.%d = private unnamed_addr constant [%zu x i8] c\"",
             sc->id, sc->len);
        escape_str_for_ir(cg->out, sc->val);
        emit(cg, "\"\n");
    }
}

/* ── Expression codegen ───────────────────────────────────────────────────── */

/* Returns the LLVM value string representing the result (e.g. "%t3", "42", "@.str.0") */
typedef struct { char buf[64]; } Val;

static Val val_tmp(int id)  { Val v; snprintf(v.buf, sizeof(v.buf), "%%t%d", id); return v; }
static Val val_str(const char *s) { Val v; snprintf(v.buf, sizeof(v.buf), "%s", s); return v; }

/* forward declarations */
static Val cg_expr(CG *cg, Expr *e, Type **out_ty);
static void cg_stmt(CG *cg, Stmt *s);

static void emit_defers_for_scope(CG *cg, Scope *sc) {
    /* Emit into the CURRENT basic block only — cg_stmt's terminated guard prevents
       double-emission: if we already have a terminator (e.g. from break/ret),
       cg_stmt returns early.  We do NOT clear sc->defers so that pop_scope can
       still emit the defers for the natural (non-break) code path. */
    for (DeferEntry *d = sc->defers; d; d = d->next)
        for (size_t i = 0; i < d->stmts.len; i++)
            cg_stmt(cg, d->stmts.data[i]);
}

static Val cg_expr(CG *cg, Expr *e, Type **out_ty) {
    if (out_ty) *out_ty = NULL;

    switch (e->kind) {
        case EXPR_INT: {
            Val v; snprintf(v.buf, sizeof(v.buf), "%" PRIu64, e->ival);
            return v;
        }
        case EXPR_FLOAT: {
            /* LLVM IR requires IEEE 754 hex: 0x followed by 16 uppercase hex digits */
            union { double d; uint64_t u; } bits;
            bits.d = e->fval;
            Val v; snprintf(v.buf, sizeof(v.buf), "0x%016" PRIX64, bits.u);
            return v;
        }
        case EXPR_BOOL: {
            Val v; snprintf(v.buf, sizeof(v.buf), "%d", e->bval ? 1 : 0);
            return v;
        }
        case EXPR_CHAR: {
            Val v; snprintf(v.buf, sizeof(v.buf), "%d", (int)e->cval);
            return v;
        }
        case EXPR_NULL: {
            return val_str("null");
        }
        case EXPR_UNDEF: {
            return val_str("undef");
        }
        case EXPR_DISCARD: {
            return val_str("undef");
        }

        case EXPR_STR: {
            /* Produce a ptr to the string constant */
            int id  = intern_str(cg, e->sval);
            size_t len = strlen(e->sval) + 1;
            int t = new_tmp(cg);
            emit(cg, "  %%t%d = getelementptr inbounds [%zu x i8],"
                     " ptr @.str.%d, i32 0, i32 0\n", t, len, id);
            return val_tmp(t);
        }

        case EXPR_IDENT: {
            Symbol *sym = lookup(cg, e->ident.name);
            if (!sym)
                fatal_at(e->span, "undefined identifier '%s'", e->ident.name);
            if (out_ty) *out_ty = sym->ty;
            /* Struct values: return alloca ptr (no load). Enums/scalars: load. */
            if (sym->ty && sym->ty->kind == TY_NAMED && !find_enum(cg, sym->ty->named.name))
                return val_str(sym->llvm_name);
            int t = new_tmp(cg);
            const char *llt = effective_llvm_type(cg, sym->ty);
            emit(cg, "  %%t%d = load %s, ptr %s\n", t, llt, sym->llvm_name);
            return val_tmp(t);
        }

        case EXPR_BUILTIN: {
            const char *name = e->builtin.name;

            /* @pf / @epf — string interpolation print */
            if (!strcmp(name, "pf") || !strcmp(name, "epf")) {
                if (e->builtin.args.len == 0)
                    fatal_at(e->span, "@pf requires at least a format string");

                size_t na = e->builtin.args.len;
                Expr *fmt_arg = e->builtin.args.data[0];

                /* Interpolated path: format string has \x01 sentinels from parser */
                if (fmt_arg->kind == EXPR_STR && strchr(fmt_arg->sval, '\x01')) {
                    Val   *ivals     = malloc(sizeof(Val)   * na);
                    Type **itys      = malloc(sizeof(Type*) * na);
                    Val   *printable = malloc(sizeof(Val)   * na);

                    for (size_t i = 1; i < na; i++) {
                        itys[i]  = NULL;
                        ivals[i] = cg_expr(cg, e->builtin.args.data[i], &itys[i]);
                    }

                    /* Pre-extract ptr field for str args (str is { ptr, i64 }) */
                    for (size_t i = 1; i < na; i++) {
                        if (itys[i] && itys[i]->kind == TY_STR) {
                            int sv = new_tmp(cg);
                            emit(cg, "  %%t%d = extractvalue { ptr, i64 } %s, 0\n",
                                 sv, ivals[i].buf);
                            printable[i] = val_tmp(sv);
                        } else {
                            printable[i] = ivals[i];
                        }
                    }

                    /* Build printf format string: replace \x01<spec>\x02 blocks.
                       Empty spec means auto-detect from arg type. */
                    char pf_fmt[4096];
                    size_t pff = 0, ai = 1;
                    for (const char *fs = fmt_arg->sval;
                         *fs && pff < sizeof(pf_fmt) - 32; fs++) {
                        if ((unsigned char)*fs == '\x01') {
                            fs++;
                            /* read explicit spec until \x02 */
                            char spec_buf[64];
                            size_t sl = 0;
                            while (*fs && (unsigned char)*fs != '\x02'
                                   && sl < sizeof(spec_buf) - 1)
                                spec_buf[sl++] = *fs++;
                            /* fs now points at \x02; the for-loop ++ will skip it */
                            spec_buf[sl] = '\0';
                            pf_fmt[pff++] = '%';
                            if (sl > 0) {
                                /* user-supplied spec */
                                for (size_t j = 0; j < sl; j++)
                                    pf_fmt[pff++] = spec_buf[j];
                            } else {
                                /* auto-detect: pf_specifier returns "%X", skip the % */
                                const char *auto_spec = pf_specifier(itys[ai]);
                                for (const char *sp = auto_spec + 1; *sp; sp++)
                                    pf_fmt[pff++] = *sp;
                            }
                            ai++;
                        } else {
                            pf_fmt[pff++] = *fs;
                        }
                    }
                    pf_fmt[pff] = '\0';

                    int fmtid = intern_str(cg, arena_strndup(cg->arena, pf_fmt, pff));
                    int ft    = new_tmp(cg);
                    emit(cg, "  %%t%d = getelementptr inbounds [%zu x i8],"
                             " ptr @.str.%d, i32 0, i32 0\n", ft, pff + 1, fmtid);

                    int t = new_tmp(cg);
                    emit(cg, "  %%t%d = call i32 (ptr, ...) @printf(ptr %%t%d", t, ft);
                    for (size_t i = 1; i < na; i++) {
                        const char *llt = (itys[i] && itys[i]->kind == TY_STR)
                                          ? "ptr" : (itys[i] ? llvm_type(itys[i]) : "i32");
                        emit(cg, ", %s %s", llt, printable[i].buf);
                    }
                    emit(cg, ")\n");

                    free(ivals); free(itys); free(printable);
                    return val_tmp(t);
                }

                /* Non-interpolated path: pass args directly to printf */
                Val   *pf_vals = malloc(sizeof(Val)   * na);
                Type **pf_tys  = malloc(sizeof(Type*) * na);
                for (size_t i = 0; i < na; i++) {
                    pf_tys[i]  = NULL;
                    pf_vals[i] = cg_expr(cg, e->builtin.args.data[i], &pf_tys[i]);
                }
                int t = new_tmp(cg);
                emit(cg, "  %%t%d = call i32 (ptr, ...) @printf(ptr %s",
                     t, pf_vals[0].buf);
                for (size_t i = 1; i < na; i++) {
                    const char *llt = pf_tys[i] ? llvm_type(pf_tys[i]) : "i32";
                    emit(cg, ", %s %s", llt, pf_vals[i].buf);
                }
                emit(cg, ")\n");
                free(pf_vals);
                free(pf_tys);
                return val_tmp(t);
            }

            /* @exit */
            if (!strcmp(name, "exit")) {
                Val code = cg_expr(cg, e->builtin.args.data[0], NULL);
                emit(cg, "  call void @exit(i32 %s)\n", code.buf);
                emit_br(cg, "  unreachable\n");
                return val_str("0");
            }

            /* @panic */
            if (!strcmp(name, "panic")) {
                Val msg = cg_expr(cg, e->builtin.args.data[0], NULL);
                emit(cg, "  call i32 (ptr, ...) @printf(ptr %s)\n", msg.buf);
                emit(cg, "  call void @exit(i32 1)\n");
                emit_br(cg, "  unreachable\n");
                return val_str("0");
            }

            /* @assert */
            if (!strcmp(name, "assert")) {
                Val cond = cg_expr(cg, e->builtin.args.data[0], NULL);
                int pass  = new_label(cg);
                int fail  = new_label(cg);
                emit_br(cg, "  br i1 %s, label %%l%d, label %%l%d\n",
                        cond.buf, pass, fail);
                emit_label(cg, fail);
                emit(cg, "  call void @exit(i32 1)\n");
                emit_br(cg, "  unreachable\n");
                emit_label(cg, pass);
                return val_str("0");
            }

            /* @args — returns []str (argc/argv from main, passed via globals) */
            if (!strcmp(name, "args")) {
                int t = new_tmp(cg);
                emit(cg, "  %%t%d = load i32, ptr @__przp_argc\n", t);
                int t2 = new_tmp(cg);
                emit(cg, "  %%t%d = load ptr, ptr @__przp_argv\n", t2);
                /* pack into { ptr, i64 } slice */
                int sl = new_tmp(cg);
                emit(cg, "  %%t%d = alloca { ptr, i64 }\n", sl);
                int p0 = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr { ptr, i64 }, ptr %%t%d, i32 0, i32 0\n", p0, sl);
                emit(cg, "  store ptr %%t%d, ptr %%t%d\n", t2, p0);
                int p1 = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr { ptr, i64 }, ptr %%t%d, i32 0, i32 1\n", p1, sl);
                int argc64 = new_tmp(cg);
                emit(cg, "  %%t%d = sext i32 %%t%d to i64\n", argc64, t);
                emit(cg, "  store i64 %%t%d, ptr %%t%d\n", argc64, p1);
                int res = new_tmp(cg);
                emit(cg, "  %%t%d = load { ptr, i64 }, ptr %%t%d\n", res, sl);
                return val_tmp(res);
            }

            /* @alo */
            if (!strcmp(name, "alo")) {
                /* @alo(T) or @alo(T, N) — emit malloc call */
                int t = new_tmp(cg);
                emit(cg, "  %%t%d = call ptr @malloc(i64 8)\n", t); /* placeholder size */
                return val_tmp(t);
            }

            /* @free */
            if (!strcmp(name, "free")) {
                Val ptr = cg_expr(cg, e->builtin.args.data[0], NULL);
                emit(cg, "  call void @free(ptr %s)\n", ptr.buf);
                return val_str("0");
            }

            /* @min / @max */
            if (!strcmp(name, "min") || !strcmp(name, "max")) {
                Type *ta = NULL, *tb = NULL;
                Val a = cg_expr(cg, e->builtin.args.data[0], &ta);
                Val b = cg_expr(cg, e->builtin.args.data[1], &tb);
                const char *llt = ta ? llvm_type(ta) : "i32";
                int cmp = new_tmp(cg);
                const char *pred = (!strcmp(name,"min"))
                    ? (type_is_signed(ta) ? "slt" : "ult")
                    : (type_is_signed(ta) ? "sgt" : "ugt");
                emit(cg, "  %%t%d = icmp %s %s %s, %s\n",
                     cmp, pred, llt, a.buf, b.buf);
                int sel = new_tmp(cg);
                emit(cg, "  %%t%d = select i1 %%t%d, %s %s, %s %s\n",
                     sel, cmp, llt, a.buf, llt, b.buf);
                return val_tmp(sel);
            }

            /* @abs */
            if (!strcmp(name, "abs")) {
                Type *ta = NULL;
                Val a = cg_expr(cg, e->builtin.args.data[0], &ta);
                const char *llt = ta ? llvm_type(ta) : "i32";
                int neg = new_tmp(cg);
                emit(cg, "  %%t%d = sub %s 0, %s\n", neg, llt, a.buf);
                int cmp = new_tmp(cg);
                emit(cg, "  %%t%d = icmp slt %s %s, 0\n", cmp, llt, a.buf);
                int sel = new_tmp(cg);
                emit(cg, "  %%t%d = select i1 %%t%d, %s %%t%d, %s %s\n",
                     sel, cmp, llt, neg, llt, a.buf);
                return val_tmp(sel);
            }

            /* @size */
            if (!strcmp(name, "size")) {
                /* return placeholder 8 — sema will fill in real sizes */
                return val_str("8");
            }

            /* @len */
            if (!strcmp(name, "len")) {
                /* extract len field from fat pointer struct */
                Type *ta = NULL;
                Val a = cg_expr(cg, e->builtin.args.data[0], &ta);
                int t = new_tmp(cg);
                emit(cg, "  %%t%d = extractvalue { ptr, i64 } %s, 1\n", t, a.buf);
                return val_tmp(t);
            }

            fatal_at(e->span, "unknown builtin '@%s'", name);
        }

        case EXPR_CAST: {
            Type *src_ty = NULL;
            Val src = cg_expr(cg, e->cast.val, &src_ty);
            const char *dst = e->cast.ty_name;
            int t = new_tmp(cg);

            if (!strcmp(dst, "str")) {
                /* int/float → string: call sprintf into a temp buffer — simplified */
                int buf = new_tmp(cg);
                emit(cg, "  %%t%d = alloca [32 x i8]\n", buf);
                emit(cg, "  call i32 (ptr, ptr, ...) @sprintf("
                         "ptr %%t%d, ptr @.fmt.d, i32 %s)\n", buf, src.buf);
                emit(cg, "  %%t%d = getelementptr [32 x i8], ptr %%t%d, i32 0, i32 0\n",
                     t, buf);
            } else if (!strcmp(dst, "i32") && src_ty &&
                       (src_ty->kind == TY_STR)) {
                emit(cg, "  %%t%d = call i32 @atoi(ptr %s)\n", t, src.buf);
            } else {
                /* numeric cast */
                const char *src_llt = src_ty ? llvm_type(src_ty) : "i32";
                if (!strcmp(dst,"i8"))    emit(cg, "  %%t%d = trunc %s %s to i8\n",  t, src_llt, src.buf);
                else if (!strcmp(dst,"i16")) emit(cg, "  %%t%d = trunc %s %s to i16\n", t, src_llt, src.buf);
                else if (!strcmp(dst,"i32")) emit(cg, "  %%t%d = trunc %s %s to i32\n", t, src_llt, src.buf);
                else if (!strcmp(dst,"i64")) emit(cg, "  %%t%d = sext %s %s to i64\n",  t, src_llt, src.buf);
                else if (!strcmp(dst,"u8"))  emit(cg, "  %%t%d = trunc %s %s to i8\n",  t, src_llt, src.buf);
                else if (!strcmp(dst,"u16")) emit(cg, "  %%t%d = trunc %s %s to i16\n", t, src_llt, src.buf);
                else if (!strcmp(dst,"u32")) emit(cg, "  %%t%d = trunc %s %s to i32\n", t, src_llt, src.buf);
                else if (!strcmp(dst,"u64")) emit(cg, "  %%t%d = zext %s %s to i64\n",  t, src_llt, src.buf);
                else if (!strcmp(dst,"f32")) {
                    if (type_is_float(src_ty))
                        emit(cg, "  %%t%d = fptrunc %s %s to float\n", t, src_llt, src.buf);
                    else if (type_is_signed(src_ty))
                        emit(cg, "  %%t%d = sitofp %s %s to float\n", t, src_llt, src.buf);
                    else
                        emit(cg, "  %%t%d = uitofp %s %s to float\n", t, src_llt, src.buf);
                }
                else if (!strcmp(dst,"f64")) {
                    if (type_is_float(src_ty))
                        emit(cg, "  %%t%d = fpext %s %s to double\n", t, src_llt, src.buf);
                    else if (type_is_signed(src_ty))
                        emit(cg, "  %%t%d = sitofp %s %s to double\n", t, src_llt, src.buf);
                    else
                        emit(cg, "  %%t%d = uitofp %s %s to double\n", t, src_llt, src.buf);
                }
                else {
                    emit(cg, "  %%t%d = bitcast %s %s to i64\n", t, src_llt, src.buf);
                }
            }
            return val_tmp(t);
        }

        case EXPR_BINOP: {
            Type *lt = NULL, *rt = NULL;
            Val l = cg_expr(cg, e->binop.l, &lt);
            Val r = cg_expr(cg, e->binop.r, &rt);
            Type *ty = lt ? lt : rt;
            const char *llt = ty ? llvm_type(ty) : "i32";
            int t = new_tmp(cg);
            int is_flt = type_is_float(ty);
            int is_sgn = type_is_signed(ty);

            switch (e->binop.op) {
                case BINOP_ADD: emit(cg, "  %%t%d = %s %s %s, %s\n", t, is_flt?"fadd":"add", llt, l.buf, r.buf); break;
                case BINOP_SUB: emit(cg, "  %%t%d = %s %s %s, %s\n", t, is_flt?"fsub":"sub", llt, l.buf, r.buf); break;
                case BINOP_MUL: emit(cg, "  %%t%d = %s %s %s, %s\n", t, is_flt?"fmul":"mul", llt, l.buf, r.buf); break;
                case BINOP_DIV: emit(cg, "  %%t%d = %s %s %s, %s\n", t, is_flt?"fdiv":(is_sgn?"sdiv":"udiv"), llt, l.buf, r.buf); break;
                case BINOP_MOD: emit(cg, "  %%t%d = %s %s %s, %s\n", t, is_flt?"frem":(is_sgn?"srem":"urem"), llt, l.buf, r.buf); break;
                case BINOP_AMP: emit(cg, "  %%t%d = and %s %s, %s\n", t, llt, l.buf, r.buf); break;
                case BINOP_PIPE:emit(cg, "  %%t%d = or  %s %s, %s\n", t, llt, l.buf, r.buf); break;
                case BINOP_XOR: emit(cg, "  %%t%d = xor %s %s, %s\n", t, llt, l.buf, r.buf); break;
                case BINOP_SHL: emit(cg, "  %%t%d = shl %s %s, %s\n", t, llt, l.buf, r.buf); break;
                case BINOP_SHR: emit(cg, "  %%t%d = %s %s %s, %s\n", t, is_sgn?"ashr":"lshr", llt, l.buf, r.buf); break;
                case BINOP_AND: emit(cg, "  %%t%d = and i1 %s, %s\n", t, l.buf, r.buf); break;
                case BINOP_OR:  emit(cg, "  %%t%d = or  i1 %s, %s\n", t, l.buf, r.buf); break;
                case BINOP_EQ:  emit(cg, "  %%t%d = %s %s %s, %s\n", t, is_flt?"fcmp oeq":"icmp eq",  llt, l.buf, r.buf); break;
                case BINOP_NE:  emit(cg, "  %%t%d = %s %s %s, %s\n", t, is_flt?"fcmp one":"icmp ne",  llt, l.buf, r.buf); break;
                case BINOP_LT:  emit(cg, "  %%t%d = %s %s %s, %s\n", t, is_flt?"fcmp olt":(is_sgn?"icmp slt":"icmp ult"), llt, l.buf, r.buf); break;
                case BINOP_GT:  emit(cg, "  %%t%d = %s %s %s, %s\n", t, is_flt?"fcmp ogt":(is_sgn?"icmp sgt":"icmp ugt"), llt, l.buf, r.buf); break;
                case BINOP_LE:  emit(cg, "  %%t%d = %s %s %s, %s\n", t, is_flt?"fcmp ole":(is_sgn?"icmp sle":"icmp ule"), llt, l.buf, r.buf); break;
                case BINOP_GE:  emit(cg, "  %%t%d = %s %s %s, %s\n", t, is_flt?"fcmp oge":(is_sgn?"icmp sge":"icmp uge"), llt, l.buf, r.buf); break;
                default:        emit(cg, "  %%t%d = add i32 0, 0\n", t); break;
            }
            return val_tmp(t);
        }

        case EXPR_UNOP: {
            Type *ot = NULL;
            Val o = cg_expr(cg, e->unop.operand, &ot);
            const char *llt = ot ? llvm_type(ot) : "i32";
            int t = new_tmp(cg);
            switch (e->unop.op) {
                case UNOP_NEG:    emit(cg, "  %%t%d = sub %s 0, %s\n", t, llt, o.buf); break;
                case UNOP_NOT:    emit(cg, "  %%t%d = xor i1 %s, true\n", t, o.buf); break;
                case UNOP_BITNOT: emit(cg, "  %%t%d = xor %s %s, -1\n", t, llt, o.buf); break;
                case UNOP_ADDROF: {
                    /* For address-of we need the alloca pointer of the var.
                       We stored the alloca name directly. */
                    return val_str(o.buf);
                }
            }
            return val_tmp(t);
        }

        case EXPR_CALL: {
            /* Resolve callee: for a direct ident, use @name; for indirect, cg_expr */
            char fn_name_buf[128];
            const char *fn_name;
            Type *callee_ty = NULL;
            const char *ret_llt = "i32";

            if (e->call.callee->kind == EXPR_IDENT) {
                snprintf(fn_name_buf, sizeof(fn_name_buf), "@%s",
                         e->call.callee->ident.name);
                fn_name = fn_name_buf;
                /* look up return type from sema-annotated callee expression */
                callee_ty = e->call.callee->ty;
            } else {
                Val cv = cg_expr(cg, e->call.callee, &callee_ty);
                fn_name = cv.buf;
            }

            if (callee_ty && callee_ty->kind == TY_FN && callee_ty->fn.ret)
                ret_llt = effective_llvm_type(cg, callee_ty->fn.ret);
            else if (callee_ty)
                ret_llt = effective_llvm_type(cg, callee_ty);

            /* evaluate all args before emitting the call instruction */
            size_t nargs = e->call.args.len;
            Val   *arg_vals = nargs ? malloc(sizeof(Val) * nargs) : NULL;
            Type **arg_tys  = nargs ? malloc(sizeof(Type*) * nargs) : NULL;
            for (size_t i = 0; i < nargs; i++) {
                arg_tys[i] = NULL;
                arg_vals[i] = cg_expr(cg, e->call.args.data[i], &arg_tys[i]);
            }

            int t = new_tmp(cg);
            emit(cg, "  %%t%d = call %s %s(", t, ret_llt, fn_name);
            for (size_t i = 0; i < nargs; i++) {
                const char *llt = effective_llvm_type(cg, arg_tys[i]);
                if (i) emit(cg, ", ");
                emit(cg, "%s %s", llt, arg_vals[i].buf);
            }
            emit(cg, ")\n");
            free(arg_vals);
            free(arg_tys);
            return val_tmp(t);
        }

        case EXPR_FIELD: {
            /* Enum variant access: EnumName.Variant — peek before calling cg_expr */
            if (e->field.obj->kind == EXPR_IDENT) {
                EnumInfo *ei = find_enum(cg, e->field.obj->ident.name);
                if (ei) {
                    for (size_t i = 0; i < ei->n_variants; i++) {
                        if (!strcmp(ei->variants[i].name, e->field.field)) {
                            if (out_ty) *out_ty = ei->backing_ty;
                            Val v;
                            snprintf(v.buf, sizeof(v.buf), "%" PRId64, ei->variants[i].value);
                            return v;
                        }
                    }
                    fatal_at(e->span, "enum '%s' has no variant '%s'",
                             ei->name, e->field.field);
                }
            }
            /* Struct field access */
            Type *obj_ty = NULL;
            Val obj = cg_expr(cg, e->field.obj, &obj_ty);
            if (!obj_ty || obj_ty->kind != TY_NAMED)
                fatal_at(e->span, "field access on non-struct value");
            StructInfo *si = find_struct(cg, obj_ty->named.name);
            if (!si)
                fatal_at(e->span, "unknown struct '%s'", obj_ty->named.name);
            int fidx = struct_field_index(si, e->field.field);
            if (fidx < 0)
                fatal_at(e->span, "struct '%s' has no field '%s'",
                         obj_ty->named.name, e->field.field);
            Type *fty = si->fields.data[fidx].ty;
            if (out_ty) *out_ty = fty;
            int fp = new_tmp(cg);
            emit(cg, "  %%t%d = getelementptr %%%s, ptr %s, i32 0, i32 %d\n",
                 fp, obj_ty->named.name, obj.buf, fidx);
            int t = new_tmp(cg);
            emit(cg, "  %%t%d = load %s, ptr %%t%d\n", t, llvm_type(fty), fp);
            return val_tmp(t);
        }

        case EXPR_DEREF: {
            Type *pt = NULL;
            Val ptr = cg_expr(cg, e->deref.operand, &pt);
            int t = new_tmp(cg);
            const char *inner = (pt && pt->ptr.inner) ? llvm_type(pt->ptr.inner) : "i32";
            emit(cg, "  %%t%d = load %s, ptr %s\n", t, inner, ptr.buf);
            return val_tmp(t);
        }

        case EXPR_SMARTDEREF: {
            Type *pt = NULL;
            Val ptr = cg_expr(cg, e->deref.operand, &pt);
            int t = new_tmp(cg);
            const char *inner = (pt && pt->ptr.inner) ? llvm_type(pt->ptr.inner) : "i32";
            emit(cg, "  %%t%d = load %s, ptr %s\n", t, inner, ptr.buf);
            return val_tmp(t);
        }

        case EXPR_INDEX: {
            Type *at = NULL;
            Val arr = cg_expr(cg, e->index.arr, &at);
            Val idx = cg_expr(cg, e->index.idx, NULL);
            int ptr = new_tmp(cg);
            const char *elem_llt = "i8";
            if (at && at->kind == TY_ARRAY && at->array.inner)
                elem_llt = llvm_type(at->array.inner);
            emit(cg, "  %%t%d = getelementptr %s, ptr %s, i64 %s\n",
                 ptr, elem_llt, arr.buf, idx.buf);
            int t = new_tmp(cg);
            emit(cg, "  %%t%d = load %s, ptr %%t%d\n", t, elem_llt, ptr);
            return val_tmp(t);
        }

        case EXPR_IF: {
            Val cond = cg_expr(cg, e->if_expr.cond, NULL);
            int then_l = new_label(cg), else_l = new_label(cg), end_l = new_label(cg);
            emit_br(cg, "  br i1 %s, label %%l%d, label %%l%d\n",
                    cond.buf, then_l, else_l);
            int res = new_tmp(cg);
            emit(cg, "  %%t%d = alloca i64\n", res);

            emit_label(cg, then_l);
            if (e->if_expr.then_) cg_stmt(cg, e->if_expr.then_);
            emit_br(cg, "  br label %%l%d\n", end_l);

            emit_label(cg, else_l);
            if (e->if_expr.else_) cg_stmt(cg, e->if_expr.else_);
            emit_br(cg, "  br label %%l%d\n", end_l);

            emit_label(cg, end_l);
            int load = new_tmp(cg);
            emit(cg, "  %%t%d = load i64, ptr %%t%d\n", load, res);
            return val_tmp(load);
        }

        case EXPR_STRUCT_LIT: {
            StructInfo *si = find_struct(cg, e->struct_lit.ty_name);
            int t = new_tmp(cg);
            emit(cg, "  %%t%d = alloca %%%s\n", t, e->struct_lit.ty_name);
            for (size_t i = 0; i < e->struct_lit.fields.len; i++) {
                FieldInit *fi = &e->struct_lit.fields.data[i];
                Type *fty = NULL;
                Val fv = cg_expr(cg, fi->val, &fty);
                int fidx = si ? struct_field_index(si, fi->name) : (int)i;
                if (fidx < 0) fidx = (int)i;
                if (!fty && si) fty = struct_field_type(si, fi->name);
                const char *store_llt = fty ? llvm_type(fty) : "i64";
                int fp = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr %%%s, ptr %%t%d, i32 0, i32 %d\n",
                     fp, e->struct_lit.ty_name, t, fidx);
                emit(cg, "  store %s %s, ptr %%t%d\n", store_llt, fv.buf, fp);
            }
            /* out_ty = the named struct type */
            if (out_ty) {
                Type *sty = ARENA_NEW(cg->arena, Type);
                sty->kind = TY_NAMED;
                sty->named.name = e->struct_lit.ty_name;
                *out_ty = sty;
            }
            return val_tmp(t);
        }

        case EXPR_ARRAY_LIT: {
            int arr = new_tmp(cg);
            emit(cg, "  %%t%d = alloca [%zu x i64]\n", arr, e->array_lit.len);
            for (size_t i = 0; i < e->array_lit.len; i++) {
                Val ev = cg_expr(cg, e->array_lit.data[i], NULL);
                int ep = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr [%zu x i64], ptr %%t%d, i32 0, i32 %zu\n",
                     ep, e->array_lit.len, arr, i);
                emit(cg, "  store i64 %s, ptr %%t%d\n", ev.buf, ep);
            }
            return val_tmp(arr);
        }

        default:
            fatal_at(e->span, "unhandled expression kind %d in codegen", (int)e->kind);
    }
}

/* ── Statement codegen ────────────────────────────────────────────────────── */

static void cg_stmt(CG *cg, Stmt *s) {
    if (cg->terminated) return;  /* dead code after a terminator */
    switch (s->kind) {
        case STMT_EXPR: {
            cg_expr(cg, s->expr, NULL);
            break;
        }

        case STMT_LET: {
            /* allocate storage */
            int alloca = new_tmp(cg);
            const char *llt = s->let.ty ? effective_llvm_type(cg, s->let.ty) : "i32";
            if (s->let.ty && s->let.ty->kind == TY_FAILABLE)
                llt = llvm_type(s->let.ty->ptr.inner);
            emit(cg, "  %%t%d = alloca %s\n", alloca, llt);

            if (s->let.init) {
                Type *init_ty = NULL;
                Val init = cg_expr(cg, s->let.init, &init_ty);
                int is_struct = init_ty && init_ty->kind == TY_NAMED
                                && !find_enum(cg, init_ty->named.name);
                if (is_struct) {
                    /* struct init returns a ptr — copy via load+store */
                    int loaded = new_tmp(cg);
                    emit(cg, "  %%t%d = load %s, ptr %s\n", loaded, llvm_type(init_ty), init.buf);
                    emit(cg, "  store %s %%t%d, ptr %%t%d\n", llvm_type(init_ty), loaded, alloca);
                } else {
                    const char *store_ty = init_ty ? effective_llvm_type(cg, init_ty) : llt;
                    if (init_ty && init_ty->kind == TY_FAILABLE)
                        store_ty = llvm_type(init_ty->ptr.inner);
                    emit(cg, "  store %s %s, ptr %%t%d\n", store_ty, init.buf, alloca);
                }
            }

            /* register symbol — store the alloca name */
            char llvm_name[32];
            snprintf(llvm_name, sizeof(llvm_name), "%%t%d", alloca);
            define_sym(cg, s->let.name,
                       arena_strdup(cg->arena, llvm_name), 0,
                       s->let.ty);
            break;
        }

        case STMT_ASSIGN: {
            /* look up the alloca for the lhs */
            if (s->assign.target->kind == EXPR_IDENT) {
                Symbol *sym = lookup(cg, s->assign.target->ident.name);
                if (!sym) fatal_at(s->span, "undefined '%s'", s->assign.target->ident.name);
                Type *vty = NULL;
                Val rhs = cg_expr(cg, s->assign.val, &vty);
                const char *llt = sym->ty ? llvm_type(sym->ty) : "i32";
                int is_struct_assign = s->assign.op == ASSIGN_EQ && sym->ty
                                       && sym->ty->kind == TY_NAMED
                                       && !find_enum(cg, sym->ty->named.name);
                if (is_struct_assign) {
                    /* struct copy: rhs is a ptr, load then store */
                    int loaded = new_tmp(cg);
                    emit(cg, "  %%t%d = load %s, ptr %s\n", loaded, llt, rhs.buf);
                    emit(cg, "  store %s %%t%d, ptr %s\n", llt, loaded, sym->llvm_name);
                } else if (s->assign.op == ASSIGN_EQ) {
                    emit(cg, "  store %s %s, ptr %s\n", llt, rhs.buf, sym->llvm_name);
                } else {
                    /* load, operate, store */
                    int cur_t = new_tmp(cg);
                    emit(cg, "  %%t%d = load %s, ptr %s\n", cur_t, llt, sym->llvm_name);
                    int res_t = new_tmp(cg);
                    switch (s->assign.op) {
                        case ASSIGN_ADD: emit(cg, "  %%t%d = add %s %%t%d, %s\n", res_t, llt, cur_t, rhs.buf); break;
                        case ASSIGN_SUB: emit(cg, "  %%t%d = sub %s %%t%d, %s\n", res_t, llt, cur_t, rhs.buf); break;
                        case ASSIGN_MUL: emit(cg, "  %%t%d = mul %s %%t%d, %s\n", res_t, llt, cur_t, rhs.buf); break;
                        case ASSIGN_DIV: emit(cg, "  %%t%d = sdiv %s %%t%d, %s\n",res_t, llt, cur_t, rhs.buf); break;
                        case ASSIGN_MOD: emit(cg, "  %%t%d = srem %s %%t%d, %s\n",res_t, llt, cur_t, rhs.buf); break;
                        default:         emit(cg, "  %%t%d = add %s %%t%d, 0\n",  res_t, llt, cur_t); break;
                    }
                    emit(cg, "  store %s %%t%d, ptr %s\n", llt, res_t, sym->llvm_name);
                }
            } else if (s->assign.target->kind == EXPR_FIELD) {
                /* p.field = val */
                Type *obj_ty = NULL;
                Val obj = cg_expr(cg, s->assign.target->field.obj, &obj_ty);
                if (obj_ty && obj_ty->kind == TY_NAMED) {
                    StructInfo *si = find_struct(cg, obj_ty->named.name);
                    const char *fname = s->assign.target->field.field;
                    int fidx = si ? struct_field_index(si, fname) : -1;
                    Type *fty = si ? struct_field_type(si, fname) : NULL;
                    if (fidx >= 0) {
                        Type *vty = NULL;
                        Val rhs = cg_expr(cg, s->assign.val, &vty);
                        const char *llt = fty ? llvm_type(fty) : (vty ? llvm_type(vty) : "i32");
                        int fp = new_tmp(cg);
                        emit(cg, "  %%t%d = getelementptr %%%s, ptr %s, i32 0, i32 %d\n",
                             fp, obj_ty->named.name, obj.buf, fidx);
                        emit(cg, "  store %s %s, ptr %%t%d\n", llt, rhs.buf, fp);
                    }
                }
            } else if (s->assign.target->kind == EXPR_DEREF) {
                Val ptr = cg_expr(cg, s->assign.target->deref.operand, NULL);
                Type *vty = NULL;
                Val rhs = cg_expr(cg, s->assign.val, &vty);
                const char *llt = vty ? llvm_type(vty) : "i32";
                emit(cg, "  store %s %s, ptr %s\n", llt, rhs.buf, ptr.buf);
            } else {
                /* generic lvalue — emit rhs at least */
                cg_expr(cg, s->assign.val, NULL);
            }
            break;
        }

        case STMT_RET: {
            /* evaluate return value before flushing defers */
            Val rv = val_str("0");
            const char *llt = cg->cur_fn_ret ? cg->cur_fn_ret : "void";
            int has_val = s->ret.val != NULL;
            if (has_val) {
                Type *rt = NULL;
                rv = cg_expr(cg, s->ret.val, &rt);
                if (rt) llt = llvm_type(rt);
            }
            /* flush all scopes' defers in LIFO order before the ret */
            for (Scope *sc = cg->scope; sc; sc = sc->parent)
                emit_defers_for_scope(cg, sc);
            if (has_val)
                emit_br(cg, "  ret %s %s\n", llt, rv.buf);
            else
                emit_br(cg, "  ret void\n");
            break;
        }

        case STMT_IF: {
            int end_l = new_label(cg);
            for (size_t i = 0; i < s->if_.branches.len; i++) {
                IfBranch *br_item = &s->if_.branches.data[i];
                Val cond = cg_expr(cg, br_item->cond, NULL);
                int body_l = new_label(cg);
                int next_l = (i + 1 < s->if_.branches.len || s->if_.else_body.len)
                             ? new_label(cg) : end_l;
                emit_br(cg, "  br i1 %s, label %%l%d, label %%l%d\n",
                        cond.buf, body_l, next_l);
                emit_label(cg, body_l);
                push_scope(cg);
                for (size_t j = 0; j < br_item->body.len; j++)
                    cg_stmt(cg, br_item->body.data[j]);
                pop_scope(cg);
                emit_br(cg, "  br label %%l%d\n", end_l);
                if (next_l != end_l) emit_label(cg, next_l);
            }
            if (s->if_.else_body.len) {
                push_scope(cg);
                for (size_t j = 0; j < s->if_.else_body.len; j++)
                    cg_stmt(cg, s->if_.else_body.data[j]);
                pop_scope(cg);
                emit_br(cg, "  br label %%l%d\n", end_l);
            }
            emit_label(cg, end_l);
            break;
        }

        case STMT_WHILE: {
            int cond_l = new_label(cg);
            int body_l = new_label(cg);
            int end_l  = new_label(cg);
            emit_br(cg, "  br label %%l%d\n", cond_l);
            emit_label(cg, cond_l);
            if (s->while_.cond) {
                Val cond = cg_expr(cg, s->while_.cond, NULL);
                emit_br(cg, "  br i1 %s, label %%l%d, label %%l%d\n",
                        cond.buf, body_l, end_l);
            } else {
                /* loop {} — unconditional */
                emit_br(cg, "  br label %%l%d\n", body_l);
            }
            emit_label(cg, body_l);
            push_loop_scope(cg, end_l, cond_l);
            for (size_t i = 0; i < s->while_.body.len; i++)
                cg_stmt(cg, s->while_.body.data[i]);
            pop_scope(cg);
            emit_br(cg, "  br label %%l%d\n", cond_l);
            emit_label(cg, end_l);
            break;
        }

        case STMT_FOR: {
            ForClause *fc = &s->for_.clause;
            if (fc->kind == FOR_RANGE) {
                Val start = cg_expr(cg, fc->iter, NULL);
                Val end   = cg_expr(cg, fc->range_end, NULL);
                int i_alloca = new_tmp(cg);
                emit(cg, "  %%t%d = alloca i64\n", i_alloca);
                emit(cg, "  store i64 %s, ptr %%t%d\n", start.buf, i_alloca);

                int cond_l = new_label(cg);
                int body_l = new_label(cg);
                int inc_l  = new_label(cg); /* continue target — runs increment */
                int end_l  = new_label(cg);

                emit_br(cg, "  br label %%l%d\n", cond_l);
                emit_label(cg, cond_l);
                int i_val = new_tmp(cg);
                emit(cg, "  %%t%d = load i64, ptr %%t%d\n", i_val, i_alloca);
                int cmp = new_tmp(cg);
                emit(cg, "  %%t%d = icmp %s i64 %%t%d, %s\n",
                     cmp, fc->inclusive ? "sle" : "slt", i_val, end.buf);
                emit_br(cg, "  br i1 %%t%d, label %%l%d, label %%l%d\n",
                        cmp, body_l, end_l);

                emit_label(cg, body_l);
                push_loop_scope(cg, end_l, inc_l);
                /* expose the loop variable */
                if (fc->elem) {
                    Type *i64_ty = ARENA_NEW(cg->arena, Type);
                    i64_ty->kind = TY_I64;
                    char lvar[32];
                    snprintf(lvar, sizeof(lvar), "%%t%d", i_alloca);
                    define_sym(cg, fc->elem, arena_strdup(cg->arena, lvar), 0, i64_ty);
                }
                for (size_t j = 0; j < s->for_.body.len; j++)
                    cg_stmt(cg, s->for_.body.data[j]);
                pop_scope(cg);

                emit_br(cg, "  br label %%l%d\n", inc_l);
                emit_label(cg, inc_l);
                int inc = new_tmp(cg);
                emit(cg, "  %%t%d = load i64, ptr %%t%d\n", inc, i_alloca);
                int inc2 = new_tmp(cg);
                emit(cg, "  %%t%d = add i64 %%t%d, 1\n", inc2, inc);
                emit(cg, "  store i64 %%t%d, ptr %%t%d\n", inc2, i_alloca);
                emit_br(cg, "  br label %%l%d\n", cond_l);
                emit_label(cg, end_l);

            } else if (fc->kind == FOR_EACH || fc->kind == FOR_EACH_IDX) {
                /* for v => slice  /  for i, v => slice */
                Type *iter_ty = NULL;
                Val iter = cg_expr(cg, fc->iter, &iter_ty);

                /* extract data pointer and length from the fat pointer */
                const char *elem_llt = "i8";
                Type *elem_ty = NULL;
                if (iter_ty && (iter_ty->kind == TY_SLICE || iter_ty->kind == TY_STR)) {
                    elem_ty  = (iter_ty->kind == TY_SLICE) ? iter_ty->ptr.inner : NULL;
                    elem_llt = elem_ty ? llvm_type(elem_ty) : "ptr";
                }
                int data_t = new_tmp(cg);
                int len_t  = new_tmp(cg);
                emit(cg, "  %%t%d = extractvalue { ptr, i64 } %s, 0\n", data_t, iter.buf);
                emit(cg, "  %%t%d = extractvalue { ptr, i64 } %s, 1\n", len_t,  iter.buf);

                /* loop index alloca */
                int idx_alloca = new_tmp(cg);
                emit(cg, "  %%t%d = alloca i64\n", idx_alloca);
                emit(cg, "  store i64 0, ptr %%t%d\n", idx_alloca);

                int cond_l = new_label(cg);
                int body_l = new_label(cg);
                int inc_l  = new_label(cg);
                int end_l  = new_label(cg);

                emit_br(cg, "  br label %%l%d\n", cond_l);
                emit_label(cg, cond_l);
                int idx_t = new_tmp(cg);
                emit(cg, "  %%t%d = load i64, ptr %%t%d\n", idx_t, idx_alloca);
                int cmp_t = new_tmp(cg);
                emit(cg, "  %%t%d = icmp slt i64 %%t%d, %%t%d\n", cmp_t, idx_t, len_t);
                emit_br(cg, "  br i1 %%t%d, label %%l%d, label %%l%d\n",
                        cmp_t, body_l, end_l);

                emit_label(cg, body_l);
                push_loop_scope(cg, end_l, inc_l);

                /* load current element */
                int ep_t = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr %s, ptr %%t%d, i64 %%t%d\n",
                     ep_t, elem_llt, data_t, idx_t);
                int ev_alloca = new_tmp(cg);
                emit(cg, "  %%t%d = alloca %s\n", ev_alloca, elem_llt);
                int ev_t = new_tmp(cg);
                emit(cg, "  %%t%d = load %s, ptr %%t%d\n", ev_t, elem_llt, ep_t);
                emit(cg, "  store %s %%t%d, ptr %%t%d\n", elem_llt, ev_t, ev_alloca);
                if (fc->elem) {
                    char ename[32];
                    snprintf(ename, sizeof(ename), "%%t%d", ev_alloca);
                    define_sym(cg, fc->elem, arena_strdup(cg->arena, ename), 0, elem_ty);
                }
                if (fc->kind == FOR_EACH_IDX && fc->idx) {
                    /* expose index variable */
                    Type *i64_ty = ARENA_NEW(cg->arena, Type);
                    i64_ty->kind = TY_I64;
                    char iname[32];
                    snprintf(iname, sizeof(iname), "%%t%d", idx_alloca);
                    define_sym(cg, fc->idx, arena_strdup(cg->arena, iname), 0, i64_ty);
                }

                for (size_t j = 0; j < s->for_.body.len; j++)
                    cg_stmt(cg, s->for_.body.data[j]);
                pop_scope(cg);

                emit_br(cg, "  br label %%l%d\n", inc_l);
                emit_label(cg, inc_l);
                int inc_t = new_tmp(cg);
                emit(cg, "  %%t%d = load i64, ptr %%t%d\n", inc_t, idx_alloca);
                int inc2_t = new_tmp(cg);
                emit(cg, "  %%t%d = add i64 %%t%d, 1\n", inc2_t, inc_t);
                emit(cg, "  store i64 %%t%d, ptr %%t%d\n", inc2_t, idx_alloca);
                emit_br(cg, "  br label %%l%d\n", cond_l);
                emit_label(cg, end_l);
            } else {
                emit(cg, "  ; unsupported for kind\n");
            }
            break;
        }

        case STMT_WHEN: {
            int end_l = new_label(cg);
            Type *val_ty = NULL;
            Val val = cg_expr(cg, s->when.val, &val_ty);
            const char *llt = effective_llvm_type(cg, val_ty);
            for (size_t i = 0; i < s->when.arms.len; i++) {
                WhenArm *arm = &s->when.arms.data[i];
                int body_l = new_label(cg);
                int next_l = (i + 1 < s->when.arms.len) ? new_label(cg) : end_l;

                int cond_t = -1;
                for (size_t pi = 0; pi < arm->pats.len; pi++) {
                    Expr *pat = arm->pats.data[pi];
                    if (pat->kind == EXPR_DISCARD) {
                        int wc = new_tmp(cg);
                        emit(cg, "  %%t%d = add i1 0, 1\n", wc);
                        cond_t = wc;
                        break;
                    }
                    Val pv = cg_expr(cg, pat, NULL);
                    int cmp = new_tmp(cg);
                    emit(cg, "  %%t%d = icmp eq %s %s, %s\n", cmp, llt, val.buf, pv.buf);
                    if (cond_t < 0) {
                        cond_t = cmp;
                    } else {
                        int or_t = new_tmp(cg);
                        emit(cg, "  %%t%d = or i1 %%t%d, %%t%d\n", or_t, cond_t, cmp);
                        cond_t = or_t;
                    }
                }
                if (cond_t < 0) { int wc = new_tmp(cg); emit(cg,"  %%t%d = add i1 0,1\n",wc); cond_t=wc; }

                emit_br(cg, "  br i1 %%t%d, label %%l%d, label %%l%d\n",
                        cond_t, body_l, next_l);
                emit_label(cg, body_l);
                push_scope(cg);
                cg_stmt(cg, arm->body);
                pop_scope(cg);
                emit_br(cg, "  br label %%l%d\n", end_l);
                if (next_l != end_l) emit_label(cg, next_l);
            }
            emit_label(cg, end_l);
            break;
        }

        case STMT_DEFER: {
            /* push onto current scope's LIFO defer stack */
            DeferEntry *de = ARENA_NEW(cg->arena, DeferEntry);
            de->stmts = s->defer;
            de->next  = cg->scope->defers;
            cg->scope->defers = de;
            break;
        }

        case STMT_BLOCK: {
            push_scope(cg);
            for (size_t i = 0; i < s->block.len; i++)
                cg_stmt(cg, s->block.data[i]);
            pop_scope(cg);
            break;
        }

        case STMT_BREAK: {
            /* flush defers from current scope out through (and including) the loop scope */
            Scope *loop_sc = NULL;
            for (Scope *sc = cg->scope; sc; sc = sc->parent) {
                emit_defers_for_scope(cg, sc);
                if (sc->is_loop) { loop_sc = sc; break; }
            }
            if (loop_sc)
                emit_br(cg, "  br label %%l%d\n", loop_sc->break_label);
            break;
        }

        case STMT_CONTINUE: {
            /* flush defers from current scope out through (and including) the loop scope */
            Scope *loop_sc = NULL;
            for (Scope *sc = cg->scope; sc; sc = sc->parent) {
                emit_defers_for_scope(cg, sc);
                if (sc->is_loop) { loop_sc = sc; break; }
            }
            if (loop_sc)
                emit_br(cg, "  br label %%l%d\n", loop_sc->cont_label);
            break;
        }

        default:
            emit(cg, "  ; unhandled stmt kind %d\n", (int)s->kind);
            break;
    }
}

/* ── Item codegen ─────────────────────────────────────────────────────────── */

static void cg_fn(CG *cg, Item *item) {
    const char *name = item->name;
    int is_void = (item->fn.ret == NULL);
    const char *ret_llt = is_void ? "void" : effective_llvm_type(cg, item->fn.ret);

    if (item->fn.is_inline)
        emit(cg, "define internal ");
    else
        emit(cg, "define ");

    emit(cg, "%s @%s(", ret_llt, name);
    for (size_t i = 0; i < item->fn.params.len; i++) {
        Param *par = &item->fn.params.data[i];
        if (i) emit(cg, ", ");
        emit(cg, "%s %%%s", effective_llvm_type(cg, par->ty), par->name);
    }
    if (item->fn.variadic) {
        if (item->fn.params.len) emit(cg, ", ");
        emit(cg, "...");
    }
    emit(cg, ") {\nentry:\n");

    cg->cur_fn_ret = ret_llt;
    push_scope(cg);

    /* spill parameters to allocas so they're addressable */
    for (size_t i = 0; i < item->fn.params.len; i++) {
        Param *par = &item->fn.params.data[i];
        const char *llt = effective_llvm_type(cg, par->ty);
        int alloca = new_tmp(cg);
        emit(cg, "  %%t%d = alloca %s\n", alloca, llt);
        emit(cg, "  store %s %%%s, ptr %%t%d\n", llt, par->name, alloca);
        char llvm_name[32];
        snprintf(llvm_name, sizeof(llvm_name), "%%t%d", alloca);
        define_sym(cg, par->name, arena_strdup(cg->arena, llvm_name), 0, par->ty);
    }

    cg->terminated = 0;
    for (size_t i = 0; i < item->fn.body.len; i++)
        cg_stmt(cg, item->fn.body.data[i]);

    pop_scope(cg);

    /* implicit return only if last block has no terminator */
    if (!cg->terminated) {
        if (is_void) emit(cg, "  ret void\n");
        else         emit(cg, "  ret %s 0\n", ret_llt);
    }

    emit(cg, "}\n\n");
}

static void cg_extern_fn(CG *cg, Item *item) {
    const char *ret_llt = item->extern_fn.ret ? llvm_type(item->extern_fn.ret) : "void";
    emit(cg, "declare %s @%s(", ret_llt, item->name);
    for (size_t i = 0; i < item->extern_fn.params.len; i++) {
        Param *par = &item->extern_fn.params.data[i];
        if (i) emit(cg, ", ");
        emit(cg, "%s", llvm_type(par->ty));
    }
    if (item->extern_fn.variadic) {
        if (item->extern_fn.params.len) emit(cg, ", ");
        emit(cg, "...");
    }
    emit(cg, ")\n");
}

static void cg_global(CG *cg, Item *item) {
    const char *llt = llvm_type(item->global.ty);
    emit(cg, "@%s = %s global %s ",
         item->name,
         item->global.mutable ? "" : "constant",
         llt);
    if (item->global.init) {
        switch (item->global.init->kind) {
            case EXPR_INT:  emit(cg, "%" PRIu64, item->global.init->ival); break;
            case EXPR_FLOAT:emit(cg, "%a", item->global.init->fval); break;
            case EXPR_BOOL: emit(cg, "%d", item->global.init->bval); break;
            default:        emit(cg, "zeroinitializer"); break;
        }
    } else {
        emit(cg, "zeroinitializer");
    }
    emit(cg, "\n");

    /* register in global scope */
    char llvm_name[128];
    snprintf(llvm_name, sizeof(llvm_name), "@%s", item->name);
    define_sym(cg, item->name, arena_strdup(cg->arena, llvm_name), 1, item->global.ty);
}

/* ── Module entry ─────────────────────────────────────────────────────────── */

int codegen(Module *mod, FILE *out) {
    CG cg = {0};
    cg.out   = out;
    cg.arena = mod->arena;

    /* global scope */
    Scope global_scope = {0};
    cg.scope = &global_scope;

    /* first pass: collect externs and globals into scope, emit their IR */
    emit(&cg, "; Perzephxne LLVM IR\n");
    emit(&cg, "target triple = \"x86_64-pc-linux-gnu\"\n\n");

    /* standard declarations always needed */
    emit(&cg, "declare i32 @printf(ptr noundef, ...)\n");
    emit(&cg, "declare i32 @sprintf(ptr, ptr, ...)\n");
    emit(&cg, "declare i32 @atoi(ptr)\n");
    emit(&cg, "declare ptr @malloc(i64)\n");
    emit(&cg, "declare void @free(ptr)\n");
    emit(&cg, "declare void @exit(i32)\n\n");

    /* globals for @args support */
    emit(&cg, "@__przp_argc = internal global i32 0\n");
    emit(&cg, "@__przp_argv = internal global ptr null\n\n");

    /* format string constants */
    emit(&cg, "@.fmt.d = private constant [3 x i8] c\"%%d\\00\"\n\n");

    /* enum variant tables (no IR to emit — enums are integer constants) */
    for (size_t i = 0; i < mod->items.len; i++) {
        Item *item = mod->items.data[i];
        if (item->kind != ITEM_ENUM) continue;
        size_t n = item->enum_.variants.len;
        EnumVariantVal *vals = ARENA_ALLOC(cg.arena, EnumVariantVal, n);
        int64_t next_val = 0;
        for (size_t j = 0; j < n; j++) {
            EnumVariant *v = &item->enum_.variants.data[j];
            if (v->val && v->val->kind == EXPR_INT)
                next_val = (int64_t)v->val->ival;
            vals[j].name  = v->name;
            vals[j].value = next_val++;
        }
        EnumInfo *ei = ARENA_NEW(cg.arena, EnumInfo);
        ei->name       = item->name;
        ei->backing_ty = item->enum_.backing ? item->enum_.backing
                                             : (Type*)NULL; /* resolved below */
        ei->n_variants = n;
        ei->variants   = vals;
        ei->next       = cg.enums;
        cg.enums       = ei;
        /* resolve backing type — default i32 */
        if (!ei->backing_ty) {
            ei->backing_ty = ARENA_NEW(cg.arena, Type);
            ei->backing_ty->kind = TY_I32;
        }
    }

    /* struct type declarations and layout table */
    for (size_t i = 0; i < mod->items.len; i++) {
        Item *item = mod->items.data[i];
        if (item->kind != ITEM_STRUCT) continue;
        /* register layout */
        StructInfo *si = ARENA_NEW(cg.arena, StructInfo);
        si->name   = item->name;
        si->fields = item->struct_.fields;
        si->next   = cg.structs;
        cg.structs = si;
        /* emit LLVM named type */
        emit(&cg, "%%%s = type { ", item->name);
        for (size_t j = 0; j < item->struct_.fields.len; j++) {
            if (j) emit(&cg, ", ");
            emit(&cg, "%s", llvm_type(item->struct_.fields.data[j].ty));
        }
        emit(&cg, " }\n");
    }
    emit(&cg, "\n");

    /* globals and externs */
    for (size_t i = 0; i < mod->items.len; i++) {
        Item *item = mod->items.data[i];
        if (item->kind == ITEM_GLOBAL)     cg_global(&cg, item);
        if (item->kind == ITEM_EXTERN_FN)  cg_extern_fn(&cg, item);
    }
    emit(&cg, "\n");

    /* functions — rename user's `main` to `__przp_main` */
    int has_main = 0;
    for (size_t i = 0; i < mod->items.len; i++) {
        Item *item = mod->items.data[i];
        if (item->kind == ITEM_FN) {
            if (!strcmp(item->name, "main")) { has_main = 1; item->name = "__przp_main"; }
            cg_fn(&cg, item);
            if (!strcmp(item->name, "__przp_main")) item->name = "main"; /* restore */
        }
    }

    /* emit a real C main that stores argc/argv then calls __przp_main */
    if (has_main) {
        emit(&cg,
            "define i32 @main(i32 %%argc, ptr %%argv) {\n"
            "entry:\n"
            "  store i32 %%argc, ptr @__przp_argc\n"
            "  store ptr %%argv, ptr @__przp_argv\n"
            "  %%r = call i32 @__przp_main()\n"
            "  ret i32 %%r\n"
            "}\n\n");
    }

    /* string constants */
    emit(&cg, "\n");
    emit_str_constants(&cg);

    return !cg.had_error;
}
