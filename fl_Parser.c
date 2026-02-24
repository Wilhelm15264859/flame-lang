#include "fl_Parser.h"
#include <stdio.h>
#include <string.h>

static int i;
static vector_token *tokens;

static Token peek(int padding) {
    if (i + padding >= 0 && (unsigned)(i + padding) < tokens->size)
        return tokens->data[i + padding];
    printf("Error: token index out of bounds\n");
    Token er;
    er.type = TOK_ERROR;
    return er;
}

static Token advance() {
    Token ret = peek(0);
    i++;
    return ret;
}

Node *make_node(NodeType type, const char *str) {
    Node *n   = malloc(sizeof(Node));
    n->type   = type;
    n->childs = malloc(sizeof(vector_node));
    vn_init(n->childs, 2);
    strncpy(n->str, str ? str : "", 63);
    n->str[63] = '\0';
    return n;
}

static Node *expr_or(void);

Node parseVarDef(void);
Node parseFuncDef(void);
Node parseParams(void);
Node parseIf(void);
Node parseElse(void);
Node parseWhile(void);
Node parseFuncCall(Token t);
Node *parseExpr(void);

Node parsing(void) {
    Token current = advance();

    if (strcmp(current.value, "var") == 0)
        return parseVarDef();
    else if (strcmp(current.value, "func") == 0)
        return parseFuncDef();
    else if (strcmp(current.value, "if") == 0)
        return parseIf();
    else if (strcmp(current.value, "else") == 0)
        return parseElse();
    else if (strcmp(current.value, "while") == 0)
        return parseWhile();
    else if (current.type == TOK_IDENT)
        return parseFuncCall(current);
    else if (current.type == TOK_EOF) {
        Node eof;
        eof.type  = NODE_EOF;
        eof.childs = malloc(sizeof(vector_node));
        vn_init(eof.childs, 1);
        eof.str[0] = '\0';
        return eof;
    }

    printf("Error: unknown token\n");
    Node error;
    error.type = NODE_ERROR;
    return error;
}

vector_node *parse(int it, vector_token* tokenss) {
    i = it;
    tokens = tokenss;
    
    vector_node *nodes = malloc(sizeof(vector_node));
    vn_init(nodes, 4);

    while (1) {
        Node temp = parsing();
        if (temp.type == NODE_EOF)  break;
        if (temp.type == NODE_ERROR) return NULL;
        vn_push_back(nodes, temp);
    }

    return nodes;
}

Node parseFuncCall(Token t) {
    Node call;
    call.childs = malloc(sizeof(vector_node));
    vn_init(call.childs, 4);
    strncpy(call.str, t.value, 63);
    call.str[63] = '\0';

    Node *name = make_node(NODE_IDENT, t.value);
    vn_push_back(call.childs, *name);
    free(name);

    Node *args = make_node(NODE_ARGS, "");

    if (strcmp(peek(0).value, "(") == 0) {
        advance();

        while (strcmp(peek(0).value, ")") != 0) {
            if (peek(0).type == TOK_EOF) {
                printf("Error: unexpected EOF in function call\n");
                break;
            }
            Node *thing = parseExpr();
            if (thing) {
                vn_push_back(args->childs, *thing);
                free(thing);
            }
            if (peek(0).type == TOK_COMMA)
                advance();
            else
                break;
        }

        if (strcmp(peek(0).value, ")") == 0)
            advance();
        else
            printf("Error: expected ')' in function call\n");
    } else {
        printf("Error: expected '(' in function call\n");
    }

    vn_push_back(call.childs, *args);
    free(args);

    if (peek(0).type == TOK_SEMICOLON) advance();

    call.type = NODE_FUNC_CALL;
    return call;
}

