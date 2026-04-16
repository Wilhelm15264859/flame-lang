#include "fl_Parser.h"
#include "fl_Preproc.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

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

#define MAX_OVERLOADS      256
#define MAX_OVERLOAD_PARAMS 16

typedef struct {
    const char *prefix;
    const char *triple_component;
} AsmArchEntry;

static const AsmArchEntry asm_arch_table[] = {
    { "x86",     "x86"     },
    { "x86_64",  "x86_64"  },
    { "i386",    "i386"    },
    { "i686",    "i686"    },
    { "arm",     "arm"     },
    { "armeb",   "armeb"   },
    { "thumb",   "thumb"   },
    { "thumbeb", "thumbeb" },
    { "aarch64", "aarch64" },
    { "riscv32", "riscv32" },
    { "riscv64", "riscv64" },
    { "mips",    "mips"    },
    { "mips64",  "mips64"  },
    { "ppc",     "ppc"     },
    { "ppc64",   "ppc64"   },
    { "wasm32",  "wasm32"  },
    { "wasm64",  "wasm64"  },
    { "avr",     "avr"     },
    { "msp430",  "msp430"  },
    { "bpf",     "bpf"     },
    { "sparc",   "sparc"   },
    { "sparc64", "sparc64" },
    { NULL, NULL }
};

static int triple_has_component(const char *triple, const char *component) {
    size_t clen = strlen(component);
    const char *p = triple;
    while (p && *p) {
        if (strncmp(p, component, clen) == 0 &&
            (p[clen] == '-' || p[clen] == '\0' || p[clen] == '_'))
            return 1;
        p = strchr(p, '-');
        if (p) p++;
    }
    return 0;
}

typedef struct {
    char base_name[64];
    char mangled[128];
    char param_types[MAX_OVERLOAD_PARAMS][64];
    int  param_count;
    char ret_type[64];
    int  is_static;
} OverloadEntry;

static OverloadEntry overload_table[MAX_OVERLOADS];
static int           overload_count = 0;

static void overload_push(const char *base, const char *mangled,
                           const char param_types[][64], int param_count,
                           const char *ret_type)
{
    for (int i = 0; i < overload_count; i++) {
        if (strcmp(overload_table[i].mangled, mangled) == 0) return;
    }
    if (overload_count >= MAX_OVERLOADS) {
        printf("Error: overload table overflow\n");
        return;
    }
    OverloadEntry *e = &overload_table[overload_count++];
    strncpy(e->base_name, base,    63);
    strncpy(e->mangled,   mangled, 127);
    strncpy(e->ret_type,  ret_type ? ret_type : "void", 63);
    e->param_count = param_count < MAX_OVERLOAD_PARAMS
                   ? param_count : MAX_OVERLOAD_PARAMS;
    for (int i = 0; i < e->param_count; i++)
        strncpy(e->param_types[i], param_types[i], 63);
    e->is_static = 0;
}

static void overload_set_static(const char *mangled)
{
    for (int i = 0; i < overload_count; i++) {
        if (strcmp(overload_table[i].mangled, mangled) == 0) {
            overload_table[i].is_static = 1;
            return;
        }
    }
}

static void build_mangled_name(const char *base,
                                const char param_types[][64], int param_count,
                                char *out, int out_size)
{
    int pos = 0;
    out[pos++] = '_';
    for (int i = 0; base[i] && pos < out_size - 1; i++)
        out[pos++] = base[i];
    out[pos++] = '_';

    for (int i = 0; i < param_count && pos < out_size - 1; i++) {
        const char *t = param_types[i];
        if (strncmp(t, "autodel:", 8) == 0) t += 8;
        for (; *t && pos < out_size - 1; t++) {
            if (*t == ' ') continue;
            if (*t == '*') { out[pos++] = 'p'; continue; }
            out[pos++] = (char)tolower((unsigned char)*t);
        }
    }
    out[pos] = '\0';
}

static void normalize_type(const char *src, char *out, int out_size) {
    if (strncmp(src, "autodel:", 8) == 0) src += 8;
    int j = 0;
    for (; *src && j < out_size - 1; src++) {
        if (*src == ' ') continue;
        if (*src == '*') { out[j++] = 'p'; continue; }
        out[j++] = (char)tolower((unsigned char)*src);
    }
    out[j] = '\0';
}

static void collect_stars(char *type_buf, int buf_size) {
    while (peek(0).type == TOK_OP && strcmp(peek(0).value, "*") == 0) {
        advance();
        strncat(type_buf, "*", buf_size - strlen(type_buf) - 1);
    }
}

static int prescan_collect_stars(vector_token *toks, int *pos, int n,
                                  char *type_buf, int buf_size) {
    int count = 0;
    while (*pos < n && strcmp(toks->data[*pos].value, "*") == 0) {
        strncat(type_buf, "*", buf_size - strlen(type_buf) - 1);
        (*pos)++;
        count++;
    }
    return count;
}

static int node_infer_type(Node *n, char *out, int out_size) {
    if (!n) return 0;
    switch (n->type) {
        case NODE_I32:    strncpy(out, "int",    out_size - 1); return 1;
        case NODE_I64:    strncpy(out, "long",   out_size - 1); return 1;
        case NODE_I16:    strncpy(out, "short",  out_size - 1); return 1;
        case NODE_I8:     strncpy(out, "char",   out_size - 1); return 1;
        case NODE_FLOAT:  strncpy(out, "float",  out_size - 1); return 1;
        case NODE_DOUBLE: strncpy(out, "double", out_size - 1); return 1;
        case NODE_STRING: strncpy(out, "char*",  out_size - 1); return 1;
        case NODE_VAR:
        case NODE_IDENT: {
            const char *vt = var_type_lookup(n->str);
            if (vt) { strncpy(out, vt, out_size - 1); return 1; }
            return 0;
        }
        case NODE_FUNC_CALL: {
            for (int i = 0; i < overload_count; i++) {
                if (strcmp(overload_table[i].mangled, n->str) == 0) {
                    strncpy(out, overload_table[i].ret_type, out_size - 1);
                    return 1;
                }
            }
            return 0;
        }
        case NODE_MEMBER_DOT:
        case NODE_MEMBER_ARROW: {
            return 0;
        }
        case NODE_BINOP: {
            if (n->childs && n->childs->size >= 1)
                return node_infer_type(&n->childs->data[0], out, out_size);
            return 0;
        }
        default: return 0;
    }
}

static const char *resolve_overload(const char *base, Node *args_node)
{
    int argc = (int)(args_node ? args_node->childs->size : 0);

    char arg_types[MAX_OVERLOAD_PARAMS][64];
    int  known[MAX_OVERLOAD_PARAMS];
    for (int i = 0; i < argc && i < MAX_OVERLOAD_PARAMS; i++) {
        known[i] = node_infer_type(&args_node->childs->data[i],
                                    arg_types[i], 64);
    }

    OverloadEntry *candidates[MAX_OVERLOADS];
    int cand_count = 0;
    for (int i = 0; i < overload_count; i++) {
        if (strcmp(overload_table[i].base_name, base) == 0 &&
            overload_table[i].param_count == argc)
            candidates[cand_count++] = &overload_table[i];
    }

    if (cand_count == 0) return NULL;
    if (cand_count == 1) return candidates[0]->mangled;

    for (int ci = 0; ci < cand_count; ci++) {
        OverloadEntry *e = candidates[ci];
        int match = 1;
        for (int pi = 0; pi < argc && pi < MAX_OVERLOAD_PARAMS; pi++) {
            if (!known[pi]) continue;
            char norm_arg[64] = {0}, norm_param[64] = {0};
            normalize_type(arg_types[pi],        norm_arg,   sizeof(norm_arg));
            normalize_type(e->param_types[pi],   norm_param, sizeof(norm_param));
            if (strcmp(norm_arg, norm_param) != 0) { match = 0; break; }
        }
        if (match) return e->mangled;
    }

    printf("Warning: overload resolution ambiguous for '%s', using '%s'\n",
           base, candidates[0]->mangled);
    return candidates[0]->mangled;
}

/* Resolve for method call: tries with self first (non-static), then without (static).
   Returns mangled name and sets *out_is_static. */
static const char *resolve_method(const char *full_name, Node *user_args,
                                   const char *self_var, const char *op,
                                   int *out_is_static)
{
    /* Build probe with self prepended */
    Node probe;
    probe.childs = malloc(sizeof(vector_node));
    vn_init(probe.childs, user_args->childs->size + 1);

    Node fake_self;
    fake_self.type = (strcmp(op, ".") == 0) ? NODE_ADDR : NODE_VAR;
    fake_self.childs = malloc(sizeof(vector_node));
    vn_init(fake_self.childs, 1);
    fake_self.str = malloc(strlen(self_var) + 1);
    strcpy(fake_self.str, self_var);

    vn_push_back(probe.childs, fake_self);
    for (unsigned long long k = 0; k < user_args->childs->size; k++)
        vn_push_back(probe.childs, user_args->childs->data[k]);

    const char *res_with_self = resolve_overload(full_name, &probe);

    vn_free(probe.childs);
    free(probe.childs);
    free(fake_self.str);
    vn_free(fake_self.childs);
    free(fake_self.childs);

    if (res_with_self) {
        for (int oi = 0; oi < overload_count; oi++) {
            if (strcmp(overload_table[oi].mangled, res_with_self) == 0) {
                if (!overload_table[oi].is_static) {
                    *out_is_static = 0;
                    return res_with_self;
                }
                break;
            }
        }
    }

    const char *res_no_self = resolve_overload(full_name, user_args);
    if (res_no_self) {
        *out_is_static = 1;
        return res_no_self;
    }

    if (res_with_self) {
        *out_is_static = 0;
        return res_with_self;
    }

    *out_is_static = 0;
    return NULL;
}

static void prescan_skip_block(vector_token *toks, int *pos) {
    if (*pos >= (int)toks->size || strcmp(toks->data[*pos].value, "{") != 0)
        return;
    int depth = 1;
    (*pos)++;
    while (*pos < (int)toks->size && depth > 0) {
        if      (strcmp(toks->data[*pos].value, "{") == 0) depth++;
        else if (strcmp(toks->data[*pos].value, "}") == 0) depth--;
        (*pos)++;
    }
}

static void prescan_params(vector_token *toks, int *pos,
                            char param_types[][64], int *param_count)
{
    *param_count = 0;
    if (*pos >= (int)toks->size || strcmp(toks->data[*pos].value, "(") != 0)
        return;
    (*pos)++;

    while (*pos < (int)toks->size && strcmp(toks->data[*pos].value, ")") != 0) {
        Token *t = &toks->data[*pos];
        if (t->type == TOK_TYPE || t->type == TOK_IDENT) {
            char type_buf[64];
            strncpy(type_buf, t->value, 63);
            (*pos)++;
            prescan_collect_stars(toks, pos, (int)toks->size, type_buf, 64);
            if (*param_count < MAX_OVERLOAD_PARAMS)
                strncpy(param_types[(*param_count)++], type_buf, 63);
            if (*pos < (int)toks->size && toks->data[*pos].type == TOK_IDENT)
                (*pos)++;
        }
        if (*pos < (int)toks->size && toks->data[*pos].type == TOK_COMMA)
            (*pos)++;
    }
    if (*pos < (int)toks->size && strcmp(toks->data[*pos].value, ")") == 0)
        (*pos)++;
}

