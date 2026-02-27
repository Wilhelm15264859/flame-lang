#include "fl_Parser.h"
#include <stdio.h>
#include <string.h>

static int i;
static vector_token *tokens;

typedef struct { char var[64]; char type[64]; } VarType;
static VarType var_types[256];
static int var_type_count = 0;

static void var_type_push(const char *var, const char *type) {
    if (var_type_count >= 256) return;
    strncpy(var_types[var_type_count].var,  var,  63);
    strncpy(var_types[var_type_count].type, type, 63);
    var_type_count++;
}

static const char *var_type_lookup(const char *var) {
    for (int j = var_type_count - 1; j >= 0; j--)
        if (strcmp(var_types[j].var, var) == 0)
            return var_types[j].type;
    return NULL;
}

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
    Node *n = malloc(sizeof(Node));
    n->type = type;
    n->childs = malloc(sizeof(vector_node));
    vn_init(n->childs, 2);
    
    if (str) {
        n->str = malloc(strlen(str) + 1);
        strcpy(n->str, str);
    } else {
        n->str = malloc(1);
        n->str[0] = '\0';
    }
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
Node parseAssign(Token t);
Node parseReturn(void);
Node parsePtrAssign(void);
Node *parseExpr(void);
Node parseStruct(void);
Node parseAsm(void);
Node parseClass(void);
Node parseCompoundAssign(Token t);
static Node *expr_or(void);
static Node *expr_unary(void);

Node parsing(void) {
    Token current = advance();
    printf("DEBUG parsing: type=%d value='%s'\n", current.type, current.value);

    if (current.type == TOK_KEYWORD) {
        if (strcmp(current.value, "var")    == 0) return parseVarDef();
        if (strcmp(current.value, "func")   == 0) return parseFuncDef();
        if (strcmp(current.value, "if")     == 0) return parseIf();
        if (strcmp(current.value, "else")   == 0) return parseElse();
        if (strcmp(current.value, "while")  == 0) return parseWhile();
        if (strcmp(current.value, "return") == 0) return parseReturn();
        if (strcmp(current.value, "struct") == 0) return parseStruct();
        if (strcmp(current.value, "class")  == 0) return parseClass();
        if (strcmp(current.value, "x86")    == 0) return parseAsm();
    }

    if (current.type == TOK_OP && strcmp(current.value, "*") == 0)
        return parsePtrAssign();

    if (current.type == TOK_IDENT) {
        if (strcmp(peek(0).value, "(") == 0)
            return parseFuncCall(current);
        else if ((strcmp(peek(0).value, ".") == 0 || strcmp(peek(0).value, "->") == 0) &&
                peek(1).type == TOK_IDENT &&
                strcmp(peek(2).value, "(") == 0) {
            i--;
            Node *expr = parseExpr();
            if (peek(0).type == TOK_SEMICOLON) advance();
            if (!expr) {
                Node error;
                error.type = NODE_ERROR;
                error.childs = NULL;
                error.str = NULL;
                return error;
            }
            return *expr;
        }
        else if (strcmp(peek(0).value, "++") == 0 ||
                 strcmp(peek(0).value, "--") == 0)
            return parseCompoundAssign(current);
        else if (strcmp(peek(0).value, "+=") == 0 ||
                 strcmp(peek(0).value, "-=") == 0 ||
                 strcmp(peek(0).value, "*=") == 0 ||
                 strcmp(peek(0).value, "/=") == 0 ||
                 strcmp(peek(0).value, "%=") == 0)
            return parseCompoundAssign(current);
        else if (strcmp(peek(0).value, "=") == 0 ||
                 strcmp(peek(0).value, "[") == 0 ||
                 strcmp(peek(0).value, ".") == 0 ||
                 strcmp(peek(0).value, "->") == 0)
            return parseAssign(current);
        else {
            printf("Error: unexpected identifier '%s'\n", current.value);
            Node error;
            error.type = NODE_ERROR;
            error.childs = NULL;
            error.str = NULL;
            return error;
        }
    }

    if (current.type == TOK_EOF) {
        Node eof;
        eof.type = NODE_EOF;
        eof.childs = malloc(sizeof(vector_node));
        vn_init(eof.childs, 1);
        eof.str = NULL;
        return eof;
    }

    printf("Error: unknown token '%s'\n", current.value);
    Node error;
    error.type = NODE_ERROR;
    error.childs = NULL;
    error.str = NULL;
    return error;
}

