#include "parser.h"
#include "error.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── Parser state ─────────────────────────────────────────────────────────── */

typedef struct {
    Lexer  lexer;
    Token  cur;
    Token  peek;
    Arena *arena;
} Parser;

/* ── helpers ──────────────────────────────────────────────────────────────── */

static void advance(Parser *p) {
    p->cur  = p->peek;
    p->peek = lexer_next(&p->lexer);
}

static Token cur(Parser *p)  { return p->cur; }
static Token peek(Parser *p) { return p->peek; }

static int check(Parser *p, TokenKind k)  { return p->cur.kind == k; }
static int check2(Parser *p, TokenKind k) { return p->peek.kind == k; }

static Token expect(Parser *p, TokenKind k) {
    if (p->cur.kind != k)
        fatal_at(p->cur.span, "expected %s, got %s",
                 tok_kind_str(k), tok_kind_str(p->cur.kind));
    Token t = p->cur;
    advance(p);
    return t;
}

static int eat(Parser *p, TokenKind k) {
    if (p->cur.kind == k) { advance(p); return 1; }
    return 0;
}

/* ── list helpers ─────────────────────────────────────────────────────────── */

#define LIST_PUSH(arena, lst, T, val) do {                              \
    size_t _n = (lst)->len + 1;                                         \
    T **_d = arena_alloc((arena), _n * sizeof(T *));                    \
    if ((lst)->len) memcpy(_d, (lst)->data, (lst)->len * sizeof(T *));  \
    _d[_n - 1] = (val);                                                 \
    (lst)->data = _d; (lst)->len = _n;                                  \
} while (0)

#define SLICE_PUSH(arena, lst, T, val) do {                             \
    size_t _n = (lst)->len + 1;                                         \
    T *_d = arena_alloc((arena), _n * sizeof(T));                       \
    if ((lst)->len) memcpy(_d, (lst)->data, (lst)->len * sizeof(T));    \
    _d[_n - 1] = (val);                                                 \
    (lst)->data = _d; (lst)->len = _n;                                  \
} while (0)

/* ── forward declarations ─────────────────────────────────────────────────── */

static Type  *parse_type(Parser *p);
static Expr  *parse_expr(Parser *p);
static Expr  *parse_expr_bp(Parser *p, int min_bp);
static Stmt  *parse_stmt(Parser *p);
static StmtList parse_block(Parser *p);
static Item  *parse_item(Parser *p);

/* ── Types ────────────────────────────────────────────────────────────────── */

static Type *mktype(Parser *p, TypeKind k, Span span) {
    Type *t = ARENA_NEW(p->arena, Type);
    t->kind = k; t->span = span;
    return t;
}

static TypeKind prim_keyword(TokenKind k) {
    switch (k) {
        case TOK_IDENT: return -1;
        default:        return -1;
    }
}

/* match "i32", "u8", etc. from identifier text */
static TypeKind prim_from_name(const char *s) {
    if (!strcmp(s,"i8"))    return TY_I8;
    if (!strcmp(s,"i16"))   return TY_I16;
    if (!strcmp(s,"i32"))   return TY_I32;
    if (!strcmp(s,"i64"))   return TY_I64;
    if (!strcmp(s,"u8"))    return TY_U8;
    if (!strcmp(s,"u16"))   return TY_U16;
    if (!strcmp(s,"u32"))   return TY_U32;
    if (!strcmp(s,"u64"))   return TY_U64;
    if (!strcmp(s,"f16"))   return TY_F16;
    if (!strcmp(s,"f32"))   return TY_F32;
    if (!strcmp(s,"f64"))   return TY_F64;
    if (!strcmp(s,"usize")) return TY_USIZE;
    if (!strcmp(s,"bool"))  return TY_BOOL;
    if (!strcmp(s,"char"))  return TY_CHAR;
    if (!strcmp(s,"str"))   return TY_STR;
    if (!strcmp(s,"any"))   return TY_ANY;
    return -1;
}

static Type *parse_type(Parser *p) {
    Span  span = cur(p).span;
    Token t    = cur(p);

    /* !T — failable */
    if (t.kind == TOK_BANG) {
        advance(p);
        Type *inner = parse_type(p);
        Type *ty = mktype(p, TY_FAILABLE, span_merge(span, inner->span));
        ty->ptr.inner = inner;
        return ty;
    }

    /* *T — raw pointer */
    if (t.kind == TOK_STAR) {
        advance(p);
        Type *inner = parse_type(p);
        Type *ty = mktype(p, TY_PTR, span_merge(span, inner->span));
        ty->ptr.inner = inner;
        return ty;
    }

    /* ^T — smart pointer */
    if (t.kind == TOK_CARET) {
        advance(p);
        Type *inner = parse_type(p);
        Type *ty = mktype(p, TY_SMART_PTR, span_merge(span, inner->span));
        ty->ptr.inner = inner;
        return ty;
    }

    /* [N]T or []T */
    if (t.kind == TOK_LBRACKET) {
        advance(p);
        if (eat(p, TOK_RBRACKET)) {
            /* []T — slice */
            Type *inner = parse_type(p);
            Type *ty = mktype(p, TY_SLICE, span_merge(span, inner->span));
            ty->ptr.inner = inner;
            return ty;
        }
        /* [N]T — array */
        Expr *sz = parse_expr(p);
        expect(p, TOK_RBRACKET);
        Type *inner = parse_type(p);
        Type *ty = mktype(p, TY_ARRAY, span_merge(span, inner->span));
        ty->array.inner = inner;
        ty->array.size  = sz;
        return ty;
    }

    /* fn(T,...) -> T */
    if (t.kind == TOK_FN) {
        advance(p);
        expect(p, TOK_LPAREN);
        TypeList params = {0};
        while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
            Type *pt = parse_type(p);
            LIST_PUSH(p->arena, &params, Type, pt);
            if (!eat(p, TOK_COMMA)) break;
        }
        expect(p, TOK_RPAREN);
        Type *ret = NULL;
        if (eat(p, TOK_ARROW)) ret = parse_type(p);
        Type *ty = mktype(p, TY_FN, span_merge(span, cur(p).span));
        ty->fn.params = params;
        ty->fn.ret    = ret;
        return ty;
    }

    /* named / primitive */
    if (t.kind == TOK_IDENT) {
        advance(p);
        int pk = prim_from_name(t.sval);
        if (pk >= 0) {
            return mktype(p, (TypeKind)pk, span);
        }
        Type *ty = mktype(p, TY_NAMED, span);
        ty->named.name = t.sval;
        return ty;
    }

    fatal_at(span, "expected a type, got %s", tok_kind_str(t.kind));
}

