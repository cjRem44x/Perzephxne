#pragma once
#include "span.h"
#include "arena.h"
#include <stdint.h>
#include <stddef.h>

/* ── forward declarations ─────────────────────────────────────────────────── */
typedef struct Type   Type;
typedef struct Expr   Expr;
typedef struct Stmt   Stmt;
typedef struct Item   Item;

/* ── dynamic arrays (arena-backed) ──────────────────────────────────────────
   All list types below follow the same pattern: a pointer + a count.
   The parser fills them using arena_alloc.                                   */

typedef struct { Type **data; size_t len; } TypeList;
typedef struct { Expr **data; size_t len; } ExprList;
typedef struct { Stmt **data; size_t len; } StmtList;
typedef struct { Item **data; size_t len; } ItemList;

/* ── Types ───────────────────────────────────────────────────────────────── */

typedef enum {
    TY_I8, TY_I16, TY_I32, TY_I64,
    TY_U8, TY_U16, TY_U32, TY_U64,
    TY_F16, TY_F32, TY_F64,
    TY_USIZE,
    TY_BOOL, TY_CHAR, TY_STR, TY_ANY, TY_VOID,
    TY_PTR,          /* *T      */
    TY_SMART_PTR,    /* ^T      */
    TY_ARRAY,        /* [N]T    */
    TY_SLICE,        /* []T     */
    TY_FN,           /* fn(...) -> T */
    TY_NAMED,        /* user type */
    TY_FAILABLE,     /* !T      */
    TY_GENERIC,      /* T (type param) */
    TY_TUPLE,        /* (T1, T2, ...) */
    TY_SELF,         /* @self — pre-resolution marker, parser-only; sema
                        rewrites an impl method's first parameter from this
                        into *StructName with is_self_alias set below */
} TypeKind;

struct Type {
    TypeKind kind;
    Span     span;
    /* set on the pointer-to-struct type sema resolves an impl method's
       `self: @self` parameter into — marks it as accepting a value, raw
       pointer, or smart pointer receiver interchangeably at each call
       site, rather than requiring an exact kind match the way an
       explicitly-declared self param does. */
    int      is_self_alias;
    union {
        struct { Type *inner; }                       ptr;       /* PTR, SMART_PTR, SLICE, FAILABLE */
        struct { Type *inner; Expr *size; }           array;     /* ARRAY */
        struct { TypeList params; Type *ret; int variadic; } fn; /* FN */
        struct { const char *name; }                  named;     /* NAMED, GENERIC */
        struct { TypeList elems; }                    tuple;     /* TUPLE */
    };
};

/* ── Expressions ─────────────────────────────────────────────────────────── */

typedef enum {
    BINOP_ADD, BINOP_SUB, BINOP_MUL, BINOP_DIV, BINOP_MOD,
    BINOP_AMP, BINOP_PIPE, BINOP_XOR, BINOP_SHL, BINOP_SHR,
    BINOP_AND, BINOP_OR,
    BINOP_EQ, BINOP_NE, BINOP_LT, BINOP_GT, BINOP_LE, BINOP_GE,
    BINOP_RANGE, BINOP_RANGE_INC,
} BinOp;

typedef enum {
    UNOP_NEG, UNOP_NOT, UNOP_BITNOT, UNOP_ADDROF,
} UnOp;

typedef enum {
    ASSIGN_EQ,
    ASSIGN_ADD, ASSIGN_SUB, ASSIGN_MUL, ASSIGN_DIV, ASSIGN_MOD,
    ASSIGN_AMP, ASSIGN_PIPE, ASSIGN_XOR, ASSIGN_SHL, ASSIGN_SHR,
} AssignOp;

/* field name + value pair used in struct literals and when arms */
typedef struct { const char *name; Expr *val; } FieldInit;
typedef struct { FieldInit *data; size_t len; } FieldInitList;

/* when arm: patterns => body */
typedef struct {
    ExprList   pats;     /* one or more patterns (multi-pattern uses | in source) */
    const char *bind;    /* optional binding name for `any` / tagged union */
    Stmt       *body;    /* single stmt or block */
    Span        span;
} WhenArm;
typedef struct { WhenArm *data; size_t len; } WhenArmList;

