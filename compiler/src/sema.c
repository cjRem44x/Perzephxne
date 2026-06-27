#include "sema.h"
#include "error.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

/* ── Symbol table ─────────────────────────────────────────────────────────── */

typedef struct Sym {
    struct Sym *next;
    const char *name;
    Type       *ty;
    int         is_mut;    /* 0 = immutable (imu), 1 = mutable */
    int         is_type;   /* entry is a type name (struct/enum/alias) */
} Sym;

typedef struct Scope {
    struct Scope *parent;
    Sym          *syms;
} Scope;

/* ── Struct field table ───────────────────────────────────────────────────── */

typedef struct StructEntry {
    struct StructEntry *next;
    const char         *name;
    FieldList          *fields; /* points into the Item's field list */
} StructEntry;

/* ── Enum variant table ───────────────────────────────────────────────────── */

typedef struct { const char *name; int64_t value; } EnumVariantVal;

typedef struct EnumInfo {
    struct EnumInfo *next;
    const char      *name;
    Type            *backing_ty;
    size_t           n_variants;
    EnumVariantVal  *variants;
} EnumInfo;

/* ── Tagged union variant table ───────────────────────────────────────────── */

typedef struct { const char *name; Type *ty; } UnionVariantInfo; /* ty=NULL → unit variant */

typedef struct UnionInfo {
    struct UnionInfo *next;
    const char       *name;
    size_t            n_variants;
    UnionVariantInfo *variants;
} UnionInfo;

/* ── Generic template table ───────────────────────────────────────────────── */

typedef struct GenericTemplate {
    struct GenericTemplate *next;
    Item       *item;
    const char *name;
} GenericTemplate;

/* ── Sema context ─────────────────────────────────────────────────────────── */

typedef struct {
    Arena            *arena;
    Scope            *scope;
    Type             *cur_ret;   /* return type of the function being checked */
    int               errors;
    StructEntry      *structs;   /* name → field list for struct lookup */
    EnumInfo         *enums;     /* name → variant values for enum lookup */
    UnionInfo        *unions;    /* name → tagged union variant table */
    GenericTemplate  *generics;  /* uninstantiated generic templates */
    /* built-in types — interned once */
    Type   *ty_void, *ty_bool, *ty_i8, *ty_i16, *ty_i32, *ty_i64;
    Type   *ty_u8,   *ty_u16,  *ty_u32, *ty_u64, *ty_usize;
    Type   *ty_f16,  *ty_f32,  *ty_f64;
    Type   *ty_str,  *ty_char, *ty_any;
} Sema;

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static Type *make_ty(Sema *s, TypeKind k) {
    Type *t = ARENA_NEW(s->arena, Type);
    t->kind = k;
    return t;
}

static Type *make_ptr(Sema *s, TypeKind k, Type *inner) {
    Type *t = make_ty(s, k);
    t->ptr.inner = inner;
    return t;
}

static void sema_error(Sema *s, Span span, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    error_at(span, "%s", buf);
    s->errors++;
}

/* ── Scope management ─────────────────────────────────────────────────────── */

static void push_scope(Sema *s) {
    Scope *sc = ARENA_NEW(s->arena, Scope);
    sc->parent = s->scope;
    s->scope   = sc;
}

static void pop_scope(Sema *s) {
    s->scope = s->scope->parent;
}

static void define(Sema *s, Span span, const char *name, Type *ty, int is_mut, int is_type) {
    /* check for redefinition in the same scope */
    for (Sym *sym = s->scope->syms; sym; sym = sym->next) {
        if (!strcmp(sym->name, name)) {
            sema_error(s, span, "redefinition of '%s'", name);
            return;
        }
    }
    Sym *sym   = ARENA_NEW(s->arena, Sym);
    sym->name  = name;
    sym->ty    = ty;
    sym->is_mut = is_mut;
    sym->is_type = is_type;
    sym->next  = s->scope->syms;
    s->scope->syms = sym;
}

static Sym *lookup(Sema *s, const char *name) {
    for (Scope *sc = s->scope; sc; sc = sc->parent)
        for (Sym *sym = sc->syms; sym; sym = sym->next)
            if (!strcmp(sym->name, name)) return sym;
    return NULL;
}

/* ── Type utilities ───────────────────────────────────────────────────────── */

static int ty_eq(Type *a, Type *b) {
    if (!a || !b) return a == b;
    if (a->kind != b->kind) return 0;
    switch (a->kind) {
        case TY_PTR:
        case TY_SMART_PTR:
        case TY_SLICE:
        case TY_FAILABLE:
            return ty_eq(a->ptr.inner, b->ptr.inner);
        case TY_ARRAY:
            return ty_eq(a->array.inner, b->array.inner);
        case TY_NAMED:
        case TY_GENERIC:
            return !strcmp(a->named.name, b->named.name);
        default:
            return 1;
    }
}

static int ty_is_int(Type *t) {
    if (!t) return 0;
    switch (t->kind) {
        case TY_I8: case TY_I16: case TY_I32: case TY_I64:
        case TY_U8: case TY_U16: case TY_U32: case TY_U64:
        case TY_USIZE: case TY_CHAR:
            return 1;
        default: return 0;
    }
}

static int ty_is_float(Type *t) {
    if (!t) return 0;
    return t->kind == TY_F16 || t->kind == TY_F32 || t->kind == TY_F64;
}

static int ty_is_numeric(Type *t) {
    return ty_is_int(t) || ty_is_float(t);
}

static int ty_is_ptr(Type *t) {
    if (!t) return 0;
    return t->kind == TY_PTR || t->kind == TY_SMART_PTR;
}

static const char *ty_str(Type *t) {
    if (!t) return "void";
    switch (t->kind) {
        case TY_VOID:      return "void";
        case TY_BOOL:      return "bool";
        case TY_CHAR:      return "char";
        case TY_STR:       return "str";
        case TY_ANY:       return "any";
        case TY_I8:        return "i8";
        case TY_I16:       return "i16";
        case TY_I32:       return "i32";
        case TY_I64:       return "i64";
        case TY_U8:        return "u8";
        case TY_U16:       return "u16";
        case TY_U32:       return "u32";
        case TY_U64:       return "u64";
        case TY_USIZE:     return "usize";
        case TY_F16:       return "f16";
        case TY_F32:       return "f32";
        case TY_F64:       return "f64";
        case TY_PTR:       return "*<T>";
        case TY_SMART_PTR: return "^<T>";
        case TY_SLICE:     return "[]<T>";
        case TY_ARRAY:     return "[N]<T>";
        case TY_FAILABLE:  return "!<T>";
        case TY_NAMED:
        case TY_GENERIC:   return t->named.name;
        default:           return "?";
    }
}

/* resolve a TY_NAMED by looking it up in the scope as a type */
static Type *resolve_named(Sema *s, Type *ty) {
    if (!ty || ty->kind != TY_NAMED) return ty;
    Sym *sym = lookup(s, ty->named.name);
    if (!sym || !sym->is_type) return ty; /* leave unresolved; error elsewhere */
    return sym->ty;
}

/* coercion: can `from` be used where `to` is expected? */
static int ty_coerces(Type *from, Type *to) {
    if (!from || !to) return 1; /* unknown types pass silently */
    if (ty_eq(from, to)) return 1;
    /* integer literals (i32 default) coerce to any integer width */
    if (from->kind == TY_I32 && ty_is_int(to)) return 1;
    /* f64 default coerces to any float */
    if (from->kind == TY_F64 && ty_is_float(to)) return 1;
    /* null coerces to any pointer */
    if (from->kind == TY_PTR && !from->ptr.inner && ty_is_ptr(to)) return 1;
    /* enum ↔ integer: integer types coerce into enum named types and vice versa */
    if (ty_is_int(from) && to->kind   == TY_NAMED) return 1;
    if (from->kind == TY_NAMED && ty_is_int(to))   return 1;
    /* T coerces to !T (auto-wrap as @ok on return) */
    if (to->kind == TY_FAILABLE && ty_coerces(from, to->ptr.inner)) return 1;
    /* !T coerces to T (extract value part in failable destructure) */
    if (from->kind == TY_FAILABLE && ty_coerces(from->ptr.inner, to)) return 1;
    return 0;
}

