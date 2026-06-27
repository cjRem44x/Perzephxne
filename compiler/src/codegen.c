#include "codegen.h"
#include "error.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>

/* ── Val forward declaration (defined fully below) ───────────────────────── */
typedef struct { char buf[64]; } Val;

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

typedef struct { const char *name; Type *ty; } UnionVariantCG; /* ty=NULL → unit variant */

typedef struct UnionInfoCG {
    struct UnionInfoCG *next;
    const char         *name;
    size_t              n_variants;
    UnionVariantCG     *variants;
    int                 payload_size; /* bytes — 0 if all unit variants */
} UnionInfoCG;

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

typedef struct RCDrop {
    struct RCDrop *next;
    const char    *alloca; /* llvm name ("%tN") of the alloca holding the ^T ptr */
} RCDrop;

typedef struct Scope {
    struct Scope  *parent;
    Symbol        *syms;
    DeferEntry    *defers;      /* LIFO — head = most recently deferred */
    RCDrop        *rc_drops;    /* ^T locals to decrement at scope exit */
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
    UnionInfoCG *unions;     /* name → tagged union variant table */
    int          str_id;
    int          tmp_id;      /* next %t<n> temporary */
    int          label_id;    /* next label suffix     */
    Scope       *scope;
    const char  *cur_fn_ret;   /* LLVM type string of current function return */
    const char  *skip_rc_drop; /* alloca to skip in RC drops (being moved out by ret) */
    int          terminated;   /* 1 = current block already has a terminator */
    int          had_error;
    int          release;      /* 1 = --release build (@debug=false, @release=true) */
    int          cur_label;    /* -1 = entry block, else the current l%d label id */
    /* dedup tracker for extern fn declarations/wrappers */
    const char  *declared_fns[512];
    size_t       n_declared_fns;
    /* failable destructure cache: val, err: !T = expr() */
    const Expr  *last_fail_init;   /* init expr pointer from the val-side let */
    Val          last_fail_val;    /* aggregate value returned by cg_expr for it */
    Type        *last_fail_ty;     /* TY_FAILABLE type (needed for extractvalue) */
    /* current function return type (for @ok/@err builtins in STMT_RET) */
    Type        *cur_fn_ret_ty;
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
    cg->cur_label  = id;
}