Node parseWhile(void) {
    Node whilex;
    whilex.childs = malloc(sizeof(vector_node));
    vn_init(whilex.childs, 4);

    Token current = advance();
    if (strcmp(current.value, "(") == 0) {
        Node *expr = parseExpr();
        vn_push_back(whilex.childs, *expr);
        free(expr);

        current = advance();
        if (strcmp(current.value, ")") != 0)
            printf("Error: expected ')' after while condition\n");
    } else {
        printf("Error: expected '(' after while\n");
    }

    current = advance();
    if (strcmp(current.value, "{") == 0) {
        Node *scope = make_node(NODE_SCOPE, "");
        while (strcmp(peek(0).value, "}") != 0) {
            if (peek(0).type == TOK_EOF) {
                printf("Error: unexpected EOF in while body\n");
                break;
            }
            Node temp = parsing();
            if (temp.type != NODE_ERROR)
                vn_push_back(scope->childs, temp);
            else
                printf("Error in while scope\n");
        }
        advance();
        vn_push_back(whilex.childs, *scope);
        free(scope);
    } else {
        Node d = parsing();
        vn_push_back(whilex.childs, d);
    }

    whilex.type = NODE_WHILE;
    return whilex;
}

Node parseElse(void) {
    Node elseexpr;
    elseexpr.childs = malloc(sizeof(vector_node));
    vn_init(elseexpr.childs, 4);

    Token current = advance();
    if (strcmp(current.value, "{") == 0) {
        Node *scope = make_node(NODE_SCOPE, "");
        while (strcmp(peek(0).value, "}") != 0) {
            if (peek(0).type == TOK_EOF) {
                printf("Error: unexpected EOF in else body\n");
                break;
            }
            Node temp = parsing();
            if (temp.type != NODE_ERROR)
                vn_push_back(scope->childs, temp);
            else
                printf("Error in else scope\n");
        }
        advance();
        vn_push_back(elseexpr.childs, *scope);
        free(scope);
    } else {
        Node d = parsing();
        vn_push_back(elseexpr.childs, d);
    }

    elseexpr.type = NODE_ELSE;
    return elseexpr;
}

Node parseIf(void) {
    Node ifexpr;
    ifexpr.childs = malloc(sizeof(vector_node));
    vn_init(ifexpr.childs, 4);

    Token current = advance();
    if (strcmp(current.value, "(") == 0) {
        Node *expr = parseExpr();
        vn_push_back(ifexpr.childs, *expr);
        free(expr);

        current = advance();
        if (strcmp(current.value, ")") != 0)
            printf("Error: expected ')' after if condition\n");
    } else {
        printf("Error: expected '(' after if\n");
    }

    current = advance();
    if (strcmp(current.value, "{") == 0) {
        Node *scope = make_node(NODE_SCOPE, "");
        while (strcmp(peek(0).value, "}") != 0) {
            if (peek(0).type == TOK_EOF) {
                printf("Error: unexpected EOF in if body\n");
                break;
            }
            Node temp = parsing();
            if (temp.type != NODE_ERROR)
                vn_push_back(scope->childs, temp);
            else
                printf("Error in if scope\n");
        }
        advance();
        vn_push_back(ifexpr.childs, *scope);
        free(scope);
    } else {
        Node d = parsing();
        vn_push_back(ifexpr.childs, d);
    }

    ifexpr.type = NODE_IF;
    return ifexpr;
}

Node parseFuncDef(void) {
    Node func;
    func.childs = malloc(sizeof(vector_node));
    vn_init(func.childs, 4);

    Token current = advance();
    if (current.type == TOK_TYPE || strcmp(current.value, "void") == 0) {
        Node *type = make_node(NODE_TYPE, current.value);
        vn_push_back(func.childs, *type);
        free(type);
    } else {
        printf("Error: unknown function return type\n");
    }

    current = advance();
    if (current.type == TOK_IDENT) {
        Node *ident = make_node(NODE_IDENT, current.value);
        vn_push_back(func.childs, *ident);
        free(ident);
    } else {
        printf("Error: expected function name\n");
    }

    current = advance();
    if (strcmp(current.value, "(") == 0) {
        if (strcmp(peek(0).value, ")") == 0) {
            Node *params = make_node(NODE_PARAMS, "");
            vn_push_back(func.childs, *params);
            free(params);
        } else {
            Node params = parseParams();
            vn_push_back(func.childs, params);
        }

        current = advance();
        if (strcmp(current.value, ")") != 0)
            printf("Error: expected ')' after params\n");
    } else {
        printf("Error: expected '(' in function def\n");
    }

    current = advance();
    if (strcmp(current.value, "{") == 0) {
        Node *scope = make_node(NODE_SCOPE, "");
        while (strcmp(peek(0).value, "}") != 0) {
            if (peek(0).type == TOK_EOF) {
                printf("Error: unexpected EOF in function body\n");
                break;
            }
            Node temp = parsing();
            if (temp.type != NODE_ERROR)
                vn_push_back(scope->childs, temp);
            else
                printf("Error in function scope\n");
        }
        advance();
        vn_push_back(func.childs, *scope);
        free(scope);
    } else {
        printf("Error: expected '{' in function def\n");
    }

    func.type = NODE_FUNC_DEF;
    return func;
}

