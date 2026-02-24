#ifndef ELEMENTS_H
#define ELEMENTS_H

typedef struct vector_node vector_node;

typedef enum {
    NODE_TYPE,
    NODE_IDENT,
    NODE_NUMBER,
    NODE_FLOAT,
    NODE_VAR,
    NODE_INDEX,
    NODE_UNOP,
    NODE_BINOP,
    NODE_UNDEF,
    NODE_PARAMS,
    NODE_SCOPE,
    NODE_ERROR,
    NODE_VAR_DEF,
    NODE_FUNC_DEF,
    NODE_IF,
    NODE_ELSE,
    NODE_WHILE,
    NODE_PARAM,
    NODE_ARGS,
    NODE_FUNC_CALL, 
    NODE_EOF
} NodeType;

typedef struct Node {
    NodeType type;
    vector_node *childs;
    char str[64];
} Node;

typedef enum {
    TOK_INT,        /* целое число          */
    TOK_FLOAT,      /* вещественное число   */
    TOK_STRING,     /* строка "..."         */
    TOK_IDENT,      /* идентификатор        */
    TOK_KEYWORD,    /* ключевое слово       */
    TOK_OP,         /* оператор             */
    TOK_PAREN,      /* скобки () [] {}      */
    TOK_SEMICOLON,  /* ;                    */
    TOK_COMMA,      /* ,                    */
    TOK_EOF,        /* конец файла          */
    TOK_ERROR,      /* неизвестный символ   */
    TOK_TYPE        /* тип                  */
} TokenType;

typedef struct {
    TokenType type;
    char      value[64];
    int       line;
    int       col;
} Token;

#endif