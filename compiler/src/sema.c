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

/* ── Sema context ─────────────────────────────────────────────────────────── */

typedef struct {
    Arena       *arena;
    Scope       *scope;
    Type        *cur_ret;   /* return type of the function being checked */
    int          errors;
    StructEntry *structs;   /* name → field list for struct lookup */
    EnumInfo    *enums;     /* name → variant values for enum lookup */
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
    /* enum ↔ enum (same name already caught by ty_eq) */
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
    if (!strcmp(name, "memcpy") || !strcmp(name, "memset")) return s->ty_void;
    if (!strcmp(name, "os.linux") || !strcmp(name, "os.windows") ||
        !strcmp(name, "os.mac"))                      return s->ty_bool;
    if (!strncmp(name, "arch.", 5))                   return s->ty_bool;
    if (!strcmp(name, "args")) {
        Type *sl = make_ptr(s, TY_SLICE, s->ty_str);
        return sl;
    }
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
            Sym *sym = lookup(s, e->ident.name);
            if (!sym) {
                sema_error(s, e->span, "undefined identifier '%s'", e->ident.name);
                e->ty = s->ty_i32; /* recover */
            } else {
                e->ty = sym->ty;
            }
            break;
        }

        case EXPR_BUILTIN: {
            /* check all args */
            for (size_t i = 0; i < e->builtin.args.len; i++)
                check_expr(s, e->builtin.args.data[i]);
            Type *ret = builtin_ret_ty(s, e->builtin.name);
            /* for min/max/abs: inherit first arg type */
            if (!ret && e->builtin.args.len > 0)
                ret = e->builtin.args.data[0]->ty;
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
            switch (e->binop.op) {
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
            /* type compatibility check */
            if (lt && rt && !ty_eq(lt, rt) && !ty_coerces(lt, rt) && !ty_coerces(rt, lt)) {
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
                /* check arg count */
                if (e->call.args.len != callee_ty->fn.params.len) {
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
                if (!e->ty)
                    sema_error(s, e->span, "type '%s' has no field '%s'",
                               obj_ty->named.name, e->field.field);
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
            for (size_t i = 0; i < e->when.arms.len; i++) {
                WhenArm *arm = &e->when.arms.data[i];
                for (size_t pi = 0; pi < arm->pats.len; pi++) {
                    Type *pt = check_expr(s, arm->pats.data[pi]);
                    if (vt && pt && !ty_coerces(pt, vt))
                        sema_error(s, arm->span, "pattern type '%s' doesn't match value type '%s'",
                                   ty_str(pt), ty_str(vt));
                }
                check_stmt(s, arm->body);
            }
            e->ty = NULL;
            break;
        }

        case EXPR_STRUCT_LIT: {
            /* check field values; leave e->ty as the named struct type */
            for (size_t i = 0; i < e->struct_lit.fields.len; i++)
                check_expr(s, e->struct_lit.fields.data[i].val);
            Sym *sym = lookup(s, e->struct_lit.ty_name);
            e->ty = (sym && sym->is_type) ? sym->ty : NULL;
            if (!e->ty) {
                Type *t = make_ty(s, TY_NAMED);
                t->named.name = e->struct_lit.ty_name;
                e->ty = t;
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
            Type *arr = make_ty(s, TY_SLICE);
            arr->ptr.inner = elem_ty;
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
                /* the actual value type is the inner */
                define(s, st->span, st->let.name, ty->ptr.inner, st->let.mutable, 0);
                /* err binding handled at parse time as a sibling let */
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
            for (size_t i = 0; i < st->when.arms.len; i++) {
                WhenArm *arm = &st->when.arms.data[i];
                push_scope(s);
                /* bind name for `any` arms */
                if (arm->bind && vt)
                    define(s, arm->span, arm->bind, vt, 0, 0);
                for (size_t pi = 0; pi < arm->pats.len; pi++)
                    check_expr(s, arm->pats.data[pi]);
                check_stmt(s, arm->body);
                pop_scope(s);
            }
            break;
        }

        case STMT_DEFER:
            for (size_t i = 0; i < st->defer.len; i++)
                check_stmt(s, st->defer.data[i]);
            break;

        case STMT_BLOCK:
            push_scope(s);
            for (size_t i = 0; i < st->block.len; i++)
                check_stmt(s, st->block.data[i]);
            pop_scope(s);
            break;

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
    define(s, item->span, item->name, ty, item->global.mutable, 0);
}

static void check_extern_fn(Sema *s, Item *item) {
    /* build a fn type and register the name */
    Type *ty = make_ty(s, TY_FN);
    ty->fn.ret = check_type(s, item->extern_fn.ret);
    for (size_t i = 0; i < item->extern_fn.params.len; i++) {
        Param *p = &item->extern_fn.params.data[i];
        p->ty = check_type(s, p->ty);
    }
    define(s, item->span, item->name, ty, 0, 0);
}

static void check_type_alias(Sema *s, Item *item) {
    Type *ty = check_type(s, item->type_alias.ty);
    define(s, item->span, item->name, ty, 0, 1);
}

/* ── First-pass: register all top-level names ─────────────────────────────── */

static void register_item(Sema *s, Item *item) {
    switch (item->kind) {
        case ITEM_FN: {
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
        case ITEM_TYPE_ALIAS: {
            /* register placeholder; will be resolved in second pass */
            define(s, item->span, item->name, item->type_alias.ty, 0, 1);
            break;
        }
        case ITEM_EXTERN_FN: {
            Type *ty = make_ty(s, TY_FN);
            ty->fn.ret = item->extern_fn.ret;
            define(s, item->span, item->name, ty, 0, 0);
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

    /* pass 2: full check */
    for (size_t i = 0; i < mod->items.len; i++) {
        Item *item = mod->items.data[i];
        switch (item->kind) {
            case ITEM_FN:         check_fn(&s, item);         break;
            case ITEM_STRUCT:     check_struct(&s, item);     break;
            case ITEM_ENUM:       check_enum(&s, item);       break;
            case ITEM_IMPL:       check_impl(&s, item);       break;
            case ITEM_GLOBAL:     check_global(&s, item);     break;
            case ITEM_EXTERN_FN:  check_extern_fn(&s, item);  break;
            case ITEM_TYPE_ALIAS: check_type_alias(&s, item); break;
            case ITEM_UNION:      break; /* TODO */
            case ITEM_IMPORT:     break; /* resolved by module loader */
            default:              break;
        }
    }

    return s.errors == 0;
}