static void prescan(vector_token *toks) {
    int pos = 0;
    int n   = (int)toks->size;

    while (pos < n) {
        Token *t = &toks->data[pos];

        if (t->type == TOK_KEYWORD && strcmp(t->value, "extern") == 0 &&
            pos + 1 < n &&
            toks->data[pos + 1].type == TOK_STRING &&
            strcmp(toks->data[pos + 1].value, "C") == 0)
        {
            pos += 2;
            if (pos >= n) break;
            char ret_type[64] = "void";
            if (toks->data[pos].type == TOK_TYPE || toks->data[pos].type == TOK_IDENT) {
                strncpy(ret_type, toks->data[pos].value, 63);
                pos++;
                prescan_collect_stars(toks, &pos, n, ret_type, 64);
            }
            if (pos >= n || toks->data[pos].type != TOK_IDENT) continue;
            char func_name[64];
            strncpy(func_name, toks->data[pos].value, 63);
            pos++;
            char param_types[MAX_OVERLOAD_PARAMS][64];
            int  param_count = 0;
            prescan_params(toks, &pos, param_types, &param_count);
            char tagged[128];
            snprintf(tagged, sizeof(tagged), "__extern_c__%s", func_name);
            overload_push(func_name, tagged,
                          (const char (*)[64])param_types, param_count, ret_type);
            printf("DEBUG prescan: extern C func '%s' (no mangle)\n", func_name);
            if (pos < n && strcmp(toks->data[pos].value, "{") == 0)
                prescan_skip_block(toks, &pos);
            continue;
        }

        if ((t->type == TOK_TYPE || t->type == TOK_IDENT) &&
            t->type != TOK_KEYWORD)
        {
            int p = pos;
            char ret_type[64];
            strncpy(ret_type, toks->data[p].value, 63);
            p++;
            prescan_collect_stars(toks, &p, n, ret_type, 64);
            if (p >= n || toks->data[p].type != TOK_IDENT) { pos++; continue; }
            char func_name[64];
            strncpy(func_name, toks->data[p].value, 63);
            p++;
            if (p >= n || strcmp(toks->data[p].value, "(") != 0) { pos++; continue; }

            pos = p;
            char param_types[MAX_OVERLOAD_PARAMS][64];
            int  param_count = 0;
            prescan_params(toks, &pos, param_types, &param_count);

            char mangled[128];
            if (strcmp(func_name, "main") == 0)
                strncpy(mangled, "main", sizeof(mangled));
            else
                build_mangled_name(func_name,
                                   (const char (*)[64])param_types, param_count,
                                   mangled, sizeof(mangled));
            overload_push(func_name, mangled,
                          (const char (*)[64])param_types, param_count,
                          ret_type);
            printf("DEBUG prescan: func '%s' -> '%s' ret='%s'\n",
                   func_name, mangled, ret_type);

            if (pos < n && strcmp(toks->data[pos].value, "{") == 0)
                prescan_skip_block(toks, &pos);
            continue;
        }

        if (t->type == TOK_KEYWORD && strcmp(t->value, "class") == 0) {
            pos++;
            if (pos >= n || toks->data[pos].type != TOK_IDENT) continue;
            char class_name[64];
            strncpy(class_name, toks->data[pos].value, 63);
            pos++;

            if (pos < n && strcmp(toks->data[pos].value, "<") == 0) {
                pos++;
                if (pos < n) pos++;
            }

            if (pos >= n || strcmp(toks->data[pos].value, "{") != 0) continue;
            pos++;

            int depth = 1;
            while (pos < n && depth > 0) {
                Token *ct = &toks->data[pos];

                if (ct->type == TOK_KEYWORD && strcmp(ct->value, "new") == 0) {
                    pos++;
                    char ctor_base[128];
                    snprintf(ctor_base, sizeof(ctor_base), "%s_new", class_name);
                    char self_type[68];
                    snprintf(self_type, sizeof(self_type), "%s*", class_name);
                    char param_types[MAX_OVERLOAD_PARAMS][64];
                    int  param_count = 0;
                    strncpy(param_types[param_count++], self_type, 63);
                    char user_ptypes[MAX_OVERLOAD_PARAMS][64];
                    int  user_pcount = 0;
                    prescan_params(toks, &pos, user_ptypes, &user_pcount);
                    for (int ui = 0; ui < user_pcount && param_count < MAX_OVERLOAD_PARAMS; ui++)
                        strncpy(param_types[param_count++], user_ptypes[ui], 63);
                    char mangled[128];
                    build_mangled_name(ctor_base,
                                       (const char (*)[64])param_types,
                                       param_count, mangled, sizeof(mangled));
                    overload_push(ctor_base, mangled,
                                  (const char (*)[64])param_types, param_count,
                                  "void");
                    printf("DEBUG prescan: ctor '%s' -> '%s'\n", ctor_base, mangled);
                    if (pos < n && strcmp(toks->data[pos].value, "{") == 0)
                        prescan_skip_block(toks, &pos);
                    continue;
                }

                if (ct->type == TOK_KEYWORD && strcmp(ct->value, "delete") == 0) {
                    pos++;
                    char dtor_base[128];
                    snprintf(dtor_base, sizeof(dtor_base), "%s_delete", class_name);
                    char self_type[68];
                    snprintf(self_type, sizeof(self_type), "%s*", class_name);
                    char param_types[MAX_OVERLOAD_PARAMS][64];
                    int  param_count = 0;
                    strncpy(param_types[param_count++], self_type, 63);
                    char user_ptypes[MAX_OVERLOAD_PARAMS][64];
                    int  user_pcount = 0;
                    prescan_params(toks, &pos, user_ptypes, &user_pcount);
                    for (int ui = 0; ui < user_pcount && param_count < MAX_OVERLOAD_PARAMS; ui++)
                        strncpy(param_types[param_count++], user_ptypes[ui], 63);
                    char mangled[128];
                    build_mangled_name(dtor_base,
                                       (const char (*)[64])param_types,
                                       param_count, mangled, sizeof(mangled));
                    overload_push(dtor_base, mangled,
                                  (const char (*)[64])param_types, param_count,
                                  "void");
                    printf("DEBUG prescan: dtor '%s' -> '%s'\n", dtor_base, mangled);
                    if (pos < n && strcmp(toks->data[pos].value, "{") == 0)
                        prescan_skip_block(toks, &pos);
                    continue;
                }

                /* Check static keyword */
                int is_static_method = 0;
                if (ct->type == TOK_KEYWORD && strcmp(ct->value, "static") == 0) {
                    is_static_method = 1;
                    pos++;
                    if (pos >= n) break;
                    ct = &toks->data[pos];
                }

                if ((ct->type == TOK_TYPE || ct->type == TOK_IDENT) &&
                    ct->type != TOK_KEYWORD)
                {
                    char ret_type[64];
                    strncpy(ret_type, ct->value, 63);
                    pos++;
                    prescan_collect_stars(toks, &pos, n, ret_type, 64);

                    if (pos >= n || toks->data[pos].type != TOK_IDENT) continue;
                    char mname[64];
                    strncpy(mname, toks->data[pos].value, 63);
                    pos++;
                    if (pos >= n || strcmp(toks->data[pos].value, "(") != 0) continue;

                    char base_method[128];
                    snprintf(base_method, sizeof(base_method), "%s_%s",
                             class_name, mname);

                    char param_types[MAX_OVERLOAD_PARAMS][64];
                    int  param_count = 0;
                    if (!is_static_method) {
                        char self_type[68];
                        snprintf(self_type, sizeof(self_type), "%s*", class_name);
                        strncpy(param_types[param_count++], self_type, 63);
                    }
                    char user_ptypes[MAX_OVERLOAD_PARAMS][64];
                    int  user_pcount = 0;
                    prescan_params(toks, &pos, user_ptypes, &user_pcount);
                    for (int ui = 0; ui < user_pcount && param_count < MAX_OVERLOAD_PARAMS; ui++)
                        strncpy(param_types[param_count++], user_ptypes[ui], 63);

                    char mangled[128];
                    build_mangled_name(base_method,
                                       (const char (*)[64])param_types,
                                       param_count, mangled, sizeof(mangled));
                    overload_push(base_method, mangled,
                                  (const char (*)[64])param_types, param_count,
                                  ret_type);

                    /* Mark static AFTER push */
                    if (is_static_method)
                        overload_set_static(mangled);

                    printf("DEBUG prescan: method '%s' -> '%s' ret='%s' static=%d\n",
                           base_method, mangled, ret_type, is_static_method);

                    if (pos < n && strcmp(toks->data[pos].value, "{") == 0)
                        prescan_skip_block(toks, &pos);
                    continue;
                }

                if (strcmp(toks->data[pos].value, "{") == 0) depth++;
                else if (strcmp(toks->data[pos].value, "}") == 0) { depth--; }
                pos++;
            }
            continue;
        }

        pos++;
    }
}

static char known_classes[64][64];
static int  known_class_count = 0;

static void class_push(const char *name) {
    if (known_class_count >= 64) return;
    strncpy(known_classes[known_class_count++], name, 63);
}

static int class_lookup(const char *name) {
    for (int j = 0; j < known_class_count; j++)
        if (strcmp(known_classes[j], name) == 0) return 1;
    return 0;
}