Node parseParams(void) {
    Node params;
    params.childs = malloc(sizeof(vector_node));
    vn_init(params.childs, 4);

    while (1) {
        Node param;
        param.childs = malloc(sizeof(vector_node));
        vn_init(param.childs, 2);
        param.type   = NODE_PARAM;
        param.str[0] = '\0';

        Token current = advance();
        if (current.type == TOK_TYPE) {
            Node *type = make_node(NODE_TYPE, current.value);
            vn_push_back(param.childs, *type);
            free(type);
        } else {
            printf("Error: expected type in param\n");
        }

        current = advance();
        if (current.type == TOK_IDENT) {
            Node *ident = make_node(NODE_IDENT, current.value);
            vn_push_back(param.childs, *ident);
            free(ident);
        } else {
            printf("Error: expected identifier in param\n");
        }

        vn_push_back(params.childs, param);

        if (peek(0).type == TOK_COMMA)
            advance();
        else
            break;
    }

    params.type   = NODE_PARAMS;
    params.str[0] = '\0';
    return params;
}

Node parseVarDef(void) {
    Node var;
    var.childs = malloc(sizeof(vector_node));
    vn_init(var.childs, 4);

    Token current = advance();
    if (current.type == TOK_TYPE) {
        Node *type = make_node(NODE_TYPE, current.value);
        vn_push_back(var.childs, *type);
        free(type);
    } else {
        printf("Error: unknown variable type\n");
    }

    current = advance();
    if (current.type == TOK_IDENT) {
        Node *ident = make_node(NODE_IDENT, current.value);
        vn_push_back(var.childs, *ident);
        free(ident);
    } else {
        printf("Error: expected identifier\n");
    }

    if (strcmp(peek(0).value, "=") == 0) {
        advance();
        Node *expr = parseExpr();
        if (expr) {
            vn_push_back(var.childs, *expr);
            free(expr);
        }
    } else {
        Node *undef = make_node(NODE_UNDEF, "");
        vn_push_back(var.childs, *undef);
        free(undef);
    }

    if (peek(0).type == TOK_SEMICOLON)
        advance();

    var.type   = NODE_VAR_DEF;
    var.str[0] = '\0';
    return var;
}

static Node *expr_primary(void) {
    Token t = peek(0);

    if (t.type == TOK_INT) {
        advance();
        return make_node(NODE_NUMBER, t.value);
    }

    if (t.type == TOK_FLOAT) {
        advance();
        return make_node(NODE_FLOAT, t.value);
    }

    if (t.type == TOK_IDENT) {
        advance();

        if (strcmp(peek(0).value, "(") == 0) {
            Node call = parseFuncCall(t);
            Node *n   = malloc(sizeof(Node));
            *n = call;
            return n;
        }

        if (strcmp(peek(0).value, "[") == 0) {
            advance();
            Node *idx  = expr_or();
            Node *node = make_node(NODE_INDEX, "[]");
            Node *var  = make_node(NODE_VAR, t.value);
            vn_push_back(node->childs, *var);
            vn_push_back(node->childs, *idx);
            free(var); free(idx);
            if (strcmp(peek(0).value, "]") == 0)
                advance();
            else
                printf("Error: expected ']'\n");
            return node;
        }

        return make_node(NODE_VAR, t.value);
    }

    if (strcmp(t.value, "(") == 0) {
        advance();
        Node *inner = expr_or();
        if (strcmp(peek(0).value, ")") == 0)
            advance();
        else
            printf("Error: expected ')'\n");
        return inner;
    }

    printf("Error: unexpected token in expression\n");
    return NULL;
}