/* ── Type resolution ──────────────────────────────────────────────────────── */

/* Walk a parsed Type and resolve NAMED types; fill in defaults. Returns ty. */
static Type *check_type(Sema *s, Type *ty) {
    if (!ty) return NULL;
    switch (ty->kind) {
        case TY_PTR:
        case TY_SMART_PTR:
        case TY_SLICE:
        case TY_FAILABLE:
            ty->ptr.inner = check_type(s, ty->ptr.inner);
            break;
        case TY_ARRAY:
            ty->array.inner = check_type(s, ty->array.inner);
            break;
        case TY_FN:
            for (size_t i = 0; i < ty->fn.params.len; i++)
                ty->fn.params.data[i] = check_type(s, ty->fn.params.data[i]);
            ty->fn.ret = check_type(s, ty->fn.ret);
            break;
        case TY_NAMED: {
            Sym *sym = lookup(s, ty->named.name);
            if (sym && sym->is_type) return sym->ty;
            /* leave as NAMED — may be a generic param */
            break;
        }
        default:
            break;
    }
    return ty;
}

/* ── Built-in type of @builtin expressions ──────────────────────────────── */

static Type *builtin_ret_ty(Sema *s, const char *name) {
    if (!strcmp(name, "pf") || !strcmp(name, "epf")) return s->ty_i32;
    if (!strcmp(name, "fmt"))                         return s->ty_str;
    if (!strcmp(name, "cin"))                         return s->ty_str;
    if (!strcmp(name, "exit"))                        return s->ty_void;
    if (!strcmp(name, "panic"))                       return s->ty_void;
    if (!strcmp(name, "assert"))                      return s->ty_void;
    if (!strcmp(name, "alo"))                         return make_ptr(s, TY_PTR, NULL);
    if (!strcmp(name, "free"))                        return s->ty_void;
    if (!strcmp(name, "size") || !strcmp(name, "align")) return s->ty_usize;
    if (!strcmp(name, "len"))                         return s->ty_usize;
    if (!strcmp(name, "min") || !strcmp(name, "max")) return NULL; /* inferred from args */
    if (!strcmp(name, "abs"))                         return NULL;
    if (!strcmp(name, "sqrt"))                        return s->ty_f64;
    if (!strcmp(name, "zeroed"))                      return NULL; /* inferred from arg */
    if (!strcmp(name, "memcpy") || !strcmp(name, "memset")) return s->ty_void;
    if (!strcmp(name, "debug") || !strcmp(name, "release")) return s->ty_bool;
    if (!strcmp(name, "offsetof"))  return s->ty_usize;
    if (!strcmp(name, "typeof"))    return s->ty_str;
    if (!strcmp(name, "os.linux") || !strcmp(name, "os.windows") ||
        !strcmp(name, "os.mac"))                      return s->ty_bool;
    if (!strncmp(name, "arch.", 5))                   return s->ty_bool;
    if (!strcmp(name, "args")) {
        Type *sl = make_ptr(s, TY_SLICE, s->ty_str);
        return sl;
    }
    if (!strcmp(name, "str_raw"))  return s->ty_str;
    return NULL;
}

/* ── Expression checker ───────────────────────────────────────────────────── */

/* forward declaration */
static Type *check_expr(Sema *s, Expr *e);
static void  check_stmt(Sema *s, Stmt *st);

