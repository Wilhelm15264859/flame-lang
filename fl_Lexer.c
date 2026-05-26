#include <string.h>
#include <stdio.h>
#include "fl_Lexer.h"
#include "utils/vector_token.h"

const char *TWO_CHAR_OPSfl[] = {
        "==", "!=", "<=", ">=", "&&", "||", "++", "--",
        "->", "+=", "-=", "*=", "/=", "%=", "<<", ">>",
        "!!",
        NULL
    };
const char SINGLE_OPSfl[] = "+-*/%=<>!&|^~?:.#$";

const char *KEYWORDSfl[] = {
    "return", "struct", "sizeof", "x86", "new", "class", "static",
    "for", "do", "if", "else", "while",  "delete",  "extern", "exception", "instruction",
    "check", "replace", "notdel", "enum", "const", "typedef",
    "async", "await", "channel",

    "sparc64", "sparc", "bpf", "msp430", "avr", "wasm64", "wasm32", "ppc64", "ppc", "mips64", "mips", "riscv64", "riscv32",
    "aarch64", "thumbeb", "thumb",  "armeb",  "arm", "i686", "i386", "x86_64",
    NULL
};

const char *TYPESfl[] = {
    "int", "short", "long", "char", "float", "double", "void", "T", "F",
    "uint", "ushort", "ulong", "uchar",
    NULL
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

typedef struct {
    const char *src;
    int         pos;
    int         line;
    int         col;
    char        file[512];
} Lexer;

static void lexer_init(Lexer *lex, const char *src) {
    lex->src  = src;
    lex->pos  = 0;
    lex->line = 1;
    lex->col  = 1;
    lex->file[0] = '\0';
}

static char peek(Lexer *lex) {
    return lex->src[lex->pos];
}

static char advance(Lexer *lex) {
    char c = lex->src[lex->pos++];
    if (c == '\n') { lex->line++; lex->col = 1; }
    else lex->col++;
    return c;
}

static void skip_whitespace_comments(Lexer *lex) {
    while (1) {
        while (isspace(peek(lex))) advance(lex);

        if (peek(lex) == '#') {
            int saved_pos  = lex->pos;
            int saved_line = lex->line;
            int saved_col  = lex->col;

            advance(lex);
            while (peek(lex) == ' ' || peek(lex) == '\t') advance(lex);

            char dir[8]; int di = 0;
            while (isalpha((unsigned char)peek(lex)) && di < 7)
                dir[di++] = advance(lex);
            dir[di] = '\0';

            if (strcmp(dir, "line") == 0) {
                while (peek(lex) == ' ' || peek(lex) == '\t') advance(lex);

                int new_line = 0;
                while (isdigit((unsigned char)peek(lex)))
                    new_line = new_line * 10 + (advance(lex) - '0');

                while (peek(lex) == ' ' || peek(lex) == '\t') advance(lex);

                if (peek(lex) == '"') {
                    advance(lex);
                    int fi = 0;
                    while (peek(lex) && peek(lex) != '"' && peek(lex) != '\n') {
                        if (fi < (int)sizeof(lex->file) - 1)
                            lex->file[fi++] = advance(lex);
                        else
                            advance(lex);
                    }
                    lex->file[fi] = '\0';
                    if (peek(lex) == '"') advance(lex);
                }

                while (peek(lex) && peek(lex) != '\n') advance(lex);
                if (peek(lex) == '\n') advance(lex);

                if (new_line > 0) {
                    lex->line = new_line;
                    lex->col  = 1;
                }
                continue;
            } else {
                lex->pos  = saved_pos;
                lex->line = saved_line;
                lex->col  = saved_col;
                break;
            }
        }

        if (peek(lex) == '/' && lex->src[lex->pos + 1] == '/') {
            while (peek(lex) && peek(lex) != '\n') advance(lex);
            continue;
        }

        if (peek(lex) == '/' && lex->src[lex->pos + 1] == '*') {
            advance(lex); advance(lex);
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

static Token next_token(Lexer *lex) {
    Token tok;
    tok.value[0] = '\0';

    skip_whitespace_comments(lex);

    tok.line = lex->line;
    tok.col  = lex->col;
    strncpy(tok.file, lex->file, sizeof(tok.file) - 1);
    tok.file[sizeof(tok.file) - 1] = '\0';

    char c = peek(lex);

    if (c == '\0') {
        tok.type = TOK_EOF;
        strcpy(tok.value, "EOF");
        return tok;
    }

    if (c == '\'') {
        advance(lex);
        char val = 0;
        if (peek(lex) == '\\') {
            advance(lex);

            if (peek(lex) == '\0') error("Error: unexpected EOF after '\\'\n");

            char esc = advance(lex);
            switch (esc) {
                case 'n':  val = '\n'; break;
                case 't':  val = '\t'; break;
                case '0':  val = '\0'; break;
                case '\\': val = '\\'; break;
                case '\'': val = '\''; break;
                case 'r':  val = '\r'; break;
                default:   val = esc;  break;
            }
        } else {
            val = advance(lex);
        }
        if (peek(lex) == '\'') advance(lex);
        else error("Error: unclosed char literal at line %d\n", lex->line);
        snprintf(tok.value, sizeof(tok.value), "%dc", (unsigned char)val);
        tok.type = TOK_INT;
        return tok;
    } else if (c == '"') {
        advance(lex);
        int i = 0;
        while (peek(lex) && peek(lex) != '"') {
            if (peek(lex) == '\\') {
                advance(lex);

                if (peek(lex) == '\0') error("Lexer ERROR: unexpected EOF after '\\'\n");
                
                char esc = advance(lex);
                switch (esc) {
                    case 'n':  tok.value[i++] = '\n'; break;
                    case 't':  tok.value[i++] = '\t'; break;
                    case '0':  tok.value[i++] = '\0'; break;
                    case '\\': tok.value[i++] = '\\'; break;
                    case '"':  tok.value[i++] = '"';  break;
                    default:   tok.value[i++] = esc;  break;
                }
            } else {
                tok.value[i++] = advance(lex);
            }
            if (i >= 254) break;
        }
        if (peek(lex) == '"') advance(lex);
        tok.value[i] = '\0';
        tok.type = TOK_STRING;
        return tok;
    }

    if (isdigit(c) || (c == '.' && isdigit(lex->src[lex->pos + 1]))) {
        int i = 0;
        int is_float = 0;
        if (c == '0' && (lex->src[lex->pos + 1] == 'x' || lex->src[lex->pos + 1] == 'X')) {
            tok.value[i++] = advance(lex);
            tok.value[i++] = advance(lex);
            while (isxdigit(peek(lex)))
                tok.value[i++] = advance(lex);
            tok.value[i] = '\0';
            tok.type = TOK_INT;
            return tok;
        }
        while (isdigit(peek(lex))) tok.value[i++] = advance(lex);
        if (peek(lex) == '.') {
            is_float = 1;
            tok.value[i++] = advance(lex);
            while (isdigit(peek(lex))) tok.value[i++] = advance(lex);
        }
        if (peek(lex) == 'e' || peek(lex) == 'E') {
            is_float = 1;
            tok.value[i++] = advance(lex);
            if (peek(lex) == '+' || peek(lex) == '-')
                tok.value[i++] = advance(lex);
            while (isdigit(peek(lex))) tok.value[i++] = advance(lex);
        }
        if (!is_float && (peek(lex) == 's' || peek(lex) == 'S' ||
                peek(lex) == 'l' || peek(lex) == 'L' ||
                peek(lex) == 'c' || peek(lex) == 'C')) {
            tok.value[i++] = advance(lex);
        } else if (is_float && (peek(lex) == 'f' || peek(lex) == 'F')) {
            tok.value[i++] = advance(lex);
        }
        tok.value[i] = '\0';
        tok.type = is_float ? TOK_FLOAT : TOK_INT;
        return tok;
    }

    if (isalpha(c) || c == '_') {
        int i = 0;
        while (isalnum(peek(lex)) || peek(lex) == '_')
            tok.value[i++] = advance(lex);
        tok.value[i] = '\0';
        if (tok.value[0] == '_') {
            error("Lexer ERROR: identifiers starting with '_' are reserved "
                    "(mangling namespace), got '%s' at line %d col %d\n",
                    tok.value, lex->line, lex->col);
        }
        if (is_type(tok.value))
            tok.type = TOK_TYPE;
        else if (is_keyword(tok.value))
            tok.type = TOK_KEYWORD;
        else
            tok.type = TOK_IDENT;
        return tok;
    }

    if (c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}') {
        tok.value[0] = advance(lex);
        tok.value[1] = '\0';
        tok.type = TOK_PAREN;
        return tok;
    }

    if (c == ';') {
        tok.value[0] = advance(lex);
        tok.value[1] = '\0';
        tok.type = TOK_SEMICOLON;
        return tok;
    }

    if (c == ',') {
        tok.value[0] = advance(lex);
        tok.value[1] = '\0';
        tok.type = TOK_COMMA;
        return tok;
    }

    if (c == '=' && lex->src[lex->pos+1] == '=' && lex->src[lex->pos+2] == '>') {
        advance(lex); advance(lex); advance(lex);
        strcpy(tok.value, "==>");
        tok.type = TOK_OP;
        return tok;
    }

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
    if (strchr(SINGLE_OPSfl, c)) {
        tok.value[0] = advance(lex);
        tok.value[1] = '\0';
        tok.type = TOK_OP;
        return tok;
    }

    tok.value[0] = advance(lex);
    tok.value[1] = '\0';
    error("Lexer ERROR: unknown char '%c' (0x%02x) at line %d col %d\n",
        c, (unsigned char)c, lex->line, lex->col);
    tok.type = TOK_ERROR;
    return tok;
}

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