Node parseCompoundAssign(Token t) {
    Node assign;
    assign.childs = malloc(sizeof(vector_node));
    vn_init(assign.childs, 4);
    assign.str = NULL;

    Token op = advance();

    Node *ident = make_node(NODE_IDENT, t.value);
    vn_push_back(assign.childs, *ident);
    free(ident);

    Node *undef = make_node(NODE_UNDEF, "");
    vn_push_back(assign.childs, *undef);
    free(undef);

    Node *var_ref = make_node(NODE_VAR, t.value);

    if (strcmp(op.value, "++") == 0 || strcmp(op.value, "--") == 0) {
        Node *one = make_node(NODE_I32, "1");
        const char *binop = (strcmp(op.value, "++") == 0) ? "+" : "-";
        Node *binop_node = make_node(NODE_BINOP, binop);
        vn_push_back(binop_node->childs, *var_ref);
        vn_push_back(binop_node->childs, *one);
        free(var_ref); free(one);
        vn_push_back(assign.childs, *binop_node);
        free(binop_node);
    } else {
        Node *rhs = parseExpr();
        if (!rhs) {
            free(var_ref);
            assign.type = NODE_ERROR;
            return assign;
        }
        char binop[2] = { op.value[0], '\0' }; 
        Node *binop_node = make_node(NODE_BINOP, binop);
        vn_push_back(binop_node->childs, *var_ref);
        vn_push_back(binop_node->childs, *rhs);
        free(var_ref); free(rhs);
        vn_push_back(assign.childs, *binop_node);
        free(binop_node);
    }

    if (peek(0).type == TOK_SEMICOLON) advance();

    assign.type = NODE_ASSIGN;
    return assign;
}

Node parseAsm(void) {
    Node asmnode;
    memset(&asmnode, 0, sizeof(Node));
    asmnode.childs = malloc(sizeof(vector_node));
    vn_init(asmnode.childs, 4);
    asmnode.str = malloc(256);
    asmnode.str[0] = '\0';

    char instr[256];
    instr[0] = '\0';
    int operand_count = 0;

    Node arch_node;
    memset(&arch_node, 0, sizeof(Node));
    arch_node.type   = NODE_IDENT;
    arch_node.childs = malloc(sizeof(vector_node));
    vn_init(arch_node.childs, 1);
    arch_node.str = malloc(8);
    strcpy(arch_node.str, "x86");
    vn_push_back(asmnode.childs, arch_node);

    Token mnemonic = advance();
    if (mnemonic.type != TOK_IDENT) {
        printf("Error: expected instruction mnemonic after 'x86'\n");
        asmnode.type = NODE_ERROR;
        return asmnode;
    }
    strncpy(instr, mnemonic.value, sizeof(instr) - 1);

    while (peek(0).type != TOK_SEMICOLON &&
           peek(0).type != TOK_EOF &&
           peek(0).type != TOK_ERROR) {
        Token op = advance();
        size_t len = strlen(instr);

        if (strcmp(op.value, ",") != 0 && len > 0)
            strncat(instr, " ", sizeof(instr) - len - 1);

        if (strcmp(op.value, "$") == 0) {
            Token var_tok = advance();
            char placeholder[8];
            snprintf(placeholder, sizeof(placeholder), "$%d", operand_count);
            strncat(instr, placeholder, sizeof(instr) - strlen(instr) - 1);

            Node operand_node;
            memset(&operand_node, 0, sizeof(Node));
            operand_node.type   = NODE_VAR;
            operand_node.childs = malloc(sizeof(vector_node));
            vn_init(operand_node.childs, 1);
            operand_node.str = malloc(strlen(var_tok.value) + 1);
            strcpy(operand_node.str, var_tok.value);
            vn_push_back(asmnode.childs, operand_node);
            operand_count++;
        } else {
            strncat(instr, op.value, sizeof(instr) - strlen(instr) - 1);
        }
    }

    if (peek(0).type == TOK_SEMICOLON)
        advance();

    strncpy(asmnode.str, instr, 255);
    asmnode.str[255] = '\0';
    asmnode.type = NODE_ASM;
    return asmnode;
}