static Type *check_expr(Sema *s, Expr *e) {
    if (!e) return NULL;

    switch (e->kind) {
        case EXPR_INT:    e->ty = s->ty_i32;  break;  /* default int literal */
        case EXPR_FLOAT:  e->ty = s->ty_f64;  break;
        case EXPR_BOOL:   e->ty = s->ty_bool; break;
        case EXPR_CHAR:   e->ty = s->ty_char; break;
        case EXPR_STR:    e->ty = s->ty_str;  break;
        case EXPR_NULL:   e->ty = make_ptr(s, TY_PTR, NULL); break;
        case EXPR_UNDEF:  e->ty = NULL; break; /* any type — caller picks */
        case EXPR_DISCARD:e->ty = NULL; break;

        case EXPR_IDENT: {
            /* primitive type names used as type arguments to builtins (@alo, @zeroed, etc.) */
            static const struct { const char *name; TypeKind k; } type_names[] = {
                {"i8",TY_I8},{"i16",TY_I16},{"i32",TY_I32},{"i64",TY_I64},
                {"u8",TY_U8},{"u16",TY_U16},{"u32",TY_U32},{"u64",TY_U64},
                {"f16",TY_F16},{"f32",TY_F32},{"f64",TY_F64},
                {"usize",TY_USIZE},{"bool",TY_BOOL},{"char",TY_CHAR},{"str",TY_STR},{NULL,0}
            };
            int resolved = 0;
            for (int i = 0; type_names[i].name; i++) {
                if (!strcmp(e->ident.name, type_names[i].name)) {
                    e->ty = make_ty(s, type_names[i].k);
                    resolved = 1;
                    break;
                }
            }
            if (!resolved) {
                Sym *sym = lookup(s, e->ident.name);
                if (!sym) {
                    sema_error(s, e->span, "undefined identifier '%s'", e->ident.name);
                    e->ty = s->ty_i32; /* recover */
                } else {
                    e->ty = sym->ty;
                }
            }
            break;
        }

        case EXPR_BUILTIN: {
            /* @offsetof(T, field) — both args are names, not expressions */
            if (!strcmp(e->builtin.name, "offsetof")) {
                e->ty = s->ty_usize;
                break;
            }
            /* check all args */
            for (size_t i = 0; i < e->builtin.args.len; i++)
                check_expr(s, e->builtin.args.data[i]);
            Type *ret = builtin_ret_ty(s, e->builtin.name);
            if (!strcmp(e->builtin.name, "new") && e->builtin.args.len > 0) {
                /* @new(val: T) → ^T */
                ret = make_ptr(s, TY_SMART_PTR, e->builtin.args.data[0]->ty);
            } else if (!strcmp(e->builtin.name, "clone") && e->builtin.args.len > 0) {
                /* @clone(ptr: ^T) → ^T */
                ret = e->builtin.args.data[0]->ty;
            } else if (!ret && e->builtin.args.len > 0) {
                /* for min/max/abs: inherit first arg type */
                ret = e->builtin.args.data[0]->ty;
            }
            e->ty = ret;
            break;
        }

        case EXPR_CAST: {
            check_expr(s, e->cast.val);
            /* resolve the target type name to a primitive */
            static const struct { const char *name; TypeKind k; } prims[] = {
                {"i8",TY_I8},{"i16",TY_I16},{"i32",TY_I32},{"i64",TY_I64},
                {"u8",TY_U8},{"u16",TY_U16},{"u32",TY_U32},{"u64",TY_U64},
                {"f16",TY_F16},{"f32",TY_F32},{"f64",TY_F64},
                {"usize",TY_USIZE},{"bool",TY_BOOL},{"char",TY_CHAR},
                {"str",TY_STR},{NULL,0}
            };
            e->ty = NULL;
            for (int i = 0; prims[i].name; i++) {
                if (!strcmp(e->cast.ty_name, prims[i].name)) {
                    e->ty = make_ty(s, prims[i].k);
                    break;
                }
            }
            if (!e->ty) {
                /* could be a named type — look it up */
                Sym *sym = lookup(s, e->cast.ty_name);
                if (sym && sym->is_type) e->ty = sym->ty;
                else {
                    sema_error(s, e->span, "unknown cast target type '%s'", e->cast.ty_name);
                    e->ty = s->ty_i32;
                }
            }
            break;
        }

        case EXPR_BINOP: {
            Type *lt = check_expr(s, e->binop.l);
            Type *rt = check_expr(s, e->binop.r);
            /* pointer arithmetic: *T +/- integer → *T */
            int ptr_arith = 0;
            if ((e->binop.op == BINOP_ADD || e->binop.op == BINOP_SUB)) {
                if (lt && lt->kind == TY_PTR && rt && ty_is_int(rt)) {
                    e->ty = lt; ptr_arith = 1;
                } else if (rt && rt->kind == TY_PTR && lt && ty_is_int(lt)) {
                    e->ty = rt; ptr_arith = 1;
                } else if (lt && lt->kind == TY_PTR && rt && rt->kind == TY_PTR) {
                    /* ptr - ptr → usize */
                    e->ty = s->ty_usize; ptr_arith = 1;
                }
            }
            if (!ptr_arith) switch (e->binop.op) {
                case BINOP_EQ: case BINOP_NE:
                case BINOP_LT: case BINOP_GT:
                case BINOP_LE: case BINOP_GE:
                case BINOP_AND: case BINOP_OR:
                    e->ty = s->ty_bool;
                    break;
                case BINOP_RANGE: case BINOP_RANGE_INC:
                    e->ty = NULL; /* range used in for, not a value */
                    break;
                default:
                    /* arithmetic/bitwise: take the "wider" type */
                    if (lt && ty_is_numeric(lt)) e->ty = lt;
                    else if (rt && ty_is_numeric(rt)) e->ty = rt;
                    else e->ty = s->ty_i32;
                    break;
            }
            /* type compatibility check (skip pointer arithmetic cases) */
            if (!ptr_arith && lt && rt && !ty_eq(lt, rt)
                    && !ty_coerces(lt, rt) && !ty_coerces(rt, lt)) {
                switch (e->binop.op) {
                    case BINOP_EQ: case BINOP_NE:
                    case BINOP_LT: case BINOP_GT:
                    case BINOP_LE: case BINOP_GE:
                        sema_error(s, e->span, "type mismatch in comparison: '%s' vs '%s'",
                                   ty_str(lt), ty_str(rt));
                        break;
                    default:
                        sema_error(s, e->span, "type mismatch in binary op: '%s' vs '%s'",
                                   ty_str(lt), ty_str(rt));
                        break;
                }
            }
            break;
        }

        case EXPR_UNOP: {
            Type *ot = check_expr(s, e->unop.operand);
            switch (e->unop.op) {
                case UNOP_NEG:
                    if (ot && !ty_is_numeric(ot))
                        sema_error(s, e->span, "unary '-' on non-numeric type '%s'", ty_str(ot));
                    e->ty = ot;
                    break;
                case UNOP_NOT:
                    if (ot && ot->kind != TY_BOOL)
                        sema_error(s, e->span, "'!' requires bool, got '%s'", ty_str(ot));
                    e->ty = s->ty_bool;
                    break;
                case UNOP_BITNOT:
                    if (ot && !ty_is_int(ot))
                        sema_error(s, e->span, "'~' requires integer type, got '%s'", ty_str(ot));
                    e->ty = ot;
                    break;
                case UNOP_ADDROF:
                    e->ty = make_ptr(s, TY_PTR, ot);
                    break;
            }
            break;
        }

        case EXPR_CALL: {
            Type *callee_ty = check_expr(s, e->call.callee);
            for (size_t i = 0; i < e->call.args.len; i++)
                check_expr(s, e->call.args.data[i]);
            if (callee_ty && callee_ty->kind == TY_FN) {
                /* check arg count (variadic fns accept any number >= param count) */
                int arg_ok = callee_ty->fn.variadic
                             ? (e->call.args.len >= callee_ty->fn.params.len)
                             : (e->call.args.len == callee_ty->fn.params.len);
                if (!arg_ok) {
                    sema_error(s, e->span, "expected %zu arguments, got %zu",
                               callee_ty->fn.params.len, e->call.args.len);
                }
                e->ty = callee_ty->fn.ret;
            } else {
                e->ty = NULL; /* unknown callee type; recover */
            }
            break;
        }

        case EXPR_INDEX: {
            Type *arr_ty = check_expr(s, e->index.arr);
            Type *idx_ty = check_expr(s, e->index.idx);
            if (idx_ty && !ty_is_int(idx_ty))
                sema_error(s, e->span, "array index must be integer, got '%s'", ty_str(idx_ty));
            if (arr_ty) {
                if (arr_ty->kind == TY_ARRAY || arr_ty->kind == TY_SLICE)
                    e->ty = arr_ty->array.inner;
                else if (arr_ty->kind == TY_STR)
                    e->ty = s->ty_char;
                else {
                    sema_error(s, e->span, "cannot index into '%s'", ty_str(arr_ty));
                    e->ty = NULL;
                }
            }
            break;
        }

        case EXPR_FIELD: {
            Type *obj_ty = check_expr(s, e->field.obj);
            obj_ty = resolve_named(s, obj_ty);
            /* auto-deref: *Struct.field transparently accesses the struct's field */
            if (obj_ty && obj_ty->kind == TY_PTR && obj_ty->ptr.inner
                    && obj_ty->ptr.inner->kind == TY_NAMED)
                obj_ty = obj_ty->ptr.inner;
            e->ty = NULL;
            if (obj_ty && obj_ty->kind == TY_NAMED) {
                /* enum variant access: EnumName.Variant */
                for (EnumInfo *ei = s->enums; ei; ei = ei->next) {
                    if (!strcmp(ei->name, obj_ty->named.name)) {
                        for (size_t i = 0; i < ei->n_variants; i++) {
                            if (!strcmp(ei->variants[i].name, e->field.field)) {
                                e->ty = ei->backing_ty;
                                return e->ty;
                            }
                        }
                        sema_error(s, e->span, "enum '%s' has no variant '%s'",
                                   ei->name, e->field.field);
                        return e->ty;
                    }
                }
                /* struct field access */
                for (StructEntry *se = s->structs; se; se = se->next) {
                    if (!strcmp(se->name, obj_ty->named.name)) {
                        for (size_t i = 0; i < se->fields->len; i++) {
                            if (!strcmp(se->fields->data[i].name, e->field.field)) {
                                e->ty = se->fields->data[i].ty;
                                break;
                            }
                        }
                        break;
                    }
                }
                if (!e->ty) {
                    /* impl method lookup: try "StructName__field" */
                    char mangled[256];
                    snprintf(mangled, sizeof(mangled), "%s__%s",
                             obj_ty->named.name, e->field.field);
                    Sym *method_sym = lookup(s, mangled);
                    if (method_sym && method_sym->ty && method_sym->ty->kind == TY_FN) {
                        Type *full = method_sym->ty;
                        size_t np = full->fn.params.len;
                        /* detect static call: obj is a type-name identifier */
                        int is_static = 0;
                        if (e->field.obj->kind == EXPR_IDENT) {
                            Sym *obj_sym = lookup(s, e->field.obj->ident.name);
                            is_static = obj_sym && obj_sym->is_type;
                        }
                        /* build reduced TY_FN: drop self param for instance calls */
                        Type *reduced = make_ty(s, TY_FN);
                        reduced->fn.ret = full->fn.ret;
                        size_t skip = is_static ? 0 : 1;
                        if (np > skip) {
                            reduced->fn.params.len  = np - skip;
                            reduced->fn.params.data = ARENA_ALLOC(s->arena, Type *, np - skip);
                            for (size_t k = skip; k < np; k++)
                                reduced->fn.params.data[k - skip] = full->fn.params.data[k];
                        }
                        e->ty = reduced;
                        /* is_method: 1=instance, 2=static */
                        e->field.is_method    = is_static ? 2 : 1;
                        e->field.mangled_name = arena_strdup(s->arena, mangled);
                    } else {
                        sema_error(s, e->span, "type '%s' has no field or method '%s'",
                                   obj_ty->named.name, e->field.field);
                    }
                }
            } else if (obj_ty && (obj_ty->kind == TY_STR || obj_ty->kind == TY_SLICE)) {
                /* str/slice pseudo-fields: .len -> usize, .data -> *u8 / *T */
                if (!strcmp(e->field.field, "len")) {
                    e->ty = s->ty_usize;
                } else if (!strcmp(e->field.field, "data") || !strcmp(e->field.field, "ptr")) {
                    Type *inner = (obj_ty->kind == TY_STR)
                                  ? make_ty(s, TY_U8)
                                  : (obj_ty->ptr.inner ? obj_ty->ptr.inner : make_ty(s, TY_U8));
                    Type *pt = make_ty(s, TY_PTR); pt->ptr.inner = inner;
                    e->ty = pt;
                } else {
                    sema_error(s, e->span, "type '%s' has no field '%s'",
                               ty_str(obj_ty), e->field.field);
                }
            } else if (obj_ty) {
                sema_error(s, e->span, "field access on non-struct type '%s'", ty_str(obj_ty));
            }
            break;
        }

        case EXPR_DEREF:
        case EXPR_SMARTDEREF: {
            Type *pt = check_expr(s, e->deref.operand);
            if (pt && (pt->kind == TY_PTR || pt->kind == TY_SMART_PTR)) {
                e->ty = pt->ptr.inner;
            } else if (pt) {
                sema_error(s, e->span, "cannot dereference non-pointer type '%s'", ty_str(pt));
                e->ty = NULL;
            }
            break;
        }

        case EXPR_IF: {
            Type *ct = check_expr(s, e->if_expr.cond);
            if (ct && ct->kind != TY_BOOL)
                sema_error(s, e->span, "if condition must be bool, got '%s'", ty_str(ct));
            if (e->if_expr.then_) check_stmt(s, e->if_expr.then_);
            if (e->if_expr.else_) check_stmt(s, e->if_expr.else_);
            e->ty = NULL; /* if-expr type requires branch unification — defer to sema v2 */
            break;
        }

        case EXPR_WHEN: {
            Type *vt = check_expr(s, e->when.cond);
            /* check if subject is a tagged union */
            UnionInfo *wui = NULL;
            if (vt && vt->kind == TY_NAMED) {
                for (UnionInfo *u = s->unions; u; u = u->next)
                    if (!strcmp(u->name, vt->named.name)) { wui = u; break; }
            }
            for (size_t i = 0; i < e->when.arms.len; i++) {
                WhenArm *arm = &e->when.arms.data[i];
                push_scope(s);
                if (wui) {
                    for (size_t pi = 0; pi < arm->pats.len; pi++) {
                        Expr *pat = arm->pats.data[pi];
                        if (pat->kind == EXPR_DISCARD) continue;
                        if (pat->kind == EXPR_IDENT && pat->ident.name[0] == '.') {
                            const char *vname = pat->ident.name + 1;
                            int found = 0;
                            for (size_t vi = 0; vi < wui->n_variants; vi++) {
                                if (!strcmp(wui->variants[vi].name, vname)) {
                                    found = 1;
                                    if (arm->bind && wui->variants[vi].ty)
                                        define(s, arm->span, arm->bind,
                                               wui->variants[vi].ty, 0, 0);
                                    break;
                                }
                            }
                            if (!found)
                                sema_error(s, arm->span, "union '%s' has no variant '%s'",
                                           wui->name, vname);
                            pat->ty = vt;
                        } else {
                            check_expr(s, pat);
                        }
                    }
                } else {
                    if (arm->bind && vt) define(s, arm->span, arm->bind, vt, 0, 0);
                    for (size_t pi = 0; pi < arm->pats.len; pi++)
                        check_expr(s, arm->pats.data[pi]);
                }
                check_stmt(s, arm->body);
                /* infer result type from the first arm that is a bare expression */
                if (!e->ty && arm->body && arm->body->kind == STMT_EXPR && arm->body->expr)
                    e->ty = arm->body->expr->ty;
                pop_scope(s);
            }
            break;
        }

        case EXPR_STRUCT_LIT: {
            /* check field values; leave e->ty as the named struct/union type */
            for (size_t i = 0; i < e->struct_lit.fields.len; i++)
                check_expr(s, e->struct_lit.fields.data[i].val);
            Sym *sym = lookup(s, e->struct_lit.ty_name);
            e->ty = (sym && sym->is_type) ? sym->ty : NULL;
            if (!e->ty) {
                Type *t = make_ty(s, TY_NAMED);
                t->named.name = e->struct_lit.ty_name;
                e->ty = t;
            }
            /* validate tagged union construction: exactly one field, valid variant */
            for (UnionInfo *ui = s->unions; ui; ui = ui->next) {
                if (!strcmp(ui->name, e->struct_lit.ty_name)) {
                    if (e->struct_lit.fields.len != 1) {
                        sema_error(s, e->span,
                            "tagged union literal for '%s' must set exactly one variant",
                            ui->name);
                    } else {
                        const char *fname = e->struct_lit.fields.data[0].name;
                        int found = 0;
                        for (size_t vi = 0; vi < ui->n_variants; vi++) {
                            if (!strcmp(ui->variants[vi].name, fname)) { found = 1; break; }
                        }
                        if (!found)
                            sema_error(s, e->span, "union '%s' has no variant '%s'",
                                       ui->name, fname);
                    }
                    break;
                }
            }
            break;
        }

        case EXPR_ARRAY_LIT: {
            Type *elem_ty = NULL;
            for (size_t i = 0; i < e->array_lit.len; i++) {
                Type *et = check_expr(s, e->array_lit.data[i]);
                if (!elem_ty) elem_ty = et;
                else if (et && !ty_coerces(et, elem_ty))
                    sema_error(s, e->span, "inconsistent element types in array literal");
            }
            /* type as [N]T, not []T — length is known at compile time */
            Type *arr = make_ty(s, TY_ARRAY);
            arr->array.inner = elem_ty;
            Expr *sz = ARENA_NEW(s->arena, Expr);
            sz->kind = EXPR_INT; sz->ival = e->array_lit.len;
            arr->array.size  = sz;
            e->ty = arr;
            break;
        }

        default:
            e->ty = NULL;
            break;
    }

    return e->ty;
}