/* ── Expressions ──────────────────────────────────────────────────────────── */

static Expr *mkexpr(Parser *p, ExprKind k, Span span) {
    Expr *e = ARENA_NEW(p->arena, Expr);
    e->kind = k; e->span = span;
    return e;
}

/* binding power table — returns {left_bp, right_bp}, 0 means not an infix op */
static void infix_bp(TokenKind k, int *lbp, int *rbp) {
    *lbp = 0; *rbp = 0;
    switch (k) {
        /* assign ops: lbp=0 so the Pratt loop exits and parse_stmt handles them */
        case TOK_EQ: case TOK_PLUSEQ: case TOK_MINUSEQ:
        case TOK_STAREQ: case TOK_SLASHEQ: case TOK_PERCENTEQ:
        case TOK_AMPEQ: case TOK_PIPEEQ: case TOK_CARETEQ:
        case TOK_SHLEQ: case TOK_SHREQ:
            *lbp = 0; *rbp = 0; return;
        case TOK_OR:     *lbp = 4;  *rbp = 5;  return;
        case TOK_AND:    *lbp = 6;  *rbp = 7;  return;
        case TOK_PIPE:   *lbp = 8;  *rbp = 9;  return;
        case TOK_CARET:  *lbp = 10; *rbp = 11; return;
        case TOK_AMP:    *lbp = 12; *rbp = 13; return;
        case TOK_EQEQ: case TOK_BANGEQ:
                         *lbp = 14; *rbp = 15; return;
        case TOK_LT: case TOK_GT: case TOK_LTEQ: case TOK_GTEQ:
                         *lbp = 16; *rbp = 17; return;
        case TOK_SHL: case TOK_SHR:
                         *lbp = 18; *rbp = 19; return;
        case TOK_DOTDOT: case TOK_DOTDOTEQ:
                         *lbp = 20; *rbp = 21; return;
        case TOK_PLUS: case TOK_MINUS:
                         *lbp = 22; *rbp = 23; return;
        case TOK_STAR: case TOK_SLASH: case TOK_PERCENT:
                         *lbp = 24; *rbp = 25; return;
        default: return;
    }
}

static BinOp tok_to_binop(TokenKind k) {
    switch (k) {
        case TOK_PLUS:    return BINOP_ADD;
        case TOK_MINUS:   return BINOP_SUB;
        case TOK_STAR:    return BINOP_MUL;
        case TOK_SLASH:   return BINOP_DIV;
        case TOK_PERCENT: return BINOP_MOD;
        case TOK_AMP:     return BINOP_AMP;
        case TOK_PIPE:    return BINOP_PIPE;
        case TOK_CARET:   return BINOP_XOR;
        case TOK_SHL:     return BINOP_SHL;
        case TOK_SHR:     return BINOP_SHR;
        case TOK_AND:     return BINOP_AND;
        case TOK_OR:      return BINOP_OR;
        case TOK_EQEQ:    return BINOP_EQ;
        case TOK_BANGEQ:  return BINOP_NE;
        case TOK_LT:      return BINOP_LT;
        case TOK_GT:      return BINOP_GT;
        case TOK_LTEQ:    return BINOP_LE;
        case TOK_GTEQ:    return BINOP_GE;
        case TOK_DOTDOT:  return BINOP_RANGE;
        case TOK_DOTDOTEQ:return BINOP_RANGE_INC;
        default:          return BINOP_ADD; /* unreachable */
    }
}

static AssignOp tok_to_assignop(TokenKind k) {
    switch (k) {
        case TOK_EQ:        return ASSIGN_EQ;
        case TOK_PLUSEQ:    return ASSIGN_ADD;
        case TOK_MINUSEQ:   return ASSIGN_SUB;
        case TOK_STAREQ:    return ASSIGN_MUL;
        case TOK_SLASHEQ:   return ASSIGN_DIV;
        case TOK_PERCENTEQ: return ASSIGN_MOD;
        case TOK_AMPEQ:     return ASSIGN_AMP;
        case TOK_PIPEEQ:    return ASSIGN_PIPE;
        case TOK_CARETEQ:   return ASSIGN_XOR;
        case TOK_SHLEQ:     return ASSIGN_SHL;
        case TOK_SHREQ:     return ASSIGN_SHR;
        default:            return ASSIGN_EQ;
    }
}

