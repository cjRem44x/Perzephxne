#include "lexer.h"
#include "error.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdio.h>

void lexer_init(Lexer *l, const char *src, uint32_t file_id, Arena *arena) {
    l->src     = src;
    l->cur     = src;
    l->pos     = (Pos){ .line = 1, .col = 1 };
    l->file_id = file_id;
    l->arena   = arena;
}

static char peek(Lexer *l)          { return *l->cur; }
static char peek2(Lexer *l)         { return l->cur[1]; }

static char advance(Lexer *l) {
    char c = *l->cur++;
    if (c == '\n') { l->pos.line++; l->pos.col = 1; }
    else            { l->pos.col++; }
    return c;
}

static Span make_span(Lexer *l, Pos start) {
    return (Span){ .start = start, .end = l->pos, .file_id = l->file_id };
}

static void skip_whitespace(Lexer *l) {
    for (;;) {
        while (peek(l) && isspace((unsigned char)peek(l))) advance(l);

        if (peek(l) == '#') {
            if (peek2(l) == '#') {
                /* multi-line comment: ## ... ## */
                advance(l); advance(l);
                while (peek(l)) {
                    if (peek(l) == '#' && peek2(l) == '#') {
                        advance(l); advance(l);
                        break;
                    }
                    advance(l);
                }
            } else {
                /* single-line comment */
                while (peek(l) && peek(l) != '\n') advance(l);
            }
        } else {
            break;
        }
    }
}

static uint8_t parse_escape(Lexer *l, Pos start) {
    char c = advance(l);
    switch (c) {
        case 'n':  return '\n';
        case 't':  return '\t';
        case 'r':  return '\r';
        case '0':  return '\0';
        case '\\': return '\\';
        case '"':  return '"';
        case '\'': return '\'';
        case 'x': {
            char hi = advance(l), lo = advance(l);
            if (!isxdigit((unsigned char)hi) || !isxdigit((unsigned char)lo))
                fatal_at(make_span(l, start), "invalid hex escape");
            return (uint8_t)((isdigit((unsigned char)hi) ? hi - '0' : tolower((unsigned char)hi) - 'a' + 10) << 4
                           | (isdigit((unsigned char)lo) ? lo - '0' : tolower((unsigned char)lo) - 'a' + 10));
        }
        default:
            fatal_at(make_span(l, start), "unknown escape sequence '\\%c'", c);
    }
}

static Token lex_string(Lexer *l, Pos start) {
    /* opening '"' already consumed */
    char buf[4096];
    size_t len = 0;
    while (peek(l) && peek(l) != '"') {
        if (len >= sizeof(buf) - 1)
            fatal_at(make_span(l, start), "string literal too long");
        if (peek(l) == '\\') {
            advance(l);
            buf[len++] = (char)parse_escape(l, start);
        } else {
            buf[len++] = advance(l);
        }
    }
    if (!peek(l)) fatal_at(make_span(l, start), "unterminated string literal");
    advance(l); /* closing '"' */
    buf[len] = '\0';
    return (Token){
        .kind = TOK_STR,
        .span = make_span(l, start),
        .sval = arena_strndup(l->arena, buf, len)
    };
}

static Token lex_char(Lexer *l, Pos start) {
    /* opening '\'' already consumed */
    uint8_t val;
    if (peek(l) == '\\') {
        advance(l);
        val = parse_escape(l, start);
    } else {
        val = (uint8_t)advance(l);
    }
    if (peek(l) != '\'') fatal_at(make_span(l, start), "unterminated char literal");
    advance(l);
    return (Token){ .kind = TOK_CHAR, .span = make_span(l, start), .cval = val };
}