/* ── Statement checker ────────────────────────────────────────────────────── */

static void check_stmt(Sema *s, Stmt *st) {
    if (!st) return;
    switch (st->kind) {
        case STMT_LET: {
            Type *init_ty = st->let.init ? check_expr(s, st->let.init) : NULL;
            Type *decl_ty = st->let.ty ? check_type(s, st->let.ty) : NULL;

            if (decl_ty && init_ty && !ty_coerces(init_ty, decl_ty)) {
                sema_error(s, st->span,
                    "cannot initialize '%s' with value of type '%s'",
                    ty_str(decl_ty), ty_str(init_ty));
            }

            /* infer type from init if not declared */
            Type *ty = decl_ty ? decl_ty : init_ty;

            /* handle failable: val, err: !T = func() */
            if (ty && ty->kind == TY_FAILABLE) {
                /* This let is the err variable: define as i32 error code */
                Type *err_ty = ARENA_NEW(s->arena, Type);
                err_ty->kind = TY_I32;
                define(s, st->span, st->let.name, err_ty, st->let.mutable, 0);
            } else {
                define(s, st->span, st->let.name, ty, st->let.mutable, 0);
            }
            break;
        }

        case STMT_ASSIGN: {
            Type *lhs = check_expr(s, st->assign.target);
            Type *rhs = check_expr(s, st->assign.val);

            /* check mutability for simple ident targets */
            if (st->assign.target->kind == EXPR_IDENT) {
                Sym *sym = lookup(s, st->assign.target->ident.name);
                if (sym && !sym->is_mut)
                    sema_error(s, st->span, "cannot assign to immutable binding '%s'",
                               st->assign.target->ident.name);
            }

            if (lhs && rhs && !ty_coerces(rhs, lhs))
                sema_error(s, st->span, "cannot assign '%s' to '%s'",
                           ty_str(rhs), ty_str(lhs));
            break;
        }

        case STMT_EXPR:
            check_expr(s, st->expr);
            break;

        case STMT_RET: {
            Type *vt = st->ret.val ? check_expr(s, st->ret.val) : NULL;
            if (s->cur_ret) {
                if (!vt && s->cur_ret->kind != TY_VOID)
                    sema_error(s, st->span, "missing return value: expected '%s'",
                               ty_str(s->cur_ret));
                else if (vt && s->cur_ret->kind == TY_VOID)
                    sema_error(s, st->span, "void function should not return a value");
                else if (vt && !ty_coerces(vt, s->cur_ret))
                    sema_error(s, st->span, "return type mismatch: got '%s', expected '%s'",
                               ty_str(vt), ty_str(s->cur_ret));
            }
            break;
        }

        case STMT_IF: {
            for (size_t i = 0; i < st->if_.branches.len; i++) {
                IfBranch *br = &st->if_.branches.data[i];
                Type *ct = check_expr(s, br->cond);
                if (ct && ct->kind != TY_BOOL)
                    sema_error(s, st->span, "if condition must be bool, got '%s'", ty_str(ct));
                push_scope(s);
                for (size_t j = 0; j < br->body.len; j++)
                    check_stmt(s, br->body.data[j]);
                pop_scope(s);
            }
            push_scope(s);
            for (size_t i = 0; i < st->if_.else_body.len; i++)
                check_stmt(s, st->if_.else_body.data[i]);
            pop_scope(s);
            break;
        }

        case STMT_WHILE: {
            Type *ct = check_expr(s, st->while_.cond);
            if (ct && ct->kind != TY_BOOL)
                sema_error(s, st->span, "while condition must be bool, got '%s'", ty_str(ct));
            push_scope(s);
            for (size_t i = 0; i < st->while_.body.len; i++)
                check_stmt(s, st->while_.body.data[i]);
            pop_scope(s);
            break;
        }

        case STMT_FOR: {
            push_scope(s);
            ForClause *fc = &st->for_.clause;
            switch (fc->kind) {
                case FOR_RANGE: {
                    Type *start_ty = check_expr(s, fc->iter);
                    Type *end_ty   = check_expr(s, fc->range_end);
                    if (start_ty && !ty_is_int(start_ty))
                        sema_error(s, st->span, "range start must be integer, got '%s'", ty_str(start_ty));
                    if (end_ty && !ty_is_int(end_ty))
                        sema_error(s, st->span, "range end must be integer, got '%s'", ty_str(end_ty));
                    if (fc->elem)
                        define(s, st->span, fc->elem, start_ty ? start_ty : s->ty_i32, 1, 0);
                    break;
                }
                case FOR_EACH:
                case FOR_EACH_IDX: {
                    Type *iter_ty = check_expr(s, fc->iter);
                    Type *elem_ty = NULL;
                    if (iter_ty) {
                        if (iter_ty->kind == TY_ARRAY || iter_ty->kind == TY_SLICE)
                            elem_ty = iter_ty->array.inner;
                        else if (iter_ty->kind == TY_STR)
                            elem_ty = s->ty_char;
                        else
                            sema_error(s, st->span, "cannot iterate over '%s'", ty_str(iter_ty));
                    }
                    if (fc->elem) define(s, st->span, fc->elem, elem_ty, 0, 0);
                    if (fc->idx)  define(s, st->span, fc->idx,  s->ty_usize, 0, 0);
                    break;
                }
                case FOR_C:
                    if (fc->init) check_stmt(s, fc->init);
                    if (fc->cond) {
                        Type *ct = check_expr(s, fc->cond);
                        if (ct && ct->kind != TY_BOOL)
                            sema_error(s, st->span, "for condition must be bool, got '%s'", ty_str(ct));
                    }
                    if (fc->step) check_stmt(s, fc->step);
                    break;
            }
            for (size_t i = 0; i < st->for_.body.len; i++)
                check_stmt(s, st->for_.body.data[i]);
            pop_scope(s);
            break;
        }

        case STMT_WHEN: {
            Type *vt = check_expr(s, st->when.val);
            /* check if subject is a tagged union */
            UnionInfo *ui = NULL;
            if (vt && vt->kind == TY_NAMED) {
                for (UnionInfo *u = s->unions; u; u = u->next)
                    if (!strcmp(u->name, vt->named.name)) { ui = u; break; }
            }
            for (size_t i = 0; i < st->when.arms.len; i++) {
                WhenArm *arm = &st->when.arms.data[i];
                push_scope(s);
                if (ui) {
                    /* tagged union: patterns are ".variantName" dot-prefixed idents */
                    for (size_t pi = 0; pi < arm->pats.len; pi++) {
                        Expr *pat = arm->pats.data[pi];
                        if (pat->kind == EXPR_DISCARD) continue;
                        if (pat->kind == EXPR_IDENT && pat->ident.name[0] == '.') {
                            const char *vname = pat->ident.name + 1;
                            int found = 0;
                            for (size_t vi = 0; vi < ui->n_variants; vi++) {
                                if (!strcmp(ui->variants[vi].name, vname)) {
                                    found = 1;
                                    /* bind payload type if arm has a binding name */
                                    if (arm->bind && ui->variants[vi].ty)
                                        define(s, arm->span, arm->bind,
                                               ui->variants[vi].ty, 0, 0);
                                    break;
                                }
                            }
                            if (!found)
                                sema_error(s, arm->span,
                                    "union '%s' has no variant '%s'", ui->name, vname);
                            pat->ty = vt;
                        } else {
                            check_expr(s, pat);
                        }
                    }
                } else {
                    /* regular when: bind name for `any` arms */
                    if (arm->bind && vt)
                        define(s, arm->span, arm->bind, vt, 0, 0);
                    for (size_t pi = 0; pi < arm->pats.len; pi++)
                        check_expr(s, arm->pats.data[pi]);
                }
                check_stmt(s, arm->body);
                pop_scope(s);
            }
            break;
        }

        case STMT_DEFER:
            for (size_t i = 0; i < st->defer.len; i++)
                check_stmt(s, st->defer.data[i]);
            break;

        case STMT_BLOCK: {
            /* failable destructure block: two lets where second has !T — no new scope */
            int is_fail = (st->block.len == 2
                && st->block.data[0]->kind == STMT_LET
                && st->block.data[1]->kind == STMT_LET
                && st->block.data[1]->let.ty
                && st->block.data[1]->let.ty->kind == TY_FAILABLE);
            if (!is_fail) push_scope(s);
            for (size_t i = 0; i < st->block.len; i++)
                check_stmt(s, st->block.data[i]);
            if (!is_fail) pop_scope(s);
            break;
        }

        case STMT_BREAK:
        case STMT_CONTINUE:
            break;

        default:
            break;
    }
}