static int is_assign_op(TokenKind k) {
    switch (k) {
        case TOK_EQ: case TOK_PLUSEQ: case TOK_MINUSEQ:
        case TOK_STAREQ: case TOK_SLASHEQ: case TOK_PERCENTEQ:
        case TOK_AMPEQ: case TOK_PIPEEQ: case TOK_CARETEQ:
        case TOK_SHLEQ: case TOK_SHREQ: return 1;
        default: return 0;
    }
}

/* parse argument list: ( expr, expr, ... ) */
static ExprList parse_args(Parser *p) {
    ExprList args = {0};
    expect(p, TOK_LPAREN);
    while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
        Expr *e = parse_expr(p);
        LIST_PUSH(p->arena, &args, Expr, e);
        if (!eat(p, TOK_COMMA)) break;
    }
    expect(p, TOK_RPAREN);
    return args;
}

/* parse primary expression */
static Expr *parse_primary(Parser *p) {
    Token t = cur(p);
    Span  span = t.span;

    switch (t.kind) {
        case TOK_INT: {
            advance(p);
            Expr *e = mkexpr(p, EXPR_INT, span);
            e->ival = t.ival;
            return e;
        }
        case TOK_FLOAT: {
            advance(p);
            Expr *e = mkexpr(p, EXPR_FLOAT, span);
            e->fval = t.fval;
            return e;
        }
        case TOK_STR: {
            advance(p);
            Expr *e = mkexpr(p, EXPR_STR, span);
            e->sval = t.sval;
            return e;
        }
        case TOK_CHAR: {
            advance(p);
            Expr *e = mkexpr(p, EXPR_CHAR, span);
            e->cval = t.cval;
            return e;
        }
        case TOK_TRUE: {
            advance(p);
            Expr *e = mkexpr(p, EXPR_BOOL, span);
            e->bval = 1;
            return e;
        }
        case TOK_FALSE: {
            advance(p);
            Expr *e = mkexpr(p, EXPR_BOOL, span);
            e->bval = 0;
            return e;
        }
        case TOK_UNDEF: {
            advance(p);
            return mkexpr(p, EXPR_UNDEF, span);
        }
        case TOK_NULL: {
            advance(p);
            return mkexpr(p, EXPR_NULL, span);
        }
        case TOK_UNDER: {
            advance(p);
            return mkexpr(p, EXPR_DISCARD, span);
        }

        /* builtin call or cast: @name(args) or @T(val) */
        case TOK_BUILTIN: {
            advance(p);
            const char *name = t.sval;
            /* check if it's a type-cast builtin like @i32, @str, etc. */
            int is_cast = (!strcmp(name,"i8")  || !strcmp(name,"i16") ||
                           !strcmp(name,"i32") || !strcmp(name,"i64") ||
                           !strcmp(name,"u8")  || !strcmp(name,"u16") ||
                           !strcmp(name,"u32") || !strcmp(name,"u64") ||
                           !strcmp(name,"f16") || !strcmp(name,"f32") ||
                           !strcmp(name,"f64") || !strcmp(name,"usize") ||
                           !strcmp(name,"bool")|| !strcmp(name,"char") ||
                           !strcmp(name,"str"));
            if (is_cast) {
                expect(p, TOK_LPAREN);
                Expr *val = parse_expr(p);
                expect(p, TOK_RPAREN);
                Expr *e = mkexpr(p, EXPR_CAST, span_merge(span, cur(p).span));
                e->cast.ty_name = name;
                e->cast.val     = val;
                return e;
            }
            /* regular builtin */
            ExprList args = {0};
            if (check(p, TOK_LPAREN)) {
                args = parse_args(p);
            }
            Expr *e = mkexpr(p, EXPR_BUILTIN, span_merge(span, cur(p).span));
            e->builtin.name = name;
            e->builtin.args = args;
            return e;
        }

        /* identifier or struct literal */
        case TOK_IDENT: {
            advance(p);
            const char *name = t.sval;
            /* struct literal: Foo{.x=1, ...} */
            if (check(p, TOK_LBRACE) && !check2(p, TOK_RBRACE)) {
                /* peek for .field = to distinguish from block */
                if (peek(p).kind == TOK_DOT) {
                    advance(p); /* { */
                    FieldInitList fields = {0};
                    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
                        expect(p, TOK_DOT);
                        Token fn = expect(p, TOK_IDENT);
                        expect(p, TOK_EQ);
                        Expr *val = parse_expr(p);
                        FieldInit fi = { .name = fn.sval, .val = val };
                        SLICE_PUSH(p->arena, &fields, FieldInit, fi);
                        eat(p, TOK_COMMA);
                    }
                    Span end = cur(p).span;
                    expect(p, TOK_RBRACE);
                    Expr *e = mkexpr(p, EXPR_STRUCT_LIT, span_merge(span, end));
                    e->struct_lit.ty_name = name;
                    e->struct_lit.fields  = fields;
                    return e;
                }
            }
            /* bare struct zero-init: Foo (followed by something that's not a call) */
            Expr *e = mkexpr(p, EXPR_IDENT, span);
            e->ident.name = name;
            return e;
        }

        /* unary minus */
        case TOK_MINUS: {
            advance(p);
            Expr *operand = parse_expr_bp(p, 30);
            Expr *e = mkexpr(p, EXPR_UNOP, span_merge(span, operand->span));
            e->unop.op      = UNOP_NEG;
            e->unop.operand = operand;
            return e;
        }

        /* unary not */
        case TOK_NOT: {
            advance(p);
            Expr *operand = parse_expr_bp(p, 30);
            Expr *e = mkexpr(p, EXPR_UNOP, span_merge(span, operand->span));
            e->unop.op      = UNOP_NOT;
            e->unop.operand = operand;
            return e;
        }

        /* bitwise NOT */
        case TOK_TILDE: {
            advance(p);
            Expr *operand = parse_expr_bp(p, 30);
            Expr *e = mkexpr(p, EXPR_UNOP, span_merge(span, operand->span));
            e->unop.op      = UNOP_BITNOT;
            e->unop.operand = operand;
            return e;
        }

        /* address-of */
        case TOK_AMP: {
            advance(p);
            Expr *operand = parse_expr_bp(p, 30);
            Expr *e = mkexpr(p, EXPR_UNOP, span_merge(span, operand->span));
            e->unop.op      = UNOP_ADDROF;
            e->unop.operand = operand;
            return e;
        }

        /* array literal: [1, 2, 3] */
        case TOK_LBRACKET: {
            advance(p);
            ExprList elems = {0};
            while (!check(p, TOK_RBRACKET) && !check(p, TOK_EOF)) {
                Expr *el = parse_expr(p);
                LIST_PUSH(p->arena, &elems, Expr, el);
                if (!eat(p, TOK_COMMA)) break;
            }
            Span end = cur(p).span;
            expect(p, TOK_RBRACKET);
            Expr *e = mkexpr(p, EXPR_ARRAY_LIT, span_merge(span, end));
            e->array_lit = elems;
            return e;
        }

        /* grouped expression */
        case TOK_LPAREN: {
            advance(p);
            Expr *e = parse_expr(p);
            expect(p, TOK_RPAREN);
            return e;
        }

        /* if-as-expression */
        case TOK_IF: {
            advance(p);
            Expr *cond  = parse_expr(p);
            StmtList tb = parse_block(p);
            StmtList eb = {0};
            if (eat(p, TOK_ELSE)) eb = parse_block(p);
            /* wrap block lists as block stmts */
            Stmt *then_ = ARENA_NEW(p->arena, Stmt);
            then_->kind = STMT_BLOCK; then_->block = tb; then_->span = span;
            Stmt *else_ = NULL;
            if (eb.len) {
                else_ = ARENA_NEW(p->arena, Stmt);
                else_->kind = STMT_BLOCK; else_->block = eb; else_->span = span;
            }
            Expr *e = mkexpr(p, EXPR_IF, span_merge(span, cur(p).span));
            e->if_expr.cond  = cond;
            e->if_expr.then_ = then_;
            e->if_expr.else_ = else_;
            return e;
        }

        /* when-as-expression */
        case TOK_WHEN: {
            advance(p);
            Expr *val = parse_expr(p);
            expect(p, TOK_LBRACE);
            WhenArmList arms = {0};
            while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
                WhenArm arm = {0};
                arm.span = cur(p).span;
                /* parse pattern(s): pat1 | pat2 | ... */
                Expr *pat = parse_expr_bp(p, 1);
                LIST_PUSH(p->arena, &arm.pats, Expr, pat);
                while (eat(p, TOK_PIPE)) {
                    pat = parse_expr_bp(p, 1);
                    LIST_PUSH(p->arena, &arm.pats, Expr, pat);
                }
                /* optional bind: `i32 name =>` */
                if (check(p, TOK_IDENT) && check2(p, TOK_FATARROW)) {
                    arm.bind = cur(p).sval;
                    advance(p);
                }
                expect(p, TOK_FATARROW);
                arm.body = parse_stmt(p);
                eat(p, TOK_COMMA);
                SLICE_PUSH(p->arena, &arms, WhenArm, arm);
            }
            Span end = cur(p).span;
            expect(p, TOK_RBRACE);
            Expr *e = mkexpr(p, EXPR_WHEN, span_merge(span, end));
            e->when.cond = val;
            e->when.arms = arms;
            return e;
        }

        default:
            fatal_at(span, "unexpected token %s in expression", tok_kind_str(t.kind));
    }
}

