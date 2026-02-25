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
    NODE_EOF,
    NODE_RETURN,
    NODE_ARRAY_SIZE,
    NODE_ARRAY_DEF,
    NODE_INDEX_ASSIGN,
    NODE_ASSIGN,
    NODE_PTR_ASSIGN,
    NODE_ADDR,
    NODE_DEREF
} NodeType;

typedef struct Node {
    NodeType type;
    vector_node *childs;
    char str[64];
} Node;

typedef enum {
    TOK_INT,
    TOK_FLOAT,
    TOK_STRING,
    TOK_IDENT,
    TOK_KEYWORD,
    TOK_OP,
    TOK_PAREN,
    TOK_SEMICOLON,
    TOK_COMMA,
    TOK_EOF,
    TOK_ERROR,
    TOK_TYPE
} TokenType;

typedef struct {
    TokenType type;
    char      value[64];
    int       line;
    int       col;
} Token;

#endif