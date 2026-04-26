#ifndef ELEMENTS_H
#define ELEMENTS_H

typedef struct vector_node vector_node;

typedef enum {
    TYPE_BASE,      // int, char, struct Foo
    TYPE_PTR,       // *
    TYPE_ARRAY,     // [N]
    TYPE_FUNC       // (int, float)
} TypeKind;

typedef struct TypeInfo {
    TypeKind kind;
    char base_name[64];
    struct TypeInfo *inner;
    int array_size;
    struct TypeInfo **params;
    int param_count;
} TypeInfo;

typedef enum {
    NODE_TYPE,
    NODE_IDENT,
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
    NODE_DEREF,
    NODE_STRING,
    NODE_STRUCT_DEF,
    NODE_MEMBER_DOT,
    NODE_MEMBER_ARROW,
    NODE_MEMBER_ASSIGN,
    NODE_SIZEOF,
    NODE_ASM,
    NODE_I32,
    NODE_I64,
    NODE_I8,
    NODE_I16,
    NODE_DOUBLE,
    NODE_NEW,
    NODE_STATIC_VAR_DEF,
    NODE_STATIC_FUNC_DEF,
    NODE_FOR,
    NODE_DO_WHILE,
    NODE_DELETE,
    NODE_EXTERN_FUNC_DEF,
    NODE_ADDR_INDEX,
    NODE_MEMBER_INDEX_ASSIGN,
    NODE_ENUM,
    NODE_TYPE_LITERAL,
    NODE_TYPEDEF,
    NODE_EXTERN_VAR_DEF,
    NODE_EXTERN_STRUCT_DEF,
    NODE_CAST,
    /* ── Coroutine nodes ── */
    NODE_ASYNC_FUNC_DEF,      /* async void foo(...)  — fire-and-forget by default */
    NODE_AWAIT_FUNC_DEF,      /* await void foo(...)  — blocking by default        */
    NODE_CHANNEL_VAR_DEF,     /* channel int x = 0;   — writable by coroutines     */
    NODE_ASYNC_CALL,          /* async foo(...)  — fire-and-forget call             */
    NODE_AWAIT_CALL           /* await foo(...)  — blocking call                   */
} NodeType;

typedef struct Node {
    NodeType type;
    vector_node *childs;
    char *str;
    TypeInfo *result_type;
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