/* postfix: call, index, field access, .*, .^ */
static Expr *parse_postfix(Parser *p, Expr *e) {
    for (;;) {
        Span span = e->span;
        if (check(p, TOK_LPAREN)) {
            ExprList args = parse_args(p);
            Expr *call = mkexpr(p, EXPR_CALL, span_merge(span, cur(p).span));
            call->call.callee = e;
            call->call.args   = args;
            e = call;
        } else if (check(p, TOK_LBRACKET)) {
            advance(p);
            Expr *idx = parse_expr(p);
            Span end = cur(p).span;
            expect(p, TOK_RBRACKET);
            Expr *ie = mkexpr(p, EXPR_INDEX, span_merge(span, end));
            ie->index.arr = e;
            ie->index.idx = idx;
            e = ie;
        } else if (check(p, TOK_DOTSTAR)) {
            advance(p);
            Expr *d = mkexpr(p, EXPR_DEREF, span_merge(span, cur(p).span));
            d->deref.operand = e;
            e = d;
        } else if (check(p, TOK_DOTCARET)) {
            advance(p);
            Expr *d = mkexpr(p, EXPR_SMARTDEREF, span_merge(span, cur(p).span));
            d->deref.operand = e;
            e = d;
        } else if (check(p, TOK_DOT)) {
            advance(p);
            Token fname = expect(p, TOK_IDENT);
            Expr *fe = mkexpr(p, EXPR_FIELD, span_merge(span, fname.span));
            fe->field.obj   = e;
            fe->field.field = fname.sval;
            e = fe;
        } else {
            break;
        }
    }
    return e;
}

