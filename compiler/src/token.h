#pragma once
#include "span.h"
#include <stdint.h>

typedef enum {
    /* literals */
    TOK_INT, TOK_FLOAT, TOK_STR, TOK_CHAR,
    TOK_TRUE, TOK_FALSE,

    /* keywords */
    TOK_FN, TOK_RET, TOK_IF, TOK_ELIF, TOK_ELSE,
    TOK_WHEN, TOK_WHILE, TOK_FOR,
    TOK_STRUCT, TOK_IMPL, TOK_ENUM, TOK_UNN,
    TOK_TYPE, TOK_IMPORT, TOK_EXTERN, TOK_INLINE,
    TOK_DEFER, TOK_AND, TOK_OR, TOK_NOT,
    TOK_UNDEF, TOK_NULL, TOK_BREAK, TOK_CONTINUE, TOK_ASM,
    TOK_GOTO,

    /* identifier / builtin */
    TOK_IDENT,      /* foo */
    TOK_BUILTIN,    /* @name  (stored without the @) */
    TOK_UNDER,      /* _  */

    /* arithmetic */
    TOK_PLUS, TOK_MINUS, TOK_STAR, TOK_SLASH, TOK_PERCENT,
    TOK_INC, TOK_DEC,    /* ++ -- */

    /* bitwise */
    TOK_AMP, TOK_PIPE, TOK_CARET, TOK_TILDE, TOK_SHL, TOK_SHR,

    /* comparison */
    TOK_EQEQ, TOK_BANGEQ, TOK_LT, TOK_GT, TOK_LTEQ, TOK_GTEQ,

    /* logical / failable */
    TOK_BANG,       /* ! */

    /* assignment */
    TOK_EQ,
    TOK_PLUSEQ, TOK_MINUSEQ, TOK_STAREQ, TOK_SLASHEQ, TOK_PERCENTEQ,
    TOK_AMPEQ,  TOK_PIPEEQ,  TOK_CARETEQ,
    TOK_SHLEQ,  TOK_SHREQ,

    /* punctuation */
    TOK_ARROW,      /* -> */
    TOK_FATARROW,   /* => */
    TOK_DOT,        /* .  */
    TOK_DOTSTAR,    /* .* */
    TOK_DOTCARET,   /* .^ */
    TOK_DOTDOT,     /* .. */
    TOK_DOTDOTEQ,   /* ..= */
    TOK_COLON,      /* :  */
    TOK_COLONEQ,    /* := */
    TOK_COLONCOLON, /* :: */
    TOK_COMMA,      /* ,  */
    TOK_SEMI,       /* ;  */

    /* delimiters */
    TOK_LPAREN, TOK_RPAREN,
    TOK_LBRACE, TOK_RBRACE,
    TOK_LBRACKET, TOK_RBRACKET,

    TOK_EOF,
} TokenKind;

typedef struct {
    TokenKind kind;
    Span      span;
    union {
        uint64_t    ival;
        double      fval;
        const char *sval;   /* interned / arena string */
        uint8_t     cval;
    };
    const char *suffix;     /* numeric literal type suffix ("u8", "f32", ...) or NULL */
} Token;

const char *tok_kind_str(TokenKind k);