Node parsePtrAssign(void) {
    Node assign;
    assign.childs = malloc(sizeof(vector_node));
    vn_init(assign.childs, 2);
    assign.str = NULL;

    Token t = advance();
    if (t.type != TOK_IDENT) {
        printf("Error: expected identifier after '*'\n");
        assign.type = NODE_ERROR;
        return assign;
    }

    Node *ident = make_node(NODE_IDENT, t.value);
    vn_push_back(assign.childs, *ident);
    free(ident);

    if (strcmp(peek(0).value, "=") == 0)
        advance();
    else
        printf("Error: expected '=' after '*%s'\n", t.value);

    Node *expr = parseExpr();
    if (expr) {
        vn_push_back(assign.childs, *expr);
        free(expr);
    }

    if (peek(0).type == TOK_SEMICOLON) advance();

    assign.type = NODE_PTR_ASSIGN;
    return assign;
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
        
        Node *node_copy = malloc(sizeof(Node));
        *node_copy = temp;
        vn_push_back(nodes, *node_copy);
        free(node_copy);
    }

    return nodes;
}

Node parseAssign(Token t) {
    Node assign;
    assign.childs = malloc(sizeof(vector_node));
    vn_init(assign.childs, 4);
    assign.str = NULL;
    
    if (strcmp(peek(0).value, "[") == 0) {
        Node *ident = make_node(NODE_IDENT, t.value);
        vn_push_back(assign.childs, *ident);
        free(ident);
        
        advance(); // [
        Node *idx = parseExpr();
        if (idx) {
            vn_push_back(assign.childs, *idx);
            free(idx);
        }
        if (strcmp(peek(0).value, "]") == 0)
            advance();
        else
            printf("Error: expected ']'\n");
        
        assign.type = NODE_INDEX_ASSIGN;
    } 
    else if (peek(0).type == TOK_OP && 
            (strcmp(peek(0).value, ".") == 0 || strcmp(peek(0).value, "->") == 0)) {
        Node *var = make_node(NODE_VAR, t.value);
        
        while (peek(0).type == TOK_OP && 
               (strcmp(peek(0).value, ".") == 0 || strcmp(peek(0).value, "->") == 0)) {
            Token op = advance();
            Token field = peek(0);
            if (field.type != TOK_IDENT) {
                printf("Error: expected field name\n");
                break;
            }
            advance();
            
            Node *member = make_node(
                strcmp(op.value, ".") == 0 ? NODE_MEMBER_DOT : NODE_MEMBER_ARROW,
                field.value
            );
            vn_push_back(member->childs, *var);
            free(var);
            var = member;
        }
        
        vn_push_back(assign.childs, *var);
        free(var);
        
        Node *undef = make_node(NODE_UNDEF, "");
        vn_push_back(assign.childs, *undef);
        free(undef);
        
        assign.type = NODE_MEMBER_ASSIGN;
    }
    else {
        Node *ident = make_node(NODE_IDENT, t.value);
        vn_push_back(assign.childs, *ident);
        free(ident);
        
        Node *undef = make_node(NODE_UNDEF, "");
        vn_push_back(assign.childs, *undef);
        free(undef);
        
        assign.type = NODE_ASSIGN;
    }

    if (strcmp(peek(0).value, "=") == 0)
        advance();
    else
        printf("Error: expected '='\n");

    Node *expr = parseExpr();
    if (expr) {
        vn_push_back(assign.childs, *expr);
        free(expr);
    }

    if (peek(0).type == TOK_SEMICOLON) advance();

    return assign;
}