static Token lex_number(Lexer *l, Pos start) {
    const char *begin = l->cur - 1; /* we already advanced the first digit */
    int is_float = 0;

    /* determine base */
    int base = 10;
    if (*begin == '0' && (peek(l) == 'x' || peek(l) == 'X')) {
        advance(l); base = 16; begin = l->cur;
    } else if (*begin == '0' && (peek(l) == 'b' || peek(l) == 'B')) {
        advance(l); base = 2;  begin = l->cur;
    } else if (*begin == '0' && (peek(l) == 'o' || peek(l) == 'O')) {
        advance(l); base = 8;  begin = l->cur;
    }

    /* consume digits (allow _ separator) */
    while ((base == 16 && isxdigit((unsigned char)peek(l))) ||
           (base == 10 && isdigit((unsigned char)peek(l)))  ||
           (base ==  8 && peek(l) >= '0' && peek(l) <= '7') ||
           (base ==  2 && (peek(l) == '0' || peek(l) == '1')) ||
           peek(l) == '_') {
        advance(l);
    }

    /* float? */
    if (base == 10 && peek(l) == '.' && peek2(l) != '.' && peek2(l) != '=') {
        is_float = 1;
        advance(l);
        while (isdigit((unsigned char)peek(l)) || peek(l) == '_') advance(l);
        if (peek(l) == 'e' || peek(l) == 'E') {
            advance(l);
            if (peek(l) == '+' || peek(l) == '-') advance(l);
            while (isdigit((unsigned char)peek(l))) advance(l);
        }
    }

    /* optional type suffix — just consume, type inference handles it */
    if (peek(l) == 'u' || peek(l) == 'i' || peek(l) == 'f') {
        while (isalnum((unsigned char)peek(l))) advance(l);
    }

    Span span = make_span(l, start);

    /* build a clean copy without underscores */
    char clean[128]; size_t ci = 0;
    for (const char *p = begin; p < l->cur; p++) {
        if (*p != '_' && !isalpha((unsigned char)*p)) {
            if (ci < sizeof(clean) - 1) clean[ci++] = *p;
        }
    }
    clean[ci] = '\0';

    if (is_float) {
        return (Token){ .kind = TOK_FLOAT, .span = span, .fval = strtod(clean, NULL) };
    } else {
        return (Token){ .kind = TOK_INT, .span = span,
                        .ival = (uint64_t)strtoull(clean, NULL, base) };
    }
}

/* resolve identifier → keyword or TOK_IDENT */
static TokenKind keyword_or_ident(const char *s) {
    if (!strcmp(s, "fn"))       return TOK_FN;
    if (!strcmp(s, "ret"))      return TOK_RET;
    if (!strcmp(s, "if"))       return TOK_IF;
    if (!strcmp(s, "elif"))     return TOK_ELIF;
    if (!strcmp(s, "else"))     return TOK_ELSE;
    if (!strcmp(s, "when"))     return TOK_WHEN;
    if (!strcmp(s, "while"))    return TOK_WHILE;
    if (!strcmp(s, "for"))      return TOK_FOR;
    if (!strcmp(s, "struct"))   return TOK_STRUCT;
    if (!strcmp(s, "impl"))     return TOK_IMPL;
    if (!strcmp(s, "enum"))     return TOK_ENUM;
    if (!strcmp(s, "unn"))      return TOK_UNN;
    if (!strcmp(s, "type"))     return TOK_TYPE;
    if (!strcmp(s, "import"))   return TOK_IMPORT;
    if (!strcmp(s, "extern"))   return TOK_EXTERN;
    if (!strcmp(s, "inline"))   return TOK_INLINE;
    if (!strcmp(s, "defer"))    return TOK_DEFER;
    if (!strcmp(s, "and"))      return TOK_AND;
    if (!strcmp(s, "or"))       return TOK_OR;
    if (!strcmp(s, "not"))      return TOK_NOT;
    if (!strcmp(s, "true"))     return TOK_TRUE;
    if (!strcmp(s, "false"))    return TOK_FALSE;
    if (!strcmp(s, "undef"))    return TOK_UNDEF;
    if (!strcmp(s, "null"))     return TOK_NULL;
    if (!strcmp(s, "break"))    return TOK_BREAK;
    if (!strcmp(s, "continue")) return TOK_CONTINUE;
    if (!strcmp(s, "asm"))      return TOK_ASM;
    if (!strcmp(s, "_"))        return TOK_UNDER;
    return TOK_IDENT;
}