static Expr *parse_expr_bp(Parser *p, int min_bp) {
    Expr *lhs = parse_postfix(p, parse_primary(p));

    for (;;) {
        TokenKind op = cur(p).kind;
        int lbp, rbp;
        infix_bp(op, &lbp, &rbp);
        if (lbp < min_bp) break;

        Span op_span = cur(p).span;
        advance(p);
        (void)op_span;

        /* assign ops have lbp=0 and never reach here; only binary ops do */
        Expr *rhs = parse_expr_bp(p, rbp);
        Expr *e   = mkexpr(p, EXPR_BINOP, span_merge(lhs->span, rhs->span));
        e->binop.l  = lhs;
        e->binop.op = tok_to_binop(op);
        e->binop.r  = rhs;
        lhs = e;
    }
    return lhs;
}

static Expr *parse_expr(Parser *p) {
    return parse_expr_bp(p, 1);
}

/* ── Statements ───────────────────────────────────────────────────────────── */

static StmtList parse_block(Parser *p) {
    expect(p, TOK_LBRACE);
    StmtList list = {0};
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        Stmt *s = parse_stmt(p);
        LIST_PUSH(p->arena, &list, Stmt, s);
    }
    expect(p, TOK_RBRACE);
    return list;
}

static Stmt *mkstmt(Parser *p, StmtKind k, Span span) {
    Stmt *s = ARENA_NEW(p->arena, Stmt);
    s->kind = k; s->span = span;
    return s;
}

/* variable declaration: name: type = expr  or  name: type : expr  or  name, name: !type = expr */
static int is_var_decl(Parser *p) {
    /* simple heuristic: ident followed by ':', possibly followed by '_' */
    return (check(p, TOK_IDENT) || check(p, TOK_UNDER)) &&
           (check2(p, TOK_COLON));
}

static Stmt *parse_let(Parser *p) {
    Span   span = cur(p).span;

    /* collect names (possibly two for failable destructure: val, err) */
    const char *name1 = NULL, *name2 = NULL;
    if (check(p, TOK_UNDER)) { advance(p); name1 = "_"; }
    else { name1 = expect(p, TOK_IDENT).sval; }

    if (eat(p, TOK_COMMA)) {
        if (check(p, TOK_UNDER)) { advance(p); name2 = "_"; }
        else name2 = expect(p, TOK_IDENT).sval;
    }

    expect(p, TOK_COLON);

    /* type annotation */
    Type *ty = parse_type(p);

    /* : = immutable,  = = mutable */
    int mut = 0;
    if (eat(p, TOK_EQ)) {
        mut = 1;
    } else if (eat(p, TOK_COLON)) {
        mut = 0;
    } else {
        fatal_at(cur(p).span, "expected '=' or ':' after type in variable declaration");
    }

    Expr *init = parse_expr(p);

    if (name2) {
        /* failable destructure — emit two let stmts wrapped in a block */
        /* For now emit a synthetic block with two lets sharing the same init */
        Stmt *block = mkstmt(p, STMT_BLOCK, span);
        StmtList bl = {0};

        Stmt *s1 = mkstmt(p, STMT_LET, span);
        s1->let.name    = name1;
        s1->let.ty      = ty->ptr.inner; /* !T -> T for the value */
        s1->let.mutable = mut;
        s1->let.init    = init;
        LIST_PUSH(p->arena, &bl, Stmt, s1);

        /* err name gets the error side — we mark with a special type for sema */
        Stmt *s2 = mkstmt(p, STMT_LET, span);
        s2->let.name    = name2;
        s2->let.ty      = ty;   /* keep !T for the error */
        s2->let.mutable = mut;
        s2->let.init    = init;
        LIST_PUSH(p->arena, &bl, Stmt, s2);

        block->block = bl;
        return block;
    }

    Stmt *s = mkstmt(p, STMT_LET, span_merge(span, init->span));
    s->let.name    = name1;
    s->let.ty      = ty;
    s->let.mutable = mut;
    s->let.init    = init;
    return s;
}

