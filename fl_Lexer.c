#include <string.h>
#include "fl_Lexer.h"
#include "utils/vector_token.h"

const char *TWO_CHAR_OPSfl[] = {
        "==", "!=", "<=", ">=", "&&", "||", "++", "--",
        "->", "+=", "-=", "*=", "/=", "%=", "<<", ">>",
        ((void*)0)
    };
const char SINGLE_OPSfl[] = "+-*/%=<>!&|^~?:.#";

/* ─── Ключевые слова ───────────────────────────────────────────── */
const char *KEYWORDSfl[] = {
    "var", "func",
    ((void*)0)
};

const char *TYPESfl[] = {
    "int", "short", "long", "char", "float", "double", ((void*)0)
};

static int is_keyword(const char *s) {
    for (int i = 0; KEYWORDSfl[i]; i++)
        if (strcmp(KEYWORDSfl[i], s) == 0) return 1;
    return 0;
}

static int is_type(const char *s) {
    for (int i = 0; TYPESfl[i]; i++)
        if (strcmp(TYPESfl[i], s) == 0) return 1;
    return 0;
}

static const char *token_type_name(TokenType t) {
    switch (t) {
        case TOK_INT:       return "INT";
        case TOK_FLOAT:     return "FLOAT";
        case TOK_STRING:    return "STRING";
        case TOK_IDENT:     return "IDENT";
        case TOK_KEYWORD:   return "KEYWORD";
        case TOK_OP:        return "OP";
        case TOK_PAREN:     return "PAREN";
        case TOK_SEMICOLON: return "SEMICOLON";
        case TOK_COMMA:     return "COMMA";
        case TOK_EOF:       return "EOF";
        case TOK_TYPE:      return "TYPE";
        default:            return "ERROR";
    }
}

/* ─── Лексер ───────────────────────────────────────────────────── */
typedef struct {
    const char *src;
    int         pos;
    int         line;
    int         col;
} Lexer;

static void lexer_init(Lexer *lex, const char *src) {
    lex->src  = src;
    lex->pos  = 0;
    lex->line = 1;
    lex->col  = 1;
}

static char peek(Lexer *lex) {
    return lex->src[lex->pos];
}

static char advance(Lexer *lex) {
    char c = lex->src[lex->pos++];
    if (c == '\n') { lex->line++; lex->col = 1; }
    else            { lex->col++; }
    return c;
}

/* пропустить пробелы и комментарии */
static void skip_whitespace_comments(Lexer *lex) {
    while (1) {
        /* пробелы */
        while (isspace(peek(lex))) advance(lex);

        /* однострочный комментарий // */
        if (peek(lex) == '/' && lex->src[lex->pos + 1] == '/') {
            while (peek(lex) && peek(lex) != '\n') advance(lex);
            continue;
        }

        /* многострочный комментарий /* */
        if (peek(lex) == '/' && lex->src[lex->pos + 1] == '*') {
            advance(lex); advance(lex); /* съедаем /* */
            while (peek(lex)) {
                if (peek(lex) == '*' && lex->src[lex->pos + 1] == '/') {
                    advance(lex); advance(lex);
                    break;
                }
                advance(lex);
            }
            continue;
        }

        break;
    }
}

/* прочитать следующий токен */
static Token next_token(Lexer *lex) {
    Token tok;
    tok.value[0] = '\0';

    skip_whitespace_comments(lex);

    tok.line = lex->line;
    tok.col  = lex->col;

    char c = peek(lex);

    /* EOF */
    if (c == '\0') {
        tok.type = TOK_EOF;
        strcpy(tok.value, "EOF");
        return tok;
    }

    /* строка "..." */
    if (c == '"') {
        advance(lex);
        int i = 0;
        while (peek(lex) && peek(lex) != '"') {
            if (peek(lex) == '\\') { /* escape-символ */
                tok.value[i++] = advance(lex);
            }
            tok.value[i++] = advance(lex);
            if (i >= 254) break;
        }
        if (peek(lex) == '"') advance(lex);
        tok.value[i] = '\0';
        tok.type = TOK_STRING;
        return tok;
    }

    /* число: int или float */
    if (isdigit(c) || (c == '.' && isdigit(lex->src[lex->pos + 1]))) {
        int i = 0;
        int is_float = 0;
        while (isdigit(peek(lex))) tok.value[i++] = advance(lex);
        if (peek(lex) == '.') {
            is_float = 1;
            tok.value[i++] = advance(lex);
            while (isdigit(peek(lex))) tok.value[i++] = advance(lex);
        }
        /* экспонента: e+3, E-2 */
        if (peek(lex) == 'e' || peek(lex) == 'E') {
            is_float = 1;
            tok.value[i++] = advance(lex);
            if (peek(lex) == '+' || peek(lex) == '-')
                tok.value[i++] = advance(lex);
            while (isdigit(peek(lex))) tok.value[i++] = advance(lex);
        }
        tok.value[i] = '\0';
        tok.type = is_float ? TOK_FLOAT : TOK_INT;
        return tok;
    }

    /* идентификатор или ключевое слово */
    if (isalpha(c) || c == '_') {
        int i = 0;
        while (isalnum(peek(lex)) || peek(lex) == '_')
            tok.value[i++] = advance(lex);
        tok.value[i] = '\0';
        tok.type = is_keyword(tok.value) ? TOK_KEYWORD : TOK_IDENT;
        tok.type = is_type(tok.value) ? TOK_TYPE : TOK_IDENT;
        return tok;
    }

    /* скобки */
    if (c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}') {
        tok.value[0] = advance(lex);
        tok.value[1] = '\0';
        tok.type = TOK_PAREN;
        return tok;
    }

    /* точка с запятой */
    if (c == ';') {
        tok.value[0] = advance(lex);
        tok.value[1] = '\0';
        tok.type = TOK_SEMICOLON;
        return tok;
    }

    /* запятая */
    if (c == ',') {
        tok.value[0] = advance(lex);
        tok.value[1] = '\0';
        tok.type = TOK_COMMA;
        return tok;
    }

    /* операторы (включая двухсимвольные: ==, !=, <=, >=, &&, ||, ++, --, ->) */
    for (int i = 0; TWO_CHAR_OPSfl[i]; i++) {
        if (c == TWO_CHAR_OPSfl[i][0] &&
            lex->src[lex->pos + 1] == TWO_CHAR_OPSfl[i][1]) {
            tok.value[0] = advance(lex);
            tok.value[1] = advance(lex);
            tok.value[2] = '\0';
            tok.type = TOK_OP;
            return tok;
        }
    }
    /* односимвольные операторы */
    if (strchr(SINGLE_OPSfl, c)) {
        tok.value[0] = advance(lex);
        tok.value[1] = '\0';
        tok.type = TOK_OP;
        return tok;
    }

    /* неизвестный символ */
    tok.value[0] = advance(lex);
    tok.value[1] = '\0';
    tok.type = TOK_ERROR;
    return tok;
}

/* ─── Главная функция (демонстрация) ──────────────────────────── */
vector_token *lexing(char *source) {
    Lexer lex;
    lexer_init(&lex, source);

    Token tok;
    vector_token *vec = malloc(sizeof(vector_token));
    vt_init(vec, 4);
    do {
        tok = next_token(&lex);
        vt_push_back(vec, tok);
    } while (tok.type != TOK_EOF && tok.type != TOK_ERROR);

    return vec;
}