Token lexer_next(Lexer *l) {
    skip_whitespace(l);
    Pos start = l->pos;

    if (!peek(l))
        return (Token){ .kind = TOK_EOF, .span = make_span(l, start) };

    char c = advance(l);

    /* builtin: @name */
    if (c == '@') {
        if (!isalpha((unsigned char)peek(l)) && peek(l) != '_')
            fatal_at(make_span(l, start), "expected builtin name after '@'");
        const char *beg = l->cur;
        while (isalnum((unsigned char)peek(l)) || peek(l) == '_' || peek(l) == '.') advance(l);
        size_t len = (size_t)(l->cur - beg);
        return (Token){ .kind = TOK_BUILTIN, .span = make_span(l, start),
                        .sval = arena_strndup(l->arena, beg, len) };
    }

    /* string */
    if (c == '"') return lex_string(l, start);

    /* char */
    if (c == '\'') return lex_char(l, start);

    /* number */
    if (isdigit((unsigned char)c)) return lex_number(l, start);

    /* identifier / keyword */
    if (isalpha((unsigned char)c) || c == '_') {
        const char *beg = l->cur - 1;
        while (isalnum((unsigned char)peek(l)) || peek(l) == '_') advance(l);
        size_t len = (size_t)(l->cur - beg);
        char   buf[256];
        if (len >= sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, beg, len); buf[len] = '\0';
        TokenKind k = keyword_or_ident(buf);
        Token t = { .kind = k, .span = make_span(l, start) };
        if (k == TOK_IDENT) t.sval = arena_strndup(l->arena, beg, len);
        return t;
    }

    /* dot-prefixed operators */
    if (c == '.') {
        if (peek(l) == '*') { advance(l); return (Token){ .kind = TOK_DOTSTAR,  .span = make_span(l, start) }; }
        if (peek(l) == '^') { advance(l); return (Token){ .kind = TOK_DOTCARET, .span = make_span(l, start) }; }
        if (peek(l) == '.') {
            advance(l);
            if (peek(l) == '=') { advance(l); return (Token){ .kind = TOK_DOTDOTEQ, .span = make_span(l, start) }; }
            return (Token){ .kind = TOK_DOTDOT, .span = make_span(l, start) };
        }
        return (Token){ .kind = TOK_DOT, .span = make_span(l, start) };
    }

    /* two-char and single-char operators */
#define TOK1(k)        return (Token){ .kind = (k), .span = make_span(l, start) }
#define TOK2(c2, k2, k1) \
    if (peek(l) == (c2)) { advance(l); TOK1(k2); } TOK1(k1)

    switch (c) {
        case '+': TOK2('=', TOK_PLUSEQ,    TOK_PLUS);
        case '-': if (peek(l)=='>') { advance(l); TOK1(TOK_ARROW); } TOK2('=', TOK_MINUSEQ, TOK_MINUS);
        case '*': TOK2('=', TOK_STAREQ,    TOK_STAR);
        case '/': TOK2('=', TOK_SLASHEQ,   TOK_SLASH);
        case '%': TOK2('=', TOK_PERCENTEQ, TOK_PERCENT);
        case '~': TOK1(TOK_TILDE);
        case '^': TOK2('=', TOK_CARETEQ,   TOK_CARET);
        case '&': TOK2('=', TOK_AMPEQ,     TOK_AMP);
        case '|': TOK2('=', TOK_PIPEEQ,    TOK_PIPE);
        case '<':
            if (peek(l)=='<') {
                advance(l);
                TOK2('=', TOK_SHLEQ, TOK_SHL);
            }
            TOK2('=', TOK_LTEQ, TOK_LT);
        case '>':
            if (peek(l)=='>') {
                advance(l);
                TOK2('=', TOK_SHREQ, TOK_SHR);
            }
            TOK2('=', TOK_GTEQ, TOK_GT);
        case '=':
            if (peek(l)=='=') { advance(l); TOK1(TOK_EQEQ); }
            if (peek(l)=='>') { advance(l); TOK1(TOK_FATARROW); }
            TOK1(TOK_EQ);
        case '!': TOK2('=', TOK_BANGEQ, TOK_BANG);
        case '(': TOK1(TOK_LPAREN);
        case ')': TOK1(TOK_RPAREN);
        case '{': TOK1(TOK_LBRACE);
        case '}': TOK1(TOK_RBRACE);
        case '[': TOK1(TOK_LBRACKET);
        case ']': TOK1(TOK_RBRACKET);
        case ':': TOK1(TOK_COLON);
        case ',': TOK1(TOK_COMMA);
        case ';': TOK1(TOK_SEMI);
        default:
            fatal_at(make_span(l, start), "unexpected character '%c'", c);
    }

#undef TOK1
#undef TOK2
}