/* ── Item checker ─────────────────────────────────────────────────────────── */

static void check_fn(Sema *s, Item *item) {
    push_scope(s);

    /* params into scope */
    for (size_t i = 0; i < item->fn.params.len; i++) {
        Param *p = &item->fn.params.data[i];
        p->ty = check_type(s, p->ty);
        define(s, item->span, p->name, p->ty, 1, 0);
    }

    Type *ret = check_type(s, item->fn.ret);
    Type *prev_ret = s->cur_ret;
    s->cur_ret = ret;

    for (size_t i = 0; i < item->fn.body.len; i++)
        check_stmt(s, item->fn.body.data[i]);

    s->cur_ret = prev_ret;
    pop_scope(s);
}

static void check_struct(Sema *s, Item *item) {
    /* type already registered in register_item — just validate field types */
    for (size_t i = 0; i < item->struct_.fields.len; i++) {
        Field *f = &item->struct_.fields.data[i];
        f->ty = check_type(s, f->ty);
    }
}

static void check_enum(Sema *s, Item *item) {
    /* type and variant table already registered in register_item;
       validate explicit variant value expressions here */
    for (size_t i = 0; i < item->enum_.variants.len; i++) {
        EnumVariant *v = &item->enum_.variants.data[i];
        if (v->val) check_expr(s, v->val);
    }
}