Node parseReturn(void) {
    Node ret;
    ret.childs = malloc(sizeof(vector_node));
    vn_init(ret.childs, 2);
    ret.str = NULL;

    if (peek(0).type == TOK_SEMICOLON) {
        advance();
        Node *undef = make_node(NODE_UNDEF, "");
        vn_push_back(ret.childs, *undef);
        free(undef);
    } else {
        Node *expr = parseExpr();
        if (expr) {
            vn_push_back(ret.childs, *expr);
            free(expr);
        }
        if (peek(0).type == TOK_SEMICOLON)
            advance();
        else
            printf("Error: expected ';' after return value\n");
    }

    ret.type = NODE_RETURN;
    return ret;
}

Node parseFuncCall(Token t) {
    Node call;
    call.childs = malloc(sizeof(vector_node));
    vn_init(call.childs, 4);
    
    call.str = malloc(strlen(t.value) + 1);
    strcpy(call.str, t.value);

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
    whilex.str = NULL;

    Token current = advance();
    if (strcmp(current.value, "(") == 0) {
        Node *expr = parseExpr();
        if (expr) {
            vn_push_back(whilex.childs, *expr);
            free(expr);
        }

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
            if (temp.type != NODE_ERROR) {
                Node *temp_copy = malloc(sizeof(Node));
                *temp_copy = temp;
                vn_push_back(scope->childs, *temp_copy);
                free(temp_copy);
            } else
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
    elseexpr.str = NULL;

    Token current = advance();
    if (strcmp(current.value, "{") == 0) {
        Node *scope = make_node(NODE_SCOPE, "");
        while (strcmp(peek(0).value, "}") != 0) {
            if (peek(0).type == TOK_EOF) {
                printf("Error: unexpected EOF in else body\n");
                break;
            }
            Node temp = parsing();
            if (temp.type != NODE_ERROR) {
                Node *temp_copy = malloc(sizeof(Node));
                *temp_copy = temp;
                vn_push_back(scope->childs, *temp_copy);
                free(temp_copy);
            } else
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
    ifexpr.str = NULL;

    Token current = advance();
    if (strcmp(current.value, "(") == 0) {
        Node *expr = parseExpr();
        if (expr) {
            vn_push_back(ifexpr.childs, *expr);
            free(expr);
        }

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
            if (temp.type != NODE_ERROR) {
                Node *temp_copy = malloc(sizeof(Node));
                *temp_copy = temp;
                vn_push_back(scope->childs, *temp_copy);
                free(temp_copy);
            } else
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
    func.str = NULL;

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
            if (temp.type != NODE_ERROR) {
                Node *temp_copy = malloc(sizeof(Node));
                *temp_copy = temp;
                vn_push_back(scope->childs, *temp_copy);
                free(temp_copy);
            } else
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
    params.str = NULL;

    while (1) {
        Node param;
        param.childs = malloc(sizeof(vector_node));
        vn_init(param.childs, 2);
        param.type = NODE_PARAM;
        param.str = NULL;

        Token current = advance();
        if (current.type == TOK_TYPE || current.type == TOK_IDENT) {
            /* проверяем указатель */
            if (strcmp(peek(0).value, "*") == 0) {
                advance();
                char ptr_type_str[68];
                snprintf(ptr_type_str, sizeof(ptr_type_str), "%s*", current.value);
                Node *type = make_node(NODE_TYPE, ptr_type_str);
                vn_push_back(param.childs, *type);
                free(type);
            } else {
                Node *type = make_node(NODE_TYPE, current.value);
                vn_push_back(param.childs, *type);
                free(type);
            }
        } else {
            printf("Error: expected type in param, got '%s'\n", current.value);
        }

        current = advance();
        if (current.type == TOK_IDENT) {
            Node *ident = make_node(NODE_IDENT, current.value);
            vn_push_back(param.childs, *ident);
            free(ident);
        } else {
            printf("Error: expected identifier in param, got '%s'\n", current.value);
        }

        vn_push_back(params.childs, param);

        if (peek(0).type == TOK_COMMA)
            advance();
        else
            break;
    }

    params.type = NODE_PARAMS;
    return params;
}

Node parseVarDef(void) {
    Node var;
    var.childs = malloc(sizeof(vector_node));
    vn_init(var.childs, 4);
    var.str = NULL;

    Token current = advance();

    char base_type[64];
    base_type[0] = '\0';

    if (current.type == TOK_TYPE || current.type == TOK_IDENT) {
        if (strcmp(peek(0).value, "*") == 0) {
            advance();
            char ptr_type_str[68];
            snprintf(ptr_type_str, sizeof(ptr_type_str), "%s*", current.value);
            Node *type = make_node(NODE_TYPE, ptr_type_str);
            vn_push_back(var.childs, *type);
            free(type);
            strncpy(base_type, current.value, 63); // без *
        } else {
            Node *type = make_node(NODE_TYPE, current.value);
            vn_push_back(var.childs, *type);
            free(type);
            strncpy(base_type, current.value, 63);
        }
    } else {
        printf("Error: unknown variable type '%s'\n", current.value);
    }

    current = advance();
    char var_name[64];
    var_name[0] = '\0';

    if (current.type == TOK_IDENT) {
        strncpy(var_name, current.value, 63);
        Node *ident = make_node(NODE_IDENT, current.value);
        vn_push_back(var.childs, *ident);
        free(ident);
    } else {
        printf("Error: expected identifier\n");
    }

    var_type_push(var_name, base_type);

    if (strcmp(peek(0).value, "[") == 0) {
        advance();
        if (peek(0).type == TOK_INT) {
            Token size_tok = advance();
            Node *size = make_node(NODE_ARRAY_SIZE, size_tok.value);
            vn_push_back(var.childs, *size);
            free(size);
        } else {
            printf("Error: expected array size\n");
        }
        if (strcmp(peek(0).value, "]") == 0)
            advance();
        else
            printf("Error: expected ']'\n");
        Node *undef = make_node(NODE_UNDEF, "");
        vn_push_back(var.childs, *undef);
        free(undef);
        if (peek(0).type == TOK_SEMICOLON) advance();
        var.type = NODE_ARRAY_DEF;
        return var;
    }

    if (strcmp(peek(0).value, "=") == 0) {
        advance();

        if (peek(0).type == TOK_KEYWORD && strcmp(peek(0).value, "new") == 0) {
            advance();

            Node *new_node = make_node(NODE_NEW, base_type);

            Node *varname_node = make_node(NODE_IDENT, var_name);
            vn_push_back(new_node->childs, *varname_node);
            free(varname_node);

            Node *args = make_node(NODE_ARGS, "");
            if (strcmp(peek(0).value, "(") == 0) {
                advance();
                while (strcmp(peek(0).value, ")") != 0) {
                    if (peek(0).type == TOK_EOF) break;
                    Node *arg = parseExpr();
                    if (arg) {
                        vn_push_back(args->childs, *arg);
                        free(arg);
                    }
                    if (peek(0).type == TOK_COMMA) advance();
                    else break;
                }
                if (strcmp(peek(0).value, ")") == 0) advance();
            }
            vn_push_back(new_node->childs, *args);
            free(args);

            vn_push_back(var.childs, *new_node);
            free(new_node);
        } else {
            Node *expr = parseExpr();
            if (expr) {
                vn_push_back(var.childs, *expr);
                free(expr);
            }
        }
    } else {
        Node *undef = make_node(NODE_UNDEF, "");
        vn_push_back(var.childs, *undef);
        free(undef);
    }

    if (peek(0).type == TOK_SEMICOLON)
        advance();

    var.type = NODE_VAR_DEF;
    return var;
}

Node parseStruct(void) {
    Node str;
    str.childs = malloc(sizeof(vector_node));
    vn_init(str.childs, 4);
    str.str = NULL;

    Token name = advance();
    if (name.type != TOK_IDENT) {
        printf("Error: expected struct name\n");
        str.type = NODE_ERROR;
        return str;
    }
    
    str.str = malloc(strlen(name.value) + 1);
    strcpy(str.str, name.value);

    Token brace = advance();
    if (strcmp(brace.value, "{") != 0) {
        printf("Error: expected '{' after struct name\n");
        str.type = NODE_ERROR;
        return str;
    }

    while (strcmp(peek(0).value, "}") != 0) {
        if (peek(0).type == TOK_EOF) {
            printf("Error: unexpected EOF in struct body\n");
            break;
        }

        if (strcmp(peek(0).value, "var") != 0) {
            printf("Error: expected 'var' in struct body\n");
            str.type = NODE_ERROR;
            return str;
        }
        advance();

        Node field = parseVarDef();
        if (field.type == NODE_ERROR) {
            printf("Error in struct field\n");
            str.type = NODE_ERROR;
            return str;
        }
        vn_push_back(str.childs, field);
    }

    advance();

    str.type = NODE_STRUCT_DEF;
    return str;
}

Node parseClass(void) {
    Node cls;
    cls.childs = malloc(sizeof(vector_node));
    vn_init(cls.childs, 8);
    cls.str = NULL;

    Token name = advance();
    if (name.type != TOK_IDENT) {
        printf("Error: expected class name\n");
        cls.type = NODE_ERROR;
        return cls;
    }

    cls.str = malloc(strlen(name.value) + 1);
    strcpy(cls.str, name.value);

    Token brace = advance();
    if (strcmp(brace.value, "{") != 0) {
        printf("Error: expected '{' after class name\n");
        cls.type = NODE_ERROR;
        return cls;
    }

    while (strcmp(peek(0).value, "}") != 0) {
        if (peek(0).type == TOK_EOF) {
            printf("Error: unexpected EOF in class body\n");
            break;
        }

        if (peek(0).type == TOK_KEYWORD && strcmp(peek(0).value, "var") == 0) {
            advance();
            Node field = parseVarDef();
            if (field.type == NODE_ERROR) {
                printf("Error in class field\n");
                cls.type = NODE_ERROR;
                return cls;
            }
            vn_push_back(cls.childs, field);
        }
        else if (peek(0).type == TOK_KEYWORD && strcmp(peek(0).value, "func") == 0) {
            advance();
            Node method = parseFuncDef();
            if (method.type == NODE_ERROR) {
                printf("Error in class method\n");
                cls.type = NODE_ERROR;
                return cls;
            }

            Node *method_name_node = &method.childs->data[1];
            char new_name[128];
            snprintf(new_name, sizeof(new_name), "%s_%s", name.value, method_name_node->str);
            free(method_name_node->str);
            method_name_node->str = malloc(strlen(new_name) + 1);
            strcpy(method_name_node->str, new_name);

            Node *params_node = &method.childs->data[2];

            Node self_param;
            self_param.childs = malloc(sizeof(vector_node));
            vn_init(self_param.childs, 2);
            self_param.type = NODE_PARAM;
            self_param.str  = NULL;

            char self_type[68];
            snprintf(self_type, sizeof(self_type), "%s*", name.value);
            Node *self_type_node = make_node(NODE_TYPE, self_type);
            vn_push_back(self_param.childs, *self_type_node);
            free(self_type_node);

            Node *self_name_node = make_node(NODE_IDENT, "self");
            vn_push_back(self_param.childs, *self_name_node);
            free(self_name_node);

            vector_node *new_params = malloc(sizeof(vector_node));
            vn_init(new_params, params_node->childs->size + 1);
            vn_push_back(new_params, self_param);
            for (unsigned long long k = 0; k < params_node->childs->size; k++)
                vn_push_back(new_params, params_node->childs->data[k]);
            vn_free(params_node->childs);
            free(params_node->childs);
            params_node->childs = new_params;

            vn_push_back(cls.childs, method);
        }
        else {
            printf("Error: expected 'var' or 'func' in class body, got '%s'\n", peek(0).value);
            cls.type = NODE_ERROR;
            return cls;
        }
    }

    advance(); // }

    cls.type = NODE_STRUCT_DEF;
    return cls;
}

static Node *expr_member(Node *left) {
    Token t = peek(0);
    
    while (t.type == TOK_OP && (strcmp(t.value, ".") == 0 || strcmp(t.value, "->") == 0)) {
        advance();
        
        Token field = peek(0);
        if (field.type != TOK_IDENT) {
            printf("Error: expected field name after '%s'\n", t.value);
            return left;
        }
        advance();
        
        Node *node = make_node(
            strcmp(t.value, ".") == 0 ? NODE_MEMBER_DOT : NODE_MEMBER_ARROW,
            field.value
        );
        
        vn_push_back(node->childs, *left);
        free(left);
        
        left = node;
        t = peek(0);
    }
    
    return left;
}

static Node *expr_primary(void) {
    Token t = peek(0);

    if (strcmp(t.value, "sizeof") == 0) {
        advance(); 
        Token type_tok = advance();
        if (type_tok.type != TOK_TYPE && type_tok.type != TOK_IDENT) {
            printf("Error: expected type after 'sizeof'\n");
            return NULL;
        }
        return make_node(NODE_SIZEOF, type_tok.value);
    }

    if (t.type == TOK_STRING) {
        advance();
        return make_node(NODE_STRING, t.value);
    }

    if (t.type == TOK_OP && strcmp(t.value, "&") == 0) {
        advance();
        Token var_tok = advance();
        if (var_tok.type != TOK_IDENT) {
            printf("Error: expected identifier after '&'\n");
            return NULL;
        }
        return make_node(NODE_ADDR, var_tok.value);
    }

    if (t.type == TOK_OP && strcmp(t.value, "*") == 0) {
        advance();
        Node *inner = expr_unary();
        if (!inner) return NULL;
        Node *node = make_node(NODE_DEREF, "");
        vn_push_back(node->childs, *inner);
        free(inner);
        return node;
    }

    if (t.type == TOK_INT) {
        advance();
        size_t len = strlen(t.value);

        if (len > 1 && (t.value[len-1] == 's' || t.value[len-1] == 'S')) {
            char buf[64]; strncpy(buf, t.value, len-1); buf[len-1] = '\0';
            return make_node(NODE_I16, buf);
        }
        if (len > 1 && (t.value[len-1] == 'l' || t.value[len-1] == 'L')) {
            char buf[64]; strncpy(buf, t.value, len-1); buf[len-1] = '\0';
            return make_node(NODE_I64, buf);
        }
        long long val = atoll(t.value);
        if (val > 2147483647LL || val < -2147483648LL)
            return make_node(NODE_I64, t.value);
        return make_node(NODE_I32, t.value);
    }

    if (t.type == TOK_FLOAT) {
        advance();
        size_t len = strlen(t.value);
        if (len > 0 && (t.value[len-1] == 'f' || t.value[len-1] == 'F')) {
            char buf[64];
            strncpy(buf, t.value, len-1);
            buf[len-1] = '\0';
            return make_node(NODE_FLOAT, buf);
        }
        return make_node(NODE_DOUBLE, t.value);
    }

    if (t.type == TOK_IDENT) {
        advance();

        if ((strcmp(peek(0).value, ".") == 0 || strcmp(peek(0).value, "->") == 0) &&
            peek(1).type == TOK_IDENT &&
            strcmp(peek(2).value, "(") == 0)
        {
            Token op    = advance();
            Token mname = advance();

            const char *cls = var_type_lookup(t.value);
            if (!cls) {
                printf("Error: unknown type for variable '%s'\n", t.value);
                return NULL;
            }

            char full_name[128];
            snprintf(full_name, sizeof(full_name), "%s_%s", cls, mname.value);

            Token fake;
            fake.type = TOK_IDENT;
            strncpy(fake.value, full_name, 63);

            Node call = parseFuncCall(fake);
            vector_node *args_node = call.childs->data[1].childs;

            vector_node *new_args_childs = malloc(sizeof(vector_node));
            vn_init(new_args_childs, args_node->size + 1);

            Node *self_arg = (strcmp(op.value, ".") == 0)
                ? make_node(NODE_ADDR, t.value)
                : make_node(NODE_VAR,  t.value);
            vn_push_back(new_args_childs, *self_arg);
            free(self_arg);

            for (unsigned long long k = 0; k < args_node->size; k++)
                vn_push_back(new_args_childs, args_node->data[k]);

            vn_free(call.childs->data[1].childs);
            free(call.childs->data[1].childs);
            call.childs->data[1].childs = new_args_childs;

            Node *result = malloc(sizeof(Node));
            *result = call;
            return result;
        }
        
        if (strcmp(peek(0).value, "(") == 0) {
            Node call = parseFuncCall(t);
            Node *n = malloc(sizeof(Node));
            *n = call;
            return n;
        }
        
        if (strcmp(peek(0).value, "[") == 0) {
            advance();
            Node *idx = expr_or();
            Node *node = make_node(NODE_INDEX, "[]");
            Node *var = make_node(NODE_VAR, t.value);
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
        (strcmp(t.value, "-") == 0 ||
         strcmp(t.value, "!") == 0 ||
         strcmp(t.value, "~") == 0))
    {
        advance();
        Node *operand = expr_unary();
        if (!operand) return NULL;
        Node *node = make_node(NODE_UNOP, t.value);
        vn_push_back(node->childs, *operand);
        free(operand);
        return node;
    }
    return expr_primary();
}

Node *parseExpr(void) {
    Node *expr = expr_or();
    return expr_member(expr);
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

static Node *expr_shift(void) {
    Node *left = expr_term();
    while (peek(0).type == TOK_OP &&
           (strcmp(peek(0).value, "<<") == 0 ||
            strcmp(peek(0).value, ">>") == 0))
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

static Node *expr_add(void) {
    Node *left = expr_shift();
    while (peek(0).type == TOK_OP &&
           (strcmp(peek(0).value, "+") == 0 ||
            strcmp(peek(0).value, "-") == 0))
    {
        Token op    = advance();
        Node *right = expr_shift();
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

static Node *expr_bitand(void) {
    Node *left = expr_eq();
    while (peek(0).type == TOK_OP && strcmp(peek(0).value, "&") == 0)
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

static Node *expr_bitxor(void) {
    Node *left = expr_bitand();
    while (peek(0).type == TOK_OP && strcmp(peek(0).value, "^") == 0)
    {
        Token op    = advance();
        Node *right = expr_bitand();
        Node *node  = make_node(NODE_BINOP, op.value);
        vn_push_back(node->childs, *left);
        vn_push_back(node->childs, *right);
        free(left); free(right);
        left = node;
    }
    return left;
}

static Node *expr_bitor(void) {
    Node *left = expr_bitxor();
    while (peek(0).type == TOK_OP && strcmp(peek(0).value, "|") == 0)
    {
        Token op    = advance();
        Node *right = expr_bitxor();
        Node *node  = make_node(NODE_BINOP, op.value);
        vn_push_back(node->childs, *left);
        vn_push_back(node->childs, *right);
        free(left); free(right);
        left = node;
    }
    return left;
}

static Node *expr_and(void) {
    Node *left = expr_bitor();
    while (peek(0).type == TOK_OP && strcmp(peek(0).value, "&&") == 0)
    {
        Token op    = advance();
        Node *right = expr_bitor();
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