const char *tok_kind_str(TokenKind k) {
    switch (k) {
        case TOK_INT:      return "integer";
        case TOK_FLOAT:    return "float";
        case TOK_STR:      return "string";
        case TOK_CHAR:     return "char";
        case TOK_TRUE:     return "'true'";
        case TOK_FALSE:    return "'false'";
        case TOK_FN:       return "'fn'";
        case TOK_RET:      return "'ret'";
        case TOK_IF:       return "'if'";
        case TOK_ELIF:     return "'elif'";
        case TOK_ELSE:     return "'else'";
        case TOK_WHEN:     return "'when'";
        case TOK_WHILE:    return "'while'";
        case TOK_FOR:      return "'for'";
        case TOK_STRUCT:   return "'struct'";
        case TOK_IMPL:     return "'impl'";
        case TOK_ENUM:     return "'enum'";
        case TOK_UNN:      return "'unn'";
        case TOK_TYPE:     return "'type'";
        case TOK_IMPORT:   return "'import'";
        case TOK_EXTERN:   return "'extern'";
        case TOK_INLINE:   return "'inline'";
        case TOK_DEFER:    return "'defer'";
        case TOK_AND:      return "'and'";
        case TOK_OR:       return "'or'";
        case TOK_NOT:      return "'not'";
        case TOK_UNDEF:    return "'undef'";
        case TOK_NULL:     return "'null'";
        case TOK_BREAK:    return "'break'";
        case TOK_CONTINUE: return "'continue'";
        case TOK_ASM:      return "'asm'";
        case TOK_IDENT:    return "identifier";
        case TOK_BUILTIN:  return "builtin";
        case TOK_UNDER:    return "'_'";
        case TOK_PLUS:     return "'+'";
        case TOK_MINUS:    return "'-'";
        case TOK_STAR:     return "'*'";
        case TOK_SLASH:    return "'/'";
        case TOK_PERCENT:  return "'%'";
        case TOK_AMP:      return "'&'";
        case TOK_PIPE:     return "'|'";
        case TOK_CARET:    return "'^'";
        case TOK_TILDE:    return "'~'";
        case TOK_SHL:      return "'<<'";
        case TOK_SHR:      return "'>>'";
        case TOK_EQEQ:     return "'=='";
        case TOK_BANGEQ:   return "'!='";
        case TOK_LT:       return "'<'";
        case TOK_GT:       return "'>'";
        case TOK_LTEQ:     return "'<='";
        case TOK_GTEQ:     return "'>='";
        case TOK_BANG:     return "'!'";
        case TOK_EQ:       return "'='";
        case TOK_PLUSEQ:   return "'+='";
        case TOK_MINUSEQ:  return "'-='";
        case TOK_STAREQ:   return "'*='";
        case TOK_SLASHEQ:  return "'/='";
        case TOK_PERCENTEQ:return "'%='";
        case TOK_AMPEQ:    return "'&='";
        case TOK_PIPEEQ:   return "'|='";
        case TOK_CARETEQ:  return "'^='";
        case TOK_SHLEQ:    return "'<<='";
        case TOK_SHREQ:    return "'>>='";
        case TOK_ARROW:    return "'->'";
        case TOK_FATARROW: return "'=>'";
        case TOK_DOT:      return "'.'";
        case TOK_DOTSTAR:  return "'.*'";
        case TOK_DOTCARET: return "'.^'";
        case TOK_DOTDOT:   return "'..'";
        case TOK_DOTDOTEQ: return "'..='";
        case TOK_COLON:    return "':'";
        case TOK_COMMA:    return "','";
        case TOK_SEMI:     return "';'";
        case TOK_LPAREN:   return "'('";
        case TOK_RPAREN:   return "')'";
        case TOK_LBRACE:   return "'{'";
        case TOK_RBRACE:   return "'}'";
        case TOK_LBRACKET: return "'['";
        case TOK_RBRACKET: return "']'";
        case TOK_EOF:      return "<eof>";
        default:           return "?";
    }
}