static void check_union(Sema *s, Item *item) {
    for (size_t i = 0; i < item->union_.fields.len; i++) {
        Field *f = &item->union_.fields.data[i];
        if (f->ty) f->ty = check_type(s, f->ty);
    }
}

static void check_impl(Sema *s, Item *item) {
    /* check each method as a function */
    for (size_t i = 0; i < item->impl.methods.len; i++)
        if (item->impl.methods.data[i]->kind == ITEM_FN)
            check_fn(s, item->impl.methods.data[i]);
}

static void check_global(Sema *s, Item *item) {
    Type *ty = check_type(s, item->global.ty);
    if (item->global.init) {
        Type *init_ty = check_expr(s, item->global.init);
        if (ty && init_ty && !ty_coerces(init_ty, ty))
            sema_error(s, item->span, "global '%s': cannot initialize '%s' with '%s'",
                       item->name, ty_str(ty), ty_str(init_ty));
    }
    /* update existing sym (pre-registered in pass 1.7) rather than re-defining */
    for (Sym *sym = s->scope->syms; sym; sym = sym->next) {
        if (!strcmp(sym->name, item->name)) { sym->ty = ty; return; }
    }
    define(s, item->span, item->name, ty, item->global.mutable, 0);
}

static void check_extern_fn(Sema *s, Item *item) {
    /* build a fn type with resolved param types */
    Type *ty = make_ty(s, TY_FN);
    ty->fn.ret = check_type(s, item->extern_fn.ret);
    size_t np = item->extern_fn.params.len;
    if (np) {
        ty->fn.params.data = ARENA_ALLOC(s->arena, Type *, np);
        ty->fn.params.len  = np;
        for (size_t i = 0; i < np; i++) {
            Param *p = &item->extern_fn.params.data[i];
            p->ty = check_type(s, p->ty);
            ty->fn.params.data[i] = p->ty;
        }
    }
    ty->fn.variadic = item->extern_fn.variadic;
    /* update existing sym from first pass rather than re-defining */
    for (Sym *sym = s->scope->syms; sym; sym = sym->next) {
        if (!strcmp(sym->name, item->name)) { sym->ty = ty; return; }
    }
    define(s, item->span, item->name, ty, 0, 0);
}

static void check_type_alias(Sema *s, Item *item) {
    Type *ty = check_type(s, item->type_alias.ty);
    define(s, item->span, item->name, ty, 0, 1);
}

/* ── Generics: substitution and instantiation ─────────────────────────────── */

/* Produce a mangling-safe string for a type (duplicated from parser.c for sema use) */
static const char *gen_type_str(Type *ty, Arena *a) {
    if (!ty) return "void";
    switch (ty->kind) {
        case TY_I8:    return "i8";
        case TY_I16:   return "i16";
        case TY_I32:   return "i32";
        case TY_I64:   return "i64";
        case TY_U8:    return "u8";
        case TY_U16:   return "u16";
        case TY_U32:   return "u32";
        case TY_U64:   return "u64";
        case TY_F16:   return "f16";
        case TY_F32:   return "f32";
        case TY_F64:   return "f64";
        case TY_USIZE: return "usize";
        case TY_BOOL:  return "bool";
        case TY_CHAR:  return "char";
        case TY_STR:   return "str";
        case TY_VOID:  return "void";
        case TY_NAMED: case TY_GENERIC: return ty->named.name;
        default: return "T";
    }
}

/* Substitute type-param strings inside a mangled name like "Box__T" → "Box__i32". */
static const char *subst_mangled(const char *name, const char **params,
                                  Type **concretes, size_t n, Arena *a) {
    const char *result = name;
    for (size_t i = 0; i < n; i++) {
        const char *concrete_str = gen_type_str(concretes[i], a);
        size_t plen = strlen(params[i]);
        /* find "__<param>" in the name */
        char search[256];
        snprintf(search, sizeof(search), "__%s", params[i]);
        const char *pos = strstr(result, search);
        if (!pos) continue;
        const char *after = pos + 2 + plen;
        /* must be at word boundary: end of string or followed by "__" */
        if (*after != '\0' && strncmp(after, "__", 2) != 0) continue;
        size_t prefix_len = (size_t)(pos - result);
        size_t new_len = prefix_len + 2 + strlen(concrete_str) + strlen(after) + 1;
        char *new_name = arena_alloc(a, new_len);
        memcpy(new_name, result, prefix_len);
        snprintf(new_name + prefix_len, new_len - prefix_len, "__%s%s", concrete_str, after);
        result = new_name;
    }
    return result;
}

static void subst_expr(Expr *e, const char **params, Type **concretes, size_t n, Arena *a);

static Type *subst_type(Type *ty, const char **params, Type **concretes,
                         size_t n, Arena *a) {
    if (!ty) return NULL;
    if (ty->kind == TY_NAMED || ty->kind == TY_GENERIC) {
        /* exact match: "T" → i32 */
        for (size_t i = 0; i < n; i++)
            if (!strcmp(ty->named.name, params[i])) return concretes[i];
        /* compound match: "Box__T" → "Box__i32" */
        const char *new_name = subst_mangled(ty->named.name, params, concretes, n, a);
        if (new_name != ty->named.name) {
            Type *copy = ARENA_NEW(a, Type);
            *copy = *ty;
            copy->named.name = new_name;
            return copy;
        }
        return ty;
    }
    Type *copy = ARENA_NEW(a, Type);
    *copy = *ty;
    switch (ty->kind) {
        case TY_PTR: case TY_SMART_PTR: case TY_SLICE: case TY_FAILABLE:
            copy->ptr.inner = subst_type(ty->ptr.inner, params, concretes, n, a);
            break;
        case TY_ARRAY:
            copy->array.inner = subst_type(ty->array.inner, params, concretes, n, a);
            break;
        case TY_FN: {
            if (ty->fn.params.len) {
                Type **np = ARENA_ALLOC(a, Type *, ty->fn.params.len);
                for (size_t i = 0; i < ty->fn.params.len; i++)
                    np[i] = subst_type(ty->fn.params.data[i], params, concretes, n, a);
                copy->fn.params.data = np;
            }
            copy->fn.ret = subst_type(ty->fn.ret, params, concretes, n, a);
            break;
        }
        default: break;
    }
    return copy;
}