static Node *make_node(NodeType type, const char *str) {
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
static Node *expr_unary(void);

Node parseVarDef(Token t, int m);
Node parseFuncDef(Token t);
static Node parseFuncDefInner(Token t, int do_mangle);
Node parseParams(void);
Node parseIf(void);
Node parseWhile(void);
Node parseFuncCall(Token t);
Node parseAssign(Token t);
Node parseReturn(void);
Node parsePtrAssign(void);
Node *parseExpr(void);
Node parseStruct(void);
Node parseAsm(Token arch_tok);
Node parseClass(void);
Node parseCompoundAssign(Token t, int f);
Node parseFor(void);
Node parseDelete(void);
Node parseExternFuncDef(void);
Node parseExternCFuncDef(void);
Node parseDoWhile(void);
Node parseMemberCompoundAssign(Token obj);
Node *parse_body(void);
Node parseEnum(void);
Node parseVarDefConst(Token t);
Node parseTypedef(void);

static int is_func_type(const char *s) {
    return strncmp(s, "F<", 2) == 0;
}

Node parsing(void) {
    Token current = advance();
    printf("DEBUG parsing: type=%d value='%s'\n col='%i' line='%i'\n", current.type, current.value, current.col, current.line);

    if (current.type == TOK_KEYWORD) {
        if (strcmp(current.value, "if")     == 0) return parseIf();
        if (strcmp(current.value, "while")  == 0) return parseWhile();
        if (strcmp(current.value, "return") == 0) return parseReturn();
        if (strcmp(current.value, "struct") == 0) return parseStruct();
        if (strcmp(current.value, "class")  == 0) return parseClass();
        if (strcmp(current.value, "for")    == 0) return parseFor();
        if (strcmp(current.value, "do")     == 0) return parseDoWhile();
        if (strcmp(current.value, "delete") == 0) return parseDelete();
        if (strcmp(current.value, "enum")   == 0) return parseEnum();
        if (strcmp(current.value, "extern") == 0) {
            if (peek(0).type == TOK_STRING && strcmp(peek(0).value, "C") == 0)
                return parseExternCFuncDef();
            return parseExternFuncDef();
        }
        if (strcmp(current.value, "notdel") == 0) {
            Token type_tok = advance();
            return parseVarDef(type_tok, 0);
        }
        if (strcmp(current.value, "typedef") == 0) {
            return parseTypedef();
        }
        if (strcmp(current.value, "const") == 0) {
            Token type_tok = advance();
            return parseVarDefConst(type_tok);
        }
        if (current.type == TOK_TYPE) goto type_decl;
        return parseAsm(current);
    }

    if (current.type == TOK_OP && strcmp(current.value, "*") == 0)
        return parsePtrAssign();

    /* F<RetType> name ... — function-type variable/array/param */
    if (current.type == TOK_IDENT && strcmp(current.value, "F") == 0 &&
        peek(0).type == TOK_OP && strcmp(peek(0).value, "<") == 0)
    {
        /* Re-wind one token so parseVarDef can call parse_type_token */
        i--;
        Token fake;
        fake = advance(); /* re-consume 'F' */
        return parseVarDef(fake, 1);
    }

type_decl:
    if (current.type == TOK_TYPE ||
        (current.type == TOK_IDENT && class_lookup(current.value)))
    {
        int ptr_offset = 0;
        while (peek(ptr_offset).type == TOK_OP && strcmp(peek(ptr_offset).value, "*") == 0)
            ptr_offset++;
        Token name_tok = peek(ptr_offset);
        if (name_tok.type == TOK_IDENT) {
            Token after_name = peek(ptr_offset + 1);
            if (strcmp(after_name.value, "(") == 0)
                return parseFuncDef(current);
            else
                return parseVarDef(current, 1);
        }
    }

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
        else if ((strcmp(peek(0).value, ".") == 0 || strcmp(peek(0).value, "->") == 0) &&
                peek(1).type == TOK_IDENT &&
                (strcmp(peek(2).value, "++") == 0 || strcmp(peek(2).value, "--") == 0 ||
                strcmp(peek(2).value, "+=") == 0  || strcmp(peek(2).value, "-=") == 0 ||
                strcmp(peek(2).value, "*=") == 0  || strcmp(peek(2).value, "/=") == 0 ||
                strcmp(peek(2).value, "%=") == 0))
            return parseMemberCompoundAssign(current);
        else if (strcmp(peek(0).value, "++") == 0 ||
                 strcmp(peek(0).value, "--") == 0)
            return parseCompoundAssign(current, 1);
        else if (strcmp(peek(0).value, "+=") == 0 ||
                 strcmp(peek(0).value, "-=") == 0 ||
                 strcmp(peek(0).value, "*=") == 0 ||
                 strcmp(peek(0).value, "/=") == 0 ||
                 strcmp(peek(0).value, "%=") == 0)
            return parseCompoundAssign(current, 1);
        else if (strcmp(peek(0).value, "=") == 0 ||
                 strcmp(peek(0).value, "[") == 0 ||
                 strcmp(peek(0).value, ".") == 0 ||
                 strcmp(peek(0).value, "->") == 0)
            return parseAssign(current);
        else {
            printf("Error: unexpected identifier '%s'\n", current.value);
            exit(1);
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
    exit(1);
    Node error;
    error.type = NODE_ERROR;
    error.childs = NULL;
    error.str = NULL;
    return error;
}

static int parse_type_token(char *out, int out_size) {
    Token t = advance();

    if (strcmp(t.value, "F") == 0 &&
        peek(0).type == TOK_OP && strcmp(peek(0).value, "<") == 0)
    {
        advance(); /* < */
        Token ret_tok = advance();
        if (peek(0).type == TOK_OP && strcmp(peek(0).value, ">") == 0)
            advance(); /* > */

        char buf[128];
        snprintf(buf, sizeof(buf), "F<%s>(", ret_tok.value);

        /* If the next token is an ident (param name) followed by '(' — arg list */
        if (peek(0).type == TOK_IDENT &&
            peek(1).type == TOK_PAREN && strcmp(peek(1).value, "(") == 0)
        {
            advance(); /* param name */
            advance(); /* ( */
            int first = 1;
            while (strcmp(peek(0).value, ")") != 0 && peek(0).type != TOK_EOF) {
                if (!first) strncat(buf, ",", sizeof(buf) - strlen(buf) - 1);
                first = 0;
                Token arg_type = advance();
                strncat(buf, arg_type.value, sizeof(buf) - strlen(buf) - 1);
                if (peek(0).type == TOK_IDENT) advance(); /* optional arg name */
                if (peek(0).type == TOK_COMMA) advance();
            }
            if (strcmp(peek(0).value, ")") == 0) advance();
        }
        /* F<Ret> name — no arg list, just a plain function-pointer variable */

        strncat(buf, ")", sizeof(buf) - strlen(buf) - 1);
        strncpy(out, buf, out_size - 1);
        out[out_size - 1] = '\0';
        return 1; /* is F<> type — caller must consume name separately */
    }

    strncpy(out, t.value, out_size - 1);
    out[out_size - 1] = '\0';
    while (peek(0).type == TOK_OP && strcmp(peek(0).value, "*") == 0) {
        advance();
        strncat(out, "*", out_size - strlen(out) - 1);
    }
    return 0;
}

Node parseVarDefConst(Token t) {
    char new_type[80];
    snprintf(new_type, sizeof(new_type), "const:%s", t.value);
    Token patched = t;
    strncpy(patched.value, new_type, 63);
    return parseVarDef(patched, 0);
}

Node parseTypedef(void) {
    /* typedef <original_type> [*...] <alias_name> ; */
    Node td;
    td.childs = malloc(sizeof(vector_node));
    vn_init(td.childs, 2);
    td.str = NULL;

    Token type_tok = advance();
    if (type_tok.type != TOK_TYPE && type_tok.type != TOK_IDENT) {
        printf("Error: expected type after 'typedef'\n");
        exit(1);
        td.type = NODE_ERROR;
        return td;
    }

    char type_str[80];
    strncpy(type_str, type_tok.value, 79);
    type_str[79] = '\0';
    collect_stars(type_str, 80);

    Token alias_tok = advance();
    if (alias_tok.type != TOK_IDENT) {
        printf("Error: expected alias name in typedef\n");
        exit(1);
        td.type = NODE_ERROR;
        return td;
    }

    /* Регистрируем как псевдоним типа, чтобы парсер его распознавал */
    class_push(alias_tok.value);

    Node *orig  = make_node(NODE_TYPE,  type_str);
    Node *alias = make_node(NODE_IDENT, alias_tok.value);
    vn_push_back(td.childs, *orig);  free(orig);
    vn_push_back(td.childs, *alias); free(alias);

    if (peek(0).type == TOK_SEMICOLON) advance();
    else { printf("Error: semicolon expected after typedef\n"); exit(1); }

    td.type = NODE_TYPEDEF;
    return td;
}

Node parseEnum(void) {
    Node en;
    en.childs = malloc(sizeof(vector_node));
    vn_init(en.childs, 8);
    en.str = NULL;

    if (peek(0).type == TOK_IDENT)
        advance();

    Token brace = advance();
    if (strcmp(brace.value, "{") != 0) {
        printf("Error: expected '{' after enum name\n");
        exit(1);
        en.type = NODE_ERROR;
        return en;
    }

    int counter = 0;
    while (strcmp(peek(0).value, "}") != 0) {
        if (peek(0).type == TOK_EOF) {
            printf("Error: unexpected EOF in enum body\n");
            exit(1);
            break;
        }

        Token name_tok = advance();
        if (name_tok.type != TOK_IDENT) {
            printf("Error: expected identifier in enum, got '%s'\n", name_tok.value);
            exit(1);
            en.type = NODE_ERROR;
            return en;
        }

        int value = counter;

        if (peek(0).type == TOK_OP && strcmp(peek(0).value, "=") == 0) {
            advance();
            Token val_tok = advance();
            if (val_tok.type != TOK_INT) {
                printf("Error: expected integer after '=' in enum\n");
                exit(1);
                en.type = NODE_ERROR;
                return en;
            }
            value = (int)strtol(val_tok.value, NULL, 0);
        }

        counter = value + 1;

        Node *var = make_node(NODE_VAR_DEF, "");
        Node *type  = make_node(NODE_TYPE,  "int");
        Node *ident = make_node(NODE_IDENT, name_tok.value);
        char  val_str[32];
        snprintf(val_str, sizeof(val_str), "%d", value);
        Node *val_node = make_node(NODE_I32, val_str);

        vn_push_back(var->childs, *type);     free(type);
        vn_push_back(var->childs, *ident);    free(ident);
        vn_push_back(var->childs, *val_node); free(val_node);
        var->type = NODE_VAR_DEF;

        vn_push_back(en.childs, *var);
        free(var);

        if (peek(0).type == TOK_COMMA) advance();
    }

    advance();

    if (peek(0).type == TOK_SEMICOLON) advance();

    en.type = NODE_ENUM;
    return en;
}

Node parseExternCFuncDef(void) {
    advance();

    Node func;
    func.childs = malloc(sizeof(vector_node));
    vn_init(func.childs, 4);
    func.str = NULL;

    Token current = advance();
    if (current.type == TOK_TYPE || current.type == TOK_IDENT) {
        char ret_str[80];
        strncpy(ret_str, current.value, 79);
        collect_stars(ret_str, 80);
        Node *type = make_node(NODE_TYPE, ret_str);
        vn_push_back(func.childs, *type);
        free(type);
    } else {
        printf("Error: expected return type in extern \"C\" func\n");
        exit(1);
    }

    current = advance();
    if (current.type == TOK_IDENT) {
        char tagged[128];
        snprintf(tagged, sizeof(tagged), "__extern_c__%s", current.value);
        Node *ident = make_node(NODE_IDENT, tagged);
        vn_push_back(func.childs, *ident);
        free(ident);
    } else {
        printf("Error: expected function name in extern \"C\" func\n");
        exit(1);
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
        if (strcmp(current.value, ")") != 0) {
            printf("Error: expected ')' in extern \"C\" func\n");
            exit(1);
        }
    } else {
        printf("Error: expected '(' in extern \"C\" func\n");
        exit(1);
    }

    current = advance();

    if (current.type == TOK_SEMICOLON) {
        func.type = NODE_EXTERN_FUNC_DEF;
        return func;
    }

    if (strcmp(current.value, "{") == 0) {
        Node *scope = make_node(NODE_SCOPE, "");
        while (strcmp(peek(0).value, "}") != 0) {
            if (peek(0).type == TOK_EOF) {
                printf("Error: unexpected EOF in extern \"C\" function body\n");
                exit(1);
                break;
            }
            Node temp = parsing();
            if (temp.type != NODE_ERROR) {
                Node *tmp = malloc(sizeof(Node));
                *tmp = temp;
                vn_push_back(scope->childs, *tmp);
                free(tmp);
            }
        }
        advance();
        vn_push_back(func.childs, *scope);
        free(scope);
        func.type = NODE_FUNC_DEF;
        return func;
    }

    printf("Error: expected '{' or ';' in extern \"C\" func\n");
    exit(1);
    func.type = NODE_ERROR;
    return func;
}

Node parseExternFuncDef(void) {
    Node ext;
    ext.childs = malloc(sizeof(vector_node));
    vn_init(ext.childs, 3);
    ext.str = NULL;

    Token t = advance();
    char ret_type_str[80];
    if (t.type != TOK_TYPE && t.type != TOK_IDENT) {
        printf("Error: expected return type in extern func\n");
        exit(1);
        ext.type = NODE_ERROR;
        return ext;
    }
    strncpy(ret_type_str, t.value, 79);
    collect_stars(ret_type_str, 80);
    Node *ret_type = make_node(NODE_TYPE, ret_type_str);
    vn_push_back(ext.childs, *ret_type);
    free(ret_type);

    t = advance();
    if (t.type != TOK_IDENT) {
        printf("Error: expected function name in extern func\n");
        exit(1);
        ext.type = NODE_ERROR;
        return ext;
    }
    Node *fname = make_node(NODE_IDENT, t.value);
    vn_push_back(ext.childs, *fname);
    free(fname);

    t = advance();
    if (strcmp(t.value, "(") != 0) {
        printf("Error: expected '(' in extern func\n");
        exit(1);
        ext.type = NODE_ERROR;
        return ext;
    }

    if (strcmp(peek(0).value, ")") == 0) {
        Node *params = make_node(NODE_PARAMS, "");
        vn_push_back(ext.childs, *params);
        free(params);
    } else {
        Node params = parseParams();
        vn_push_back(ext.childs, params);
    }

    t = advance();
    if (strcmp(t.value, ")") != 0) {
        printf("Error: expected ')' in extern func\n");
        exit(1);
    }

    if (peek(0).type == TOK_SEMICOLON) advance();
    else {
        printf("Error: semicolon expected");
        exit(1);
    }

    ext.type = NODE_EXTERN_FUNC_DEF;
    return ext;
}

Node parseDelete(void) {
    Node del;
    del.childs = malloc(sizeof(vector_node));
    vn_init(del.childs, 2);
    del.str = NULL;

    Token t = advance();
    if (strcmp(t.value, "(") != 0) {
        printf("Error: expected '(' after 'delete'\n");
        del.type = NODE_ERROR;
        return del;
    }

    Node *args = make_node(NODE_ARGS, "");
    while (strcmp(peek(0).value, ")") != 0) {
        if (peek(0).type == TOK_EOF) break;
        Node *arg = parseExpr();
        if (arg) { vn_push_back(args->childs, *arg); free(arg); }
        if (peek(0).type == TOK_COMMA) advance();
        else break;
    }
    t = advance();
    if (strcmp(t.value, ")") != 0)
        printf("Error: expected ')' after delete args\n");

    vn_push_back(del.childs, *args);
    free(args);

    t = advance();
    if (t.type != TOK_IDENT) {
        printf("Error: expected variable name after 'delete(...)'\n");
        del.type = NODE_ERROR;
        return del;
    }

    Node *var = make_node(NODE_VAR, t.value);
    vn_push_back(del.childs, *var);
    free(var);

    if (peek(0).type == TOK_SEMICOLON) advance();
    else {
        printf("Error: semicolon expected");
        exit(1);
    }

    del.type = NODE_DELETE;
    return del;
}

Node *parse_body(void) {
    if (strcmp(peek(0).value, "{") == 0) {
        advance();
        Node *scope = make_node(NODE_SCOPE, "");
        while (strcmp(peek(0).value, "}") != 0) {
            if (peek(0).type == TOK_EOF) {
                printf("Error: unexpected EOF in body\n");
                exit(1);
                break;
            }
            Node temp = parsing();
            if (temp.type != NODE_ERROR) {
                Node *tc = malloc(sizeof(Node));
                *tc = temp;
                vn_push_back(scope->childs, *tc);
                free(tc);
            } else {
                printf("Error in body\n");
                exit(1);
            }
        }
        advance();
        return scope;
    } else {
        Node temp = parsing();
        Node *tc = malloc(sizeof(Node));
        *tc = temp;
        return tc;
    }
}

Node parseMemberCompoundAssign(Token obj) {
    Token op_tok    = advance();
    Token field_tok = advance();
    Token cmp_tok   = advance();

    Node *lhs_obj = make_node(NODE_VAR, obj.value);
    Node *lhs = make_node(
        strcmp(op_tok.value, ".") == 0 ? NODE_MEMBER_DOT : NODE_MEMBER_ARROW,
        field_tok.value
    );
    vn_push_back(lhs->childs, *lhs_obj); free(lhs_obj);

    Node assign;
    assign.childs = malloc(sizeof(vector_node));
    vn_init(assign.childs, 3);
    assign.str  = NULL;
    assign.type = NODE_MEMBER_ASSIGN;
    vn_push_back(assign.childs, *lhs); free(lhs);

    Node *undef = make_node(NODE_UNDEF, "");
    vn_push_back(assign.childs, *undef); free(undef);

    Node *rhs_obj = make_node(NODE_VAR, obj.value);
    Node *lhs2 = make_node(
        strcmp(op_tok.value, ".") == 0 ? NODE_MEMBER_DOT : NODE_MEMBER_ARROW,
        field_tok.value
    );
    vn_push_back(lhs2->childs, *rhs_obj); free(rhs_obj);

    Node *rhs_expr;
    if (strcmp(cmp_tok.value, "++") == 0 || strcmp(cmp_tok.value, "--") == 0) {
        Node *one = make_node(NODE_I32, "1");
        const char *op = (strcmp(cmp_tok.value, "++") == 0) ? "+" : "-";
        rhs_expr = make_node(NODE_BINOP, op);
        vn_push_back(rhs_expr->childs, *lhs2); free(lhs2);
        vn_push_back(rhs_expr->childs, *one);  free(one);
    } else {
        Node *rhs = parseExpr();
        char binop[2] = { cmp_tok.value[0], '\0' };
        rhs_expr = make_node(NODE_BINOP, binop);
        vn_push_back(rhs_expr->childs, *lhs2); free(lhs2);
        vn_push_back(rhs_expr->childs, *rhs);  free(rhs);
    }
    vn_push_back(assign.childs, *rhs_expr); free(rhs_expr);

    if (peek(0).type == TOK_SEMICOLON) advance();
    else {
        printf("Error: semicolon expected");
        exit(1);
    }
    return assign;
}

Node parseCompoundAssign(Token t, int f) {
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
    else if (f == 1) {
        printf("Error: semicolon expected");
        exit(1);
    }

    assign.type = NODE_ASSIGN;
    return assign;
}

Node parseAsm(Token arch_tok) {
    Node asmnode;
    memset(&asmnode, 0, sizeof(Node));
    asmnode.childs = malloc(sizeof(vector_node));
    vn_init(asmnode.childs, 8);
    asmnode.str = malloc(2048);
    asmnode.str[0] = '\0';

    const char *asm_component = NULL;
    for (int ai = 0; asm_arch_table[ai].prefix != NULL; ai++) {
        if (strcmp(arch_tok.value, asm_arch_table[ai].prefix) == 0) {
            asm_component = asm_arch_table[ai].triple_component;
            break;
        }
    }

    if (asm_component == NULL) {
        printf("Error [line %d, col %d]: unknown keyword or asm arch prefix '%s'\n",
               arch_tok.line, arch_tok.col, arch_tok.value);
        exit(1);
        asmnode.type = NODE_ERROR;
        return asmnode;
    }

    const char *current_triple = preprocess_get_target();
    if (current_triple != NULL && current_triple[0] != '\0') {
        if (!triple_has_component(current_triple, asm_component)) {
            printf("Error [line %d, col %d]: "
                   "asm arch '%s' is incompatible with target '%s'\n"
                   "  Hint: wrap platform-specific asm in "
                   "#ifdef %s ... #endif\n",
                   arch_tok.line, arch_tok.col,
                   arch_tok.value, current_triple,
                   arch_tok.value);
            exit(1);
            asmnode.type = NODE_ERROR;
            return asmnode;
        }
    }

    Node arch_node;
    memset(&arch_node, 0, sizeof(Node));
    arch_node.type   = NODE_IDENT;
    arch_node.childs = malloc(sizeof(vector_node));
    vn_init(arch_node.childs, 1);
    arch_node.str = malloc(strlen(arch_tok.value) + 1);
    strcpy(arch_node.str, arch_tok.value);
    vn_push_back(asmnode.childs, arch_node);

    /* ── Собираем тело: одна инструкция или блок { ... } ── */
    char instr[1024];
    instr[0] = '\0';
    int operand_count = 0;
    int is_block = (peek(0).type == TOK_PAREN && strcmp(peek(0).value, "{") == 0);

    if (is_block) {
        advance(); /* { */
        int first_instr = 1;
        while (!(peek(0).type == TOK_PAREN && strcmp(peek(0).value, "}") == 0) &&
               peek(0).type != TOK_EOF) {
            /* Каждый оператор внутри блока разделён ';' */
            if (!first_instr) strncat(instr, "\n", sizeof(instr) - strlen(instr) - 1);
            first_instr = 0;

            Token mnemonic = advance();
            if (mnemonic.type == TOK_SEMICOLON) { first_instr = 1; continue; }
            strncat(instr, mnemonic.value, sizeof(instr) - strlen(instr) - 1);

            while (peek(0).type != TOK_SEMICOLON &&
                   !(peek(0).type == TOK_PAREN && strcmp(peek(0).value, "}") == 0) &&
                   peek(0).type != TOK_EOF &&
                   !(peek(0).type == TOK_OP && strcmp(peek(0).value, "->") == 0)) {
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
            if (peek(0).type == TOK_SEMICOLON) advance();
        }
        if (peek(0).type == TOK_PAREN && strcmp(peek(0).value, "}") == 0)
            advance(); /* } */
    } else {
        /* Одиночная инструкция */
        Token mnemonic = advance();
        if (mnemonic.type != TOK_IDENT) {
            printf("Error [line %d, col %d]: expected instruction mnemonic after '%s'\n",
                   mnemonic.line, mnemonic.col, arch_tok.value);
            exit(1);
            asmnode.type = NODE_ERROR;
            return asmnode;
        }
        strncpy(instr, mnemonic.value, sizeof(instr) - 1);

        while (peek(0).type != TOK_SEMICOLON &&
               peek(0).type != TOK_EOF &&
               peek(0).type != TOK_ERROR &&
               !(peek(0).type == TOK_OP && strcmp(peek(0).value, "==>") == 0) &&
               !(peek(0).type == TOK_OP && strcmp(peek(0).value, "->") == 0)) {
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
    }

    /* Структура хранится в str узла как:
     *   <instr>\x01<outputs_csv>\x01<clobbers_csv>\x01<inputs_csv>
     * outputs_csv:  "rax:varName" 
     * clobbers_csv: "rax,rdi,memory"
     * inputs_csv:   "rax:varName,rdi:varName2"
     */
    char clobbers_str[256] = "";
    char inputs_str[256]   = "";
    char out_v[64] = "";
    char out_var[64] = "";
    char out_reg[64] = "";

    if (peek(0).type == TOK_OP && strcmp(peek(0).value, ">") == 0) {
        advance();
        if (peek(0).type == TOK_IDENT) {
            strncpy(out_reg, advance().value, 63);

            if (strcmp(peek(0).value, "(") == 0) {
                advance();
                
                if (peek(0).type == TOK_IDENT) {
                    strncpy(out_v, advance().value, 63);
                }
                else {
                    printf("Error: variable's name expected");
                    exit(1);
                }

                if (strcmp(peek(0).value, ")") != 0) {
                    printf("Error: ')' expected in %i", peek(0).line);
                    exit(1);
                }

                advance();
            }
            else {
                printf("Error: '(' expected in %i", peek(0).line);
                exit(1);
            }

            strncat(out_var, out_reg, sizeof(out_var) - strlen(out_var) - 1);
            strncat(out_var, ":",     sizeof(out_var) - strlen(out_var) - 1);
            strncat(out_var, out_v,   sizeof(out_var) - strlen(out_var) - 1);
        }
        else {
            printf("Error: register's name expected");
            exit(1);
        }
    }

    if (peek(0).type == TOK_OP && strcmp(peek(0).value, ":") == 0) {
        advance(); /* -> */
        /* Читаем регистры-клоберы до ':' или ';' */
        while (peek(0).type != TOK_SEMICOLON &&
               peek(0).type != TOK_EOF &&
               !(peek(0).type == TOK_OP && strcmp(peek(0).value, ":") == 0)) {
            Token reg_tok = advance();
            if (clobbers_str[0]) strncat(clobbers_str, ",", sizeof(clobbers_str) - strlen(clobbers_str) - 1);
            strncat(clobbers_str, reg_tok.value, sizeof(clobbers_str) - strlen(clobbers_str) - 1);
        }
        /* Читаем inputs: : reg(varName) reg2(varName2) */
        if (peek(0).type == TOK_OP && strcmp(peek(0).value, ":") == 0) {
            advance(); /* : */
            while (peek(0).type != TOK_SEMICOLON && peek(0).type != TOK_EOF) {
                /* reg(varName) */
                Token reg_tok = advance(); /* регистр */
                if (peek(0).type != TOK_PAREN || strcmp(peek(0).value, "(") != 0) {
                    printf("Error: expected '(varName)' after register in asm inputs\n");
                    exit(1);
                }
                advance(); /* ( */
                Token var_tok = advance(); /* имя переменной */
                if (peek(0).type == TOK_PAREN && strcmp(peek(0).value, ")") == 0) advance();
                if (inputs_str[0]) strncat(inputs_str, ",", sizeof(inputs_str) - strlen(inputs_str) - 1);
                strncat(inputs_str, reg_tok.value,  sizeof(inputs_str) - strlen(inputs_str) - 1);
                strncat(inputs_str, ":",             sizeof(inputs_str) - strlen(inputs_str) - 1);
                strncat(inputs_str, var_tok.value,   sizeof(inputs_str) - strlen(inputs_str) - 1);
            }
        }
    }

    /* Кодируем всё в asmnode.str через разделители \x01 */
    /* Формат: instr \x01 out_var \x01 clobbers \x01 inputs */
    {
        snprintf(asmnode.str, 2047, "%s\x01%s\x01%s\x01%s",
                 instr, out_var, clobbers_str, inputs_str);
        asmnode.str[2047] = '\0';
    }

    /* Узел вывода (for backward compat в codegen_asm) */
    Node out_node;
    out_node.type   = NODE_IDENT;
    out_node.childs = malloc(sizeof(vector_node));
    vn_init(out_node.childs, 1);
    out_node.str = strdup(out_var[0] ? out_var : "");
    vn_push_back(asmnode.childs, out_node);

    if (peek(0).type == TOK_SEMICOLON)
        advance();
    else if (!is_block) {
        printf("Error: semicolon expected after asm\n");
        exit(1);
    }

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
        exit(1);
        assign.type = NODE_ERROR;
        return assign;
    }

    Node *ident = make_node(NODE_IDENT, t.value);
    vn_push_back(assign.childs, *ident);
    free(ident);

    if (strcmp(peek(0).value, "=") == 0)
        advance();
    else {
        printf("Error: expected '=' after '*%s'\n", t.value);
        exit(1);
    }

    Node *expr = parseExpr();
    if (expr) {
        vn_push_back(assign.childs, *expr);
        free(expr);
    }

    if (peek(0).type == TOK_SEMICOLON) advance();
    else {
        printf("Error: semicolon expected");
        exit(1);
    }

    assign.type = NODE_PTR_ASSIGN;
    return assign;
}

vector_node *parse(int it, vector_token* tokenss) {
    i = it;
    tokens = tokenss;
    var_type_count    = 0;
    known_class_count = 0;
    overload_count    = 0;

    prescan(tokenss);

    vector_node *nodes = malloc(sizeof(vector_node));
    vn_init(nodes, 4);

    while (1) {
        Node temp = parsing();
        if (temp.type == NODE_EOF || temp.type == NODE_ERROR) break;
        if (temp.type == NODE_ENUM) {
            for (unsigned long long k = 0; k < temp.childs->size; k++) {
                Node *nc = malloc(sizeof(Node));
                *nc = temp.childs->data[k];
                if (nc->type == NODE_VAR_DEF &&
                    nc->childs && nc->childs->size >= 1 &&
                    nc->childs->data[0].str)
                {
                    char *old_type = nc->childs->data[0].str;
                    char new_type[80];
                    snprintf(new_type, sizeof(new_type), "const:%s", old_type);
                    free(old_type);
                    nc->childs->data[0].str = malloc(strlen(new_type) + 1);
                    strcpy(nc->childs->data[0].str, new_type);
                }
                vn_push_back(nodes, *nc);
                free(nc);
            }
        } else {
            Node *node_copy = malloc(sizeof(Node));
            *node_copy = temp;
            vn_push_back(nodes, *node_copy);
            free(node_copy);
        }
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
        
        advance();
        Node *idx = parseExpr();
        if (idx) {
            vn_push_back(assign.childs, *idx);
            free(idx);
        }
        if (strcmp(peek(0).value, "]") == 0)
            advance();
        else {
            printf("Error: expected ']'\n");
            exit(1);
        }
        
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
                exit(1);
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

        if (strcmp(peek(0).value, "[") == 0) {
            advance();
            Node *idx = parseExpr();
            if (strcmp(peek(0).value, "]") == 0)
                advance();
            else { printf("Error: expected ']'\n"); exit(1); }

            /* chain continues: [idx].field or [idx]->field */
            if (peek(0).type == TOK_OP &&
                (strcmp(peek(0).value, ".") == 0 ||
                strcmp(peek(0).value, "->") == 0))
            {
                /* wrap into INDEX node, then continue chaining */
                Node *index_node = make_node(NODE_INDEX, "[]");
                vn_push_back(index_node->childs, *var); free(var);
                vn_push_back(index_node->childs, *idx); free(idx);
                var = index_node;

                while (peek(0).type == TOK_OP &&
                    (strcmp(peek(0).value, ".") == 0 ||
                        strcmp(peek(0).value, "->") == 0))
                {
                    Token op2 = advance();
                    Token field2 = peek(0);
                    if (field2.type != TOK_IDENT) {
                        printf("Error: expected field name\n");
                        exit(1); break;
                    }
                    advance();
                    Node *member2 = make_node(
                        strcmp(op2.value, ".") == 0 ? NODE_MEMBER_DOT : NODE_MEMBER_ARROW,
                        field2.value
                    );
                    vn_push_back(member2->childs, *var); free(var);
                    var = member2;
                }

                if (strcmp(peek(0).value, "[") == 0) {
                    advance();
                    Node *idx2 = parseExpr();
                    if (strcmp(peek(0).value, "]") == 0) advance();
                    else { printf("Error: expected ']'\n"); exit(1); }
                    vn_push_back(assign.childs, *var); free(var);
                    if (idx2) { vn_push_back(assign.childs, *idx2); free(idx2); }
                    assign.type = NODE_MEMBER_INDEX_ASSIGN;
                } else {
                    vn_push_back(assign.childs, *var); free(var);
                    Node *undef = make_node(NODE_UNDEF, "");
                    vn_push_back(assign.childs, *undef); free(undef);
                    assign.type = NODE_MEMBER_ASSIGN;
                }
            } else {
                vn_push_back(assign.childs, *var); free(var);
                if (idx) { vn_push_back(assign.childs, *idx); free(idx); }
                assign.type = NODE_MEMBER_INDEX_ASSIGN;
            }
        } else {
            vn_push_back(assign.childs, *var);
            free(var);
            Node *undef = make_node(NODE_UNDEF, "");
            vn_push_back(assign.childs, *undef);
            free(undef);
            assign.type = NODE_MEMBER_ASSIGN;
        }
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

    if (strcmp(peek(0).value, "=") == 0) {
        advance();

        if (peek(0).type == TOK_KEYWORD && strcmp(peek(0).value, "new") == 0) {
            advance();
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
            const char *class_name = NULL;
            char class_name_buf[64] = "";
            if (peek(0).type == TOK_IDENT) {
                Token cls_tok = advance();
                strncpy(class_name_buf, cls_tok.value, 63);
                class_name = class_name_buf;
            } else {
                const char *vt = var_type_lookup(t.value);
                if (vt) strncpy(class_name_buf, vt, 63);
                class_name = class_name_buf;
            }
            Node *new_node = make_node(NODE_NEW, class_name);
            Node *varname_node = make_node(NODE_IDENT, class_name);
            vn_push_back(new_node->childs, *varname_node);
            free(varname_node);
            vn_push_back(new_node->childs, *args);
            free(args);
            vn_push_back(assign.childs, *new_node);
            free(new_node);
        } else {
            Node *expr = parseExpr();
            if (expr) {
                vn_push_back(assign.childs, *expr);
                free(expr);
            }
        }
    }
    else {
        printf("Error: expected '='\n");
        exit(1);
    }

    if (peek(0).type == TOK_SEMICOLON) advance();
    else {
        printf("Error: semicolon expected");
        exit(1);
    }

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
        else {
            printf("Error: expected ';' after return value\n");
            exit(1);
        }
    }

    ret.type = NODE_RETURN;
    return ret;
}

static Node parseFuncCallInner(Token t, int is_stmt) {
    printf("DEBUG funcall: '%s' is_stmt=%d peek='%s'\n", 
           t.value, is_stmt, peek(0).value);
    Node call;
    call.childs = malloc(sizeof(vector_node));
    vn_init(call.childs, 4);
    call.str = malloc(strlen(t.value) + 1);
    strcpy(call.str, t.value);

    Node *name_node = make_node(NODE_IDENT, t.value);
    vn_push_back(call.childs, *name_node);
    free(name_node);

    Node *args = make_node(NODE_ARGS, "");
    if (strcmp(peek(0).value, "(") == 0) {
        advance();
        while (strcmp(peek(0).value, ")") != 0) {
            printf("DEBUG funcall arg: peek='%s'\n", peek(0).value);
            if (peek(0).type == TOK_EOF) break;
            Node *thing = parseExpr();
            if (thing) { vn_push_back(args->childs, *thing); free(thing); }
            if (peek(0).type == TOK_COMMA) advance();
            else break;
        }
        if (strcmp(peek(0).value, ")") == 0)
            advance();
        else {
            printf("Error: expected ')' in function call\n");
            exit(1);
        }
    }
    vn_push_back(call.childs, *args);
    free(args);

    {
        Node *args_in_call = &call.childs->data[1];
        const char *resolved = resolve_overload(t.value, args_in_call);
        if (resolved) {
            free(call.str);
            call.str = malloc(strlen(resolved) + 1);
            strcpy(call.str, resolved);
            free(call.childs->data[0].str);
            call.childs->data[0].str = malloc(strlen(resolved) + 1);
            strcpy(call.childs->data[0].str, resolved);
            printf("DEBUG funcall: resolved '%s' -> '%s'\n", t.value, resolved);
        }
    }

    if (is_stmt) {
        if (peek(0).type == TOK_SEMICOLON) advance();
        else {
            printf("Error: semicolon expected");
            exit(1);
        }
    }

    call.type = NODE_FUNC_CALL;
    return call;
}

Node parseFuncCall(Token t) {
    return parseFuncCallInner(t, 1);
}

Node parseIf(void) {
    Node ifexpr;
    ifexpr.childs = malloc(sizeof(vector_node));
    vn_init(ifexpr.childs, 4);
    ifexpr.str = NULL;

    Token current = advance();
    if (strcmp(current.value, "(") == 0) {
        Node *expr = parseExpr();
        if (expr) { vn_push_back(ifexpr.childs, *expr); free(expr); }
        current = advance();
        if (strcmp(current.value, ")") != 0) {
            printf("Error: expected ')' after if condition\n");
            exit(1);
        }
    } else {
        printf("Error: expected '(' after if\n");
        exit(1);
    }

    Node *body = parse_body();
    if (body) { vn_push_back(ifexpr.childs, *body); free(body); }

    if (peek(0).type == TOK_KEYWORD && strcmp(peek(0).value, "else") == 0) {
        advance();

        if (peek(0).type == TOK_KEYWORD && strcmp(peek(0).value, "if") == 0) {
            advance();
            Node nested_if = parseIf();
            vn_push_back(ifexpr.childs, nested_if);
        } else {
            Node *else_body = parse_body();
            if (else_body) {
                Node *else_node = make_node(NODE_ELSE, "");
                vn_push_back(else_node->childs, *else_body);
                free(else_body);
                vn_push_back(ifexpr.childs, *else_node);
                free(else_node);
            }
        }
    }

    ifexpr.type = NODE_IF;
    return ifexpr;
}

Node parseWhile(void) {
    Node whilex;
    whilex.childs = malloc(sizeof(vector_node));
    vn_init(whilex.childs, 4);
    whilex.str = NULL;

    Token current = advance();
    if (strcmp(current.value, "(") == 0) {
        Node *expr = parseExpr();
        if (expr) { vn_push_back(whilex.childs, *expr); free(expr); }
        current = advance();
        if (strcmp(current.value, ")") != 0) {
            printf("Error: expected ')' after while condition\n");
            exit(1);
        }
    } else {
        printf("Error: expected '(' after while\n");
        exit(1);
    }

    Node *body = parse_body();
    if (body) { vn_push_back(whilex.childs, *body); free(body); }

    whilex.type = NODE_WHILE;
    return whilex;
}

Node parseDoWhile(void) {
    Node dowhile;
    dowhile.childs = malloc(sizeof(vector_node));
    vn_init(dowhile.childs, 2);
    dowhile.str = NULL;

    Token t = advance();
    if (strcmp(t.value, "while") != 0) {
        printf("Error: expected 'while' after 'do'\n");
        exit(1);
        dowhile.type = NODE_ERROR;
        return dowhile;
    }
    t = advance();
    if (strcmp(t.value, "(") != 0) {
        printf("Error: expected '(' after 'while'\n");
        exit(1);
        dowhile.type = NODE_ERROR;
        return dowhile;
    }

    Node *cond = parseExpr();
    if (!cond) {
        printf("Error: expected condition in do while\n");
        exit(1);
        dowhile.type = NODE_ERROR;
        return dowhile;
    }
    vn_push_back(dowhile.childs, *cond);
    free(cond);

    t = advance();
    if (strcmp(t.value, ")") != 0) {
        printf("Error: expected ')' after do while condition\n");
        exit(1);
    }

    Node *body = parse_body();
    if (body) { vn_push_back(dowhile.childs, *body); free(body); }

    if (peek(0).type == TOK_SEMICOLON) advance();
    else {
        printf("Error: semicolon expected");
        exit(1);
    }

    dowhile.type = NODE_DO_WHILE;
    return dowhile;
}

Node parseFor(void) {
    Node fornode;
    fornode.childs = malloc(sizeof(vector_node));
    vn_init(fornode.childs, 4);
    fornode.str = NULL;

    Token t = advance();
    if (strcmp(t.value, "(") != 0) {
        printf("Error: expected '(' after 'for'\n");
        exit(1);
        fornode.type = NODE_ERROR;
        return fornode;
    }

    if (peek(0).type == TOK_SEMICOLON) {
        advance();
        Node *undef = make_node(NODE_UNDEF, "");
        vn_push_back(fornode.childs, *undef);
        free(undef);
    } else {
        if ((peek(0).type == TOK_TYPE ||
             (peek(0).type == TOK_IDENT && class_lookup(peek(0).value))) &&
            peek(1).type == TOK_IDENT)
        {
            Token type_tok = advance();
            Node init = parseVarDef(type_tok, 1);
            vn_push_back(fornode.childs, init);
        } else {
            Node *init = parseExpr();
            if (init) { vn_push_back(fornode.childs, *init); free(init); }
            if (peek(0).type == TOK_SEMICOLON) advance();
        }
    }

    if (peek(0).type == TOK_SEMICOLON) {
        advance();
        Node *one = make_node(NODE_I32, "1");
        vn_push_back(fornode.childs, *one);
        free(one);
    } else {
        Node *cond = parseExpr();
        if (cond) { vn_push_back(fornode.childs, *cond); free(cond); }
        if (peek(0).type == TOK_SEMICOLON) advance();
    }

    if (strcmp(peek(0).value, ")") == 0) {
        Node *undef = make_node(NODE_UNDEF, "");
        vn_push_back(fornode.childs, *undef);
        free(undef);
    } else {
        Token step_tok = advance();
        Node step;
        if (step_tok.type == TOK_IDENT) {
            if (strcmp(peek(0).value, "++") == 0 || strcmp(peek(0).value, "--") == 0 ||
                strcmp(peek(0).value, "+=") == 0 || strcmp(peek(0).value, "-=") == 0 ||
                strcmp(peek(0).value, "*=") == 0 || strcmp(peek(0).value, "/=") == 0 ||
                strcmp(peek(0).value, "%=") == 0)
                step = parseCompoundAssign(step_tok, 0);
            else if (strcmp(peek(0).value, "=") == 0)
                step = parseAssign(step_tok);
            else {
                printf("Error: expected step expression in for\n");
                exit(1);
                step.type = NODE_ERROR; step.childs = NULL; step.str = NULL;
            }
        } else {
            printf("Error: expected identifier in for step\n");
            exit(1);
            step.type = NODE_ERROR; step.childs = NULL; step.str = NULL;
        }
        if (step.type == NODE_ERROR) { fornode.type = NODE_ERROR; return fornode; }
        vn_push_back(fornode.childs, step);
    }

    t = advance();
    if (strcmp(t.value, ")") != 0) {
        printf("Error: expected ')' after for header\n");
        exit(1);
    }

    Node *body = parse_body();
    if (body) { vn_push_back(fornode.childs, *body); free(body); }

    fornode.type = NODE_FOR;
    return fornode;
}

static Node parseFuncDefInner(Token t, int do_mangle) {
    Node func;
    func.childs = malloc(sizeof(vector_node));
    vn_init(func.childs, 4);
    func.str = NULL;

    Token current = t;
    if (current.type == TOK_TYPE || current.type == TOK_IDENT) {
        char ret_str[80];
        strncpy(ret_str, current.value, 79);
        collect_stars(ret_str, 80);
        Node *type = make_node(NODE_TYPE, ret_str);
        vn_push_back(func.childs, *type);
        free(type);
    } else {
        printf("Error: unknown function return type\n");
        exit(1);
    }

    current = advance();
    if (current.type == TOK_IDENT) {
        Node *ident = make_node(NODE_IDENT, current.value);
        vn_push_back(func.childs, *ident);
        free(ident);
    } else {
        printf("Error: expected function name\n");
        exit(1);
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
        if (strcmp(current.value, ")") != 0) {
            printf("Error: expected ')' after params\n");
            exit(1);
        }
    } else {
        printf("Error: expected '(' in function def\n");
        exit(1);
    }

    if (do_mangle) {
        Node *name_node   = &func.childs->data[1];
        Node *params_node = &func.childs->data[2];
        const char *base  = name_node->str;

        char param_types[MAX_OVERLOAD_PARAMS][64];
        int  param_count = 0;
        if (params_node->type == NODE_PARAMS) {
            for (unsigned long long pi = 0;
                 pi < params_node->childs->size && param_count < MAX_OVERLOAD_PARAMS;
                 pi++)
            {
                Node *param = &params_node->childs->data[pi];
                if (param->childs && param->childs->size >= 1)
                    strncpy(param_types[param_count++],
                            param->childs->data[0].str, 63);
            }
        }

        char mangled[128];
        if (strcmp(base, "main") == 0)
            strncpy(mangled, "main", sizeof(mangled));
        else
            build_mangled_name(base, (const char (*)[64])param_types,
                               param_count, mangled, sizeof(mangled));

        overload_push(base, mangled,
                      (const char (*)[64])param_types, param_count,
                      func.childs->data[0].str);

        free(name_node->str);
        name_node->str = malloc(strlen(mangled) + 1);
        strcpy(name_node->str, mangled);

        printf("DEBUG parser: func '%s' -> mangled '%s' (%d params)\n",
               base, mangled, param_count);
    }

    current = advance();
    if (current.type == TOK_SEMICOLON) {
        func.type = NODE_EXTERN_FUNC_DEF;
        return func;
    }
    if (strcmp(current.value, "{") == 0) {
        Node *scope = make_node(NODE_SCOPE, "");
        while (strcmp(peek(0).value, "}") != 0) {
            if (peek(0).type == TOK_EOF) {
                printf("Error: unexpected EOF in function body\n");
                exit(1);
                break;
            }
            Node temp = parsing();
            if (temp.type != NODE_ERROR) {
                Node *temp_copy = malloc(sizeof(Node));
                *temp_copy = temp;
                vn_push_back(scope->childs, *temp_copy);
                free(temp_copy);
            } else {
                printf("Error in function scope\n");
                exit(1);
            }
        }
        advance();
        vn_push_back(func.childs, *scope);
        free(scope);
    } else {
        printf("Error: expected '{' in function def\n");
        exit(1);
    }

    func.type = NODE_FUNC_DEF;
    return func;
}

Node parseFuncDef(Token t) {
    return parseFuncDefInner(t, 1);
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
        param.str  = NULL;

        char type_buf[128] = "";
        int is_ftype = parse_type_token(type_buf, sizeof(type_buf));

        Node *type = make_node(NODE_TYPE, type_buf);
        vn_push_back(param.childs, *type);
        free(type);

        if (!is_ftype) {
            Token name_tok = advance();
            if (name_tok.type == TOK_IDENT) {
                Node *ident = make_node(NODE_IDENT, name_tok.value);
                vn_push_back(param.childs, *ident);
                free(ident);
            }
        } else {
            Node *ident = make_node(NODE_IDENT, "_f_param");
            vn_push_back(param.childs, *ident);
            free(ident);
        }

        vn_push_back(params.childs, param);
        if (peek(0).type == TOK_COMMA) advance();
        else break;
    }

    params.type = NODE_PARAMS;
    return params;
}

Node parseVarDef(Token t, int m) {
    Node var;
    var.childs = malloc(sizeof(vector_node));
    vn_init(var.childs, 4);
    var.str = NULL;

    int is_autodel = m;

    /* --- Handle F<Ret>(Args) type --- */
    if (strcmp(t.value, "F") == 0 &&
        peek(0).type == TOK_OP && strcmp(peek(0).value, "<") == 0)
    {
        /* Re-use parse_type_token to read the full F<> type string.
           But parse_type_token calls advance() internally, so we must
           push back 'F' by decrementing i first. */
        i--; /* put 'F' back */
        char ftype_buf[128] = "";
        parse_type_token(ftype_buf, sizeof(ftype_buf));
        /* ftype_buf is now e.g. "F<int>(int,int)" */

        /* Next token: variable name */
        Token name_tok = advance();
        if (name_tok.type != TOK_IDENT) {
            printf("Error: expected variable name after F<> type\n");
            exit(1);
            var.type = NODE_ERROR;
            return var;
        }

        Node *type_node = make_node(NODE_TYPE, ftype_buf);
        vn_push_back(var.childs, *type_node);
        free(type_node);

        Node *ident_node = make_node(NODE_IDENT, name_tok.value);
        vn_push_back(var.childs, *ident_node);
        free(ident_node);

        var_type_push(name_tok.value, ftype_buf);

        /* Array of function pointers: F<int>() callbacks[N] */
        if (strcmp(peek(0).value, "[") == 0) {
            advance();
            Node *size_expr = parseExpr();
            if (!size_expr) {
                printf("Error: expected array size\n");
                exit(1);
            }
            vn_push_back(var.childs, *size_expr);
            free(size_expr);
            if (strcmp(peek(0).value, "]") == 0) advance();
            else { printf("Error: expected ']'\n"); exit(1); }
            Node *undef = make_node(NODE_UNDEF, "");
            vn_push_back(var.childs, *undef);
            free(undef);
            if (peek(0).type == TOK_SEMICOLON) advance();
            var.type = NODE_ARRAY_DEF;
            return var;
        }

        /* Optional initialiser: = someFunc */
        if (strcmp(peek(0).value, "=") == 0) {
            advance();
            Node *rhs = parseExpr();
            if (rhs) { vn_push_back(var.childs, *rhs); free(rhs); }
        } else {
            Node *undef = make_node(NODE_UNDEF, "");
            vn_push_back(var.childs, *undef);
            free(undef);
        }

        if (peek(0).type == TOK_SEMICOLON) advance();
        var.type = NODE_VAR_DEF;
        return var;
    }

    /* --- Normal variable definition --- */
    Token current = t;

    char base_type[64];
    base_type[0] = '\0';

    if (current.type == TOK_TYPE || current.type == TOK_IDENT) {
        char stars[16] = "";
        {
            int sc = 0;
            while (peek(sc).type == TOK_OP && strcmp(peek(sc).value, "*") == 0) sc++;
            for (int si = 0; si < sc; si++) { advance(); strncat(stars, "*", sizeof(stars)-1); }
        }
        char ptr_type_str[96];
        if (is_autodel && m == 1)
            snprintf(ptr_type_str, sizeof(ptr_type_str), "autodel:%s%s", current.value, stars);
        else
            snprintf(ptr_type_str, sizeof(ptr_type_str), "%s%s", current.value, stars);
        Node *type = make_node(NODE_TYPE, ptr_type_str);
        vn_push_back(var.childs, *type);
        free(type);
        strncpy(base_type, current.value, 63);
    } else {
        printf("Error: unknown variable type '%s'\n", current.value);
        exit(1);
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
        printf("Error: expected identifier, got '%s'\n", current.value);
        exit(1);
    }

    var_type_push(var_name, base_type);

    if (strcmp(peek(0).value, "[") == 0) {
        advance();
        Node *size_expr = parseExpr();
        if (!size_expr) {
            printf("Error: expected array size expression\n");
            exit(1);
        }
        vn_push_back(var.childs, *size_expr);
        free(size_expr);
        if (strcmp(peek(0).value, "]") == 0)
            advance();
        else {
            printf("Error: expected ']'\n");
            exit(1);
        }
        Node *undef = make_node(NODE_UNDEF, "");
        vn_push_back(var.childs, *undef);
        free(undef);
        if (peek(0).type == TOK_SEMICOLON) advance();
        var.type = NODE_ARRAY_DEF;
        return var;
    }

    printf("Type assign: %i, got '%s' in line %i\n", peek(0).type, peek(0).value, peek(0).line);

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
        exit(1);
        str.type = NODE_ERROR;
        return str;
    }

    class_push(name.value);
    
    str.str = malloc(strlen(name.value) + 1);
    strcpy(str.str, name.value);

    Token brace = advance();
    if (strcmp(brace.value, "{") != 0) {
        printf("Error: expected '{' after struct name\n");
        exit(1);
        str.type = NODE_ERROR;
        return str;
    }

    while (strcmp(peek(0).value, "}") != 0) {
        if (peek(0).type == TOK_EOF) {
            printf("Error: unexpected EOF in struct body\n");
            exit(1);
            break;
        }

        if (peek(0).type != TOK_TYPE &&
            !(peek(0).type == TOK_IDENT && class_lookup(peek(0).value))) {
            printf("Error: expected type in struct body\n");
            exit(1);
            str.type = NODE_ERROR;
            return str;
        }

        Token field_type = advance();
        Node field = parseVarDef(field_type, 0);
        if (field.type == NODE_ERROR) {
            printf("Error in struct field\n");
            exit(1);
            str.type = NODE_ERROR;
            return str;
        }
        vn_push_back(str.childs, field);
    }

    advance();
    if (peek(0).type == TOK_SEMICOLON) advance();

    str.type = NODE_STRUCT_DEF;
    return str;
}

static Node make_field_delete_node(const char *field_name, const char *field_type)
{
    (void)field_type;

    Node *del = make_node(NODE_DELETE, "");

    Node *args = make_node(NODE_ARGS, "");
    vn_push_back(del->childs, *args); free(args);

    Node *self_var = make_node(NODE_VAR, "self");
    Node *member   = make_node(NODE_MEMBER_ARROW, field_name);
    vn_push_back(member->childs, *self_var); free(self_var);

    vn_push_back(del->childs, *member); free(member);

    Node result = *del;
    free(del);
    return result;
}

Node parseClass(void) {
    Node cls;
    cls.childs = malloc(sizeof(vector_node));
    vn_init(cls.childs, 8);
    cls.str = NULL;

    Token name = advance();
    if (name.type != TOK_IDENT) {
        printf("Error: expected class name\n");
        exit(1);
        cls.type = NODE_ERROR;
        return cls;
    }

    cls.str = malloc(strlen(name.value) + 1);
    strcpy(cls.str, name.value);
    class_push(name.value);

    char parent_name[64] = "";
    if (peek(0).type == TOK_OP && strcmp(peek(0).value, "<") == 0) {
        advance();
        Token parent = advance();
        if (parent.type != TOK_IDENT) {
            printf("Error: expected parent class name\n");
            exit(1);
            cls.type = NODE_ERROR;
            return cls;
        }
        strncpy(parent_name, parent.value, 63);
    }
    if (parent_name[0] != '\0') {
        char full_name[128];
        snprintf(full_name, sizeof(full_name), "%s<%s", name.value, parent_name);
        free(cls.str);
        cls.str = malloc(strlen(full_name) + 1);
        strcpy(cls.str, full_name);
    }

    Token brace = advance();
    printf("%i\n", peek(0).type);
    if (brace.type == TOK_SEMICOLON) {
        cls.type = NODE_STRUCT_DEF;
        return cls;
    }
    if (strcmp(brace.value, "{") != 0) {
        printf("Error: expected '{' after class name\n");
        exit(1);
        cls.type = NODE_ERROR;
        return cls;
    }
    
    char autodel_fields[32][64];
    char autodel_field_types[32][64];
    int  autodel_field_count = 0;
    
    int has_explicit_dtor = 0;

    while (strcmp(peek(0).value, "}") != 0) {
        if (peek(0).type == TOK_EOF) {
            printf("Error: unexpected EOF in class body\n");
            exit(1);
            break;
        }

        int is_static = 0;
        if (peek(0).type == TOK_KEYWORD && strcmp(peek(0).value, "static") == 0) {
            is_static = 1;
            advance();
        }

        if (peek(0).type == TOK_TYPE ||
            (peek(0).type == TOK_IDENT && class_lookup(peek(0).value)))
        {
            int ptr_off = 0;
            while (peek(ptr_off + 1).type == TOK_OP && strcmp(peek(ptr_off + 1).value, "*") == 0)
                ptr_off++;
            Token after_name = peek(ptr_off + 2);
            Token type_tok = advance();

            if (strcmp(after_name.value, "(") == 0) {
                var_type_push("self", name.value);
                Node method = parseFuncDefInner(type_tok, 0);
                if (method.type == NODE_ERROR) {
                    printf("Error in class method\n");
                    exit(1);
                    cls.type = NODE_ERROR;
                    return cls;
                }

                Node *method_name_node = &method.childs->data[1];
                Node *method_params    = &method.childs->data[2];

                char raw_name[64];
                strncpy(raw_name, method_name_node->str, 63);

                char base_method[128];
                snprintf(base_method, sizeof(base_method), "%s_%s",
                         name.value, raw_name);

                char param_types[MAX_OVERLOAD_PARAMS][64];
                int  param_count = 0;
                if (!is_static) {
                    char self_type[68];
                    snprintf(self_type, sizeof(self_type), "%s*", name.value);
                    strncpy(param_types[param_count++], self_type, 63);
                }
                if (method_params->type == NODE_PARAMS) {
                    for (unsigned long long pi = 0;
                         pi < method_params->childs->size && param_count < MAX_OVERLOAD_PARAMS;
                         pi++)
                    {
                        Node *param = &method_params->childs->data[pi];
                        if (param->childs && param->childs->size >= 1)
                            strncpy(param_types[param_count++],
                                    param->childs->data[0].str, 63);
                    }
                }

                char mangled_method[128];
                build_mangled_name(base_method,
                                   (const char (*)[64])param_types, param_count,
                                   mangled_method, sizeof(mangled_method));

                overload_push(base_method, mangled_method,
                              (const char (*)[64])param_types, param_count,
                              method.childs->data[0].str);

                if (is_static)
                    overload_set_static(mangled_method);

                free(method_name_node->str);
                method_name_node->str = malloc(strlen(mangled_method) + 1);
                strcpy(method_name_node->str, mangled_method);

                printf("DEBUG parser: method '%s' -> mangled '%s' static=%d\n",
                       base_method, mangled_method, is_static);

                if (!is_static) {
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

                    method.type = NODE_FUNC_DEF;
                } else {
                    method.type = NODE_STATIC_FUNC_DEF;
                }

                vn_push_back(cls.childs, method);
            } else {
                Node field = parseVarDef(type_tok, 1);
                if (field.type == NODE_ERROR) {
                    printf("Error in class field\n");
                    exit(1);
                    cls.type = NODE_ERROR;
                    return cls;
                }
                if (is_static)
                    field.type = NODE_STATIC_VAR_DEF;

                if (!is_static &&
                    field.childs && field.childs->size >= 2 &&
                    field.childs->data[0].type == NODE_TYPE &&
                    field.childs->data[1].type == NODE_IDENT)
                {
                    const char *ftype = field.childs->data[0].str;
                    const char *fname = field.childs->data[1].str;
                    if (strncmp(ftype, "autodel:", 8) == 0) {
                        const char *inner = ftype + 8;
                        if (inner[strlen(inner) - 1] == '*') {
                            if (autodel_field_count < 32) {
                                strncpy(autodel_fields[autodel_field_count], fname, 63);
                                strncpy(autodel_field_types[autodel_field_count], inner, 63);
                                autodel_field_count++;
                                printf("DEBUG parseClass: autodel field '%s' of type '%s'\n",
                                       fname, ftype);
                            }
                        }
                    }
                }

                vn_push_back(cls.childs, field);
            }
        }
        else if (peek(0).type == TOK_KEYWORD && strcmp(peek(0).value, "new") == 0) {
            advance();

            Node method;
            method.childs = malloc(sizeof(vector_node));
            vn_init(method.childs, 4);
            method.str = NULL;

            Node *ret_type = make_node(NODE_TYPE, "void");
            vn_push_back(method.childs, *ret_type);
            free(ret_type);

            char ctor_name[128];
            snprintf(ctor_name, sizeof(ctor_name), "%s_new", name.value);
            Node *fname = make_node(NODE_IDENT, ctor_name);
            vn_push_back(method.childs, *fname);
            free(fname);

            Node *params_node = make_node(NODE_PARAMS, "");

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

            vn_push_back(params_node->childs, self_param);

            Token tok = advance();
            if (strcmp(tok.value, "(") != 0) {
                printf("Error: expected '(' after 'new'\n");
                exit(1);
                cls.type = NODE_ERROR;
                return cls;
            }
            if (strcmp(peek(0).value, ")") != 0) {
                Node user_params = parseParams();
                for (unsigned long long k = 0; k < user_params.childs->size; k++)
                    vn_push_back(params_node->childs, user_params.childs->data[k]);
            }
            tok = advance();
            if (strcmp(tok.value, ")") != 0) {
                printf("Error: expected ')' after constructor params\n");
                exit(1);
            }

            vn_push_back(method.childs, *params_node);
            free(params_node);

            {
                Node *nm   = &method.childs->data[1];
                Node *prms = &method.childs->data[2];
                char param_types[MAX_OVERLOAD_PARAMS][64];
                int  param_count = 0;
                for (unsigned long long pi = 0;
                     pi < prms->childs->size && param_count < MAX_OVERLOAD_PARAMS; pi++)
                {
                    Node *p = &prms->childs->data[pi];
                    if (p->childs && p->childs->size >= 1)
                        strncpy(param_types[param_count++], p->childs->data[0].str, 63);
                }
                char mangled_ctor[128];
                build_mangled_name(ctor_name,
                                   (const char (*)[64])param_types, param_count,
                                   mangled_ctor, sizeof(mangled_ctor));
                overload_push(ctor_name, mangled_ctor,
                              (const char (*)[64])param_types, param_count, "void");
                free(nm->str);
                nm->str = malloc(strlen(mangled_ctor) + 1);
                strcpy(nm->str, mangled_ctor);
                printf("DEBUG parser: ctor '%s' -> mangled '%s'\n", ctor_name, mangled_ctor);
            }

            var_type_push("self", name.value);
            Node *body = parse_body();
            if (body) {
                vn_push_back(method.childs, *body);
                free(body);
            }

            method.type = NODE_FUNC_DEF;
            vn_push_back(cls.childs, method);
        }
        else if (peek(0).type == TOK_KEYWORD && strcmp(peek(0).value, "delete") == 0) {
            has_explicit_dtor = 1;
            advance();

            Node method;
            method.childs = malloc(sizeof(vector_node));
            vn_init(method.childs, 4);
            method.str = NULL;

            Node *ret_type = make_node(NODE_TYPE, "void");
            vn_push_back(method.childs, *ret_type);
            free(ret_type);

            char dtor_name[128];
            snprintf(dtor_name, sizeof(dtor_name), "%s_delete", name.value);
            Node *fname = make_node(NODE_IDENT, dtor_name);
            vn_push_back(method.childs, *fname);
            free(fname);

            Node *params_node = make_node(NODE_PARAMS, "");

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

            vn_push_back(params_node->childs, self_param);

            Token tok = advance();
            if (strcmp(tok.value, "(") != 0) {
                printf("Error: expected '(' after 'delete'\n");
                exit(1);
                cls.type = NODE_ERROR;
                return cls;
            }
            if (strcmp(peek(0).value, ")") != 0) {
                Node user_params = parseParams();
                for (unsigned long long k = 0; k < user_params.childs->size; k++)
                    vn_push_back(params_node->childs, user_params.childs->data[k]);
            }
            tok = advance();
            if (strcmp(tok.value, ")") != 0) {
                printf("Error: expected ')' after destructor params\n");
                exit(1);
            }

            vn_push_back(method.childs, *params_node);
            free(params_node);

            {
                Node *nm   = &method.childs->data[1];
                Node *prms = &method.childs->data[2];
                char param_types[MAX_OVERLOAD_PARAMS][64];
                int  param_count = 0;
                for (unsigned long long pi = 0;
                     pi < prms->childs->size && param_count < MAX_OVERLOAD_PARAMS; pi++)
                {
                    Node *p = &prms->childs->data[pi];
                    if (p->childs && p->childs->size >= 1)
                        strncpy(param_types[param_count++], p->childs->data[0].str, 63);
                }
                char mangled_dtor[128];
                build_mangled_name(dtor_name,
                                   (const char (*)[64])param_types, param_count,
                                   mangled_dtor, sizeof(mangled_dtor));
                overload_push(dtor_name, mangled_dtor,
                              (const char (*)[64])param_types, param_count, "void");
                free(nm->str);
                nm->str = malloc(strlen(mangled_dtor) + 1);
                strcpy(nm->str, mangled_dtor);
                printf("DEBUG parser: dtor '%s' -> mangled '%s'\n", dtor_name, mangled_dtor);
            }

            var_type_push("self", name.value);
            Node *body = parse_body();
            if (body) {
                for (int fi = 0; fi < autodel_field_count; fi++) {
                    Node fdel = make_field_delete_node(autodel_fields[fi],
                                                       autodel_field_types[fi]);
                    Node *fdel_copy = malloc(sizeof(Node));
                    *fdel_copy = fdel;
                    vn_push_back(body->childs, *fdel_copy);
                    free(fdel_copy);
                    printf("DEBUG parseClass: auto-delete field '%s' appended to explicit dtor\n",
                           autodel_fields[fi]);
                }
                vn_push_back(method.childs, *body);
                free(body);
            }

            method.type = NODE_FUNC_DEF;
            vn_push_back(cls.childs, method);
        }
        else {
            printf("Error: expected type, 'new', 'delete', or 'static' in class body, got '%s'\n",
                   peek(0).value);
            exit(1);
            cls.type = NODE_ERROR;
            return cls;
        }
    }

    advance();

    if (!has_explicit_dtor && autodel_field_count > 0) {
        printf("DEBUG parseClass: generating auto-dtor for '%s' (%d fields)\n",
               name.value, autodel_field_count);

        Node method;
        method.childs = malloc(sizeof(vector_node));
        vn_init(method.childs, 4);
        method.str = NULL;

        Node *ret_type = make_node(NODE_TYPE, "void");
        vn_push_back(method.childs, *ret_type);
        free(ret_type);

        char dtor_base[128];
        snprintf(dtor_base, sizeof(dtor_base), "%s_delete", name.value);
        Node *fname = make_node(NODE_IDENT, dtor_base);
        vn_push_back(method.childs, *fname);
        free(fname);

        Node *params_node = make_node(NODE_PARAMS, "");

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

        vn_push_back(params_node->childs, self_param);
        vn_push_back(method.childs, *params_node);
        free(params_node);

        Node *scope = make_node(NODE_SCOPE, "");
        for (int fi = 0; fi < autodel_field_count; fi++) {
            Node fdel = make_field_delete_node(autodel_fields[fi],
                                               autodel_field_types[fi]);
            Node *fdel_copy = malloc(sizeof(Node));
            *fdel_copy = fdel;
            vn_push_back(scope->childs, *fdel_copy);
            free(fdel_copy);
            printf("DEBUG parseClass: auto-delete field '%s'\n", autodel_fields[fi]);
        }
        vn_push_back(method.childs, *scope);
        free(scope);

        method.type = NODE_FUNC_DEF;

        char param_types[MAX_OVERLOAD_PARAMS][64];
        int  param_count = 0;
        strncpy(param_types[param_count++], self_type, 63);
        char mangled_dtor[128];
        build_mangled_name(dtor_base,
                           (const char (*)[64])param_types, param_count,
                           mangled_dtor, sizeof(mangled_dtor));
        overload_push(dtor_base, mangled_dtor,
                      (const char (*)[64])param_types, param_count, "void");

        Node *dtor_name_node = &method.childs->data[1];
        free(dtor_name_node->str);
        dtor_name_node->str = malloc(strlen(mangled_dtor) + 1);
        strcpy(dtor_name_node->str, mangled_dtor);

        printf("DEBUG parseClass: auto-dtor '%s' -> '%s'\n", dtor_base, mangled_dtor);

        vn_push_back(cls.childs, method);
    }

    if (peek(0).type == TOK_SEMICOLON) advance();

    cls.type = NODE_STRUCT_DEF;
    return cls;
}

static Node *expr_primary(void) {
    Token t = peek(0);

    if (t.type == TOK_KEYWORD && strcmp(t.value, "new") == 0) {
        advance();

        Token paren = advance();
        if (strcmp(paren.value, "(") != 0) {
            printf("Error: expected '(' after 'new'\n");
            exit(1);
            return NULL;
        }

        Node *args = make_node(NODE_ARGS, "");
        while (strcmp(peek(0).value, ")") != 0) {
            if (peek(0).type == TOK_EOF) break;
            Node *arg = parseExpr();
            if (arg) { vn_push_back(args->childs, *arg); free(arg); }
            if (peek(0).type == TOK_COMMA) advance();
            else break;
        }
        Token close = advance();
        if (strcmp(close.value, ")") != 0) {
            printf("Error: expected ')' in new expression\n");
            exit(1);
            return NULL;
        }

        Token cls_tok = advance();
        if (cls_tok.type != TOK_IDENT && cls_tok.type != TOK_TYPE) {
            printf("Error: expected class name after 'new(...)'\n");
            exit(1);
            return NULL;
        }

        Node *new_node = make_node(NODE_NEW, cls_tok.value);
        Node *varname  = make_node(NODE_IDENT, cls_tok.value);
        vn_push_back(new_node->childs, *varname); free(varname);
        vn_push_back(new_node->childs, *args);    free(args);

        return new_node;
    }

    if (strcmp(t.value, "sizeof") == 0) {
        advance();
        Token type_tok = advance();
        if (type_tok.type != TOK_TYPE && type_tok.type != TOK_IDENT) {
            printf("Error: expected type after 'sizeof'\n");
            exit(1);
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
            exit(1);
            return NULL;
        }

        if (strcmp(peek(0).value, "[") == 0) {
            advance();
            Node *idx = expr_or();
            if (strcmp(peek(0).value, "]") == 0)
                advance();
            else {
                printf("Error: expected ']'\n");
                exit(1);
            }
            Node *node = make_node(NODE_ADDR_INDEX, var_tok.value);
            vn_push_back(node->childs, *idx);
            free(idx);
            return node;
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

        if (len > 1 && (t.value[len-1] == 'c' || t.value[len-1] == 'C')) {
            char buf[64]; strncpy(buf, t.value, len-1); buf[len-1] = '\0';
            return make_node(NODE_I8, buf);
        }
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

        /* Method call: obj.method(...) or obj->method(...) or Cls.method(...) */
        if ((strcmp(peek(0).value, ".") == 0 || strcmp(peek(0).value, "->") == 0) &&
            peek(1).type == TOK_IDENT &&
            strcmp(peek(2).value, "(") == 0)
        {
            Token op    = advance(); /* . or -> */
            Token mname = advance(); /* method name */

            /* Resolve class: could be a variable or a class name directly */
            const char *cls = var_type_lookup(t.value);
            if (!cls) {
                if (class_lookup(t.value)) {
                    cls = t.value;
                } else {
                    printf("Error: unknown type for variable '%s'\n", t.value);
                    exit(1);
                    return NULL;
                }
            }

            char full_name[128];
            snprintf(full_name, sizeof(full_name), "%s_%s", cls, mname.value);

            /* Collect user-supplied arguments */
            Node user_args;
            user_args.childs = malloc(sizeof(vector_node));
            vn_init(user_args.childs, 8);

            if (strcmp(peek(0).value, "(") == 0) {
                advance();
                while (strcmp(peek(0).value, ")") != 0) {
                    if (peek(0).type == TOK_EOF) break;
                    Node *arg = parseExpr();
                    if (arg) { vn_push_back(user_args.childs, *arg); free(arg); }
                    if (peek(0).type == TOK_COMMA) advance();
                    else break;
                }
                if (strcmp(peek(0).value, ")") == 0) advance();
            }

            /* Determine static vs non-static and resolve overload */
            int method_is_static = 0;
            const char *final_name = resolve_method(full_name, &user_args,
                                                     t.value, op.value,
                                                     &method_is_static);
            if (!final_name) final_name = full_name;

            /* Build the call node */
            Node *call_node  = make_node(NODE_FUNC_CALL, final_name);
            Node *name_child = make_node(NODE_IDENT, final_name);
            vn_push_back(call_node->childs, *name_child); free(name_child);

            Node *args_wrapper = make_node(NODE_ARGS, "");

            if (!method_is_static) {
                /* Prepend self argument */
                Node *self_arg = (strcmp(op.value, ".") == 0)
                    ? make_node(NODE_ADDR, t.value)
                    : make_node(NODE_VAR,  t.value);
                vn_push_back(args_wrapper->childs, *self_arg);
                free(self_arg);
            }

            for (unsigned long long k = 0; k < user_args.childs->size; k++)
                vn_push_back(args_wrapper->childs, user_args.childs->data[k]);

            vn_push_back(call_node->childs, *args_wrapper); free(args_wrapper);
            vn_free(user_args.childs); free(user_args.childs);

            return call_node;
        }
        
        /* Regular function call */
        if (strcmp(peek(0).value, "(") == 0) {
            /* Check if this ident is a known F<> variable — indirect call */
            const char *vtype = var_type_lookup(t.value);
            if (vtype && is_func_type(vtype)) {
                /* Indirect call through function-pointer variable */
                Node *call_node = make_node(NODE_FUNC_CALL, t.value);
                Node *name_child = make_node(NODE_IDENT, t.value);
                vn_push_back(call_node->childs, *name_child); free(name_child);

                Node *args_node = make_node(NODE_ARGS, "");
                advance(); /* ( */
                while (strcmp(peek(0).value, ")") != 0) {
                    if (peek(0).type == TOK_EOF) break;
                    Node *arg = parseExpr();
                    if (arg) { vn_push_back(args_node->childs, *arg); free(arg); }
                    if (peek(0).type == TOK_COMMA) advance();
                    else break;
                }
                if (strcmp(peek(0).value, ")") == 0) advance();
                vn_push_back(call_node->childs, *args_node); free(args_node);
                return call_node;
            }

            Node call = parseFuncCallInner(t, 0);
            Node *n = malloc(sizeof(Node));
            *n = call;
            return n;
        }
        
        /* Array index */
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
            else {
                printf("Error: expected ']'\n");
                exit(1);
            }
            return node;
        }
        
        return make_node(NODE_VAR, t.value);
    }

    if (t.type == TOK_TYPE &&
        strcmp(t.value, "T")    != 0 &&
        strcmp(t.value, "F")    != 0 &&
        strcmp(t.value, "void") != 0)
    {
        advance();
        return make_node(NODE_TYPE_LITERAL, t.value);
    }

    if (strcmp(t.value, "(") == 0) {
        advance();
        Node *inner = expr_or();
        if (strcmp(peek(0).value, ")") == 0)
            advance();
        else {
            printf("Error: expected ')'\n");
            exit(1);
        }
        return inner;
    }

    printf("Error: unexpected token in expression, got '%s'\n", t.value);
    exit(1);
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
    return expr_or();
}

static Node *expr_postfix(void) {
    Node *left = expr_unary();
    if (!left) return NULL;

    while (1) {
        if (peek(0).type == TOK_OP &&
            (strcmp(peek(0).value, ".") == 0 || strcmp(peek(0).value, "->") == 0))
        {
            Token op = advance();
            Token field = peek(0);
            if (field.type != TOK_IDENT) break;
            advance();
            Node *node = make_node(
                strcmp(op.value, ".") == 0 ? NODE_MEMBER_DOT : NODE_MEMBER_ARROW,
                field.value
            );
            vn_push_back(node->childs, *left);
            free(left);
            left = node;
        }
        else if (strcmp(peek(0).value, "[") == 0) {
            advance();
            Node *idx = expr_or();
            if (strcmp(peek(0).value, "]") == 0)
                advance();
            else {
                printf("Error: expected ']'\n");
                exit(1);
            }
            Node *node = make_node(NODE_INDEX, "[]");
            vn_push_back(node->childs, *left);
            vn_push_back(node->childs, *idx);
            free(left); free(idx);
            left = node;
        }
        else break;
    }

    return left;
}

static Node *expr_term(void) {
    Node *left = expr_postfix();
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