static Stmt *parse_stmt(Parser *p) {
    Span span = cur(p).span;

    /* block */
    if (check(p, TOK_LBRACE)) {
        StmtList bl = parse_block(p);
        Stmt *s = mkstmt(p, STMT_BLOCK, span);
        s->block = bl;
        return s;
    }

    /* variable declaration */
    if (is_var_decl(p)) return parse_let(p);

    /* ret */
    if (check(p, TOK_RET)) {
        advance(p);
        Expr *val = NULL;
        if (!check(p, TOK_RBRACE) && !check(p, TOK_EOF))
            val = parse_expr(p);
        Stmt *s = mkstmt(p, STMT_RET, span);
        s->ret.val = val;
        return s;
    }

    /* if */
    if (check(p, TOK_IF)) {
        advance(p);
        IfBranchList branches = {0};
        Expr    *cond = parse_expr(p);
        StmtList body = parse_block(p);
        IfBranch branch = { .cond = cond, .body = body };
        SLICE_PUSH(p->arena, &branches, IfBranch, branch);

        while (check(p, TOK_ELIF)) {
            advance(p);
            Expr    *ec = parse_expr(p);
            StmtList eb = parse_block(p);
            IfBranch eb2 = { .cond = ec, .body = eb };
            SLICE_PUSH(p->arena, &branches, IfBranch, eb2);
        }

        StmtList else_body = {0};
        if (eat(p, TOK_ELSE)) else_body = parse_block(p);

        Stmt *s = mkstmt(p, STMT_IF, span);
        s->if_.branches  = branches;
        s->if_.else_body = else_body;
        return s;
    }

    /* while */
    if (check(p, TOK_WHILE)) {
        advance(p);
        Expr *cond = parse_expr(p);
        const char *do_fn = NULL;
        if (eat(p, TOK_FATARROW)) {
            do_fn = expect(p, TOK_IDENT).sval;
            /* consume () if present */
            if (check(p, TOK_LPAREN)) { advance(p); expect(p, TOK_RPAREN); }
        }
        StmtList body = parse_block(p);
        Stmt *s = mkstmt(p, STMT_WHILE, span);
        s->while_.cond  = cond;
        s->while_.do_fn = do_fn;
        s->while_.body  = body;
        return s;
    }

    /* when (statement) */
    if (check(p, TOK_WHEN)) {
        advance(p);
        Expr *val = parse_expr(p);
        expect(p, TOK_LBRACE);
        WhenArmList arms = {0};
        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            WhenArm arm = {0};
            arm.span = cur(p).span;
            Expr *pat = parse_expr_bp(p, 1);
            LIST_PUSH(p->arena, &arm.pats, Expr, pat);
            while (eat(p, TOK_PIPE)) {
                pat = parse_expr_bp(p, 1);
                LIST_PUSH(p->arena, &arm.pats, Expr, pat);
            }
            if (check(p, TOK_IDENT) && check2(p, TOK_FATARROW)) {
                arm.bind = cur(p).sval;
                advance(p);
            }
            expect(p, TOK_FATARROW);
            arm.body = parse_stmt(p);
            eat(p, TOK_COMMA);
            SLICE_PUSH(p->arena, &arms, WhenArm, arm);
        }
        expect(p, TOK_RBRACE);
        Stmt *s = mkstmt(p, STMT_WHEN, span);
        s->when.val  = val;
        s->when.arms = arms;
        return s;
    }

    /* for */
    if (check(p, TOK_FOR)) {
        advance(p);
        ForClause clause = {0};

        /* detect C-style: for i := 0, ... */
        if (check(p, TOK_IDENT) && check2(p, TOK_COLON)) {
            /* Could be: for i := 0,  */
        }

        /* for IDENT in EXPR..EXPR  — range with named variable
           for IDENT in EXPR        — for-each with 'in' keyword
           for IDENT, IDENT in EXPR — for-each with index and element
           for EXPR..EXPR           — anonymous range
           for EXPR => COLLECTION   — for-each with fat-arrow */
        if (check(p, TOK_INT) || check(p, TOK_IDENT)) {
            /* peek ahead: if next is 'in' or ',', this is the variable name */
            if (check(p, TOK_IDENT) && (check2(p, TOK_IN) || check2(p, TOK_COMMA))) {
                const char *elem_name = cur(p).sval;
                advance(p); /* consume elem name */
                const char *idx_name = NULL;
                if (eat(p, TOK_COMMA)) {
                    /* for idx, elem in EXPR */
                    idx_name  = elem_name;
                    elem_name = cur(p).sval;
                    expect(p, TOK_IDENT);
                }
                expect(p, TOK_IN);
                Expr *rhs = parse_expr(p); /* full expression — may be BINOP_RANGE */
                if (rhs->kind == EXPR_BINOP &&
                    (rhs->binop.op == BINOP_RANGE || rhs->binop.op == BINOP_RANGE_INC)) {
                    clause.kind      = FOR_RANGE;
                    clause.inclusive = (rhs->binop.op == BINOP_RANGE_INC);
                    clause.elem      = elem_name;
                    clause.iter      = rhs->binop.l;
                    clause.range_end = rhs->binop.r;
                } else {
                    clause.kind = idx_name ? FOR_EACH_IDX : FOR_EACH;
                    clause.elem = elem_name;
                    clause.idx  = idx_name;
                    clause.iter = rhs;
                }
            } else {
                Expr *start = parse_expr_bp(p, 1);
                if (check(p, TOK_DOTDOT) || check(p, TOK_DOTDOTEQ)) {
                    clause.kind      = FOR_RANGE;
                    clause.inclusive = check(p, TOK_DOTDOTEQ);
                    advance(p);
                    clause.iter      = start;
                    clause.range_end = parse_expr(p);
                } else if (check(p, TOK_FATARROW)) {
                    /* for e => arr */
                    advance(p);
                    Expr *arr = parse_expr(p);
                    clause.kind = FOR_EACH;
                    clause.elem = start->ident.name;
                    clause.iter = arr;
                } else {
                    fatal_at(cur(p).span, "unexpected token in for loop");
                }
            }
        }

        StmtList body = parse_block(p);
        Stmt *s = mkstmt(p, STMT_FOR, span);
        s->for_.clause = clause;
        s->for_.body   = body;
        return s;
    }

    /* defer */
    if (check(p, TOK_DEFER)) {
        advance(p);
        StmtList dl = {0};
        if (check(p, TOK_LBRACE)) {
            dl = parse_block(p);
        } else {
            Stmt *inner = parse_stmt(p);
            LIST_PUSH(p->arena, &dl, Stmt, inner);
        }
        Stmt *s = mkstmt(p, STMT_DEFER, span);
        s->defer = dl;
        return s;
    }

    /* break / continue */
    if (check(p, TOK_BREAK)) {
        advance(p);
        const char *label = NULL;
        if (check(p, TOK_IDENT)) { label = cur(p).sval; advance(p); }
        Stmt *s = mkstmt(p, STMT_BREAK, span);
        s->break_.label = label;
        return s;
    }
    if (check(p, TOK_CONTINUE)) {
        advance(p);
        const char *label = NULL;
        if (check(p, TOK_IDENT)) { label = cur(p).sval; advance(p); }
        Stmt *s = mkstmt(p, STMT_CONTINUE, span);
        s->cont.label = label;
        return s;
    }

    /* expression or assignment */
    Expr *lhs = parse_expr(p);

    if (is_assign_op(cur(p).kind)) {
        TokenKind op = cur(p).kind;
        advance(p);
        Expr *rhs = parse_expr(p);
        Stmt *s = mkstmt(p, STMT_ASSIGN, span_merge(lhs->span, rhs->span));
        s->assign.target = lhs;
        s->assign.op     = tok_to_assignop(op);
        s->assign.val    = rhs;
        return s;
    }

    Stmt *s = mkstmt(p, STMT_EXPR, lhs->span);
    s->expr = lhs;
    return s;
}