typedef enum {
    EXPR_INT, EXPR_FLOAT, EXPR_STR, EXPR_CHAR, EXPR_BOOL,
    EXPR_IDENT,
    EXPR_BUILTIN,        /* @name(args)   */
    EXPR_CAST,           /* @T(val)       */
    EXPR_BINOP,
    EXPR_UNOP,
    EXPR_CALL,
    EXPR_INDEX,          /* arr[i]        */
    EXPR_FIELD,          /* expr.name     */
    EXPR_DEREF,          /* expr.*        */
    EXPR_SMARTDEREF,     /* expr.^        */
    EXPR_WHEN,
    EXPR_IF,             /* if as expr    */
    EXPR_STRUCT_LIT,     /* Foo{.x=1}     */
    EXPR_ARRAY_LIT,      /* [1,2,3]       */
    EXPR_TUPLE,          /* (e1, e2, ...) */
    EXPR_UNDEF,
    EXPR_NULL,
    EXPR_DISCARD,        /* _             */
} ExprKind;

struct Expr {
    ExprKind kind;
    Span     span;
    Type    *ty;         /* filled by sema */
    int      lit_suffixed; /* literal carried an explicit type suffix (42u8, 3.14f32) */
    union {
        uint64_t       ival;
        double         fval;
        const char    *sval;
        uint8_t        cval;
        int            bval;

        struct { const char *name; }                ident;
        struct { const char *name; ExprList args; } builtin;
        struct { const char *ty_name; Expr *val; }  cast;
        struct { Expr *l; BinOp op; Expr *r; }      binop;
        struct { UnOp op; Expr *operand; }           unop;
        struct { Expr *callee; ExprList args; }      call;
        struct { Expr *arr; Expr *idx; }             index;
        struct { Expr *obj; const char *field;
                 int         is_method;     /* set by sema: field is a method, not a struct field */
                 const char *mangled_name;  /* "StructName__method" — valid when is_method=1 */
               }                                      field;
        struct { Expr *operand; }                    deref;
        struct { Expr *cond; WhenArmList arms; }     when;
        struct { Expr *cond; Stmt *then_; Stmt *else_; } if_expr;
        struct { const char *ty_name; FieldInitList fields; } struct_lit;
        ExprList array_lit;
    };
};

/* ── Statements ──────────────────────────────────────────────────────────── */

typedef enum {
    FOR_EACH,       /* for e => arr                    */
    FOR_EACH_IDX,   /* for e, i => arr                 */
    FOR_RANGE,      /* for 0..N                        */
    FOR_C,          /* for i:=0, i<N, i++              */
} ForKind;

typedef struct {
    ForKind     kind;
    const char *elem;      /* element name      */
    const char *idx;       /* index name (optional) */
    Expr       *iter;      /* array / range start  */
    Expr       *range_end; /* range end            */
    int         inclusive; /* ..=                  */
    /* C-style for */
    Stmt       *init;
    Expr       *cond;
    Stmt       *step;
} ForClause;

typedef enum {
    STMT_LET,
    STMT_ASSIGN,
    STMT_EXPR,
    STMT_IF,
    STMT_WHILE,
    STMT_FOR,
    STMT_WHEN,
    STMT_DEFER,
    STMT_RET,
    STMT_BREAK,
    STMT_CONTINUE,
    STMT_BLOCK,
    STMT_LABEL,   /* name: */
    STMT_GOTO,    /* goto name */
} StmtKind;

typedef struct { Expr *cond; StmtList body; } IfBranch;
typedef struct { IfBranch *data; size_t len; } IfBranchList;

struct Stmt {
    StmtKind kind;
    Span     span;
    union {
        struct {
            const char *name;
            Type       *ty;        /* may be NULL (inferred) */
            int         mutable;   /* 1 = mutable (=), 0 = immutable (:) */
            Expr       *init;      /* may be NULL */
            int         infer;     /* 1 = := or ::, widen to platform max type */
            int         is_fail_err;  /* 1 = err-side of val,err: !T destructure */
            int         is_tuple_elem; /* 1 = element of tuple destructure */
            int         tuple_idx;    /* index into tuple (only when is_tuple_elem=1) */
        } let;

        struct { Expr *target; AssignOp op; Expr *val; } assign;

        Expr *expr;

        struct { IfBranchList branches; StmtList else_body; } if_;

        struct {
            Expr       *cond;
            const char *do_fn;   /* while cond => fn_name {} */
            StmtList    body;
            const char *label;
        } while_;

        struct { ForClause clause; StmtList body; const char *label; } for_;

        struct { Expr *val; WhenArmList arms; } when;

        StmtList defer;