/* write the current label name for phi predecessors */
static void emit_cur_label(CG *cg) {
    if (cg->cur_label < 0) emit(cg, "%%entry");
    else emit(cg, "%%l%d", cg->cur_label);
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

/* Decrement RC of a ^T local (alloca holds ptr to RC block).
   Emits: load ptr, load rc, dec, store, cmp zero, conditional free. */
static void emit_rc_dec(CG *cg, const char *alloca_name) {
    if (cg->terminated) return;
    int sp   = new_tmp(cg);
    int rc   = new_tmp(cg);
    int dec  = new_tmp(cg);
    int zero = new_tmp(cg);
    int free_l = new_label(cg);
    int done_l = new_label(cg);
    emit(cg, "  %%t%d = load ptr, ptr %s\n", sp, alloca_name);
    emit(cg, "  %%t%d = load i64, ptr %%t%d\n", rc, sp);
    emit(cg, "  %%t%d = sub i64 %%t%d, 1\n", dec, rc);
    emit(cg, "  store i64 %%t%d, ptr %%t%d\n", dec, sp);
    emit(cg, "  %%t%d = icmp eq i64 %%t%d, 0\n", zero, dec);
    emit_br(cg, "  br i1 %%t%d, label %%l%d, label %%l%d\n", zero, free_l, done_l);
    emit_label(cg, free_l);
    emit(cg, "  call void @free(ptr %%t%d)\n", sp);
    emit_br(cg, "  br label %%l%d\n", done_l);
    emit_label(cg, done_l);
}

static void emit_rc_inc(CG *cg, const char *smart_ptr_buf); /* defined after Val */

/* Emit RC decrements for all ^T locals in a scope (defers ran first).
   Skips cg->skip_rc_drop if set (for move-on-return). */
static void emit_rc_drops_for_scope(CG *cg, Scope *sc) {
    for (RCDrop *d = sc->rc_drops; d; d = d->next) {
        if (cg->skip_rc_drop && !strcmp(d->alloca, cg->skip_rc_drop)) continue;
        emit_rc_dec(cg, d->alloca);
    }
}

/* Register a ^T alloca for auto-drop at scope exit. */
static void register_rc_drop(CG *cg, const char *alloca_name) {
    RCDrop *d = ARENA_NEW(cg->arena, RCDrop);
    d->alloca = alloca_name;
    d->next   = cg->scope->rc_drops;
    cg->scope->rc_drops = d;
}

static void pop_scope(CG *cg) {
    emit_defers_for_scope(cg, cg->scope);
    emit_rc_drops_for_scope(cg, cg->scope);
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
        case TY_FAILABLE: {
            static char fbufs[4][256];
            static int  fbi = 0;
            fbi = (fbi + 1) % 4;
            snprintf(fbufs[fbi], sizeof(fbufs[fbi]), "{ %s, i32 }", llvm_type(ty->ptr.inner));
            return fbufs[fbi];
        }
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

static UnionInfoCG *find_union(CG *cg, const char *name) {
    for (UnionInfoCG *ui = cg->unions; ui; ui = ui->next)
        if (!strcmp(ui->name, name)) return ui;
    return NULL;
}

/* Approximate byte size of a type for union payload sizing */
static int cg_type_byte_size(CG *cg, Type *ty) {
    if (!ty) return 0;
    switch (ty->kind) {
        case TY_BOOL: case TY_I8: case TY_U8: case TY_CHAR: return 1;
        case TY_I16: case TY_U16: return 2;
        case TY_I32: case TY_U32: case TY_F32: return 4;
        case TY_I64: case TY_U64: case TY_F64: case TY_USIZE:
        case TY_PTR: case TY_SMART_PTR: return 8;
        case TY_STR: case TY_SLICE: case TY_ANY: return 16;
        case TY_NAMED: {
            StructInfo *si = find_struct(cg, ty->named.name);
            if (si) {
                int total = 0;
                for (size_t i = 0; i < si->fields.len; i++)
                    total += cg_type_byte_size(cg, si->fields.data[i].ty);
                return total ? total : 8;
            }
            return 8;
        }
        default: return 8;
    }
}

/* For named types: enums use their backing integer type; structs/unions use %Name. */
static const char *effective_llvm_type(CG *cg, Type *ty) {
    if (!ty) return "i32";
    if (ty->kind == TY_NAMED) {
        EnumInfo *ei = find_enum(cg, ty->named.name);
        if (ei) return llvm_type(ei->backing_ty);
        /* structs and tagged unions: %Name */
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

/* Val is defined at top of file (forward decl) for use in CG struct */

static Val val_tmp(int id)  { Val v; snprintf(v.buf, sizeof(v.buf), "%%t%d", id); return v; }
static Val val_str(const char *s) { Val v; snprintf(v.buf, sizeof(v.buf), "%s", s); return v; }

/* C ABI: small int/float args to variadic functions must be widened */
static Val promote_vararg(CG *cg, Val v, Type *ty, const char **llt_out) {
    if (ty && ty->kind == TY_F32) {
        int t = new_tmp(cg);
        emit(cg, "  %%t%d = fpext float %s to double\n", t, v.buf);
        if (llt_out) *llt_out = "double";
        return val_tmp(t);
    }
    /* C integer promotion: i8/i16/u8/u16/bool/char → i32 */
    if (ty && (ty->kind == TY_I8 || ty->kind == TY_I16
            || ty->kind == TY_U8 || ty->kind == TY_U16
            || ty->kind == TY_BOOL || ty->kind == TY_CHAR)) {
        int t = new_tmp(cg);
        int is_signed = (ty->kind == TY_I8 || ty->kind == TY_I16);
        if (is_signed)
            emit(cg, "  %%t%d = sext %s %s to i32\n", t, llvm_type(ty), v.buf);
        else
            emit(cg, "  %%t%d = zext %s %s to i32\n", t, llvm_type(ty), v.buf);
        if (llt_out) *llt_out = "i32";
        return val_tmp(t);
    }
    if (llt_out) *llt_out = ty ? llvm_type(ty) : "i32";
    return v;
}

/* Increment RC given the smart ptr value buf (e.g. "%t5"). */
static void emit_rc_inc(CG *cg, const char *smart_ptr_buf) {
    int rc  = new_tmp(cg);
    int rc1 = new_tmp(cg);
    emit(cg, "  %%t%d = load i64, ptr %s\n", rc, smart_ptr_buf);
    emit(cg, "  %%t%d = add i64 %%t%d, 1\n", rc1, rc);
    emit(cg, "  store i64 %%t%d, ptr %s\n", rc1, smart_ptr_buf);
}

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
    /* default: use sema-annotated type; individual cases may override */
    if (out_ty) *out_ty = e->ty;

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
            /* Produce a str fat-pointer { ptr, i64 } */
            int id  = intern_str(cg, e->sval);
            size_t slen = strlen(e->sval);
            int raw = new_tmp(cg);
            emit(cg, "  %%t%d = getelementptr inbounds [%zu x i8],"
                     " ptr @.str.%d, i32 0, i32 0\n", raw, slen + 1, id);
            int f1 = new_tmp(cg);
            emit(cg, "  %%t%d = insertvalue { ptr, i64 } undef, ptr %%t%d, 0\n", f1, raw);
            int f2 = new_tmp(cg);
            emit(cg, "  %%t%d = insertvalue { ptr, i64 } %%t%d, i64 %zu, 1\n", f2, f1, slen);
            if (out_ty) *out_ty = e->ty;
            return val_tmp(f2);
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

                    /* pre-emit fpext for float args before starting the call */
                    Val          *iprint_final = malloc(sizeof(Val)         * na);
                    const char  **iprint_llts  = malloc(sizeof(const char*) * na);
                    for (size_t i = 1; i < na; i++) {
                        if (itys[i] && itys[i]->kind == TY_STR) {
                            iprint_final[i] = printable[i]; /* already ptr-extracted */
                            iprint_llts[i]  = "ptr";
                        } else {
                            const char *llt;
                            iprint_final[i] = promote_vararg(cg, printable[i], itys[i], &llt);
                            iprint_llts[i]  = llt;
                        }
                    }

                    int t = new_tmp(cg);
                    int is_epf = !strcmp(name, "epf");
                    if (is_epf) {
                        int stde = new_tmp(cg);
                        emit(cg, "  %%t%d = load ptr, ptr @stderr\n", stde);
                        emit(cg, "  %%t%d = call i32 (ptr, ptr, ...) @fprintf(ptr %%t%d, ptr %%t%d",
                             t, stde, ft);
                    } else {
                        emit(cg, "  %%t%d = call i32 (ptr, ...) @printf(ptr %%t%d", t, ft);
                    }
                    for (size_t i = 1; i < na; i++) {
                        emit(cg, ", %s %s", iprint_llts[i], iprint_final[i].buf);
                    }
                    emit(cg, ")\n");
                    free(iprint_final);
                    free(iprint_llts);

                    free(ivals); free(itys); free(printable);
                    return val_tmp(t);
                }

                /* Non-interpolated path: pass args directly to printf/fprintf */
                Val   *pf_vals  = malloc(sizeof(Val)    * na);
                Val   *pf_final = malloc(sizeof(Val)    * na);
                const char **pf_llts = malloc(sizeof(const char*) * na);
                Type **pf_tys   = malloc(sizeof(Type*) * na);
                for (size_t i = 0; i < na; i++) {
                    pf_tys[i]  = NULL;
                    pf_vals[i] = cg_expr(cg, e->builtin.args.data[i], &pf_tys[i]);
                }
                /* format string (args[0]) must be a raw ptr for printf */
                Val fmt_ptr = pf_vals[0];
                if (pf_tys[0] && pf_tys[0]->kind == TY_STR) {
                    int sp0 = new_tmp(cg);
                    emit(cg, "  %%t%d = extractvalue { ptr, i64 } %s, 0\n", sp0, pf_vals[0].buf);
                    fmt_ptr = val_tmp(sp0);
                }
                /* pre-emit coercions (extractvalue / fpext) before the call */
                for (size_t i = 1; i < na; i++) {
                    if (pf_tys[i] && pf_tys[i]->kind == TY_STR) {
                        int sp = new_tmp(cg);
                        emit(cg, "  %%t%d = extractvalue { ptr, i64 } %s, 0\n", sp, pf_vals[i].buf);
                        pf_final[i] = val_tmp(sp);
                        pf_llts[i]  = "ptr";
                    } else {
                        const char *llt;
                        pf_final[i] = promote_vararg(cg, pf_vals[i], pf_tys[i], &llt);
                        pf_llts[i]  = llt;
                    }
                }
                int t = new_tmp(cg);
                int is_epf2 = !strcmp(name, "epf");
                if (is_epf2) {
                    int stde2 = new_tmp(cg);
                    emit(cg, "  %%t%d = load ptr, ptr @stderr\n", stde2);
                    emit(cg, "  %%t%d = call i32 (ptr, ptr, ...) @fprintf(ptr %%t%d, ptr %s",
                         t, stde2, fmt_ptr.buf);
                } else {
                    emit(cg, "  %%t%d = call i32 (ptr, ...) @printf(ptr %s",
                         t, fmt_ptr.buf);
                }
                for (size_t i = 1; i < na; i++) {
                    emit(cg, ", %s %s", pf_llts[i], pf_final[i].buf);
                }
                emit(cg, ")\n");
                free(pf_vals);
                free(pf_final);
                free(pf_llts);
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

            /* @new(val: T) → allocate RC block { i64 rc, T data }, RC=1, return ^T ptr */
            if (!strcmp(name, "new") && e->builtin.args.len >= 1) {
                Type *vty = NULL;
                Val val = cg_expr(cg, e->builtin.args.data[0], &vty);
                const char *inner_llt = vty ? llvm_type(vty) : "i32";
                /* malloc(8 + sizeof(T)) using GEP-from-null sizeof trick */
                int sz  = new_tmp(cg);
                int blk = new_tmp(cg);
                int dp  = new_tmp(cg);
                emit(cg, "  %%t%d = add i64 8, ptrtoint (ptr getelementptr (%s, ptr null, i32 1) to i64)\n",
                     sz, inner_llt);
                emit(cg, "  %%t%d = call ptr @malloc(i64 %%t%d)\n", blk, sz);
                emit(cg, "  store i64 1, ptr %%t%d\n", blk);  /* RC = 1 */
                emit(cg, "  %%t%d = getelementptr i8, ptr %%t%d, i64 8\n", dp, blk);
                /* for struct inner types: val is a ptr to the struct — copy it */
                if (vty && vty->kind == TY_NAMED && find_struct(cg, vty->named.name)) {
                    int loaded = new_tmp(cg);
                    emit(cg, "  %%t%d = load %s, ptr %s\n", loaded, inner_llt, val.buf);
                    emit(cg, "  store %s %%t%d, ptr %%t%d\n", inner_llt, loaded, dp);
                } else {
                    emit(cg, "  store %s %s, ptr %%t%d\n", inner_llt, val.buf, dp);
                }
                if (out_ty) *out_ty = e->ty;
                return val_tmp(blk);
            }

            /* @clone(ptr: ^T) → increment RC, return same ptr */
            if (!strcmp(name, "clone") && e->builtin.args.len >= 1) {
                Type *pty = NULL;
                Val ptr = cg_expr(cg, e->builtin.args.data[0], &pty);
                emit_rc_inc(cg, ptr.buf);
                if (out_ty) *out_ty = pty;
                return ptr;
            }

            /* @fmt — format to heap-allocated str */
            if (!strcmp(name, "fmt")) {
                if (e->builtin.args.len == 0)
                    fatal_at(e->span, "@fmt requires a format string");
                size_t na2 = e->builtin.args.len;
                Expr *fmt_arg2 = e->builtin.args.data[0];
                /* build the same format string as @pf */
                Val   *fv  = malloc(sizeof(Val)   * na2);
                Type **fty = malloc(sizeof(Type*) * na2);
                for (size_t i = 1; i < na2; i++) {
                    fty[i] = NULL;
                    fv[i]  = cg_expr(cg, e->builtin.args.data[i], &fty[i]);
                    if (fty[i] && fty[i]->kind == TY_STR) {
                        int sv = new_tmp(cg);
                        emit(cg, "  %%t%d = extractvalue { ptr, i64 } %s, 0\n", sv, fv[i].buf);
                        fv[i] = val_tmp(sv);
                    }
                }
                char pf2[4096]; size_t pf2n = 0; size_t ai2 = 1;
                if (fmt_arg2->kind == EXPR_STR && strchr(fmt_arg2->sval, '\x01')) {
                    for (const char *fs = fmt_arg2->sval; *fs && pf2n < sizeof(pf2)-32; fs++) {
                        if ((unsigned char)*fs == '\x01') {
                            fs++;
                            char spec_buf2[64]; size_t sl2 = 0;
                            while (*fs && (unsigned char)*fs != '\x02' && sl2 < 63)
                                spec_buf2[sl2++] = *fs++;
                            spec_buf2[sl2] = '\0';
                            pf2[pf2n++] = '%';
                            if (sl2 > 0) { for (size_t j=0;j<sl2;j++) pf2[pf2n++]=spec_buf2[j]; }
                            else { const char *as=pf_specifier(fty[ai2]); for(const char*sp=as+1;*sp;sp++) pf2[pf2n++]=*sp; }
                            ai2++;
                        } else { pf2[pf2n++] = *fs; }
                    }
                } else {
                    const char *s2 = fmt_arg2->sval ? fmt_arg2->sval : "";
                    while (*s2 && pf2n < sizeof(pf2)-2) pf2[pf2n++] = *s2++;
                }
                pf2[pf2n] = '\0';
                int fmtid2 = intern_str(cg, arena_strndup(cg->arena, pf2, pf2n));
                int ft2 = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr inbounds [%zu x i8],"
                     " ptr @.str.%d, i32 0, i32 0\n", ft2, pf2n+1, fmtid2);
                int buf2 = new_tmp(cg);
                emit(cg, "  %%t%d = call ptr @malloc(i64 4096)\n", buf2);
                int sp2 = new_tmp(cg);
                /* pre-emit fpext coercions before the sprintf call */
                Val         *fmt_final = malloc(sizeof(Val)        * na2);
                const char **fmt_llts  = malloc(sizeof(const char*)* na2);
                for (size_t i = 1; i < na2; i++) {
                    const char *llt2;
                    fmt_final[i] = promote_vararg(cg, fv[i], fty[i], &llt2);
                    fmt_llts[i]  = llt2;
                }
                emit(cg, "  %%t%d = call i32 (ptr, ptr, ...) @sprintf(ptr %%t%d, ptr %%t%d", sp2, buf2, ft2);
                for (size_t i = 1; i < na2; i++) {
                    emit(cg, ", %s %s", fmt_llts[i], fmt_final[i].buf);
                }
                emit(cg, ")\n");
                free(fmt_final);
                free(fmt_llts);
                int slen = new_tmp(cg);
                emit(cg, "  %%t%d = call i64 @strlen(ptr %%t%d)\n", slen, buf2);
                int sa = new_tmp(cg);
                emit(cg, "  %%t%d = alloca { ptr, i64 }\n", sa);
                int p0 = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr { ptr, i64 }, ptr %%t%d, i32 0, i32 0\n", p0, sa);
                emit(cg, "  store ptr %%t%d, ptr %%t%d\n", buf2, p0);
                int p1 = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr { ptr, i64 }, ptr %%t%d, i32 0, i32 1\n", p1, sa);
                emit(cg, "  store i64 %%t%d, ptr %%t%d\n", slen, p1);
                int res2 = new_tmp(cg);
                emit(cg, "  %%t%d = load { ptr, i64 }, ptr %%t%d\n", res2, sa);
                free(fv); free(fty);
                if (out_ty) { Type *st = ARENA_NEW(cg->arena, Type); st->kind = TY_STR; *out_ty = st; }
                return val_tmp(res2);
            }

            /* @cin — print optional prompt, read line from stdin, return str */
            if (!strcmp(name, "cin")) {
                if (e->builtin.args.len > 0) {
                    /* print prompt — extract raw ptr from str fat-pointer if needed */
                    Type *pty = NULL;
                    Val pv = cg_expr(cg, e->builtin.args.data[0], &pty);
                    Val prompt_ptr = pv;
                    if (pty && pty->kind == TY_STR) {
                        int sp0 = new_tmp(cg);
                        emit(cg, "  %%t%d = extractvalue { ptr, i64 } %s, 0\n", sp0, pv.buf);
                        prompt_ptr = val_tmp(sp0);
                    }
                    int pt = new_tmp(cg);
                    emit(cg, "  %%t%d = call i32 (ptr, ...) @printf(ptr %s)\n", pt, prompt_ptr.buf);
                }
                int cbuf = new_tmp(cg);
                emit(cg, "  %%t%d = call ptr @malloc(i64 4096)\n", cbuf);
                int sin_ptr = new_tmp(cg);
                emit(cg, "  %%t%d = load ptr, ptr @stdin\n", sin_ptr);
                int fg = new_tmp(cg);
                emit(cg, "  %%t%d = call ptr @fgets(ptr %%t%d, i32 4096, ptr %%t%d)\n",
                     fg, cbuf, sin_ptr);
                /* strip trailing newline */
                int clen = new_tmp(cg);
                emit(cg, "  %%t%d = call i64 @strlen(ptr %%t%d)\n", clen, cbuf);
                int clast = new_tmp(cg);
                emit(cg, "  %%t%d = sub i64 %%t%d, 1\n", clast, clen);
                int clp = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr i8, ptr %%t%d, i64 %%t%d\n", clp, cbuf, clast);
                int clc = new_tmp(cg);
                emit(cg, "  %%t%d = load i8, ptr %%t%d\n", clc, clp);
                int clnl = new_tmp(cg);
                emit(cg, "  %%t%d = icmp eq i8 %%t%d, 10\n", clnl, clc);
                int cl_strip = new_label(cg), cl_done = new_label(cg);
                /* capture predecessor label before the branch for the phi */
                char pred_lbl[32];
                if (cg->cur_label < 0) snprintf(pred_lbl, sizeof(pred_lbl), "%%entry");
                else snprintf(pred_lbl, sizeof(pred_lbl), "%%l%d", cg->cur_label);
                emit_br(cg, "  br i1 %%t%d, label %%l%d, label %%l%d\n", clnl, cl_strip, cl_done);
                emit_label(cg, cl_strip);
                emit(cg, "  store i8 0, ptr %%t%d\n", clp);
                int clen2 = new_tmp(cg);
                emit(cg, "  %%t%d = sub i64 %%t%d, 1\n", clen2, clen);
                emit_br(cg, "  br label %%l%d\n", cl_done);
                emit_label(cg, cl_done);
                /* phi to pick length */
                int clen_f = new_tmp(cg);
                emit(cg, "  %%t%d = phi i64 [ %%t%d, %%l%d ], [ %%t%d, %s ]\n",
                     clen_f, clen2, cl_strip, clen, pred_lbl);
                int csa = new_tmp(cg);
                emit(cg, "  %%t%d = alloca { ptr, i64 }\n", csa);
                int cp0 = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr { ptr, i64 }, ptr %%t%d, i32 0, i32 0\n", cp0, csa);
                emit(cg, "  store ptr %%t%d, ptr %%t%d\n", cbuf, cp0);
                int cp1 = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr { ptr, i64 }, ptr %%t%d, i32 0, i32 1\n", cp1, csa);
                emit(cg, "  store i64 %%t%d, ptr %%t%d\n", clen_f, cp1);
                int cres = new_tmp(cg);
                emit(cg, "  %%t%d = load { ptr, i64 }, ptr %%t%d\n", cres, csa);
                if (out_ty) { Type *st = ARENA_NEW(cg->arena, Type); st->kind = TY_STR; *out_ty = st; }
                return val_tmp(cres);
            }

            /* @unreachable / @todo */
            if (!strcmp(name, "unreachable") || !strcmp(name, "todo")) {
                emit(cg, "  call void @exit(i32 1)\n");
                emit_br(cg, "  unreachable\n");
                return val_str("0");
            }

            /* @alo */
            if (!strcmp(name, "alo")) {
                /* @alo(T) or @alo(T, N) — malloc with proper sizeof via GEP trick */
                int t = new_tmp(cg);
                /* If no type arg available at LLVM level, default to 8 bytes */
                if (e->builtin.args.len >= 1 && e->builtin.args.data[0]->ty) {
                    const char *inner_llt = llvm_type(e->builtin.args.data[0]->ty);
                    int count = 1;
                    if (e->builtin.args.len >= 2 && e->builtin.args.data[1]->kind == EXPR_INT)
                        count = (int)e->builtin.args.data[1]->ival;
                    int sz = new_tmp(cg);
                    emit(cg, "  %%t%d = mul i64 %d, ptrtoint (ptr getelementptr (%s, ptr null, i32 1) to i64)\n",
                         sz, count, inner_llt);
                    emit(cg, "  %%t%d = call ptr @malloc(i64 %%t%d)\n", t, sz);
                } else {
                    emit(cg, "  %%t%d = call ptr @malloc(i64 8)\n", t);
                }
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

            /* @realo */
            if (!strcmp(name, "realo")) {
                Val ptr = cg_expr(cg, e->builtin.args.data[0], NULL);
                Type *ety = NULL;
                if (e->builtin.args.len >= 2)
                    cg_expr(cg, e->builtin.args.data[1], &ety);
                int nsz = new_tmp(cg);
                if (ety) {
                    const char *inner = llvm_type(ety);
                    emit(cg, "  %%t%d = ptrtoint (ptr getelementptr (%s, ptr null, i32 1) to i64)\n",
                         nsz, inner);
                } else {
                    emit(cg, "  %%t%d = add i64 0, 8\n", nsz);
                }
                int t = new_tmp(cg);
                emit(cg, "  %%t%d = call ptr @realloc(ptr %s, i64 %%t%d)\n", t, ptr.buf, nsz);
                return val_tmp(t);
            }

            /* @memcpy / @memmove / @memset */
            if (!strcmp(name, "memcpy")) {
                Val dst = cg_expr(cg, e->builtin.args.data[0], NULL);
                Val src = cg_expr(cg, e->builtin.args.data[1], NULL);
                Val n   = cg_expr(cg, e->builtin.args.data[2], NULL);
                emit(cg, "  call void @llvm.memcpy.p0.p0.i64(ptr %s, ptr %s, i64 %s, i1 false)\n",
                     dst.buf, src.buf, n.buf);
                return val_str("0");
            }
            if (!strcmp(name, "memmove")) {
                Val dst = cg_expr(cg, e->builtin.args.data[0], NULL);
                Val src = cg_expr(cg, e->builtin.args.data[1], NULL);
                Val n   = cg_expr(cg, e->builtin.args.data[2], NULL);
                emit(cg, "  call void @llvm.memmove.p0.p0.i64(ptr %s, ptr %s, i64 %s, i1 false)\n",
                     dst.buf, src.buf, n.buf);
                return val_str("0");
            }
            if (!strcmp(name, "memset")) {
                Val dst = cg_expr(cg, e->builtin.args.data[0], NULL);
                Val val = cg_expr(cg, e->builtin.args.data[1], NULL);
                Val n   = cg_expr(cg, e->builtin.args.data[2], NULL);
                emit(cg, "  call void @llvm.memset.p0.i64(ptr %s, i8 %s, i64 %s, i1 false)\n",
                     dst.buf, val.buf, n.buf);
                return val_str("0");
            }

            /* @zeroed — return a zero-initialized value */
            if (!strcmp(name, "zeroed")) {
                int t = new_tmp(cg);
                const char *llt = "i64";
                Type *zt = (e->builtin.args.len > 0) ? e->builtin.args.data[0]->ty : e->ty;
                if (zt) llt = llvm_type(zt);
                emit(cg, "  %%t%d = alloca %s\n", t, llt);
                emit(cg, "  call void @llvm.memset.p0.i64(ptr %%t%d, i8 0,"
                         " i64 ptrtoint (ptr getelementptr (%s, ptr null, i32 1) to i64),"
                         " i1 false)\n", t, llt);
                int v = new_tmp(cg);
                emit(cg, "  %%t%d = load %s, ptr %%t%d\n", v, llt, t);
                return val_tmp(v);
            }

            /* @sqrt */
            if (!strcmp(name, "sqrt")) {
                Type *ta = NULL;
                Val a = cg_expr(cg, e->builtin.args.data[0], &ta);
                int t = new_tmp(cg);
                if (ta && ta->kind == TY_F32) {
                    int p = new_tmp(cg);
                    emit(cg, "  %%t%d = fpext float %s to double\n", p, a.buf);
                    emit(cg, "  %%t%d = call double @llvm.sqrt.f64(double %%t%d)\n", t, p);
                } else {
                    emit(cg, "  %%t%d = call double @llvm.sqrt.f64(double %s)\n", t, a.buf);
                }
                if (out_ty) { Type *ft = ARENA_NEW(cg->arena, Type); ft->kind = TY_F64; *out_ty = ft; }
                return val_tmp(t);
            }

            /* @clz / @ctz / @popcount / @bswap */
            if (!strcmp(name, "clz") || !strcmp(name, "ctz") || !strcmp(name, "popcount")) {
                Type *ta = NULL;
                Val a = cg_expr(cg, e->builtin.args.data[0], &ta);
                const char *llt = ta ? llvm_type(ta) : "i32";
                int t = new_tmp(cg);
                const char *intr = !strcmp(name,"clz") ? "ctlz"
                                 : !strcmp(name,"ctz") ? "cttz" : "ctpop";
                if (!strcmp(name,"clz") || !strcmp(name,"ctz"))
                    emit(cg, "  %%t%d = call %s @llvm.%s.%s(%s %s, i1 false)\n",
                         t, llt, intr, llt, llt, a.buf);
                else
                    emit(cg, "  %%t%d = call %s @llvm.%s.%s(%s %s)\n",
                         t, llt, intr, llt, llt, a.buf);
                if (out_ty) { Type *rt = ARENA_NEW(cg->arena, Type); rt->kind = TY_U32; *out_ty = rt; }
                return val_tmp(t);
            }
            if (!strcmp(name, "bswap")) {
                Type *ta = NULL;
                Val a = cg_expr(cg, e->builtin.args.data[0], &ta);
                const char *llt = ta ? llvm_type(ta) : "i32";
                int t = new_tmp(cg);
                emit(cg, "  %%t%d = call %s @llvm.bswap.%s(%s %s)\n", t, llt, llt, llt, a.buf);
                if (out_ty) *out_ty = ta;
                return val_tmp(t);
            }

            /* @checked_add / @checked_sub / @checked_mul */
            if (!strcmp(name, "checked_add") || !strcmp(name, "checked_sub") || !strcmp(name, "checked_mul")) {
                Type *ta = NULL;
                Val a = cg_expr(cg, e->builtin.args.data[0], &ta);
                Val b = cg_expr(cg, e->builtin.args.data[1], NULL);
                const char *llt = ta ? llvm_type(ta) : "i32";
                const char *op = !strcmp(name,"checked_add") ? "sadd"
                               : !strcmp(name,"checked_sub") ? "ssub" : "smul";
                int res = new_tmp(cg);
                emit(cg, "  %%t%d = call { %s, i1 } @llvm.%s.with.overflow.%s(%s %s, %s %s)\n",
                     res, llt, op, llt, llt, a.buf, llt, b.buf);
                int val = new_tmp(cg);
                emit(cg, "  %%t%d = extractvalue { %s, i1 } %%t%d, 0\n", val, llt, res);
                int ovf = new_tmp(cg);
                emit(cg, "  %%t%d = extractvalue { %s, i1 } %%t%d, 1\n", ovf, llt, res);
                /* return value; error code stored in the overflow check result (caller checks) */
                if (out_ty) *out_ty = ta;
                return val_tmp(val);
            }

            /* @size(T) — compile-time sizeof via GEP-from-null trick */
            if (!strcmp(name, "size")) {
                if (out_ty) {
                    Type *ut = ARENA_NEW(cg->arena, Type); ut->kind = TY_USIZE; *out_ty = ut;
                }
                if (e->builtin.args.len >= 1) {
                    Type *ta = e->builtin.args.data[0]->ty;
                    const char *llt = ta ? llvm_type(ta) : "i8";
                    int t = new_tmp(cg);
                    emit(cg, "  %%t%d = ptrtoint ptr getelementptr (%s, ptr null, i32 1) to i64\n",
                         t, llt);
                    return val_tmp(t);
                }
                return val_str("0");
            }

            /* @align(T) — alignment of T in bytes: offset of T in { i8, T } from null */
            if (!strcmp(name, "align")) {
                if (out_ty) {
                    Type *ut = ARENA_NEW(cg->arena, Type); ut->kind = TY_USIZE; *out_ty = ut;
                }
                if (e->builtin.args.len >= 1) {
                    Type *ta = e->builtin.args.data[0]->ty;
                    const char *llt = ta ? llvm_type(ta) : "i8";
                    int t = new_tmp(cg);
                    emit(cg, "  %%t%d = ptrtoint ptr getelementptr ({ i8, %s }, ptr null, i32 0, i32 1) to i64\n",
                         t, llt);
                    return val_tmp(t);
                }
                return val_str("1");
            }

            /* @len — always returns i64 */
            if (!strcmp(name, "len")) {
                if (out_ty) {
                    Type *i64_ty = ARENA_NEW(cg->arena, Type);
                    i64_ty->kind = TY_I64;
                    *out_ty = i64_ty;
                }
                Type *ta = NULL;
                Val a = cg_expr(cg, e->builtin.args.data[0], &ta);
                if (ta && ta->kind == TY_ARRAY) {
                    /* compile-time constant from [N]T size expression */
                    int64_t n = 0;
                    if (ta->array.size && ta->array.size->kind == EXPR_INT)
                        n = (int64_t)ta->array.size->ival;
                    Val v;
                    snprintf(v.buf, sizeof(v.buf), "%" PRId64, n);
                    return v;
                }
                /* slice / str: extract len from fat pointer */
                int t = new_tmp(cg);
                emit(cg, "  %%t%d = extractvalue { ptr, i64 } %s, 1\n", t, a.buf);
                return val_tmp(t);
            }

            /* @str_raw(ptr, len) — construct a str fat pointer from raw pointer and length */
            if (!strcmp(name, "str_raw")) {
                if (e->builtin.args.len < 2)
                    fatal_at(e->span, "@str_raw requires two arguments: @str_raw(ptr, len)");
                Val raw_ptr = cg_expr(cg, e->builtin.args.data[0], NULL);
                Val raw_len = cg_expr(cg, e->builtin.args.data[1], NULL);
                int f1 = new_tmp(cg);
                emit(cg, "  %%t%d = insertvalue { ptr, i64 } undef, ptr %s, 0\n", f1, raw_ptr.buf);
                int f2 = new_tmp(cg);
                emit(cg, "  %%t%d = insertvalue { ptr, i64 } %%t%d, i64 %s, 1\n", f2, f1, raw_len.buf);
                if (out_ty) { Type *st = ARENA_NEW(cg->arena, Type); st->kind = TY_STR; *out_ty = st; }
                return val_tmp(f2);
            }

            /* @offsetof(T, field) — byte offset of a struct field */
            if (!strcmp(name, "offsetof")) {
                if (e->builtin.args.len < 2)
                    fatal_at(e->span, "@offsetof requires two arguments: @offsetof(T, field)");
                if (out_ty) {
                    Type *ut = ARENA_NEW(cg->arena, Type); ut->kind = TY_USIZE; *out_ty = ut;
                }
                /* first arg: type name (EXPR_IDENT) */
                const char *ty_name = NULL;
                if (e->builtin.args.data[0]->kind == EXPR_IDENT)
                    ty_name = e->builtin.args.data[0]->ident.name;
                /* second arg: field name (EXPR_IDENT) */
                const char *field_name = NULL;
                if (e->builtin.args.data[1]->kind == EXPR_IDENT)
                    field_name = e->builtin.args.data[1]->ident.name;
                if (!ty_name || !field_name)
                    fatal_at(e->span, "@offsetof: expected identifier arguments");
                StructInfo *si = find_struct(cg, ty_name);
                if (!si) fatal_at(e->span, "@offsetof: '%s' is not a struct", ty_name);
                int fidx = -1;
                for (size_t fi = 0; fi < si->fields.len; fi++) {
                    if (!strcmp(si->fields.data[fi].name, field_name)) { fidx = (int)fi; break; }
                }
                if (fidx < 0)
                    fatal_at(e->span, "@offsetof: struct '%s' has no field '%s'", ty_name, field_name);
                int t = new_tmp(cg);
                emit(cg, "  %%t%d = ptrtoint ptr getelementptr (%%%s, ptr null, i32 0, i32 %d) to i64\n",
                     t, ty_name, fidx);
                return val_tmp(t);
            }

            /* @typeof(expr) — compile-time type name as a str constant */
            if (!strcmp(name, "typeof")) {
                if (e->builtin.args.len < 1)
                    fatal_at(e->span, "@typeof requires one argument");
                Type *ta = NULL;
                cg_expr(cg, e->builtin.args.data[0], &ta); /* evaluate for side effects + type */
                const char *tname = ta ? llvm_type(ta) : "unknown";
                /* for named types, use the Perzephxne name */
                if (ta && ta->kind == TY_NAMED) tname = ta->named.name;
                else if (ta) {
                    switch (ta->kind) {
                        case TY_I8: tname="i8"; break; case TY_I16: tname="i16"; break;
                        case TY_I32: tname="i32"; break; case TY_I64: tname="i64"; break;
                        case TY_U8: tname="u8"; break; case TY_U16: tname="u16"; break;
                        case TY_U32: tname="u32"; break; case TY_U64: tname="u64"; break;
                        case TY_F32: tname="f32"; break; case TY_F64: tname="f64"; break;
                        case TY_BOOL: tname="bool"; break; case TY_CHAR: tname="char"; break;
                        case TY_STR: tname="str"; break; case TY_USIZE: tname="usize"; break;
                        case TY_PTR: tname="ptr"; break; default: tname="unknown"; break;
                    }
                }
                int sid = intern_str(cg, arena_strdup(cg->arena, tname));
                size_t slen = strlen(tname);
                int ft = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr inbounds [%zu x i8], ptr @.str.%d, i32 0, i32 0\n",
                     ft, slen + 1, sid);
                int sa = new_tmp(cg);
                emit(cg, "  %%t%d = alloca { ptr, i64 }\n", sa);
                int p0 = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr { ptr, i64 }, ptr %%t%d, i32 0, i32 0\n", p0, sa);
                emit(cg, "  store ptr %%t%d, ptr %%t%d\n", ft, p0);
                int p1 = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr { ptr, i64 }, ptr %%t%d, i32 0, i32 1\n", p1, sa);
                emit(cg, "  store i64 %zu, ptr %%t%d\n", slen, p1);
                int rv = new_tmp(cg);
                emit(cg, "  %%t%d = load { ptr, i64 }, ptr %%t%d\n", rv, sa);
                if (out_ty) { Type *st = ARENA_NEW(cg->arena, Type); st->kind = TY_STR; *out_ty = st; }
                return val_tmp(rv);
            }

            /* @ok(val) — wrap a value in a failable success result */
            if (!strcmp(name, "ok")) {
                if (e->builtin.args.len < 1)
                    fatal_at(e->span, "@ok requires one argument");
                Type *vty = NULL;
                Val val = cg_expr(cg, e->builtin.args.data[0], &vty);
                const char *val_llt = vty ? effective_llvm_type(cg, vty) : "i32";
                /* build { val_llt, i32 } aggregate */
                char fail_llt[256];
                snprintf(fail_llt, sizeof(fail_llt), "{ %s, i32 }", val_llt);
                int f1 = new_tmp(cg);
                emit(cg, "  %%t%d = insertvalue %s undef, %s %s, 0\n",
                     f1, fail_llt, val_llt, val.buf);
                int f2 = new_tmp(cg);
                emit(cg, "  %%t%d = insertvalue %s %%t%d, i32 0, 1\n", f2, fail_llt, f1);
                if (out_ty && vty) {
                    Type *ft = ARENA_NEW(cg->arena, Type);
                    ft->kind = TY_FAILABLE;
                    ft->ptr.inner = vty;
                    *out_ty = ft;
                }
                return val_tmp(f2);
            }

            /* @err(code) — wrap an error code in a failable failure result */
            if (!strcmp(name, "err")) {
                if (e->builtin.args.len < 1)
                    fatal_at(e->span, "@err requires one argument (error code)");
                Type *cty = NULL;
                Val code = cg_expr(cg, e->builtin.args.data[0], &cty);
                /* determine inner value type from context (function return type) */
                const char *val_llt = "i32"; /* default inner type */
                if (cg->cur_fn_ret_ty && cg->cur_fn_ret_ty->kind == TY_FAILABLE)
                    val_llt = llvm_type(cg->cur_fn_ret_ty->ptr.inner);
                char fail_llt[256];
                snprintf(fail_llt, sizeof(fail_llt), "{ %s, i32 }", val_llt);
                int f1 = new_tmp(cg);
                emit(cg, "  %%t%d = insertvalue %s undef, i32 %s, 1\n", f1, fail_llt, code.buf);
                if (out_ty && cg->cur_fn_ret_ty && cg->cur_fn_ret_ty->kind == TY_FAILABLE) {
                    *out_ty = cg->cur_fn_ret_ty;
                }
                return val_tmp(f1);
            }

            /* @debug / @release — compile-time build mode booleans */
            if (!strcmp(name, "debug")) {
                if (out_ty) { Type *bt = ARENA_NEW(cg->arena, Type); bt->kind = TY_BOOL; *out_ty = bt; }
                return val_str(cg->release ? "0" : "1");
            }
            if (!strcmp(name, "release")) {
                if (out_ty) { Type *bt = ARENA_NEW(cg->arena, Type); bt->kind = TY_BOOL; *out_ty = bt; }
                return val_str(cg->release ? "1" : "0");
            }

            /* @os.* / @arch.* — compile-time platform booleans */
            if (!strncmp(name, "os.", 3) || !strncmp(name, "arch.", 5)) {
                if (out_ty) { Type *bt = ARENA_NEW(cg->arena, Type); bt->kind = TY_BOOL; *out_ty = bt; }
#if defined(__linux__)
                if (!strcmp(name, "os.linux"))   return val_str("1");
#else
                if (!strcmp(name, "os.linux"))   return val_str("0");
#endif
#if defined(_WIN32) || defined(_WIN64)
                if (!strcmp(name, "os.windows")) return val_str("1");
#else
                if (!strcmp(name, "os.windows")) return val_str("0");
#endif
#if defined(__APPLE__)
                if (!strcmp(name, "os.mac"))     return val_str("1");
#else
                if (!strcmp(name, "os.mac"))     return val_str("0");
#endif
#if defined(__x86_64__) || defined(_M_X64)
                if (!strcmp(name, "arch.x86_64")) return val_str("1");
#else
                if (!strcmp(name, "arch.x86_64")) return val_str("0");
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
                if (!strcmp(name, "arch.arm64")) return val_str("1");
#else
                if (!strcmp(name, "arch.arm64")) return val_str("0");
#endif
#if defined(__i386__) || defined(_M_IX86)
                if (!strcmp(name, "arch.x86"))   return val_str("1");
#else
                if (!strcmp(name, "arch.x86"))   return val_str("0");
#endif
#if defined(__arm__)
                if (!strcmp(name, "arch.arm"))   return val_str("1");
#else
                if (!strcmp(name, "arch.arm"))   return val_str("0");
#endif
                /* unknown os/arch variant — false */
                return val_str("0");
            }

            /* @err.constant — error code integer constants */
            if (!strncmp(name, "err.", 4)) {
                static const struct { const char *n; int v; } ec[] = {
                    {"ok",0},{"fail",1},{"div_zero",2},{"null_deref",3},
                    {"out_of_bounds",4},{"overflow",5},{"invalid",6},
                    {"not_found",7},{"io",8},{"oom",9},{NULL,0}
                };
                const char *errname = name + 4;
                for (int i = 0; ec[i].n; i++) {
                    if (!strcmp(errname, ec[i].n)) {
                        if (out_ty) {
                            Type *t32 = ARENA_NEW(cg->arena, Type);
                            t32->kind = TY_I32;
                            *out_ty = t32;
                        }
                        Val v; snprintf(v.buf, sizeof(v.buf), "%d", ec[i].v);
                        return v;
                    }
                }
                fatal_at(e->span, "unknown @err.%s", errname);
            }

            /* @bitcast(DstType, val) — reinterpret bits of val as DstType (same size) */
            if (!strcmp(name, "bitcast")) {
                if (e->builtin.args.len < 2)
                    fatal_at(e->span, "@bitcast requires two arguments: @bitcast(Type, val)");
                Type *dst_ty = e->builtin.args.data[0]->ty;
                Type *src_ty2 = NULL;
                Val src2 = cg_expr(cg, e->builtin.args.data[1], &src_ty2);
                const char *src_llt2 = src_ty2 ? llvm_type(src_ty2) : "i32";
                const char *dst_llt2 = dst_ty  ? llvm_type(dst_ty)  : "i32";
                int tb = new_tmp(cg);
                emit(cg, "  %%t%d = bitcast %s %s to %s\n", tb, src_llt2, src2.buf, dst_llt2);
                if (out_ty && dst_ty) *out_ty = dst_ty;
                return val_tmp(tb);
            }

            fatal_at(e->span, "unknown builtin '@%s'", name);
        }

        case EXPR_CAST: {
            Type *src_ty = NULL;
            Val src = cg_expr(cg, e->cast.val, &src_ty);
            const char *dst = e->cast.ty_name;
            int t = new_tmp(cg);

            if (!strcmp(dst, "str")) {
                /* int/float → string via sprintf into a 32-byte stack buffer;
                   returns a { ptr, i64 } str fat pointer. */
                int buf = new_tmp(cg);
                emit(cg, "  %%t%d = alloca [32 x i8]\n", buf);
                int cptr = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr [32 x i8], ptr %%t%d, i32 0, i32 0\n", cptr, buf);
                int is_src_float = src_ty && (src_ty->kind == TY_F32 || src_ty->kind == TY_F64);
                if (is_src_float) {
                    Val sv = src;
                    if (src_ty->kind == TY_F32) {
                        int tp = new_tmp(cg);
                        emit(cg, "  %%t%d = fpext float %s to double\n", tp, src.buf);
                        sv = val_tmp(tp);
                    }
                    emit(cg, "  call i32 (ptr, ptr, ...) @sprintf("
                             "ptr %%t%d, ptr @.fmt.f, double %s)\n", cptr, sv.buf);
                } else {
                    emit(cg, "  call i32 (ptr, ptr, ...) @sprintf("
                             "ptr %%t%d, ptr @.fmt.d, i32 %s)\n", cptr, src.buf);
                }
                int slen = new_tmp(cg);
                emit(cg, "  %%t%d = call i64 @strlen(ptr %%t%d)\n", slen, cptr);
                int f1 = new_tmp(cg);
                emit(cg, "  %%t%d = insertvalue { ptr, i64 } undef, ptr %%t%d, 0\n", f1, cptr);
                emit(cg, "  %%t%d = insertvalue { ptr, i64 } %%t%d, i64 %%t%d, 1\n", t, f1, slen);
                if (out_ty) { Type *st = ARENA_NEW(cg->arena, Type); st->kind = TY_STR; *out_ty = st; }
            } else if (src_ty && src_ty->kind == TY_STR && !strcmp(dst, "bool")) {
                /* str → bool: "true" or "1" → true, anything else → false (no failable) */
                int dp2 = new_tmp(cg);
                emit(cg, "  %%t%d = extractvalue { ptr, i64 } %s, 0\n", dp2, src.buf);
                int sid_t = intern_str(cg, "true");
                int sid_o = intern_str(cg, "1");
                int tptr = new_tmp(cg), optr = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr inbounds [5 x i8], ptr @.str.%d, i32 0, i32 0\n",
                     tptr, sid_t);
                emit(cg, "  %%t%d = getelementptr inbounds [2 x i8], ptr @.str.%d, i32 0, i32 0\n",
                     optr, sid_o);
                int c1 = new_tmp(cg), c2 = new_tmp(cg);
                emit(cg, "  %%t%d = call i32 @strcmp(ptr %%t%d, ptr %%t%d)\n", c1, dp2, tptr);
                emit(cg, "  %%t%d = call i32 @strcmp(ptr %%t%d, ptr %%t%d)\n", c2, dp2, optr);
                int eq1 = new_tmp(cg), eq2 = new_tmp(cg);
                emit(cg, "  %%t%d = icmp eq i32 %%t%d, 0\n", eq1, c1);
                emit(cg, "  %%t%d = icmp eq i32 %%t%d, 0\n", eq2, c2);
                emit(cg, "  %%t%d = or i1 %%t%d, %%t%d\n", t, eq1, eq2);
                if (out_ty) {
                    Type *bt = ARENA_NEW(cg->arena, Type);
                    bt->kind = TY_BOOL;
                    *out_ty = bt;
                }
            } else if (src_ty && src_ty->kind == TY_STR
                       && (!strcmp(dst,"i8")  || !strcmp(dst,"i16") || !strcmp(dst,"i32")
                        || !strcmp(dst,"i64") || !strcmp(dst,"u8")  || !strcmp(dst,"u16")
                        || !strcmp(dst,"u32") || !strcmp(dst,"u64") || !strcmp(dst,"usize")
                        || !strcmp(dst,"f32") || !strcmp(dst,"f64"))) {
                /* str → number: strtol/strtod with endptr validates full parse.
                   Returns !T failable: { 0, 1 } on failure (air value), { parsed, 0 } on success. */
                int is_float = !strcmp(dst,"f32") || !strcmp(dst,"f64");
                int dp  = new_tmp(cg);
                int ep  = new_tmp(cg);
                int raw = new_tmp(cg);
                int epl = new_tmp(cg);
                int ne  = new_tmp(cg);
                int eb  = new_tmp(cg);
                int iz  = new_tmp(cg);
                int ok  = new_tmp(cg);
                int ec  = new_tmp(cg);

                emit(cg, "  %%t%d = extractvalue { ptr, i64 } %s, 0\n", dp, src.buf);
                emit(cg, "  %%t%d = alloca ptr\n", ep);
                if (is_float)
                    emit(cg, "  %%t%d = call double @strtod(ptr %%t%d, ptr %%t%d)\n", raw, dp, ep);
                else
                    emit(cg, "  %%t%d = call i64 @strtol(ptr %%t%d, ptr %%t%d, i32 10)\n", raw, dp, ep);

                /* success = (ep != dp) && (*ep == '\0') */
                emit(cg, "  %%t%d = load ptr, ptr %%t%d\n", epl, ep);
                emit(cg, "  %%t%d = icmp ne ptr %%t%d, %%t%d\n", ne, dp, epl);
                emit(cg, "  %%t%d = load i8, ptr %%t%d\n", eb, epl);
                emit(cg, "  %%t%d = icmp eq i8 %%t%d, 0\n", iz, eb);
                emit(cg, "  %%t%d = and i1 %%t%d, %%t%d\n", ok, ne, iz);
                emit(cg, "  %%t%d = select i1 %%t%d, i32 0, i32 1\n", ec, ok);

                /* convert raw to target type */
                const char *val_llt;
                int vfinal;
                if (!strcmp(dst,"i8") || !strcmp(dst,"u8")) {
                    val_llt = "i8";  vfinal = new_tmp(cg);
                    emit(cg, "  %%t%d = trunc i64 %%t%d to i8\n", vfinal, raw);
                } else if (!strcmp(dst,"i16") || !strcmp(dst,"u16")) {
                    val_llt = "i16"; vfinal = new_tmp(cg);
                    emit(cg, "  %%t%d = trunc i64 %%t%d to i16\n", vfinal, raw);
                } else if (!strcmp(dst,"i32") || !strcmp(dst,"u32")) {
                    val_llt = "i32"; vfinal = new_tmp(cg);
                    emit(cg, "  %%t%d = trunc i64 %%t%d to i32\n", vfinal, raw);
                } else if (!strcmp(dst,"f32")) {
                    val_llt = "float"; vfinal = new_tmp(cg);
                    emit(cg, "  %%t%d = fptrunc double %%t%d to float\n", vfinal, raw);
                } else {
                    val_llt = is_float ? "double" : "i64";
                    vfinal = raw;
                }

                /* zero out value on failure: select ok ? parsed : 0 */
                int vs = new_tmp(cg);
                const char *zero = is_float ? "0.0" : "0";
                emit(cg, "  %%t%d = select i1 %%t%d, %s %%t%d, %s %s\n",
                     vs, ok, val_llt, vfinal, val_llt, zero);

                char fail_llt[64];
                snprintf(fail_llt, sizeof(fail_llt), "{ %s, i32 }", val_llt);
                int a1 = new_tmp(cg);
                emit(cg, "  %%t%d = insertvalue %s undef, %s %%t%d, 0\n", a1, fail_llt, val_llt, vs);
                emit(cg, "  %%t%d = insertvalue %s %%t%d, i32 %%t%d, 1\n", t, fail_llt, a1, ec);

                if (out_ty) {
                    Type *inner = ARENA_NEW(cg->arena, Type);
                    if      (!strcmp(dst,"i8"))    inner->kind = TY_I8;
                    else if (!strcmp(dst,"i16"))   inner->kind = TY_I16;
                    else if (!strcmp(dst,"i32"))   inner->kind = TY_I32;
                    else if (!strcmp(dst,"i64"))   inner->kind = TY_I64;
                    else if (!strcmp(dst,"u8"))    inner->kind = TY_U8;
                    else if (!strcmp(dst,"u16"))   inner->kind = TY_U16;
                    else if (!strcmp(dst,"u32"))   inner->kind = TY_U32;
                    else if (!strcmp(dst,"u64"))   inner->kind = TY_U64;
                    else if (!strcmp(dst,"usize")) inner->kind = TY_USIZE;
                    else if (!strcmp(dst,"f32"))   inner->kind = TY_F32;
                    else                           inner->kind = TY_F64;
                    Type *ft = ARENA_NEW(cg->arena, Type);
                    ft->kind = TY_FAILABLE;
                    ft->ptr.inner = inner;
                    *out_ty = ft;
                }
            } else {
                /* numeric cast — choose trunc/sext/zext based on bit widths */
                const char *src_llt = src_ty ? llvm_type(src_ty) : "i32";
                int src_bits = 32; /* default */
                if (!strcmp(src_llt,"i8"))  src_bits=8;
                else if (!strcmp(src_llt,"i16")) src_bits=16;
                else if (!strcmp(src_llt,"i32")) src_bits=32;
                else if (!strcmp(src_llt,"i64")) src_bits=64;
                int is_src_signed = type_is_signed(src_ty);
                #define INT_CAST(dst_llt, dst_bits) do { \
                    const char *op = (dst_bits < src_bits) ? "trunc" \
                                   : (dst_bits > src_bits) ? (is_src_signed ? "sext" : "zext") \
                                   : NULL; \
                    if (op) emit(cg, "  %%t%d = %s %s %s to %s\n", t, op, src_llt, src.buf, dst_llt); \
                    else    emit(cg, "  %%t%d = bitcast %s %s to %s\n", t, src_llt, src.buf, dst_llt); \
                } while(0)
                if (!strcmp(dst,"i8"))         INT_CAST("i8",  8);
                else if (!strcmp(dst,"i16"))   INT_CAST("i16",16);
                else if (!strcmp(dst,"i32"))   INT_CAST("i32",32);
                else if (!strcmp(dst,"i64"))   INT_CAST("i64",64);
                else if (!strcmp(dst,"u8"))    INT_CAST("i8",  8);
                else if (!strcmp(dst,"u16"))   INT_CAST("i16",16);
                else if (!strcmp(dst,"u32"))   INT_CAST("i32",32);
                else if (!strcmp(dst,"u64"))   INT_CAST("i64",64);
                #undef INT_CAST
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
            /* use sema type for out_ty — comparisons return bool, not operand type */
            if (out_ty) *out_ty = e->ty ? e->ty : ty;
            /* pointer arithmetic: emit GEP instead of add/sub */
            if (e->binop.op == BINOP_ADD || e->binop.op == BINOP_SUB) {
                Type *ptr_ty = (lt && lt->kind == TY_PTR) ? lt
                             : (rt && rt->kind == TY_PTR) ? rt : NULL;
                if (ptr_ty) {
                    Val ptr_v = (lt && lt->kind == TY_PTR) ? l : r;
                    Val off_v = (lt && lt->kind == TY_PTR) ? r : l;
                    Type *off_ty = (lt && lt->kind == TY_PTR) ? rt : lt;
                    /* ptr - ptr → ptrdiff as usize */
                    if (lt && lt->kind == TY_PTR && rt && rt->kind == TY_PTR) {
                        int t2 = new_tmp(cg);
                        emit(cg, "  %%t%d = ptrtoint ptr %s to i64\n", t2, l.buf);
                        int t3 = new_tmp(cg);
                        emit(cg, "  %%t%d = ptrtoint ptr %s to i64\n", t3, r.buf);
                        int t4 = new_tmp(cg);
                        emit(cg, "  %%t%d = sub i64 %%t%d, %%t%d\n", t4, t2, t3);
                        return val_tmp(t4);
                    }
                    /* sign-extend offset to i64 if needed */
                    Val idx = off_v;
                    if (off_ty && llvm_type(off_ty)[0] != 'i') {
                        /* non-int offset, unlikely; just use as-is */
                    } else if (off_ty && strcmp(llvm_type(off_ty), "i64") != 0) {
                        int ext = new_tmp(cg);
                        int is_signed_off = type_is_signed(off_ty);
                        emit(cg, "  %%t%d = %sext %s %s to i64\n",
                             ext, is_signed_off ? "s" : "z", llvm_type(off_ty), off_v.buf);
                        idx = val_tmp(ext);
                    }
                    /* negative offset for subtraction */
                    if (e->binop.op == BINOP_SUB) {
                        int neg = new_tmp(cg);
                        emit(cg, "  %%t%d = sub i64 0, %s\n", neg, idx.buf);
                        idx = val_tmp(neg);
                    }
                    Type *elem_ty = ptr_ty->ptr.inner;
                    const char *elem_llt = elem_ty ? llvm_type(elem_ty) : "i8";
                    int t2 = new_tmp(cg);
                    emit(cg, "  %%t%d = getelementptr %s, ptr %s, i64 %s\n",
                         t2, elem_llt, ptr_v.buf, idx.buf);
                    return val_tmp(t2);
                }
            }
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
                    /* &var must return the alloca pointer, not a loaded value.
                       For struct types cg_expr already returns the alloca ptr.
                       For scalar types we need to bypass the load and get the alloca. */
                    if (e->unop.operand->kind == EXPR_IDENT) {
                        Symbol *sym = lookup(cg, e->unop.operand->ident.name);
                        if (sym) return val_str(sym->llvm_name);
                    }
                    return val_str(o.buf);
                }
            }
            return val_tmp(t);
        }

        case EXPR_CALL: {
            /* Method call: callee is EXPR_FIELD with is_method flag set by sema */
            if (e->call.callee->kind == EXPR_FIELD && e->call.callee->field.is_method) {
                const char *mangled = e->call.callee->field.mangled_name;
                int is_static = (e->call.callee->field.is_method == 2);
                Type *ret_ty = e->ty;
                int is_void = (ret_ty == NULL || ret_ty->kind == TY_VOID);
                const char *ret_llt = is_void ? "void" : effective_llvm_type(cg, ret_ty);

                Type *obj_ty = NULL;
                Val self_val;
                if (!is_static)
                    self_val = cg_expr(cg, e->call.callee->field.obj, &obj_ty);

                size_t nargs = e->call.args.len;
                Val   *arg_vals = nargs ? malloc(sizeof(Val)   * nargs) : NULL;
                Type **arg_tys  = nargs ? malloc(sizeof(Type*) * nargs) : NULL;
                for (size_t i = 0; i < nargs; i++) {
                    arg_tys[i]  = NULL;
                    arg_vals[i] = cg_expr(cg, e->call.args.data[i], &arg_tys[i]);
                }

                int t = new_tmp(cg);
                if (is_static) {
                    if (is_void)
                        emit(cg, "  call void @%s(", mangled);
                    else
                        emit(cg, "  %%t%d = call %s @%s(", t, ret_llt, mangled);
                    for (size_t i = 0; i < nargs; i++) {
                        if (i) emit(cg, ", ");
                        const char *llt = effective_llvm_type(cg, arg_tys[i]);
                        emit(cg, "%s %s", llt, arg_vals[i].buf);
                    }
                } else {
                    if (is_void)
                        emit(cg, "  call void @%s(ptr %s", mangled, self_val.buf);
                    else
                        emit(cg, "  %%t%d = call %s @%s(ptr %s", t, ret_llt, mangled, self_val.buf);
                    for (size_t i = 0; i < nargs; i++) {
                        const char *llt = effective_llvm_type(cg, arg_tys[i]);
                        emit(cg, ", %s %s", llt, arg_vals[i].buf);
                    }
                }
                emit(cg, ")\n");
                free(arg_vals);
                free(arg_tys);
                if (out_ty) *out_ty = ret_ty;
                return val_tmp(t);
            }

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

            if (callee_ty && callee_ty->kind == TY_FN)
                ret_llt = callee_ty->fn.ret ? effective_llvm_type(cg, callee_ty->fn.ret) : "void";
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

            /* coerce arguments to callee param types when known */
            if (callee_ty && callee_ty->kind == TY_FN) {
                size_t np = callee_ty->fn.params.len;
                for (size_t i = 0; i < nargs && i < np; i++) {
                    Type *param_ty = callee_ty->fn.params.data[i];
                    Type *arg_ty   = arg_tys[i];
                    if (!param_ty || !arg_ty) continue;
                    const char *pt = llvm_type(param_ty);
                    const char *at = arg_ty ? effective_llvm_type(cg, arg_ty) : "i32";
                    if (!strcmp(pt, at)) continue;
                    /* str arg → *u8 param: extract .data pointer */
                    if (arg_ty->kind == TY_STR && param_ty->kind == TY_PTR) {
                        int ep = new_tmp(cg);
                        emit(cg, "  %%t%d = extractvalue { ptr, i64 } %s, 0\n",
                             ep, arg_vals[i].buf);
                        arg_vals[i] = val_tmp(ep);
                        arg_tys[i]  = param_ty;
                        continue;
                    }
                    /* integer width coercion */
                    int sv3 = 0, lv3 = 0;
                    if (!strcmp(at,"i8"))  sv3=8; else if (!strcmp(at,"i16")) sv3=16;
                    else if (!strcmp(at,"i32")) sv3=32; else if (!strcmp(at,"i64")) sv3=64;
                    if (!strcmp(pt,"i8"))  lv3=8; else if (!strcmp(pt,"i16")) lv3=16;
                    else if (!strcmp(pt,"i32")) lv3=32; else if (!strcmp(pt,"i64")) lv3=64;
                    if (sv3 && lv3 && sv3 != lv3) {
                        int ct = new_tmp(cg);
                        const char *op = (lv3 < sv3) ? "trunc"
                                       : (type_is_signed(arg_ty) ? "sext" : "zext");
                        emit(cg, "  %%t%d = %s %s %s to %s\n", ct, op, at, arg_vals[i].buf, pt);
                        arg_vals[i] = val_tmp(ct);
                        arg_tys[i]  = param_ty;
                    }
                }
            }
            int is_void_call = !strcmp(ret_llt, "void");
            int t = new_tmp(cg);
            if (is_void_call)
                emit(cg, "  call void %s(", fn_name);
            else
                emit(cg, "  %%t%d = call %s %s(", t, ret_llt, fn_name);
            for (size_t i = 0; i < nargs; i++) {
                const char *llt = effective_llvm_type(cg, arg_tys[i]);
                if (i) emit(cg, ", ");
                emit(cg, "%s %s", llt, arg_vals[i].buf);
            }
            emit(cg, ")\n");
            free(arg_vals);
            free(arg_tys);
            if (out_ty) *out_ty = e->ty;
            return val_tmp(t);
        }

        case EXPR_FIELD: {
            /* @err.constant — error code integer */
            if (e->field.obj->kind == EXPR_BUILTIN
                    && !strcmp(e->field.obj->builtin.name, "err")) {
                static const struct { const char *n; int v; } ec[] = {
                    {"ok",0},{"fail",1},{"div_zero",2},{"null_deref",3},
                    {"out_of_bounds",4},{"overflow",5},{"invalid",6},
                    {"not_found",7},{"io",8},{"oom",9},{NULL,0}
                };
                for (int i = 0; ec[i].n; i++) {
                    if (!strcmp(e->field.field, ec[i].n)) {
                        if (out_ty) {
                            Type *t32 = ARENA_NEW(cg->arena, Type); t32->kind = TY_I32; *out_ty = t32;
                        }
                        Val v; snprintf(v.buf, sizeof(v.buf), "%d", ec[i].v); return v;
                    }
                }
                fatal_at(e->span, "unknown @err.%s", e->field.field);
            }
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
            /* str / slice pseudo-fields: .len -> i64, .data/.ptr -> ptr */
            {
                Type *peek_ty = e->field.obj->ty;
                if (peek_ty && (peek_ty->kind == TY_STR || peek_ty->kind == TY_SLICE)) {
                    Type *pobj_ty = NULL;
                    Val fat = cg_expr(cg, e->field.obj, &pobj_ty);
                    int field_idx = (!strcmp(e->field.field, "len")) ? 1 : 0;
                    int t = new_tmp(cg);
                    emit(cg, "  %%t%d = extractvalue { ptr, i64 } %s, %d\n",
                         t, fat.buf, field_idx);
                    if (out_ty) *out_ty = e->ty;
                    return val_tmp(t);
                }
            }
            /* Struct field access */
            Type *obj_ty = NULL;
            Val obj = cg_expr(cg, e->field.obj, &obj_ty);
            /* auto-deref: *Struct.field — EXPR_IDENT already loaded the ptr value;
               just use it directly as the struct pointer for GEP */
            if (obj_ty && obj_ty->kind == TY_PTR && obj_ty->ptr.inner
                    && obj_ty->ptr.inner->kind == TY_NAMED)
                obj_ty = obj_ty->ptr.inner;
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
            Val block = cg_expr(cg, e->deref.operand, &pt);
            Type *inner_ty = (pt && pt->ptr.inner) ? pt->ptr.inner : NULL;
            /* data starts at byte offset 8 (after the i64 refcount) */
            int dp = new_tmp(cg);
            emit(cg, "  %%t%d = getelementptr i8, ptr %s, i64 8\n", dp, block.buf);
            if (out_ty) *out_ty = inner_ty;
            /* for struct inner types return the data ptr (struct value = ptr convention) */
            if (inner_ty && inner_ty->kind == TY_NAMED
                    && find_struct(cg, inner_ty->named.name))
                return val_tmp(dp);
            /* for scalars load the value */
            int t = new_tmp(cg);
            const char *llt = inner_ty ? llvm_type(inner_ty) : "i32";
            emit(cg, "  %%t%d = load %s, ptr %%t%d\n", t, llt, dp);
            return val_tmp(t);
        }

        case EXPR_INDEX: {
            Type *at = NULL;
            Val arr = cg_expr(cg, e->index.arr, &at);
            Val idx = cg_expr(cg, e->index.idx, NULL);

            const char *elem_llt = "i8";
            Type *elem_ty = NULL;
            const char *data_buf = arr.buf;

            if (at && at->kind == TY_SLICE) {
                /* arr is a { ptr, i64 } value — extract data pointer first */
                elem_ty  = at->ptr.inner;
                elem_llt = elem_ty ? llvm_type(elem_ty) : "i8";
                int dp = new_tmp(cg);
                emit(cg, "  %%t%d = extractvalue { ptr, i64 } %s, 0\n", dp, arr.buf);
                char buf[32];
                snprintf(buf, sizeof(buf), "%%t%d", dp);
                data_buf = arena_strdup(cg->arena, buf);
            } else if (at && at->kind == TY_ARRAY && at->array.inner) {
                elem_ty  = at->array.inner;
                elem_llt = llvm_type(elem_ty);
                /* arr is a raw alloca ptr — use directly */
            }

            int ptr = new_tmp(cg);
            emit(cg, "  %%t%d = getelementptr %s, ptr %s, i64 %s\n",
                 ptr, elem_llt, data_buf, idx.buf);
            int t = new_tmp(cg);
            emit(cg, "  %%t%d = load %s, ptr %%t%d\n", t, elem_llt, ptr);
            if (out_ty) *out_ty = elem_ty;
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

        case EXPR_WHEN: {
            /* when-as-expression: evaluate all arms, store result to a slot */
            Type *res_ty = e->ty; /* set by sema from first STMT_EXPR arm */
            const char *res_llt = res_ty ? effective_llvm_type(cg, res_ty) : "i64";
            int res_slot = new_tmp(cg);
            emit(cg, "  %%t%d = alloca %s\n", res_slot, res_llt);

            int end_l = new_label(cg);
            Type *val_ty = NULL;
            Val val = cg_expr(cg, e->when.cond, &val_ty);

            /* helper: emit arm body — store STMT_EXPR result to res_slot */
            /* NOTE: arm bodies use STMT_EXPR for result-producing arms */
            UnionInfoCG *ui = NULL;
            if (val_ty && val_ty->kind == TY_NAMED)
                ui = find_union(cg, val_ty->named.name);

            if (ui) {
                /* tagged union subject */
                int tag_ptr = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr %%%s, ptr %s, i32 0, i32 0\n",
                     tag_ptr, ui->name, val.buf);
                int tag_val = new_tmp(cg);
                emit(cg, "  %%t%d = load i32, ptr %%t%d\n", tag_val, tag_ptr);

                for (size_t i = 0; i < e->when.arms.len; i++) {
                    WhenArm *arm = &e->when.arms.data[i];
                    int body_l = new_label(cg);
                    int next_l = (i + 1 < e->when.arms.len) ? new_label(cg) : end_l;

                    int cond_t = -1;
                    Type *matched_payload_ty = NULL;
                    int   matched_disc       = -1;
                    for (size_t pi = 0; pi < arm->pats.len; pi++) {
                        Expr *pat = arm->pats.data[pi];
                        if (pat->kind == EXPR_DISCARD) {
                            int wc = new_tmp(cg); emit(cg, "  %%t%d = add i1 0, 1\n", wc);
                            cond_t = wc; break;
                        }
                        if (pat->kind == EXPR_IDENT && pat->ident.name[0] == '.') {
                            const char *vname = pat->ident.name + 1;
                            int disc = 0; Type *pty = NULL;
                            for (size_t vi = 0; vi < ui->n_variants; vi++) {
                                if (!strcmp(ui->variants[vi].name, vname)) {
                                    disc = (int)vi; pty = ui->variants[vi].ty; break;
                                }
                            }
                            if (matched_disc < 0) { matched_disc = disc; matched_payload_ty = pty; }
                            int cmp = new_tmp(cg);
                            emit(cg, "  %%t%d = icmp eq i32 %%t%d, %d\n", cmp, tag_val, disc);
                            if (cond_t < 0) { cond_t = cmp; } else {
                                int or_t = new_tmp(cg);
                                emit(cg, "  %%t%d = or i1 %%t%d, %%t%d\n", or_t, cond_t, cmp);
                                cond_t = or_t;
                            }
                        }
                    }
                    if (cond_t < 0) { int wc = new_tmp(cg); emit(cg, "  %%t%d = add i1 0, 1\n", wc); cond_t = wc; }
                    emit_br(cg, "  br i1 %%t%d, label %%l%d, label %%l%d\n", cond_t, body_l, next_l);
                    emit_label(cg, body_l);
                    push_scope(cg);

                    if (arm->bind && matched_payload_ty && ui->payload_size > 0) {
                        int pay_ptr = new_tmp(cg);
                        emit(cg, "  %%t%d = getelementptr %%%s, ptr %s, i32 0, i32 1\n",
                             pay_ptr, ui->name, val.buf);
                        char pay_buf[32]; snprintf(pay_buf, sizeof(pay_buf), "%%t%d", pay_ptr);
                        const char *bind_llvm;
                        if (matched_payload_ty->kind == TY_NAMED && find_struct(cg, matched_payload_ty->named.name)) {
                            bind_llvm = arena_strdup(cg->arena, pay_buf);
                        } else {
                            const char *pay_llt = llvm_type(matched_payload_ty);
                            int ba = new_tmp(cg);
                            emit(cg, "  %%t%d = alloca %s\n", ba, pay_llt);
                            int pv = new_tmp(cg);
                            emit(cg, "  %%t%d = load %s, ptr %%t%d\n", pv, pay_llt, pay_ptr);
                            emit(cg, "  store %s %%t%d, ptr %%t%d\n", pay_llt, pv, ba);
                            char bb[32]; snprintf(bb, sizeof(bb), "%%t%d", ba);
                            bind_llvm = arena_strdup(cg->arena, bb);
                        }
                        define_sym(cg, arm->bind, bind_llvm, 0, matched_payload_ty);
                    }

                    /* body: if STMT_EXPR, store result; otherwise just execute */
                    if (arm->body && arm->body->kind == STMT_EXPR && arm->body->expr) {
                        Type *arm_ty = NULL;
                        Val arm_val = cg_expr(cg, arm->body->expr, &arm_ty);
                        const char *store_llt = arm_ty ? effective_llvm_type(cg, arm_ty) : res_llt;
                        emit(cg, "  store %s %s, ptr %%t%d\n", store_llt, arm_val.buf, res_slot);
                    } else if (arm->body) {
                        cg_stmt(cg, arm->body);
                    }
                    pop_scope(cg);
                    emit_br(cg, "  br label %%l%d\n", end_l);
                    if (next_l != end_l) emit_label(cg, next_l);
                }
            } else {
                /* scalar / enum subject */
                const char *llt = effective_llvm_type(cg, val_ty);
                for (size_t i = 0; i < e->when.arms.len; i++) {
                    WhenArm *arm = &e->when.arms.data[i];
                    int body_l = new_label(cg);
                    int next_l = (i + 1 < e->when.arms.len) ? new_label(cg) : end_l;
                    int cond_t = -1;
                    for (size_t pi = 0; pi < arm->pats.len; pi++) {
                        Expr *pat = arm->pats.data[pi];
                        if (pat->kind == EXPR_DISCARD) {
                            int wc = new_tmp(cg); emit(cg, "  %%t%d = add i1 0, 1\n", wc);
                            cond_t = wc; break;
                        }
                        Val pv = cg_expr(cg, pat, NULL);
                        int cmp = new_tmp(cg);
                        emit(cg, "  %%t%d = icmp eq %s %s, %s\n", cmp, llt, val.buf, pv.buf);
                        if (cond_t < 0) { cond_t = cmp; } else {
                            int or_t = new_tmp(cg);
                            emit(cg, "  %%t%d = or i1 %%t%d, %%t%d\n", or_t, cond_t, cmp);
                            cond_t = or_t;
                        }
                    }
                    if (cond_t < 0) { int wc = new_tmp(cg); emit(cg, "  %%t%d = add i1 0, 1\n", wc); cond_t = wc; }
                    emit_br(cg, "  br i1 %%t%d, label %%l%d, label %%l%d\n", cond_t, body_l, next_l);
                    emit_label(cg, body_l);
                    push_scope(cg);

                    if (arm->body && arm->body->kind == STMT_EXPR && arm->body->expr) {
                        Type *arm_ty = NULL;
                        Val arm_val = cg_expr(cg, arm->body->expr, &arm_ty);
                        const char *store_llt = arm_ty ? effective_llvm_type(cg, arm_ty) : res_llt;
                        emit(cg, "  store %s %s, ptr %%t%d\n", store_llt, arm_val.buf, res_slot);
                    } else if (arm->body) {
                        cg_stmt(cg, arm->body);
                    }
                    pop_scope(cg);
                    emit_br(cg, "  br label %%l%d\n", end_l);
                    if (next_l != end_l) emit_label(cg, next_l);
                }
            }

            emit_label(cg, end_l);
            if (out_ty) *out_ty = res_ty;
            int load_res = new_tmp(cg);
            emit(cg, "  %%t%d = load %s, ptr %%t%d\n", load_res, res_llt, res_slot);
            return val_tmp(load_res);
        }

        case EXPR_STRUCT_LIT: {
            /* out_ty = the named struct/union type */
            if (out_ty) {
                Type *sty = ARENA_NEW(cg->arena, Type);
                sty->kind = TY_NAMED;
                sty->named.name = e->struct_lit.ty_name;
                *out_ty = sty;
            }

            /* tagged union construction: shape{.circle=5.0} */
            UnionInfoCG *ui = find_union(cg, e->struct_lit.ty_name);
            if (ui) {
                int t = new_tmp(cg);
                emit(cg, "  %%t%d = alloca %%%s\n", t, e->struct_lit.ty_name);
                if (e->struct_lit.fields.len == 1) {
                    FieldInit *fi = &e->struct_lit.fields.data[0];
                    /* find variant index */
                    int disc = 0;
                    Type *payload_ty = NULL;
                    for (size_t vi = 0; vi < ui->n_variants; vi++) {
                        if (!strcmp(ui->variants[vi].name, fi->name)) {
                            disc = (int)vi;
                            payload_ty = ui->variants[vi].ty;
                            break;
                        }
                    }
                    /* store discriminant at field 0 */
                    int tag_ptr = new_tmp(cg);
                    emit(cg, "  %%t%d = getelementptr %%%s, ptr %%t%d, i32 0, i32 0\n",
                         tag_ptr, e->struct_lit.ty_name, t);
                    emit(cg, "  store i32 %d, ptr %%t%d\n", disc, tag_ptr);
                    /* store payload at field 1 (if variant has a payload and a real value) */
                    if (payload_ty && fi->val && fi->val->kind != EXPR_UNDEF) {
                        Type *fty = NULL;
                        Val fv = cg_expr(cg, fi->val, &fty);
                        const char *store_llt = fty ? llvm_type(fty) : llvm_type(payload_ty);
                        int pay_ptr = new_tmp(cg);
                        emit(cg, "  %%t%d = getelementptr %%%s, ptr %%t%d, i32 0, i32 1\n",
                             pay_ptr, e->struct_lit.ty_name, t);
                        /* for struct payloads: load then store */
                        if (fty && fty->kind == TY_NAMED && find_struct(cg, fty->named.name)) {
                            int loaded = new_tmp(cg);
                            emit(cg, "  %%t%d = load %s, ptr %s\n", loaded, store_llt, fv.buf);
                            emit(cg, "  store %s %%t%d, ptr %%t%d\n", store_llt, loaded, pay_ptr);
                        } else {
                            emit(cg, "  store %s %s, ptr %%t%d\n", store_llt, fv.buf, pay_ptr);
                        }
                    }
                }
                return val_tmp(t);
            }

            /* regular struct literal */
            StructInfo *si = find_struct(cg, e->struct_lit.ty_name);
            int t = new_tmp(cg);
            emit(cg, "  %%t%d = alloca %%%s\n", t, e->struct_lit.ty_name);
            for (size_t i = 0; i < e->struct_lit.fields.len; i++) {
                FieldInit *fi = &e->struct_lit.fields.data[i];
                Type *val_ty = NULL;
                Val fv = cg_expr(cg, fi->val, &val_ty);
                int fidx = si ? struct_field_index(si, fi->name) : (int)i;
                if (fidx < 0) fidx = (int)i;
                /* use field type for store; coerce value if integer widths differ */
                Type *field_ty = (si && fi->name) ? struct_field_type(si, fi->name) : val_ty;
                const char *store_llt = field_ty ? effective_llvm_type(cg, field_ty)
                                                  : (val_ty ? llvm_type(val_ty) : "i64");
                const char *val_llt   = val_ty ? llvm_type(val_ty) : store_llt;
                if (strcmp(val_llt, store_llt) != 0) {
                    int sv = 0, lv = 0;
                    if (!strcmp(val_llt,"i8"))   sv=8;  else if (!strcmp(val_llt,"i16"))  sv=16;
                    else if (!strcmp(val_llt,"i32")) sv=32; else if (!strcmp(val_llt,"i64")) sv=64;
                    if (!strcmp(store_llt,"i8"))  lv=8;  else if (!strcmp(store_llt,"i16")) lv=16;
                    else if (!strcmp(store_llt,"i32")) lv=32; else if (!strcmp(store_llt,"i64")) lv=64;
                    if (sv && lv && sv != lv) {
                        int ct = new_tmp(cg);
                        int is_signed = val_ty ? type_is_signed(val_ty) : 0;
                        const char *op = (lv < sv) ? "trunc" : (is_signed ? "sext" : "zext");
                        emit(cg, "  %%t%d = %s %s %s to %s\n", ct, op, val_llt, fv.buf, store_llt);
                        fv = val_tmp(ct);
                    }
                }
                int fp = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr %%%s, ptr %%t%d, i32 0, i32 %d\n",
                     fp, e->struct_lit.ty_name, t, fidx);
                emit(cg, "  store %s %s, ptr %%t%d\n", store_llt, fv.buf, fp);
            }
            return val_tmp(t);
        }

        case EXPR_ARRAY_LIT: {
            size_t n = e->array_lit.len;
            /* sema sets e->ty = TY_ARRAY for fixed arrays, TY_SLICE for empty */
            int is_fixed = e->ty && e->ty->kind == TY_ARRAY;
            Type *elem_ty = is_fixed ? e->ty->array.inner
                          : (e->ty && e->ty->kind == TY_SLICE) ? e->ty->ptr.inner : NULL;
            const char *elem_llt = elem_ty ? llvm_type(elem_ty) : "i64";

            if (n == 0) {
                /* empty literal — return zero slice */
                if (out_ty) *out_ty = e->ty;
                int s0 = new_tmp(cg);
                emit(cg, "  %%t%d = insertvalue { ptr, i64 } { ptr null, i64 0 }, ptr null, 0\n", s0);
                return val_tmp(s0);
            }

            /* allocate backing storage and fill elements */
            int arr = new_tmp(cg);
            emit(cg, "  %%t%d = alloca [%zu x %s]\n", arr, n, elem_llt);
            for (size_t i = 0; i < n; i++) {
                Val ev = cg_expr(cg, e->array_lit.data[i], NULL);
                int ep = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr [%zu x %s], ptr %%t%d, i32 0, i32 %zu\n",
                     ep, n, elem_llt, arr, i);
                emit(cg, "  store %s %s, ptr %%t%d\n", elem_llt, ev.buf, ep);
            }

            if (out_ty) *out_ty = e->ty;

            if (is_fixed) {
                /* [N]T: return the alloca ptr — STMT_LET will load+store like a struct */
                return val_tmp(arr);
            }

            /* []T slice: build { ptr, i64 } fat pointer */
            int dp = new_tmp(cg);
            emit(cg, "  %%t%d = getelementptr [%zu x %s], ptr %%t%d, i32 0, i32 0\n",
                 dp, n, elem_llt, arr);
            int sl0 = new_tmp(cg);
            emit(cg, "  %%t%d = insertvalue { ptr, i64 } undef, ptr %%t%d, 0\n", sl0, dp);
            int sl1 = new_tmp(cg);
            emit(cg, "  %%t%d = insertvalue { ptr, i64 } %%t%d, i64 %zu, 1\n", sl1, sl0, n);
            return val_tmp(sl1);
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
            int is_fail_err = s->let.ty && s->let.ty->kind == TY_FAILABLE;
            /* allocate storage */
            int alloca = new_tmp(cg);
            const char *llt = s->let.ty ? effective_llvm_type(cg, s->let.ty) : "i32";
            if (is_fail_err) llt = "i32"; /* error code slot */
            emit(cg, "  %%t%d = alloca %s\n", alloca, llt);

            if (s->let.init && s->let.init->kind == EXPR_UNDEF) {
                /* undef: zero-initialize based on declared type */
                const char *zero = "0";
                if (s->let.ty && !is_fail_err) {
                    switch (s->let.ty->kind) {
                        case TY_BOOL: zero = "false"; break;
                        case TY_F16: case TY_F32: case TY_F64: zero = "0.0"; break;
                        case TY_PTR: case TY_SMART_PTR: zero = "null"; break;
                        case TY_STR: case TY_SLICE: case TY_NAMED:
                            zero = "zeroinitializer"; break;
                        default: zero = "0"; break;
                    }
                }
                emit(cg, "  store %s %s, ptr %%t%d\n", llt, zero, alloca);
            } else if (s->let.init) {
                /* failable error variable: reuse cached aggregate from val-side let */
                if (is_fail_err && s->let.init == cg->last_fail_init && cg->last_fail_ty) {
                    const char *fail_llt = llvm_type(cg->last_fail_ty);
                    int ev = new_tmp(cg);
                    emit(cg, "  %%t%d = extractvalue %s %s, 1\n",
                         ev, fail_llt, cg->last_fail_val.buf);
                    emit(cg, "  store i32 %%t%d, ptr %%t%d\n", ev, alloca);
                } else {
                    Type *init_ty = NULL;
                    Val init = cg_expr(cg, s->let.init, &init_ty);
                    /* is_struct: TY_NAMED that is NOT an enum */
                    int is_struct = init_ty && init_ty->kind == TY_NAMED
                                    && !find_enum(cg, init_ty->named.name);
                    /* init_is_ptr: expressions that return a ptr to the struct (not the value) */
                    int init_is_ptr = is_struct &&
                                      (s->let.init->kind == EXPR_IDENT
                                       || s->let.init->kind == EXPR_STRUCT_LIT);
                    /* ^T copy: auto-increment RC when source is an identifier */
                    int is_rc_copy = init_ty && init_ty->kind == TY_SMART_PTR
                                     && s->let.init->kind == EXPR_IDENT;
                    if (is_rc_copy) emit_rc_inc(cg, init.buf);
                    if (is_struct && init_is_ptr) {
                        /* struct init returns a ptr — copy via load+store */
                        int loaded = new_tmp(cg);
                        emit(cg, "  %%t%d = load %s, ptr %s\n", loaded, llvm_type(init_ty), init.buf);
                        emit(cg, "  store %s %%t%d, ptr %%t%d\n", llvm_type(init_ty), loaded, alloca);
                    } else if (is_struct) {
                        /* struct returned by value from a call — store directly */
                        emit(cg, "  store %s %s, ptr %%t%d\n", llvm_type(init_ty), init.buf, alloca);
                    } else if (!is_fail_err && init_ty && init_ty->kind == TY_FAILABLE) {
                        /* val-side of failable destructure: extract field 0, cache aggregate */
                        cg->last_fail_init = s->let.init;
                        cg->last_fail_val  = init;
                        cg->last_fail_ty   = init_ty;
                        const char *fail_llt = llvm_type(init_ty);
                        int vv = new_tmp(cg);
                        emit(cg, "  %%t%d = extractvalue %s %s, 0\n", vv, fail_llt, init.buf);
                        emit(cg, "  store %s %%t%d, ptr %%t%d\n", llt, vv, alloca);
                    } else if (is_fail_err && init_ty && init_ty->kind == TY_FAILABLE) {
                        /* err-side without cache (standalone failable let) */
                        const char *fail_llt = llvm_type(init_ty);
                        int ev = new_tmp(cg);
                        emit(cg, "  %%t%d = extractvalue %s %s, 1\n", ev, fail_llt, init.buf);
                        emit(cg, "  store i32 %%t%d, ptr %%t%d\n", ev, alloca);
                    } else {
                        const char *store_ty = init_ty ? effective_llvm_type(cg, init_ty) : llt;
                        /* coerce type if alloca type differs from init type */
                        if (strcmp(store_ty, llt) != 0) {
                            /* float width coercion: double → float */
                            if (!strcmp(store_ty,"double") && !strcmp(llt,"float")) {
                                int ct = new_tmp(cg);
                                emit(cg, "  %%t%d = fptrunc double %s to float\n", ct, init.buf);
                                init = val_tmp(ct);
                                store_ty = llt;
                            /* integer width coercion */
                            } else {
                                int sv = 0, lv = 0;
                                if (!strcmp(store_ty,"i8"))  sv=8; else if (!strcmp(store_ty,"i16")) sv=16;
                                else if (!strcmp(store_ty,"i32")) sv=32; else if (!strcmp(store_ty,"i64")) sv=64;
                                if (!strcmp(llt,"i8"))  lv=8; else if (!strcmp(llt,"i16")) lv=16;
                                else if (!strcmp(llt,"i32")) lv=32; else if (!strcmp(llt,"i64")) lv=64;
                                if (sv && lv && sv != lv) {
                                    int ct = new_tmp(cg);
                                    int is_signed = init_ty ? type_is_signed(init_ty) : 0;
                                    const char *op = (lv < sv) ? "trunc"
                                                   : (is_signed ? "sext" : "zext");
                                    emit(cg, "  %%t%d = %s %s %s to %s\n",
                                         ct, op, store_ty, init.buf, llt);
                                    init = val_tmp(ct);
                                    store_ty = llt;
                                }
                            }
                        }
                        emit(cg, "  store %s %s, ptr %%t%d\n", store_ty, init.buf, alloca);
                    }
                }
            }

            /* register symbol — store the alloca name */
            char llvm_name[32];
            snprintf(llvm_name, sizeof(llvm_name), "%%t%d", alloca);
            const char *sym_llvm = arena_strdup(cg->arena, llvm_name);
            /* error variable of a failable destructure: register as i32, not !T */
            Type *sym_ty = s->let.ty;
            if (is_fail_err) {
                sym_ty = ARENA_NEW(cg->arena, Type);
                sym_ty->kind = TY_I32;
            }
            define_sym(cg, s->let.name, sym_llvm, 0, sym_ty);

            /* ^T local: register for auto-drop at scope exit */
            if (s->let.ty && s->let.ty->kind == TY_SMART_PTR)
                register_rc_drop(cg, sym_llvm);
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
                    /* coerce rhs integer width to match lhs type */
                    if (vty && strcmp(llvm_type(vty), llt) != 0) {
                        int sv4 = 0, lv4 = 0;
                        const char *src_llt4 = llvm_type(vty);
                        if (!strcmp(src_llt4,"i8"))  sv4=8; else if (!strcmp(src_llt4,"i16")) sv4=16;
                        else if (!strcmp(src_llt4,"i32")) sv4=32; else if (!strcmp(src_llt4,"i64")) sv4=64;
                        if (!strcmp(llt,"i8"))  lv4=8; else if (!strcmp(llt,"i16")) lv4=16;
                        else if (!strcmp(llt,"i32")) lv4=32; else if (!strcmp(llt,"i64")) lv4=64;
                        if (sv4 && lv4 && sv4 != lv4) {
                            int ct4 = new_tmp(cg);
                            const char *op4 = (lv4 < sv4) ? "trunc"
                                            : (type_is_signed(vty) ? "sext" : "zext");
                            emit(cg, "  %%t%d = %s %s %s to %s\n", ct4, op4, src_llt4, rhs.buf, llt);
                            rhs = val_tmp(ct4);
                        }
                    }
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
                /* p.field = val (or self.field = val via auto-deref) */
                Type *obj_ty = NULL;
                Val obj = cg_expr(cg, s->assign.target->field.obj, &obj_ty);
                /* auto-deref: *Struct.field — ptr value already loaded by cg_expr */
                if (obj_ty && obj_ty->kind == TY_PTR && obj_ty->ptr.inner
                        && obj_ty->ptr.inner->kind == TY_NAMED)
                    obj_ty = obj_ty->ptr.inner;
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
            } else if (s->assign.target->kind == EXPR_INDEX) {
                /* xs[i] = val  (or xs[i] op= val) */
                Type *arr_ty = NULL;
                Val arr = cg_expr(cg, s->assign.target->index.arr, &arr_ty);
                Val idx = cg_expr(cg, s->assign.target->index.idx, NULL);

                /* element type from sema annotation */
                Type *elem_ty = s->assign.target->ty;
                const char *elem_llt = elem_ty ? llvm_type(elem_ty) : "i32";

                /* compute pointer to element */
                const char *data_buf = arr.buf;
                if (arr_ty && arr_ty->kind == TY_SLICE) {
                    int dp = new_tmp(cg);
                    emit(cg, "  %%t%d = extractvalue { ptr, i64 } %s, 0\n", dp, arr.buf);
                    char tmp[32];
                    snprintf(tmp, sizeof(tmp), "%%t%d", dp);
                    data_buf = arena_strdup(cg->arena, tmp);
                }
                int ep = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr %s, ptr %s, i64 %s\n",
                     ep, elem_llt, data_buf, idx.buf);

                Type *vty = NULL;
                Val rhs = cg_expr(cg, s->assign.val, &vty);

                if (s->assign.op == ASSIGN_EQ) {
                    emit(cg, "  store %s %s, ptr %%t%d\n", elem_llt, rhs.buf, ep);
                } else {
                    /* compound op: load current, operate, store */
                    int cur = new_tmp(cg);
                    emit(cg, "  %%t%d = load %s, ptr %%t%d\n", cur, elem_llt, ep);
                    int res = new_tmp(cg);
                    switch (s->assign.op) {
                        case ASSIGN_ADD: emit(cg, "  %%t%d = add %s %%t%d, %s\n",  res, elem_llt, cur, rhs.buf); break;
                        case ASSIGN_SUB: emit(cg, "  %%t%d = sub %s %%t%d, %s\n",  res, elem_llt, cur, rhs.buf); break;
                        case ASSIGN_MUL: emit(cg, "  %%t%d = mul %s %%t%d, %s\n",  res, elem_llt, cur, rhs.buf); break;
                        case ASSIGN_DIV: emit(cg, "  %%t%d = sdiv %s %%t%d, %s\n", res, elem_llt, cur, rhs.buf); break;
                        case ASSIGN_MOD: emit(cg, "  %%t%d = srem %s %%t%d, %s\n", res, elem_llt, cur, rhs.buf); break;
                        default:         emit(cg, "  %%t%d = add %s %%t%d, 0\n",   res, elem_llt, cur); break;
                    }
                    emit(cg, "  store %s %%t%d, ptr %%t%d\n", elem_llt, res, ep);
                }
            } else if (s->assign.target->kind == EXPR_DEREF) {
                Type *ptr_ty = NULL;
                Val ptr = cg_expr(cg, s->assign.target->deref.operand, &ptr_ty);
                Type *inner_ty = (ptr_ty && ptr_ty->kind == TY_PTR) ? ptr_ty->ptr.inner : NULL;
                Type *vty = NULL;
                Val rhs = cg_expr(cg, s->assign.val, &vty);
                /* prefer the pointer's inner type so narrowing stores (e.g. *u8 = 0) use i8 */
                const char *store_ty = inner_ty ? llvm_type(inner_ty)
                                                : (vty ? llvm_type(vty) : "i32");
                /* coerce rhs to store_ty if widths differ */
                if (vty && strcmp(llvm_type(vty), store_ty) != 0) {
                    int sv2 = 0, lv2 = 0;
                    const char *src_llt = llvm_type(vty);
                    if (!strcmp(src_llt,"i8"))  sv2=8; else if (!strcmp(src_llt,"i16")) sv2=16;
                    else if (!strcmp(src_llt,"i32")) sv2=32; else if (!strcmp(src_llt,"i64")) sv2=64;
                    if (!strcmp(store_ty,"i8"))  lv2=8; else if (!strcmp(store_ty,"i16")) lv2=16;
                    else if (!strcmp(store_ty,"i32")) lv2=32; else if (!strcmp(store_ty,"i64")) lv2=64;
                    if (sv2 && lv2 && sv2 != lv2) {
                        int ct = new_tmp(cg);
                        const char *op = (lv2 < sv2) ? "trunc"
                                       : (type_is_signed(vty) ? "sext" : "zext");
                        emit(cg, "  %%t%d = %s %s %s to %s\n", ct, op, src_llt, rhs.buf, store_ty);
                        rhs = val_tmp(ct);
                    }
                }
                emit(cg, "  store %s %s, ptr %s\n", store_ty, rhs.buf, ptr.buf);
            } else if (s->assign.target->kind == EXPR_SMARTDEREF) {
                /* p.^ = val : store into data portion of the RC block */
                Type *pt = NULL;
                Val block = cg_expr(cg, s->assign.target->deref.operand, &pt);
                Type *inner_ty = pt ? pt->ptr.inner : NULL;
                int dp = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr i8, ptr %s, i64 8\n", dp, block.buf);
                Type *vty = NULL;
                Val rhs = cg_expr(cg, s->assign.val, &vty);
                const char *llt = (vty ? vty : inner_ty) ? llvm_type(vty ? vty : inner_ty) : "i32";
                emit(cg, "  store %s %s, ptr %%t%d\n", llt, rhs.buf, dp);
            } else {
                /* generic lvalue — emit rhs at least */
                cg_expr(cg, s->assign.val, NULL);
            }
            break;
        }

        case STMT_RET: {
            /* evaluate return value before flushing defers */
            Val rv = val_str("0");
            /* use declared function return type as the ret instruction type */
            const char *llt = cg->cur_fn_ret ? cg->cur_fn_ret : "void";
            int has_val = s->ret.val != NULL;
            if (has_val) {
                Type *rt = NULL;
                rv = cg_expr(cg, s->ret.val, &rt);
                /* if returning a ^T identifier, exempt it from scope RC drops
                   (ownership is transferred to caller; RC stays at current value) */
                if (rt && rt->kind == TY_SMART_PTR
                        && s->ret.val->kind == EXPR_IDENT) {
                    Symbol *sym = lookup(cg, s->ret.val->ident.name);
                    if (sym) cg->skip_rc_drop = sym->llvm_name;
                }
                /* load struct from alloca when returning named struct by value */
                if (rt && rt->kind == TY_NAMED && !find_enum(cg, rt->named.name)
                        && (s->ret.val->kind == EXPR_IDENT
                            || s->ret.val->kind == EXPR_STRUCT_LIT)) {
                    int loaded = new_tmp(cg);
                    emit(cg, "  %%t%d = load %s, ptr %s\n",
                         loaded, llvm_type(rt), rv.buf);
                    rv = val_tmp(loaded);
                }
                /* auto-wrap plain T as @ok(T) when returning from a !T function */
                if (cg->cur_fn_ret_ty && cg->cur_fn_ret_ty->kind == TY_FAILABLE
                        && rt && rt->kind != TY_FAILABLE) {
                    const char *inner_llt = rt ? effective_llvm_type(cg, rt) : "i32";
                    char fail_llt[256];
                    snprintf(fail_llt, sizeof(fail_llt), "{ %s, i32 }", inner_llt);
                    int f1 = new_tmp(cg);
                    emit(cg, "  %%t%d = insertvalue %s undef, %s %s, 0\n",
                         f1, fail_llt, inner_llt, rv.buf);
                    int f2 = new_tmp(cg);
                    emit(cg, "  %%t%d = insertvalue %s %%t%d, i32 0, 1\n", f2, fail_llt, f1);
                    rv = val_tmp(f2);
                }
                /* coerce integer literals to declared return width if needed */
                else if (rt && llt && strcmp(llvm_type(rt), llt) != 0) {
                    const char *val_llt = llvm_type(rt);
                    /* integer widening: sext or zext */
                    int val_is_int = (strcmp(val_llt,"i8")==0||strcmp(val_llt,"i16")==0||
                                      strcmp(val_llt,"i32")==0||strcmp(val_llt,"i64")==0);
                    int ret_is_int = (strcmp(llt,"i8")==0||strcmp(llt,"i16")==0||
                                      strcmp(llt,"i32")==0||strcmp(llt,"i64")==0||
                                      strcmp(llt,"i64")==0);
                    if (val_is_int && ret_is_int) {
                        int t = new_tmp(cg);
                        int is_signed = (rt->kind==TY_I8||rt->kind==TY_I16||rt->kind==TY_I32||rt->kind==TY_I64);
                        if (is_signed)
                            emit(cg, "  %%t%d = sext %s %s to %s\n", t, val_llt, rv.buf, llt);
                        else
                            emit(cg, "  %%t%d = zext %s %s to %s\n", t, val_llt, rv.buf, llt);
                        rv = val_tmp(t);
                    }
                }
            }
            /* flush all scopes: defers first, then RC drops */
            for (Scope *sc = cg->scope; sc; sc = sc->parent) {
                emit_defers_for_scope(cg, sc);
                emit_rc_drops_for_scope(cg, sc);
            }
            cg->skip_rc_drop = NULL;
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
                /* for v => collection  /  for i, v => collection */
                Type *iter_ty = NULL;
                Val iter = cg_expr(cg, fc->iter, &iter_ty);

                const char *elem_llt = "i8";
                Type *elem_ty = NULL;
                int data_t = new_tmp(cg);
                int len_t  = new_tmp(cg);

                if (iter_ty && iter_ty->kind == TY_ARRAY) {
                    /* fixed array: data ptr = GEP to element 0; length = compile-time constant */
                    elem_ty  = iter_ty->array.inner;
                    elem_llt = elem_ty ? llvm_type(elem_ty) : "i8";
                    int64_t arr_n = 0;
                    if (iter_ty->array.size && iter_ty->array.size->kind == EXPR_INT)
                        arr_n = (int64_t)iter_ty->array.size->ival;
                    emit(cg, "  %%t%d = getelementptr [%" PRId64 " x %s], ptr %s, i32 0, i32 0\n",
                         data_t, arr_n, elem_llt, iter.buf);
                    emit(cg, "  %%t%d = add i64 0, %" PRId64 "\n", len_t, arr_n);
                } else {
                    /* slice / str: fat pointer { ptr, i64 } */
                    if (iter_ty && (iter_ty->kind == TY_SLICE || iter_ty->kind == TY_STR)) {
                        elem_ty  = (iter_ty->kind == TY_SLICE) ? iter_ty->ptr.inner : NULL;
                        elem_llt = elem_ty ? llvm_type(elem_ty) : "ptr";
                    }
                    emit(cg, "  %%t%d = extractvalue { ptr, i64 } %s, 0\n", data_t, iter.buf);
                    emit(cg, "  %%t%d = extractvalue { ptr, i64 } %s, 1\n", len_t, iter.buf);
                }

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

            /* check if subject is a tagged union */
            UnionInfoCG *ui = NULL;
            if (val_ty && val_ty->kind == TY_NAMED)
                ui = find_union(cg, val_ty->named.name);

            if (ui) {
                /* tagged union: load the tag (field 0) from the union alloca */
                int tag_ptr = new_tmp(cg);
                emit(cg, "  %%t%d = getelementptr %%%s, ptr %s, i32 0, i32 0\n",
                     tag_ptr, ui->name, val.buf);
                int tag_val = new_tmp(cg);
                emit(cg, "  %%t%d = load i32, ptr %%t%d\n", tag_val, tag_ptr);

                for (size_t i = 0; i < s->when.arms.len; i++) {
                    WhenArm *arm = &s->when.arms.data[i];
                    int body_l = new_label(cg);
                    int next_l = (i + 1 < s->when.arms.len) ? new_label(cg) : end_l;

                    /* build condition: OR of all pattern discriminant comparisons */
                    int cond_t = -1;
                    Type *matched_payload_ty = NULL;
                    int   matched_disc       = -1;
                    for (size_t pi = 0; pi < arm->pats.len; pi++) {
                        Expr *pat = arm->pats.data[pi];
                        if (pat->kind == EXPR_DISCARD) {
                            int wc = new_tmp(cg);
                            emit(cg, "  %%t%d = add i1 0, 1\n", wc);
                            cond_t = wc;
                            break;
                        }
                        if (pat->kind == EXPR_IDENT && pat->ident.name[0] == '.') {
                            const char *vname = pat->ident.name + 1;
                            int disc = 0;
                            Type *pty = NULL;
                            for (size_t vi = 0; vi < ui->n_variants; vi++) {
                                if (!strcmp(ui->variants[vi].name, vname)) {
                                    disc = (int)vi;
                                    pty  = ui->variants[vi].ty;
                                    break;
                                }
                            }
                            if (matched_disc < 0) {
                                matched_disc       = disc;
                                matched_payload_ty = pty;
                            }
                            int cmp = new_tmp(cg);
                            emit(cg, "  %%t%d = icmp eq i32 %%t%d, %d\n",
                                 cmp, tag_val, disc);
                            if (cond_t < 0) {
                                cond_t = cmp;
                            } else {
                                int or_t = new_tmp(cg);
                                emit(cg, "  %%t%d = or i1 %%t%d, %%t%d\n",
                                     or_t, cond_t, cmp);
                                cond_t = or_t;
                            }
                        }
                    }
                    if (cond_t < 0) {
                        int wc = new_tmp(cg);
                        emit(cg, "  %%t%d = add i1 0, 1\n", wc);
                        cond_t = wc;
                    }

                    emit_br(cg, "  br i1 %%t%d, label %%l%d, label %%l%d\n",
                            cond_t, body_l, next_l);
                    emit_label(cg, body_l);
                    push_scope(cg);

                    /* bind payload if arm has a name and the variant has a payload */
                    if (arm->bind && matched_payload_ty && ui->payload_size > 0) {
                        int pay_ptr = new_tmp(cg);
                        emit(cg, "  %%t%d = getelementptr %%%s, ptr %s, i32 0, i32 1\n",
                             pay_ptr, ui->name, val.buf);
                        char pay_buf[32];
                        snprintf(pay_buf, sizeof(pay_buf), "%%t%d", pay_ptr);
                        const char *bind_llvm;
                        if (matched_payload_ty->kind == TY_NAMED
                                && find_struct(cg, matched_payload_ty->named.name)) {
                            /* struct payload: use the GEP ptr directly (struct-as-alloca) */
                            bind_llvm = arena_strdup(cg->arena, pay_buf);
                        } else {
                            /* scalar payload: alloca + load + store */
                            const char *pay_llt = llvm_type(matched_payload_ty);
                            int bind_alloca = new_tmp(cg);
                            emit(cg, "  %%t%d = alloca %s\n", bind_alloca, pay_llt);
                            int pay_val = new_tmp(cg);
                            emit(cg, "  %%t%d = load %s, ptr %%t%d\n",
                                 pay_val, pay_llt, pay_ptr);
                            emit(cg, "  store %s %%t%d, ptr %%t%d\n",
                                 pay_llt, pay_val, bind_alloca);
                            char bind_buf[32];
                            snprintf(bind_buf, sizeof(bind_buf), "%%t%d", bind_alloca);
                            bind_llvm = arena_strdup(cg->arena, bind_buf);
                        }
                        define_sym(cg, arm->bind, bind_llvm, 0, matched_payload_ty);
                    }

                    cg_stmt(cg, arm->body);
                    pop_scope(cg);
                    emit_br(cg, "  br label %%l%d\n", end_l);
                    if (next_l != end_l) emit_label(cg, next_l);
                }
            } else {
                /* regular when: integer/enum/bool pattern matching */
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
                    if (cond_t < 0) {
                        int wc = new_tmp(cg);
                        emit(cg, "  %%t%d = add i1 0, 1\n", wc);
                        cond_t = wc;
                    }

                    emit_br(cg, "  br i1 %%t%d, label %%l%d, label %%l%d\n",
                            cond_t, body_l, next_l);
                    emit_label(cg, body_l);
                    push_scope(cg);
                    cg_stmt(cg, arm->body);
                    pop_scope(cg);
                    emit_br(cg, "  br label %%l%d\n", end_l);
                    if (next_l != end_l) emit_label(cg, next_l);
                }
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
            /* failable destructure: two lets where second has !T — no new scope */
            int is_fail = (s->block.len == 2
                && s->block.data[0]->kind == STMT_LET
                && s->block.data[1]->kind == STMT_LET
                && s->block.data[1]->let.ty
                && s->block.data[1]->let.ty->kind == TY_FAILABLE);
            if (!is_fail) push_scope(cg);
            for (size_t i = 0; i < s->block.len; i++)
                cg_stmt(cg, s->block.data[i]);
            if (!is_fail) pop_scope(cg);
            break;
        }

        case STMT_BREAK: {
            Scope *loop_sc = NULL;
            for (Scope *sc = cg->scope; sc; sc = sc->parent) {
                emit_defers_for_scope(cg, sc);
                emit_rc_drops_for_scope(cg, sc);
                if (sc->is_loop) { loop_sc = sc; break; }
            }
            if (loop_sc)
                emit_br(cg, "  br label %%l%d\n", loop_sc->break_label);
            break;
        }

        case STMT_CONTINUE: {
            Scope *loop_sc = NULL;
            for (Scope *sc = cg->scope; sc; sc = sc->parent) {
                emit_defers_for_scope(cg, sc);
                emit_rc_drops_for_scope(cg, sc);
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

    cg->cur_fn_ret    = ret_llt;
    cg->cur_fn_ret_ty = item->fn.ret;
    cg->cur_label     = -1; /* -1 = entry block */
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
        const char *sym_llvm = arena_strdup(cg->arena, llvm_name);
        define_sym(cg, par->name, sym_llvm, 0, par->ty);
        /* ^T param: increment RC at entry (caller retains its ref), auto-drop at exit */
        if (par->ty && par->ty->kind == TY_SMART_PTR) {
            char param_llvm[128];
            snprintf(param_llvm, sizeof(param_llvm), "%%%s", par->name);
            emit_rc_inc(cg, param_llvm);
            register_rc_drop(cg, sym_llvm);
        }
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

/* Names declared in the preamble — skip re-declaration from extern fn */
static const char *g_preamble_decls[] = {
    "printf", "fprintf", "sprintf", "atoi", "strlen",
    "malloc", "realloc", "free", "exit", "fgets",
    NULL
};
static int is_preamble_decl(const char *name) {
    for (const char **p = g_preamble_decls; *p; p++)
        if (!strcmp(name, *p)) return 1;
    return 0;
}

static int mark_declared(CG *cg, const char *name) {
    for (size_t i = 0; i < cg->n_declared_fns; i++)
        if (!strcmp(cg->declared_fns[i], name)) return 0; /* already done */
    if (cg->n_declared_fns < 512)
        cg->declared_fns[cg->n_declared_fns++] = name;
    return 1; /* first time */
}

static void cg_extern_fn(CG *cg, Item *item) {
    const char *c_name = item->extern_fn.c_name ? item->extern_fn.c_name : item->name;
    /* skip if this exact declaration (by mangled name) was already emitted */
    if (!mark_declared(cg, item->name)) return;
    if (is_preamble_decl(c_name)) {
        /* preamble already declares the real C function; just emit a wrapper alias */
        if (item->extern_fn.c_name) {
            const char *ret_llt = item->extern_fn.ret ? llvm_type(item->extern_fn.ret) : "void";
            int is_void = !strcmp(ret_llt, "void");
            /* emit define wrapper: alias__fn(...) -> fn(...) */
            emit(cg, "define %s @%s(", ret_llt, item->name);
            for (size_t i = 0; i < item->extern_fn.params.len; i++) {
                if (i) emit(cg, ", ");
                emit(cg, "%s %%p%zu", llvm_type(item->extern_fn.params.data[i].ty), i);
            }
            emit(cg, ") {\nentry:\n");
            if (is_void) {
                emit(cg, "  call void @%s(", c_name);
            } else {
                emit(cg, "  %%r = call %s @%s(", ret_llt, c_name);
            }
            for (size_t i = 0; i < item->extern_fn.params.len; i++) {
                if (i) emit(cg, ", ");
                emit(cg, "%s %%p%zu", llvm_type(item->extern_fn.params.data[i].ty), i);
            }
            if (item->extern_fn.variadic) {
                /* variadic wrappers aren't straightforward; just call the C fn directly */
            }
            emit(cg, ")\n");
            if (is_void) emit(cg, "  ret void\n}\n");
            else         emit(cg, "  ret %s %%r\n}\n", ret_llt);
        }
        return;
    }
    const char *ret_llt = item->extern_fn.ret ? llvm_type(item->extern_fn.ret) : "void";

    if (item->extern_fn.c_name) {
        /* Imported extern fn: declare C function + emit thin wrapper with mangled name */
        /* First declare the original C function (if not already declared) */
        if (mark_declared(cg, c_name)) {
            emit(cg, "declare %s @%s(", ret_llt, c_name);
            for (size_t i = 0; i < item->extern_fn.params.len; i++) {
                if (i) emit(cg, ", ");
                emit(cg, "%s", llvm_type(item->extern_fn.params.data[i].ty));
            }
            if (item->extern_fn.variadic) {
                if (item->extern_fn.params.len) emit(cg, ", ");
                emit(cg, "...");
            }
            emit(cg, ")\n");
        }
        /* Then emit a thin define wrapper with the mangled name */
        int is_void = !strcmp(ret_llt, "void");
        emit(cg, "define %s @%s(", ret_llt, item->name);
        for (size_t i = 0; i < item->extern_fn.params.len; i++) {
            if (i) emit(cg, ", ");
            emit(cg, "%s %%p%zu", llvm_type(item->extern_fn.params.data[i].ty), i);
        }
        if (item->extern_fn.variadic) {
            if (item->extern_fn.params.len) emit(cg, ", ");
            emit(cg, "...");
        }
        emit(cg, ") {\nentry:\n");
        if (is_void) {
            emit(cg, "  call void @%s(", c_name);
        } else {
            emit(cg, "  %%r = call %s @%s(", ret_llt, c_name);
        }
        for (size_t i = 0; i < item->extern_fn.params.len; i++) {
            if (i) emit(cg, ", ");
            emit(cg, "%s %%p%zu", llvm_type(item->extern_fn.params.data[i].ty), i);
        }
        emit(cg, ")\n");
        if (is_void) emit(cg, "  ret void\n}\n");
        else         emit(cg, "  ret %s %%r\n}\n", ret_llt);
        return;
    }

    /* Normal extern fn declaration */
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
    /* LLVM syntax: @name = [constant|global] type value */
    const char *linkage = item->global.mutable ? "global" : "constant";

    /* str globals: emit a { ptr, i64 } constant using a string literal */
    if (item->global.ty && item->global.ty->kind == TY_STR
            && item->global.init && item->global.init->kind == EXPR_STR) {
        const char *sv = item->global.init->sval;
        size_t slen = strlen(sv);
        int sid = intern_str(cg, sv);
        emit(cg, "@%s = %s { ptr, i64 } { ptr getelementptr inbounds ([%zu x i8], ptr @.str.%d, i32 0, i32 0), i64 %zu }\n",
             item->name, linkage, slen + 1, sid, slen);
        char llvm_name[128];
        snprintf(llvm_name, sizeof(llvm_name), "@%s", item->name);
        define_sym(cg, item->name, arena_strdup(cg->arena, llvm_name), 1, item->global.ty);
        return;
    }

    emit(cg, "@%s = %s %s ", item->name, linkage, llt);
    if (item->global.init) {
        switch (item->global.init->kind) {
            case EXPR_INT:  emit(cg, "%" PRIu64, item->global.init->ival); break;
            case EXPR_FLOAT: {
                union { double d; uint64_t u; } bits; bits.d = item->global.init->fval;
                emit(cg, "0x%016" PRIX64, bits.u);
                break;
            }
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

int codegen(Module *mod, FILE *out, int release) {
    CG cg = {0};
    cg.out     = out;
    cg.arena   = mod->arena;
    cg.release = release;

    /* global scope */
    Scope global_scope = {0};
    cg.scope = &global_scope;

    /* first pass: collect externs and globals into scope, emit their IR */
    emit(&cg, "; Perzephxne LLVM IR\n");
    emit(&cg, "target triple = \"x86_64-pc-linux-gnu\"\n\n");

    /* standard declarations always needed */
    emit(&cg, "declare i32 @printf(ptr noundef, ...)\n");
    emit(&cg, "declare i32 @fprintf(ptr, ptr noundef, ...)\n");
    emit(&cg, "declare i32 @sprintf(ptr, ptr, ...)\n");
    emit(&cg, "declare i32 @atoi(ptr)\n");
    emit(&cg, "declare i64 @atol(ptr)\n");
    emit(&cg, "declare double @atof(ptr)\n");
    emit(&cg, "declare i64 @strtol(ptr, ptr, i32)\n");
    emit(&cg, "declare double @strtod(ptr, ptr)\n");
    emit(&cg, "declare i32 @strcmp(ptr, ptr)\n");
    emit(&cg, "declare i64 @strlen(ptr)\n");
    emit(&cg, "declare ptr @malloc(i64)\n");
    emit(&cg, "declare ptr @realloc(ptr, i64)\n");
    emit(&cg, "declare void @free(ptr)\n");
    emit(&cg, "declare void @exit(i32)\n");
    emit(&cg, "declare ptr @fgets(ptr, i32, ptr)\n");
    emit(&cg, "declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)\n");
    emit(&cg, "declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1)\n");
    emit(&cg, "declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)\n");
    emit(&cg, "declare double @llvm.sqrt.f64(double)\n");
    emit(&cg, "@stdin  = external global ptr\n");
    emit(&cg, "@stderr = external global ptr\n\n");

    /* globals for @args support */
    emit(&cg, "@__przp_argc = internal global i32 0\n");
    emit(&cg, "@__przp_argv = internal global ptr null\n\n");

    /* format string constants */
    emit(&cg, "@.fmt.d = private constant [3 x i8] c\"%%d\\00\"\n");
    emit(&cg, "@.fmt.f = private constant [3 x i8] c\"%%f\\00\"\n\n");

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
        if (item->struct_.n_type_params > 0) continue; /* skip generic template */
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

    /* tagged union type declarations and layout table
       Layout: { i32 tag, [N x i8] payload } where N = max variant payload size */
    for (size_t i = 0; i < mod->items.len; i++) {
        Item *item = mod->items.data[i];
        if (item->kind != ITEM_UNION || !item->union_.tagged) continue;
        /* compute max payload size */
        int max_payload = 0;
        for (size_t j = 0; j < item->union_.fields.len; j++) {
            Type *fty = item->union_.fields.data[j].ty;
            if (fty) {
                int sz = cg_type_byte_size(&cg, fty);
                if (sz > max_payload) max_payload = sz;
            }
        }
        /* build variant table */
        size_t nv = item->union_.fields.len;
        UnionVariantCG *vars = ARENA_ALLOC(cg.arena, UnionVariantCG, nv);
        for (size_t j = 0; j < nv; j++) {
            vars[j].name = item->union_.fields.data[j].name;
            vars[j].ty   = item->union_.fields.data[j].ty;
        }
        UnionInfoCG *ui  = ARENA_NEW(cg.arena, UnionInfoCG);
        ui->name         = item->name;
        ui->n_variants   = nv;
        ui->variants     = vars;
        ui->payload_size = max_payload;
        ui->next         = cg.unions;
        cg.unions        = ui;
        /* emit LLVM named type */
        if (max_payload > 0)
            emit(&cg, "%%%s = type { i32, [%d x i8] }\n", item->name, max_payload);
        else
            emit(&cg, "%%%s = type { i32 }\n", item->name);
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
    int main_returns_i32 = 0;
    for (size_t i = 0; i < mod->items.len; i++) {
        Item *item = mod->items.data[i];
        if (item->kind == ITEM_FN) {
            if (item->fn.n_type_params > 0) continue; /* skip generic template */
            if (!strcmp(item->name, "main")) {
                has_main = 1;
                item->name = "__przp_main";
                /* check if user's main has an explicit i32 return */
                Type *ret = item->fn.ret;
                if (ret && ret->kind == TY_I32) main_returns_i32 = 1;
            }
            cg_fn(&cg, item);
            if (!strcmp(item->name, "__przp_main")) item->name = "main"; /* restore */
        }
    }

    /* impl methods — names already mangled by sema's register_item pass */
    for (size_t i = 0; i < mod->items.len; i++) {
        Item *item = mod->items.data[i];
        if (item->kind != ITEM_IMPL) continue;
        for (size_t j = 0; j < item->impl.methods.len; j++) {
            Item *m = item->impl.methods.data[j];
            if (m->kind == ITEM_FN) cg_fn(&cg, m);
        }
    }

    /* emit a real C main that stores argc/argv then calls __przp_main */
    if (has_main) {
        if (main_returns_i32) {
            emit(&cg,
                "define i32 @main(i32 %%argc, ptr %%argv) {\n"
                "entry:\n"
                "  store i32 %%argc, ptr @__przp_argc\n"
                "  store ptr %%argv, ptr @__przp_argv\n"
                "  %%r = call i32 @__przp_main()\n"
                "  ret i32 %%r\n"
                "}\n\n");
        } else {
            emit(&cg,
                "define i32 @main(i32 %%argc, ptr %%argv) {\n"
                "entry:\n"
                "  store i32 %%argc, ptr @__przp_argc\n"
                "  store ptr %%argv, ptr @__przp_argv\n"
                "  call void @__przp_main()\n"
                "  ret i32 0\n"
                "}\n\n");
        }
    }

    /* string constants */
    emit(&cg, "\n");
    emit_str_constants(&cg);

    return !cg.had_error;
}