static Node *expr_unary(void) {
    Token t = peek(0);
    if (t.type == TOK_OP &&
        (strcmp(t.value, "-") == 0 || strcmp(t.value, "!") == 0))
    {
        advance();
        Node *operand = expr_unary();
        Node *node    = make_node(NODE_UNOP, t.value);
        vn_push_back(node->childs, *operand);
        free(operand);
        return node;
    }
    return expr_primary();
}

static Node *expr_term(void) {
    Node *left = expr_unary();
    while (peek(0).type == TOK_OP &&
           (strcmp(peek(0).value, "*") == 0 ||
            strcmp(peek(0).value, "/") == 0 ||
            strcmp(peek(0).value, "%") == 0))
    {
        Token op    = advance();
        Node *right = expr_unary();
        Node *node  = make_node(NODE_BINOP, op.value);
        vn_push_back(node->childs, *left);
        vn_push_back(node->childs, *right);
        free(left); free(right);
        left = node;
    }
    return left;
}

static Node *expr_add(void) {
    Node *left = expr_term();
    while (peek(0).type == TOK_OP &&
           (strcmp(peek(0).value, "+") == 0 ||
            strcmp(peek(0).value, "-") == 0))
    {
        Token op    = advance();
        Node *right = expr_term();
        Node *node  = make_node(NODE_BINOP, op.value);
        vn_push_back(node->childs, *left);
        vn_push_back(node->childs, *right);
        free(left); free(right);
        left = node;
    }
    return left;
}

static Node *expr_cmp(void) {
    Node *left = expr_add();
    while (peek(0).type == TOK_OP &&
           (strcmp(peek(0).value, "<")  == 0 ||
            strcmp(peek(0).value, ">")  == 0 ||
            strcmp(peek(0).value, "<=") == 0 ||
            strcmp(peek(0).value, ">=") == 0))
    {
        Token op    = advance();
        Node *right = expr_add();
        Node *node  = make_node(NODE_BINOP, op.value);
        vn_push_back(node->childs, *left);
        vn_push_back(node->childs, *right);
        free(left); free(right);
        left = node;
    }
    return left;
}

static Node *expr_eq(void) {
    Node *left = expr_cmp();
    while (peek(0).type == TOK_OP &&
           (strcmp(peek(0).value, "==") == 0 ||
            strcmp(peek(0).value, "!=") == 0))
    {
        Token op    = advance();
        Node *right = expr_cmp();
        Node *node  = make_node(NODE_BINOP, op.value);
        vn_push_back(node->childs, *left);
        vn_push_back(node->childs, *right);
        free(left); free(right);
        left = node;
    }
    return left;
}

static Node *expr_and(void) {
    Node *left = expr_eq();
    while (peek(0).type == TOK_OP && strcmp(peek(0).value, "&&") == 0)
    {
        Token op    = advance();
        Node *right = expr_eq();
        Node *node  = make_node(NODE_BINOP, op.value);
        vn_push_back(node->childs, *left);
        vn_push_back(node->childs, *right);
        free(left); free(right);
        left = node;
    }
    return left;
}

static Node *expr_or(void) {
    Node *left = expr_and();
    while (peek(0).type == TOK_OP && strcmp(peek(0).value, "||") == 0)
    {
        Token op    = advance();
        Node *right = expr_and();
        Node *node  = make_node(NODE_BINOP, op.value);
        vn_push_back(node->childs, *left);
        vn_push_back(node->childs, *right);
        free(left); free(right);
        left = node;
    }
    return left;
}

Node *parseExpr(void) {
    return expr_or();
}