        struct { Expr *val; } ret;   /* val may be NULL */

        struct { const char *label; } break_;
        struct { const char *label; } cont;

        StmtList block;

        struct { const char *name; } label_; /* STMT_LABEL */
        struct { const char *name; } goto_;  /* STMT_GOTO  */
    };
};

/* ── Top-level Items ─────────────────────────────────────────────────────── */

typedef struct { const char *name; Type *ty; } Param;
typedef struct { Param *data; size_t len; } ParamList;
typedef struct { const char *name; Expr *val; } EnumVariant;
typedef struct { EnumVariant *data; size_t len; } EnumVariantList;
typedef struct { const char *name; Type *ty; } Field;
typedef struct { Field *data; size_t len; } FieldList;
typedef struct { const char *alias; const char *path; } ImportEntry;
typedef struct { ImportEntry *data; size_t len; } ImportList;

typedef enum {
    ATTR_PACKED,
    ATTR_ALIGN,
    ATTR_OPAQUE,   /* @opaque — hides a struct's fields/methods outside its own impl block */
} AttrKind;

typedef struct { AttrKind kind; Expr *arg; } Attr;
typedef struct { Attr *data; size_t len; } AttrList;

typedef enum {
    ITEM_FN,
    ITEM_STRUCT,
    ITEM_IMPL,
    ITEM_ENUM,
    ITEM_UNION,
    ITEM_TYPE_ALIAS,
    ITEM_GLOBAL,
    ITEM_IMPORT,
    ITEM_EXTERN_FN,
    ITEM_MOD,      /* mod Name { ...items... } — an inline namespace, in the
                       same file, accessed as Name=>Item (see main.c's
                       mangle_items, reused here to prefix the block's own
                       items with "Name__" the same way an import does). */
} ItemKind;

struct Item {
    ItemKind    kind;
    Span        span;
    const char *name;     /* NULL for ITEM_IMPORT */
    union {
        struct {
            ParamList    params;
            Type        *ret;       /* NULL = void */
            StmtList     body;
            int          is_inline;
            int          variadic;
            const char **type_params;
            size_t       n_type_params;
            int          is_test;   /* test "name" { ... } block */
            const char  *test_name; /* the quoted display name, if is_test */
            const char  *test_file; /* source file path, stamped in main.c
                                        right after parsing (the parser
                                        itself has no path, only src text) */
            int          is_pub;   /* pub fn — only meaningful inside an
                                       @opaque struct's impl block; a no-op
                                       everywhere else (plain functions, or
                                       impl methods of a non-@opaque struct) */
        } fn;

        struct {
            FieldList   fields;
            AttrList    attrs;
            /* generic type params: stored as named strings */
            const char **type_params;
            size_t       n_type_params;
            int          is_opaque;  /* extern struct Name — opaque FFI type */
        } struct_;

        struct {
            const char  *ty_name;
            const char **type_params;
            size_t       n_type_params;
            ItemList     methods;
        } impl;

        struct {
            Type            *backing;  /* NULL = no backing type */
            EnumVariantList  variants;
        } enum_;

        struct {
            FieldList fields;
            int       tagged;    /* => enum */
        } union_;

        struct { Type *ty; } type_alias;

        struct {
            Type *ty;         /* may be NULL (inferred) */
            int   mutable;
            Expr *init;
            int   is_extern;  /* extern name: T — symbol defined in C / another object */
            int   infer;      /* 1 = := or ::, widen to platform max type */
        } global;

        ImportList imports;

        struct {
            ParamList    params;
            Type        *ret;
            int          variadic;
            const char  *c_name; /* original C symbol name when imported (NULL otherwise) */
        } extern_fn;

        struct { ItemList items; } mod_;
    };
};

/* ── Generic instantiation record ───────────────────────────────────────── */

typedef struct {
    const char *mangled;  /* e.g. "Box__i32"  */
    const char *base;     /* e.g. "Box"        */
    Type      **args;     /* concrete type args (may be TY_NAMED placeholder if deferred) */
    size_t      n_args;
    int         deferred; /* 1 = args contain type-param placeholders; derive at instantiation time */
} GenInst;

typedef struct { GenInst *data; size_t len; } GenInstList;

/* ── Module (parse result) ───────────────────────────────────────────────── */

typedef struct {
    ItemList    items;
    GenInstList gen_insts; /* generic instantiations recorded during parsing */
    Arena      *arena;
} Module;