static void subst_expr(Expr *e, const char **params, Type **concretes, size_t n, Arena *a) {
    if (!e) return;
    switch (e->kind) {
        case EXPR_STRUCT_LIT:
            e->struct_lit.ty_name = subst_mangled(e->struct_lit.ty_name, params, concretes, n, a);
            for (size_t i = 0; i < e->struct_lit.fields.len; i++)
                subst_expr(e->struct_lit.fields.data[i].val, params, concretes, n, a);
            break;
        case EXPR_IDENT:
            e->ident.name = subst_mangled(e->ident.name, params, concretes, n, a);
            break;
        case EXPR_BINOP:
            subst_expr(e->binop.l, params, concretes, n, a);
            subst_expr(e->binop.r, params, concretes, n, a);
            break;
        case EXPR_UNOP:
            subst_expr(e->unop.operand, params, concretes, n, a);
            break;
        case EXPR_CALL:
            subst_expr(e->call.callee, params, concretes, n, a);
            for (size_t i = 0; i < e->call.args.len; i++)
                subst_expr(e->call.args.data[i], params, concretes, n, a);
            break;
        case EXPR_FIELD:
            subst_expr(e->field.obj, params, concretes, n, a);
            break;
        case EXPR_INDEX:
            subst_expr(e->index.arr, params, concretes, n, a);
            subst_expr(e->index.idx, params, concretes, n, a);
            break;
        case EXPR_DEREF: case EXPR_SMARTDEREF:
            subst_expr(e->deref.operand, params, concretes, n, a);
            break;
        case EXPR_BUILTIN:
            for (size_t i = 0; i < e->builtin.args.len; i++)
                subst_expr(e->builtin.args.data[i], params, concretes, n, a);
            break;
        case EXPR_IF:
            subst_expr(e->if_expr.cond, params, concretes, n, a);
            break;
        case EXPR_CAST:
            subst_expr(e->cast.val, params, concretes, n, a);
            break;
        default: break;
    }
}

static void subst_stmts(StmtList sl, const char **params, Type **concretes,
                         size_t n, Arena *a) {
    for (size_t i = 0; i < sl.len; i++) {
        Stmt *st = sl.data[i];
        if (!st) continue;
        switch (st->kind) {
            case STMT_LET:
                st->let.ty = subst_type(st->let.ty, params, concretes, n, a);
                subst_expr(st->let.init, params, concretes, n, a);
                break;
            case STMT_ASSIGN:
                subst_expr(st->assign.target, params, concretes, n, a);
                subst_expr(st->assign.val, params, concretes, n, a);
                break;
            case STMT_EXPR:
                subst_expr(st->expr, params, concretes, n, a);
                break;
            case STMT_RET:
                subst_expr(st->ret.val, params, concretes, n, a);
                break;
            case STMT_IF:
                for (size_t j = 0; j < st->if_.branches.len; j++) {
                    subst_expr(st->if_.branches.data[j].cond, params, concretes, n, a);
                    subst_stmts(st->if_.branches.data[j].body, params, concretes, n, a);
                }
                subst_stmts(st->if_.else_body, params, concretes, n, a);
                break;
            case STMT_WHILE:
                subst_expr(st->while_.cond, params, concretes, n, a);
                subst_stmts(st->while_.body, params, concretes, n, a);
                break;
            case STMT_FOR:
                subst_stmts(st->for_.body, params, concretes, n, a);
                break;
            case STMT_BLOCK:
                subst_stmts(st->block, params, concretes, n, a);
                break;
            case STMT_DEFER:
                subst_stmts(st->defer, params, concretes, n, a);
                break;
            default: break;
        }
    }
}

static Item *instantiate(Item *tmpl, const char *mangled_name,
                          Type **concretes, size_t n_concretes, Arena *a) {
    const char **params = NULL;
    size_t n_params = 0;
    if (tmpl->kind == ITEM_FN) {
        params   = tmpl->fn.type_params;
        n_params = tmpl->fn.n_type_params;
    } else if (tmpl->kind == ITEM_STRUCT) {
        params   = tmpl->struct_.type_params;
        n_params = tmpl->struct_.n_type_params;
    }
    if (n_params != n_concretes) return tmpl;

    Item *inst = ARENA_NEW(a, Item);
    *inst = *tmpl;
    inst->name = mangled_name;

    if (tmpl->kind == ITEM_STRUCT) {
        inst->struct_.type_params   = NULL;
        inst->struct_.n_type_params = 0;
        Field *nf = ARENA_ALLOC(a, Field, tmpl->struct_.fields.len);
        for (size_t i = 0; i < tmpl->struct_.fields.len; i++) {
            nf[i]    = tmpl->struct_.fields.data[i];
            nf[i].ty = subst_type(tmpl->struct_.fields.data[i].ty,
                                   params, concretes, n_concretes, a);
        }
        inst->struct_.fields.data = nf;
    } else if (tmpl->kind == ITEM_FN) {
        inst->fn.type_params   = NULL;
        inst->fn.n_type_params = 0;
        Param *np = ARENA_ALLOC(a, Param, tmpl->fn.params.len);
        for (size_t i = 0; i < tmpl->fn.params.len; i++) {
            np[i]    = tmpl->fn.params.data[i];
            np[i].ty = subst_type(tmpl->fn.params.data[i].ty,
                                   params, concretes, n_concretes, a);
        }
        inst->fn.params.data = np;
        inst->fn.ret = subst_type(tmpl->fn.ret, params, concretes, n_concretes, a);
        subst_stmts(inst->fn.body, params, concretes, n_concretes, a);
    }
    return inst;
}

/* ── First-pass: register all top-level names ─────────────────────────────── */