/* ── Items ────────────────────────────────────────────────────────────────── */

static AttrList parse_attrs(Parser *p) {
    AttrList attrs = {0};
    while (check(p, TOK_BUILTIN)) {
        const char *name = cur(p).sval;
        Span span = cur(p).span;
        advance(p);
        if (!strcmp(name, "packed")) {
            Attr a = { .kind = ATTR_PACKED, .arg = NULL };
            SLICE_PUSH(p->arena, &attrs, Attr, a);
        } else if (!strcmp(name, "align")) {
            expect(p, TOK_LPAREN);
            Expr *arg = parse_expr(p);
            expect(p, TOK_RPAREN);
            Attr a = { .kind = ATTR_ALIGN, .arg = arg };
            SLICE_PUSH(p->arena, &attrs, Attr, a);
        }
        /* unknown builtins before struct are silently skipped */
    }
    return attrs;
}

static Item *parse_item(Parser *p) {
    Span span = cur(p).span;

    /* collect struct attrs before keywords */
    AttrList attrs = {0};
    if (check(p, TOK_BUILTIN) &&
        (!strcmp(cur(p).sval, "packed") || !strcmp(cur(p).sval, "align")))
    {
        attrs = parse_attrs(p);
    }

    /* import(...) */
    if (check(p, TOK_IMPORT)) {
        advance(p);
        expect(p, TOK_LPAREN);
        ImportList imports = {0};
        while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
            const char *alias = expect(p, TOK_IDENT).sval;
            expect(p, TOK_EQ);
            const char *path  = expect(p, TOK_STR).sval;
            ImportEntry ie = { .alias = alias, .path = path };
            SLICE_PUSH(p->arena, &imports, ImportEntry, ie);
            eat(p, TOK_COMMA);
        }
        expect(p, TOK_RPAREN);
        Item *item = ARENA_NEW(p->arena, Item);
        item->kind    = ITEM_IMPORT;
        item->span    = span_merge(span, cur(p).span);
        item->imports = imports;
        return item;
    }

    /* extern fn */
    if (check(p, TOK_EXTERN)) {
        advance(p);
        expect(p, TOK_FN);
        const char *name = expect(p, TOK_IDENT).sval;
        expect(p, TOK_LPAREN);
        ParamList params = {0};
        int variadic = 0;
        while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
            if (cur(p).kind == TOK_DOTDOT || cur(p).kind == TOK_DOTDOTEQ) {
                variadic = 1; advance(p); break;
            }
            const char *pn = "_";
            if (check(p, TOK_IDENT) && check2(p, TOK_COLON)) {
                pn = cur(p).sval; advance(p); advance(p);
            }
            Type *pt = parse_type(p);
            Param par = { .name = pn, .ty = pt };
            SLICE_PUSH(p->arena, &params, Param, par);
            eat(p, TOK_COMMA);
        }
        expect(p, TOK_RPAREN);
        Type *ret = NULL;
        if (eat(p, TOK_ARROW)) ret = parse_type(p);
        Item *item = ARENA_NEW(p->arena, Item);
        item->kind             = ITEM_EXTERN_FN;
        item->name             = name;
        item->span             = span_merge(span, cur(p).span);
        item->extern_fn.params   = params;
        item->extern_fn.ret      = ret;
        item->extern_fn.variadic = variadic;
        return item;
    }

    /* inline? fn */
    int is_inline = eat(p, TOK_INLINE);

    if (check(p, TOK_FN)) {
        advance(p);
        const char *name = expect(p, TOK_IDENT).sval;
        expect(p, TOK_LPAREN);
        ParamList params = {0};
        int variadic = 0;
        while (!check(p, TOK_RPAREN) && !check(p, TOK_EOF)) {
            if (cur(p).kind == TOK_DOTDOT) { variadic = 1; advance(p); break; }
            const char *pn = expect(p, TOK_IDENT).sval;
            expect(p, TOK_COLON);
            Type *pt = parse_type(p);
            Param par = { .name = pn, .ty = pt };
            SLICE_PUSH(p->arena, &params, Param, par);
            eat(p, TOK_COMMA);
        }
        expect(p, TOK_RPAREN);
        Type *ret = NULL;
        if (eat(p, TOK_ARROW)) ret = parse_type(p);
        StmtList body = parse_block(p);
        Item *item = ARENA_NEW(p->arena, Item);
        item->kind          = ITEM_FN;
        item->name          = name;
        item->span          = span_merge(span, cur(p).span);
        item->fn.params     = params;
        item->fn.ret        = ret;
        item->fn.body       = body;
        item->fn.is_inline  = is_inline;
        item->fn.variadic   = variadic;
        return item;
    }

    if (is_inline)
        fatal_at(span, "'inline' must be followed by 'fn'");

    /* struct */
    if (check(p, TOK_STRUCT)) {
        advance(p);
        const char *name = expect(p, TOK_IDENT).sval;
        expect(p, TOK_LBRACE);
        FieldList fields = {0};
        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            const char *fn2 = expect(p, TOK_IDENT).sval;
            expect(p, TOK_COLON);
            Type *ft = parse_type(p);
            Field f = { .name = fn2, .ty = ft };
            SLICE_PUSH(p->arena, &fields, Field, f);
            eat(p, TOK_COMMA);
        }
        expect(p, TOK_RBRACE);
        Item *item = ARENA_NEW(p->arena, Item);
        item->kind            = ITEM_STRUCT;
        item->name            = name;
        item->span            = span_merge(span, cur(p).span);
        item->struct_.fields  = fields;
        item->struct_.attrs   = attrs;
        return item;
    }

    /* impl */
    if (check(p, TOK_IMPL)) {
        advance(p);
        const char *ty_name = expect(p, TOK_IDENT).sval;
        expect(p, TOK_LBRACE);
        ItemList methods = {0};
        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            Item *m = parse_item(p);
            LIST_PUSH(p->arena, &methods, Item, m);
        }
        expect(p, TOK_RBRACE);
        Item *item = ARENA_NEW(p->arena, Item);
        item->kind           = ITEM_IMPL;
        item->name           = ty_name;
        item->span           = span_merge(span, cur(p).span);
        item->impl.ty_name   = ty_name;
        item->impl.methods   = methods;
        return item;
    }

    /* enum */
    if (check(p, TOK_ENUM)) {
        advance(p);
        const char *name = expect(p, TOK_IDENT).sval;
        Type *backing = NULL;
        if (eat(p, TOK_FATARROW)) backing = parse_type(p);
        expect(p, TOK_LBRACE);
        EnumVariantList variants = {0};
        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            const char *vn = expect(p, TOK_IDENT).sval;
            Expr *vval = NULL;
            if (eat(p, TOK_EQ)) vval = parse_expr(p);
            EnumVariant ev = { .name = vn, .val = vval };
            SLICE_PUSH(p->arena, &variants, EnumVariant, ev);
            eat(p, TOK_COMMA);
        }
        expect(p, TOK_RBRACE);
        Item *item = ARENA_NEW(p->arena, Item);
        item->kind              = ITEM_ENUM;
        item->name              = name;
        item->span              = span_merge(span, cur(p).span);
        item->enum_.backing     = backing;
        item->enum_.variants    = variants;
        return item;
    }

    /* unn */
    if (check(p, TOK_UNN)) {
        advance(p);
        const char *name = expect(p, TOK_IDENT).sval;
        int tagged = 0;
        if (eat(p, TOK_FATARROW)) {
            expect(p, TOK_ENUM);
            tagged = 1;
        }
        expect(p, TOK_LBRACE);
        FieldList fields = {0};
        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            const char *fn2 = expect(p, TOK_IDENT).sval;
            Type *ft = NULL;
            if (eat(p, TOK_COLON)) ft = parse_type(p);
            Field f = { .name = fn2, .ty = ft };
            SLICE_PUSH(p->arena, &fields, Field, f);
            eat(p, TOK_COMMA);
        }
        expect(p, TOK_RBRACE);
        Item *item = ARENA_NEW(p->arena, Item);
        item->kind           = ITEM_UNION;
        item->name           = name;
        item->span           = span_merge(span, cur(p).span);
        item->union_.fields  = fields;
        item->union_.tagged  = tagged;
        return item;
    }

    /* type alias */
    if (check(p, TOK_TYPE)) {
        advance(p);
        const char *name = expect(p, TOK_IDENT).sval;
        expect(p, TOK_EQ);
        Type *ty = parse_type(p);
        Item *item = ARENA_NEW(p->arena, Item);
        item->kind             = ITEM_TYPE_ALIAS;
        item->name             = name;
        item->span             = span_merge(span, cur(p).span);
        item->type_alias.ty    = ty;
        return item;
    }

    /* global variable: name: type = expr  or  name: type : expr */
    if (check(p, TOK_IDENT) && check2(p, TOK_COLON)) {
        const char *name = cur(p).sval;
        advance(p); advance(p); /* name : */
        Type *ty = parse_type(p);
        int mut = 0;
        if (eat(p, TOK_EQ))    mut = 1;
        else if (eat(p, TOK_COLON)) mut = 0;
        else fatal_at(cur(p).span, "expected '=' or ':' in global declaration");
        Expr *init = parse_expr(p);
        Item *item = ARENA_NEW(p->arena, Item);
        item->kind           = ITEM_GLOBAL;
        item->name           = name;
        item->span           = span_merge(span, init->span);
        item->global.ty      = ty;
        item->global.mutable = mut;
        item->global.init    = init;
        return item;
    }

    fatal_at(span, "unexpected token %s at top level", tok_kind_str(cur(p).kind));
}

/* ── Entry point ──────────────────────────────────────────────────────────── */

Module *parse(const char *src, uint32_t file_id, Arena *arena) {
    Parser p = {0};
    lexer_init(&p.lexer, src, file_id, arena);
    p.arena = arena;
    /* prime the two-token lookahead */
    p.cur  = lexer_next(&p.lexer);
    p.peek = lexer_next(&p.lexer);

    Module *mod = ARENA_NEW(arena, Module);
    mod->arena  = arena;

    while (!check(&p, TOK_EOF)) {
        Item *item = parse_item(&p);
        LIST_PUSH(arena, &mod->items, Item, item);
    }

    return mod;
}