static void register_item(Sema *s, Item *item) {
    switch (item->kind) {
        case ITEM_FN: {
            if (item->fn.n_type_params > 0) {
                /* generic template: store for later instantiation */
                GenericTemplate *gt = ARENA_NEW(s->arena, GenericTemplate);
                gt->item = item;
                gt->name = item->name;
                gt->next = s->generics;
                s->generics = gt;
                break;
            }
            Type *ty = make_ty(s, TY_FN);
            ty->fn.ret = item->fn.ret;
            /* build params list so call-site arity checking works */
            if (item->fn.params.len > 0) {
                ty->fn.params.data = ARENA_ALLOC(s->arena, Type *, item->fn.params.len);
                ty->fn.params.len  = item->fn.params.len;
                for (size_t i = 0; i < item->fn.params.len; i++)
                    ty->fn.params.data[i] = item->fn.params.data[i].ty;
            }
            define(s, item->span, item->name, ty, 0, 0);
            break;
        }
        case ITEM_STRUCT: {
            if (item->struct_.n_type_params > 0) {
                /* generic template: store for later instantiation */
                GenericTemplate *gt = ARENA_NEW(s->arena, GenericTemplate);
                gt->item = item;
                gt->name = item->name;
                gt->next = s->generics;
                s->generics = gt;
                break;
            }
            Type *ty = make_ty(s, TY_NAMED);
            ty->named.name = item->name;
            define(s, item->span, item->name, ty, 0, 1);
            /* register field list so EXPR_FIELD can look up types */
            StructEntry *se = ARENA_NEW(s->arena, StructEntry);
            se->name   = item->name;
            se->fields = &item->struct_.fields;
            se->next   = s->structs;
            s->structs = se;
            break;
        }
        case ITEM_ENUM: {
            Type *ty = make_ty(s, TY_NAMED);
            ty->named.name = item->name;
            define(s, item->span, item->name, ty, 0, 1);
            /* build variant value table */
            size_t n = item->enum_.variants.len;
            EnumVariantVal *vals = ARENA_ALLOC(s->arena, EnumVariantVal, n);
            int64_t next_val = 0;
            for (size_t i = 0; i < n; i++) {
                EnumVariant *v = &item->enum_.variants.data[i];
                if (v->val && v->val->kind == EXPR_INT)
                    next_val = (int64_t)v->val->ival;
                vals[i].name  = v->name;
                vals[i].value = next_val++;
            }
            EnumInfo *ei = ARENA_NEW(s->arena, EnumInfo);
            ei->name        = item->name;
            ei->backing_ty  = item->enum_.backing ? item->enum_.backing : s->ty_i32;
            ei->n_variants  = n;
            ei->variants    = vals;
            ei->next        = s->enums;
            s->enums        = ei;
            break;
        }
        case ITEM_UNION: {
            if (!item->union_.tagged) break; /* untagged unions: no type registration yet */
            Type *ty = make_ty(s, TY_NAMED);
            ty->named.name = item->name;
            define(s, item->span, item->name, ty, 0, 1);
            /* build variant info table */
            size_t n = item->union_.fields.len;
            UnionVariantInfo *vars = ARENA_ALLOC(s->arena, UnionVariantInfo, n);
            for (size_t i = 0; i < n; i++) {
                vars[i].name = item->union_.fields.data[i].name;
                vars[i].ty   = item->union_.fields.data[i].ty;
            }
            UnionInfo *ui  = ARENA_NEW(s->arena, UnionInfo);
            ui->name       = item->name;
            ui->n_variants = n;
            ui->variants   = vars;
            ui->next       = s->unions;
            s->unions      = ui;
            break;
        }
        case ITEM_TYPE_ALIAS: {
            /* register placeholder; will be resolved in second pass */
            define(s, item->span, item->name, item->type_alias.ty, 0, 1);
            break;
        }
        case ITEM_EXTERN_FN: {
            Type *ty = make_ty(s, TY_FN);
            ty->fn.ret = item->extern_fn.ret;
            size_t np = item->extern_fn.params.len;
            if (np) {
                ty->fn.params.data = ARENA_ALLOC(s->arena, Type *, np);
                ty->fn.params.len  = np;
                for (size_t i = 0; i < np; i++)
                    ty->fn.params.data[i] = item->extern_fn.params.data[i].ty;
            }
            ty->fn.variadic = item->extern_fn.variadic;
            define(s, item->span, item->name, ty, 0, 0);
            break;
        }
        case ITEM_IMPL: {
            for (size_t i = 0; i < item->impl.methods.len; i++) {
                Item *m = item->impl.methods.data[i];
                if (m->kind != ITEM_FN) continue;
                /* mangle: "method" → "StructName__method" */
                char buf[256];
                snprintf(buf, sizeof(buf), "%s__%s", item->impl.ty_name, m->name);
                m->name = arena_strdup(s->arena, buf);
                /* register TY_FN with all params (including self) */
                Type *ty = make_ty(s, TY_FN);
                ty->fn.ret = m->fn.ret;
                if (m->fn.params.len > 0) {
                    ty->fn.params.data = ARENA_ALLOC(s->arena, Type *, m->fn.params.len);
                    ty->fn.params.len  = m->fn.params.len;
                    for (size_t j = 0; j < m->fn.params.len; j++)
                        ty->fn.params.data[j] = m->fn.params.data[j].ty;
                }
                define(s, m->span, m->name, ty, 0, 0);
            }
            break;
        }
        case ITEM_GLOBAL:
            /* registered in second pass after type resolution */
            break;
        default:
            break;
    }
}

/* ── Built-in type initialization ─────────────────────────────────────────── */

static void init_builtins(Sema *s) {
    s->ty_void  = make_ty(s, TY_VOID);
    s->ty_bool  = make_ty(s, TY_BOOL);
    s->ty_char  = make_ty(s, TY_CHAR);
    s->ty_str   = make_ty(s, TY_STR);
    s->ty_any   = make_ty(s, TY_ANY);
    s->ty_i8    = make_ty(s, TY_I8);
    s->ty_i16   = make_ty(s, TY_I16);
    s->ty_i32   = make_ty(s, TY_I32);
    s->ty_i64   = make_ty(s, TY_I64);
    s->ty_u8    = make_ty(s, TY_U8);
    s->ty_u16   = make_ty(s, TY_U16);
    s->ty_u32   = make_ty(s, TY_U32);
    s->ty_u64   = make_ty(s, TY_U64);
    s->ty_usize = make_ty(s, TY_USIZE);
    s->ty_f16   = make_ty(s, TY_F16);
    s->ty_f32   = make_ty(s, TY_F32);
    s->ty_f64   = make_ty(s, TY_F64);
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

int sema_check(Module *mod) {
    Sema s = {0};
    s.arena = mod->arena;

    Scope global = {0};
    s.scope = &global;

    init_builtins(&s);

    /* pass 1: register all top-level names so forward calls resolve */
    for (size_t i = 0; i < mod->items.len; i++)
        register_item(&s, mod->items.data[i]);

    /* pass 1.5: instantiate generics recorded during parsing */
    for (size_t i = 0; i < mod->gen_insts.len; i++) {
        GenInst *gi = &mod->gen_insts.data[i];
        if (lookup(&s, gi->mangled)) continue; /* already instantiated or concrete */
        GenericTemplate *gt = NULL;
        for (GenericTemplate *g = s.generics; g; g = g->next)
            if (!strcmp(g->name, gi->base)) { gt = g; break; }
        if (!gt) continue; /* not a known generic — pass 2 will report the error */
        Item *inst = instantiate(gt->item, gi->mangled, gi->args, gi->n_args, s.arena);
        /* append to module so pass 2 checks it */
        size_t new_len = mod->items.len + 1;
        Item **new_data = ARENA_ALLOC(s.arena, Item *, new_len);
        memcpy(new_data, mod->items.data, mod->items.len * sizeof(Item *));
        new_data[mod->items.len] = inst;
        mod->items.data = new_data;
        mod->items.len  = new_len;
        register_item(&s, inst);
    }

    /* pass 1.7: pre-register all globals so fn bodies can reference them regardless
       of item order (imported module globals appear after the main file's items) */
    for (size_t i = 0; i < mod->items.len; i++) {
        Item *item = mod->items.data[i];
        if (item->kind != ITEM_GLOBAL) continue;
        /* skip if already registered (e.g. a file-level global before any imports) */
        if (lookup(&s, item->name)) continue;
        Type *ty = check_type(&s, item->global.ty);
        item->global.ty = ty;
        define(&s, item->span, item->name, ty, item->global.mutable, 0);
    }

    /* pass 2: full check — skip uninstantiated generic templates */
    for (size_t i = 0; i < mod->items.len; i++) {
        Item *item = mod->items.data[i];
        switch (item->kind) {
            case ITEM_FN:
                if (item->fn.n_type_params > 0) break; /* skip generic template */
                check_fn(&s, item);
                break;
            case ITEM_STRUCT:
                if (item->struct_.n_type_params > 0) break; /* skip generic template */
                check_struct(&s, item);
                break;
            case ITEM_ENUM:       check_enum(&s, item);       break;
            case ITEM_IMPL:       check_impl(&s, item);       break;
            case ITEM_GLOBAL:     check_global(&s, item);     break;
            case ITEM_EXTERN_FN:  check_extern_fn(&s, item);  break;
            case ITEM_TYPE_ALIAS: check_type_alias(&s, item); break;
            case ITEM_UNION:      check_union(&s, item); break;
            case ITEM_IMPORT:     break; /* resolved by module loader */
            default:              break;
        }
    }

    return s.errors == 0;
}
