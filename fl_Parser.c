#include "fl_Parser.h"
#include "fl_Preproc.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define MAX_OVERLOADS 128

static int i;
static char *g_flags;
static vector_token *tokens;

extern OverloadEntry* global_modules_overloads[];
extern int global_modules_counts[];
extern int num_imported_modules;

static Token peek(int padding) {
    if (i + padding >= 0 && (unsigned)(i + padding) < tokens->size)
        return tokens->data[i + padding];
    error("Error: token index out of bounds\n");
    return tokens->data[0];
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
    {  NULL,       NULL    }
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

static OverloadEntry *overload_table;
static int           overload_count = 0;

static void overload_push(const char *base, const char *mangled,
                           const char param_types[][64], int param_count,
                           const char *ret_type)
{
    for (int i = 0; i < overload_count; i++) {
        if (strcmp(overload_table[i].mangled, mangled) == 0) return;
    }
    if (overload_count >= MAX_OVERLOADS) {
        error("Error: overload table overflow\n");
    }
    OverloadEntry *e = &overload_table[overload_count++];
    strncpy(e->base_name, base,    63);
    strncpy(e->mangled,   mangled, 127);
    strncpy(e->ret_type,  ret_type ? ret_type : "void", 63);
    e->param_count = param_count < MAX_OVERLOAD_PARAMS
                   ? param_count : MAX_OVERLOAD_PARAMS;
    for (int i = 0; i < e->param_count; i++) {
        strncpy(e->param_types[i], param_types[i], 63);
        
        const char *t = param_types[i];
        int tlen = (int)strlen(t);
        int has_ptr = (strchr(t, '*') != NULL);
        if (has_ptr && tlen > 0) {
            char last = t[tlen - 1];
            if (last == 'a' || last == 'b' || last == 'c')
                e->param_lifetimes[i] = last;
            else
                e->param_lifetimes[i] = 'c';
        } else {
            e->param_lifetimes[i] = '\0';
        }
    }
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

    for (int i = 0; i < param_count && pos < out_size - 1; i++) {
        out[pos++] = '_';
        const char *t = param_types[i];
        if (strncmp(t, "autodel:", 8) == 0) t += 8;

        
        int stars = 0;
        for (const char *q = t; *q; q++)
            if (*q == '*') stars++;

        
        int tlen = (int)strlen(t);
        char lt = 'c';
        int has_explicit_lt = 0;
        if (stars > 0 && tlen > 0) {
            char last = t[tlen - 1];
            if (last == 'a' || last == 'b' || last == 'c') {
                lt = last;
                has_explicit_lt = 1;
            }
        }

        if (stars > 0) {
            
            for (int s = 0; s < stars && pos < out_size - 1; s++)
                out[pos++] = '_';
            if (pos < out_size - 1) out[pos++] = lt;
        }

        
        for (int qi = 0; t[qi] && pos < out_size - 1; qi++) {
            if (t[qi] == '*') continue;
            if (t[qi] == ' ') continue;
            if (has_explicit_lt && t[qi + 1] == '\0') continue; 
            out[pos++] = (char)tolower((unsigned char)t[qi]);
        }
    }
    out[pos] = '\0';
}

static void normalize_type(const char *src, char *out, int out_size) {
    if (strncmp(src, "autodel:", 8) == 0) src += 8;
    if (strncmp(src, "notdel:",  7) == 0) src += 7;

    int stars = 0;
    for (const char *q = src; *q; q++) if (*q == '*') stars++;

    
    int slen = (int)strlen(src);
    char lt = 'c';
    int has_explicit_lt = 0;
    if (stars > 0 && slen > 0) {
        char last = src[slen - 1];
        if (last == 'a' || last == 'b' || last == 'c') {
            lt = last;
            has_explicit_lt = 1;
        }
    }

    int j = 0;
    for (int s = 0; s < stars && j < out_size - 1; s++)
        out[j++] = '_';

    if (stars > 0) {
        if (j < out_size - 1) out[j++] = lt;
    }

    for (int qi = 0; src[qi] && j < out_size - 1; qi++) {
        if (src[qi] == '*' || src[qi] == ' ') continue;
        if (has_explicit_lt && src[qi + 1] == '\0') continue; 
        out[j++] = (char)tolower((unsigned char)src[qi]);
    }
    out[j] = '\0';
}

static void collect_stars(char *type_buf, int buf_size) {
    while (strcmp(peek(0).value, "*") == 0) {
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
        if (strcmp(overload_table[i].base_name, base) == 0 && overload_table[i].param_count == argc)
            candidates[cand_count++] = &overload_table[i];
    }

    if (cand_count == 0) {
        for (int m = 0; m < num_imported_modules; m++) {
            OverloadEntry *mod_overloads = global_modules_overloads[m];
            int mod_count = global_modules_counts[m];
            for (int i = 0; i < mod_count; i++) {
                if (strcmp(mod_overloads[i].base_name, base) == 0 && mod_overloads[i].param_count == argc) {
                    if (cand_count < MAX_OVERLOADS) {
                        candidates[cand_count++] = &mod_overloads[i];
                    }
                }
            }
        }
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

    warning("Warning: overload resolution ambiguous for '%s', using '%s'\n",
           base, candidates[0]->mangled);
    return candidates[0]->mangled;
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
        if (t->type == TOK_KEYWORD && strcmp(t->value, "notdel") == 0) {
            (*pos)++;
            continue;
        }
        if (t->type == TOK_TYPE || t->type == TOK_IDENT) {
            char type_buf[64];
            strncpy(type_buf, t->value, 63);
            type_buf[63] = '\0';
            (*pos)++;
            prescan_collect_stars(toks, pos, (int)toks->size, type_buf, 64);
            
            if (*pos < (int)toks->size && toks->data[*pos].type == TOK_IDENT)
                (*pos)++;
            
            int has_ptr = (strchr(type_buf, '*') != NULL);
            if (has_ptr && *pos < (int)toks->size &&
                toks->data[*pos].type == TOK_OP) {
                char lt = '\0';
                if (strcmp(toks->data[*pos].value, "?") == 0) lt = 'a';
                else if (strcmp(toks->data[*pos].value, "!") == 0) lt = 'b';
                if (lt) {
                    
                    int tlen = (int)strlen(type_buf);
                    if (tlen < 62) { type_buf[tlen] = lt; type_buf[tlen+1] = '\0'; }
                    (*pos)++;
                } else {
                    
                    int tlen = (int)strlen(type_buf);
                    if (tlen < 62) { type_buf[tlen] = 'c'; type_buf[tlen+1] = '\0'; }
                }
            } else if (has_ptr) {
                int tlen = (int)strlen(type_buf);
                if (tlen < 62) { type_buf[tlen] = 'c'; type_buf[tlen+1] = '\0'; }
            }
            if (*param_count < MAX_OVERLOAD_PARAMS)
                strncpy(param_types[(*param_count)++], type_buf, 63);
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
            debug("DEBUG prescan: extern C func '%s' (no mangle)\n", func_name);
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
            ret_type[63] = '\0';
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
            debug("DEBUG prescan: func '%s' -> '%s' ret='%s'\n",
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
                    debug("DEBUG prescan: ctor '%s' -> '%s'\n", ctor_base, mangled);
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
                    debug("DEBUG prescan: dtor '%s' -> '%s'\n", dtor_base, mangled);
                    if (pos < n && strcmp(toks->data[pos].value, "{") == 0)
                        prescan_skip_block(toks, &pos);
                    continue;
                }

                
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
                    ret_type[63] = '\0';
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

                    
                    if (is_static_method)
                        overload_set_static(mangled);

                    debug("DEBUG prescan: method '%s' -> '%s' ret='%s' static=%d\n",
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

Node parseVarDef(Token t, int m);
Node parseFuncDef(Token t);
static Node parseFuncDefInner(Token t, int do_mangle);
Node parseParams(void);
Node parseIf(void);
Node parseWhile(void);
Node parseFuncCall(Token t);
Node parseAssign(Token t);
Node parseReturn(Token kw);
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
Node parseExternVarDef(void);

Node parsing(void) {
    Token current = advance();
    debug("DEBUG parsing in %s:\n\tat [line: %i; col: %i] => token '%s' (type: %i)\n",
           current.file, current.line, current.col, current.value, current.type);

    if (current.type == TOK_KEYWORD || current.type == TOK_IDENT) {
        if (strcmp(current.value, "async") == 0) {
            if (peek(0).type == TOK_TYPE || (peek(0).type == TOK_IDENT && class_lookup(peek(0).value)) || strcmp(peek(0).value, "void") == 0) {
                Token type_tok = advance();
                Node func = parseFuncDef(type_tok);
                func.type = NODE_ASYNC_FUNC_DEF;
                return func;
            } else if (peek(0).type == TOK_IDENT) {
                Token name_tok = advance();
                Node call = parseFuncCall(name_tok);
                call.type = NODE_ASYNC_CALL;
                return call;
            }
        }
        if (strcmp(current.value, "await") == 0) {
            if (peek(0).type == TOK_TYPE || (peek(0).type == TOK_IDENT && class_lookup(peek(0).value)) || strcmp(peek(0).value, "void") == 0) {
                Token type_tok = advance();
                Node func = parseFuncDef(type_tok);
                func.type = NODE_AWAIT_FUNC_DEF;
                return func;
            } else if (peek(0).type == TOK_IDENT) {
                Token name_tok = advance();
                Node call = parseFuncCall(name_tok);
                call.type = NODE_AWAIT_CALL;
                return call;
            }
        }
        if (strcmp(current.value, "channel") == 0) {
            Token type_tok = advance();
            Node var = parseVarDef(type_tok, 0);
            var.type = NODE_CHANNEL_VAR_DEF;
            return var;
        }
    }

    if (current.type == TOK_KEYWORD) {
        if (strcmp(current.value, "if")     == 0) return parseIf();
        if (strcmp(current.value, "while")  == 0) return parseWhile();
        if (strcmp(current.value, "return") == 0) return parseReturn(current);
        if (strcmp(current.value, "struct") == 0) return parseStruct();
        if (strcmp(current.value, "class")  == 0) return parseClass();
        if (strcmp(current.value, "for")    == 0) return parseFor();
        if (strcmp(current.value, "do")     == 0) return parseDoWhile();
        if (strcmp(current.value, "delete") == 0) return parseDelete();
        if (strcmp(current.value, "enum")   == 0) return parseEnum();
        if (strcmp(current.value, "extern") == 0) {
            if (peek(0).type == TOK_STRING && strcmp(peek(0).value, "C") == 0)
                return parseExternCFuncDef();

            
            if (peek(0).type == TOK_KEYWORD && strcmp(peek(0).value, "struct") == 0) {
                advance();
                Node str = parseStruct();
                str.type = NODE_EXTERN_STRUCT_DEF;
                return str;
            }

            
            if (peek(0).type == TOK_KEYWORD && strcmp(peek(0).value, "class") == 0) {
                advance();
                Node cls = parseClass();
                cls.type = NODE_EXTERN_STRUCT_DEF;
                return cls;
            }

            
            
            int ptr_offset = 1;
            while (strcmp(peek(ptr_offset).value, "*") == 0) {
                ptr_offset++;
            }
            
            
            
            if (strcmp(peek(ptr_offset + 1).value, "(") == 0) {
                
                return parseExternFuncDef();
            } else {
                
                return parseExternVarDef();
            }
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

    if (strcmp(current.value, "*") == 0)
        return parsePtrAssign();

    
    if (current.type == TOK_IDENT && strcmp(current.value, "F") == 0 &&
        peek(0).type == TOK_OP && strcmp(peek(0).value, "<") == 0)
    {
        
        i--;
        Token fake;
        fake = advance();
        return parseVarDef(fake, 1);
    }

type_decl:
    if (current.type == TOK_TYPE ||
        (current.type == TOK_IDENT && class_lookup(current.value)))
    {
        int ptr_offset = 0;
        while (strcmp(peek(ptr_offset).value, "*") == 0)
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
            error("Error: unexpected identifier '%s'\n", current.value);
        }
    }

    if (current.type == TOK_EOF) {
        Node eof;
        eof.type = NODE_EOF;
        eof.begin = current;
        eof.childs = malloc(sizeof(vector_node));
        vn_init(eof.childs, 1);
        eof.str = NULL;
        return eof;
    }

    error("Error: unknown token '%s'\n", current.value);
    Node *a = NULL; return *a;
}

Node parseExternVarDef(void) {
    Node var;
    var.childs = malloc(sizeof(vector_node));
    vn_init(var.childs, 2);
    var.str = NULL;
    
    Token t = advance();
    var.begin = t;
    
    char type_str[80];
    if (t.type != TOK_TYPE && t.type != TOK_IDENT) {
        error("Error: expected type in extern variable declaration\n");
    }
    strncpy(type_str, t.value, 79);
    type_str[79] = '\0';
    collect_stars(type_str, 80);
    
    Node *type_node = make_node(NODE_TYPE, type_str);
    vn_push_back(var.childs, *type_node);
    free(type_node);

    
    Token name_tok = advance();
    if (name_tok.type != TOK_IDENT) {
        error("Error: expected identifier in extern variable declaration\n");
    }
    Node *ident_node = make_node(NODE_IDENT, name_tok.value);
    vn_push_back(var.childs, *ident_node);
    free(ident_node);

    
    if (peek(0).type == TOK_SEMICOLON) {
        advance();
    } else {
        error("Error: expected ';' after extern variable '%s'\n", name_tok.value);
    }

    var.type = NODE_EXTERN_VAR_DEF;
    return var;
}

static int parse_type_token(char *out, int out_size) {
    Token t = advance();

    if (strcmp(t.value, "F") == 0 &&
        peek(0).type == TOK_OP && strcmp(peek(0).value, "<") == 0)
    {
        advance();
        Token ret_tok = advance();
        if (peek(0).type == TOK_OP && strcmp(peek(0).value, ">") == 0)
            advance();

        char buf[128];
        snprintf(buf, sizeof(buf), "F<%s>(", ret_tok.value);

        
        if (peek(0).type == TOK_IDENT &&
            peek(1).type == TOK_PAREN && strcmp(peek(1).value, "(") == 0)
        {
            advance();
            advance();
            int first = 1;
            while (strcmp(peek(0).value, ")") != 0 && peek(0).type != TOK_EOF) {
                if (!first) strncat(buf, ",", sizeof(buf) - strlen(buf) - 1);
                first = 0;
                Token arg_type = advance();
                strncat(buf, arg_type.value, sizeof(buf) - strlen(buf) - 1);
                if (peek(0).type == TOK_IDENT) advance();
                if (peek(0).type == TOK_COMMA) advance();
            }
            if (strcmp(peek(0).value, ")") == 0) advance();
        }
        

        strncat(buf, ")", sizeof(buf) - strlen(buf) - 1);
        strncpy(out, buf, out_size - 1);
        out[out_size - 1] = '\0';
        return 1;
    }

    strncpy(out, t.value, out_size - 1);
    out[out_size - 1] = '\0';
    while (strcmp(peek(0).value, "*") == 0) {
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
    Node n = parseVarDef(patched, 0);
    n.begin = t;
    return n;
}

Node parseTypedef(void) {
    Node td;
    td.childs = malloc(sizeof(vector_node));
    vn_init(td.childs, 2);
    td.str = NULL;

    Token type_tok = advance();
    td.begin = type_tok;

    if (type_tok.type != TOK_TYPE && type_tok.type != TOK_IDENT) {
        error("Error: expected type after 'typedef'\n");
    }

    char type_str[80];
    strncpy(type_str, type_tok.value, 79);
    type_str[79] = '\0';
    collect_stars(type_str, 80);

    Token alias_tok = advance();
    if (alias_tok.type != TOK_IDENT) {
        error("Error: expected alias name in typedef\n");
    }

    
    class_push(alias_tok.value);

    Node *orig  = make_node(NODE_TYPE,  type_str);
    Node *alias = make_node(NODE_IDENT, alias_tok.value);
    vn_push_back(td.childs, *orig);  free(orig);
    vn_push_back(td.childs, *alias); free(alias);

    if (peek(0).type == TOK_SEMICOLON) advance();
    else { error("Error: semicolon expected after typedef\n"); }

    td.type = NODE_TYPEDEF;
    return td;
}

Node parseEnum(void) {
    Node en;
    en.childs = malloc(sizeof(vector_node));
    vn_init(en.childs, 8);
    en.str = NULL;

    Token t = peek(0);
    en.begin = t;

    if (t.type == TOK_IDENT)
        advance();

    Token brace = advance();
    if (strcmp(brace.value, "{") != 0) {
        error("Error: expected '{' after enum name\n");
    }

    int counter = 0;
    while (strcmp(peek(0).value, "}") != 0) {
        if (peek(0).type == TOK_EOF) {
            error("Error: unexpected EOF in enum body\n");
        }

        Token name_tok = advance();
        if (name_tok.type != TOK_IDENT) {
            error("Error: expected identifier in enum, got '%s'\n", name_tok.value);
        }

        int value = counter;

        if (peek(0).type == TOK_OP && strcmp(peek(0).value, "=") == 0) {
            advance();
            Token val_tok = advance();
            if (val_tok.type != TOK_INT) {
                error("Error: expected integer after '=' in enum\n");
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
        var->begin = name_tok;

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
    Token t = advance();

    Node func;
    func.childs = malloc(sizeof(vector_node));
    vn_init(func.childs, 4);
    func.str = NULL;
    func.begin = t;

    Token current = advance();
    if (current.type == TOK_TYPE || current.type == TOK_IDENT) {
        char ret_str[80];
        strncpy(ret_str, current.value, 79);
        collect_stars(ret_str, 80);
        Node *type = make_node(NODE_TYPE, ret_str);
        vn_push_back(func.childs, *type);
        free(type);
    } else {
        error("Error: expected return type in extern \"C\" func\n");
        
    }

    current = advance();
    if (current.type == TOK_IDENT) {
        char tagged[128];
        snprintf(tagged, sizeof(tagged), "__extern_c__%s", current.value);
        Node *ident = make_node(NODE_IDENT, tagged);
        vn_push_back(func.childs, *ident);
        free(ident);
    } else {
        error("Error: expected function name in extern \"C\" func\n");
        
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
            error("Error: expected ')' in extern \"C\" func\n");
            
        }
    } else {
        error("Error: expected '(' in extern \"C\" func\n");
        
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
                error("Error: unexpected EOF in extern \"C\" function body\n");
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

    error("Error: expected '{' or ';' in extern \"C\" func\n");
    
    func.type = NODE_ERROR;
    return func;
}

Node parseExternFuncDef(void) {
    Node ext;
    ext.childs = malloc(sizeof(vector_node));
    vn_init(ext.childs, 3);
    ext.str = NULL;

    Token t = advance();
    ext.begin = t;

    char ret_type_str[80];
    if (t.type != TOK_TYPE && t.type != TOK_IDENT) {
        error("Error: expected return type in extern func\n");
    }
    strncpy(ret_type_str, t.value, 79);
    collect_stars(ret_type_str, 80);
    Node *ret_type = make_node(NODE_TYPE, ret_type_str);
    vn_push_back(ext.childs, *ret_type);
    free(ret_type);

    t = advance();
    if (t.type != TOK_IDENT) {
        error("Error: expected function name in extern func\n");
    }
    Node *fname = make_node(NODE_IDENT, t.value);
    vn_push_back(ext.childs, *fname);
    free(fname);

    t = advance();
    if (strcmp(t.value, "(") != 0) {
        error("Error: expected '(' in extern func\n");
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
        error("Error: expected ')' in extern func\n");
        
    }

    if (peek(0).type == TOK_SEMICOLON) advance();
    else {
        error("Error: semicolon expected");
        
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
    del.begin = t;

    if (strcmp(t.value, "(") != 0) {
        error("Error: expected '(' after 'delete'\n");
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
        error("Error: expected ')' after delete args\n");

    vn_push_back(del.childs, *args);
    free(args);

    t = advance();
    if (t.type != TOK_IDENT) {
        error("Error: expected variable name after 'delete(...)'\n");
        del.type = NODE_ERROR;
        return del;
    }

    Node *var = make_node(NODE_VAR, t.value);
    var->begin = t;
    vn_push_back(del.childs, *var);
    free(var);

    if (peek(0).type == TOK_SEMICOLON) advance();
    else {
        error("Error: semicolon expected");
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
                error("Error: unexpected EOF in body\n");
            }
            Node temp = parsing();
            if (temp.type != NODE_ERROR) {
                Node *tc = malloc(sizeof(Node));
                *tc = temp;
                vn_push_back(scope->childs, *tc);
                free(tc);
            } else {
                error("Error in body\n");
                
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
    lhs_obj->begin = obj;
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
    assign.begin = obj;

    Node *undef = make_node(NODE_UNDEF, "");
    vn_push_back(assign.childs, *undef); free(undef);

    Node *rhs_obj = make_node(NODE_VAR, obj.value);
    rhs_obj->begin = obj;
    Node *lhs2 = make_node(
        strcmp(op_tok.value, ".") == 0 ? NODE_MEMBER_DOT : NODE_MEMBER_ARROW,
        field_tok.value
    );
    vn_push_back(lhs2->childs, *rhs_obj); free(rhs_obj);

    Node *rhs_expr;
    if (strcmp(cmp_tok.value, "++") == 0 || strcmp(cmp_tok.value, "--") == 0) {
        Node *one = make_node(NODE_I32, "1");
        one->begin = cmp_tok;
        const char *op = (strcmp(cmp_tok.value, "++") == 0) ? "+" : "-";
        rhs_expr = make_node(NODE_BINOP, op);
        rhs_expr->begin = cmp_tok;
        vn_push_back(rhs_expr->childs, *lhs2); free(lhs2);
        vn_push_back(rhs_expr->childs, *one);  free(one);
    } else {
        Node *rhs = parseExpr();
        char binop[2] = { cmp_tok.value[0], '\0' };
        rhs_expr = make_node(NODE_BINOP, binop);
        rhs_expr->begin = cmp_tok;
        vn_push_back(rhs_expr->childs, *lhs2); free(lhs2);
        vn_push_back(rhs_expr->childs, *rhs);  free(rhs);
    }
    vn_push_back(assign.childs, *rhs_expr); free(rhs_expr);

    if (peek(0).type == TOK_SEMICOLON) advance();
    else {
        error("Error: semicolon expected");
        
    }
    return assign;
}

Node parseCompoundAssign(Token t, int f) {
    Node assign;
    assign.childs = malloc(sizeof(vector_node));
    vn_init(assign.childs, 4);
    assign.str = NULL;
    assign.begin = t;

    Token op = advance();

    Node *ident = make_node(NODE_IDENT, t.value);
    vn_push_back(assign.childs, *ident);
    free(ident);

    Node *undef = make_node(NODE_UNDEF, "");
    vn_push_back(assign.childs, *undef);
    free(undef);

    Node *var_ref = make_node(NODE_VAR, t.value);
    var_ref->begin = t;

    if (strcmp(op.value, "++") == 0 || strcmp(op.value, "--") == 0) {
        Node *one = make_node(NODE_I32, "1");
        one->begin = op;
        const char *binop = (strcmp(op.value, "++") == 0) ? "+" : "-";
        Node *binop_node = make_node(NODE_BINOP, binop);
        binop_node->begin = op;
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
        binop_node->begin = op;
        vn_push_back(binop_node->childs, *var_ref);
        vn_push_back(binop_node->childs, *rhs);
        free(var_ref); free(rhs);
        vn_push_back(assign.childs, *binop_node);
        free(binop_node);
    }

    if (peek(0).type == TOK_SEMICOLON) advance();
    else if (f == 1) {
        error("Error: semicolon expected");
        
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
    asmnode.begin = arch_tok;

    const char *asm_component = NULL;
    for (int ai = 0; asm_arch_table[ai].prefix != NULL; ai++) {
        if (strcmp(arch_tok.value, asm_arch_table[ai].prefix) == 0) {
            asm_component = asm_arch_table[ai].triple_component;
            break;
        }
    }

    if (asm_component == NULL) {
        error("Error [line %d, col %d]: unknown keyword or asm arch prefix '%s'\n",
               arch_tok.line, arch_tok.col, arch_tok.value);
    }

    const char *current_triple = preprocess_get_target();
    if (current_triple != NULL && current_triple[0] != '\0') {
        if (!triple_has_component(current_triple, asm_component)) {
            error("Error [line %d, col %d]: "
                   "asm arch '%s' is incompatible with target '%s'\n"
                   "  Hint: wrap platform-specific asm in "
                   "#ifdef %s ... #endif\n",
                   arch_tok.line, arch_tok.col,
                   arch_tok.value, current_triple,
                   arch_tok.value);
            
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

    
    char instr[1024];
    instr[0] = '\0';
    int operand_count = 0;
    int is_block = (strcmp(peek(0).value, "{") == 0);

    if (is_block) {
        advance();
        int first_instr = 1;
        while (strcmp(peek(0).value, "}") != 0 && peek(0).type != TOK_EOF) {
            if (!first_instr) strncat(instr, "\n", sizeof(instr) - strlen(instr) - 1);
            first_instr = 0;

            Token mnemonic = advance();
            if (mnemonic.type == TOK_SEMICOLON) { first_instr = 1; continue; }
            strncat(instr, mnemonic.value, sizeof(instr) - strlen(instr) - 1);

            while (peek(0).type != TOK_SEMICOLON &&
                   !(peek(0).type == TOK_PAREN && strcmp(peek(0).value, "}") == 0) &&
                   peek(0).type != TOK_EOF &&
                   !(peek(0).type == TOK_OP && strcmp(peek(0).value, ">") == 0)) {
                Token op = advance();
                size_t len = strlen(instr);

                int add_space = 0;
                if (len > 0) {
                    char last = instr[len - 1];
                    char next = op.value[0];
                    
                    add_space = 1;
                    
                    if (strchr("[(#$+-.:", last) != NULL) {
                        add_space = 0;
                    }
                    if (strchr("]),+-.:", next) != NULL) {
                        add_space = 0;
                    }
                    if (last == ' ') {
                        add_space = 0;
                    }
                }

                if (add_space) {
                    strncat(instr, " ", sizeof(instr) - len - 1);
                }
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
                    operand_node.begin = var_tok;
                    vn_push_back(asmnode.childs, operand_node);
                    operand_count++;
                } else {
                    strncat(instr, op.value, sizeof(instr) - strlen(instr) - 1);
                }
            }
            if (peek(0).type == TOK_SEMICOLON) advance();
        }
        if (peek(0).type == TOK_PAREN && strcmp(peek(0).value, "}") == 0)
            advance();
    } else {
        
        Token mnemonic = advance();
        if (mnemonic.type != TOK_IDENT) {
            error("Error [line %d, col %d]: expected instruction mnemonic after '%s'\n",
                   mnemonic.line, mnemonic.col, arch_tok.value);
        }
        strncpy(instr, mnemonic.value, sizeof(instr) - 1);

        while (peek(0).type != TOK_SEMICOLON &&
               peek(0).type != TOK_EOF &&
               peek(0).type != TOK_ERROR &&
               !(peek(0).type == TOK_OP && strcmp(peek(0).value, "==>") == 0) &&
               !(peek(0).type == TOK_OP && strcmp(peek(0).value, ">") == 0) &&
               !(peek(0).type == TOK_OP && strcmp(peek(0).value, ":") == 0) &&
               !(peek(0).type == TOK_OP && strcmp(peek(0).value, "<") == 0)) {
            Token op = advance();
            size_t len = strlen(instr);

            int add_space = 0;
            if (len > 0) {
                char last = instr[len - 1];
                char next = op.value[0];
                
                add_space = 1;
                
                if (strchr("[(#$+-.:", last) != NULL) {
                    add_space = 0;
                }
                if (strchr("]),+-.:", next) != NULL) {
                    add_space = 0;
                }
                if (last == ' ') {
                    add_space = 0;
                }
            }

            if (add_space) {
                strncat(instr, " ", sizeof(instr) - len - 1);
            }
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
                operand_node.begin = var_tok;
                vn_push_back(asmnode.childs, operand_node);
                operand_count++;
            } else {
                strncat(instr, op.value, sizeof(instr) - strlen(instr) - 1);
            }
        }
    }

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
                    error("Error: variable's name expected");
                    
                }

                if (strcmp(peek(0).value, ")") != 0) {
                    error("Error: ')' expected in %i", peek(0).line);
                }

                advance();
            }
            else {
                error("Error: '(' expected in %i", peek(0).line);
            }

            strncat(out_var, out_reg, sizeof(out_var) - strlen(out_var) - 1);
            strncat(out_var, ":",     sizeof(out_var) - strlen(out_var) - 1);
            strncat(out_var, out_v,   sizeof(out_var) - strlen(out_var) - 1);
        }
        else {
            error("Error: register's name expected");
        }
    }

    if (strcmp(peek(0).value, ":") == 0) {
        advance();
        
        while (peek(0).type != TOK_SEMICOLON &&
               peek(0).type != TOK_EOF &&
               strcmp(peek(0).value, "<") != 0) {
            if (peek(0).type != TOK_IDENT) break;
            Token reg_tok = advance();
            if (clobbers_str[0]) strncat(clobbers_str, ",", sizeof(clobbers_str) - strlen(clobbers_str) - 1);
            strncat(clobbers_str, reg_tok.value, sizeof(clobbers_str) - strlen(clobbers_str) - 1);
        }
    }
    
    if (strcmp(peek(0).value, "<") == 0) {
        advance();
        while (peek(0).type != TOK_SEMICOLON && peek(0).type != TOK_EOF) {
            Token reg_tok = advance();
            if (peek(0).type != TOK_PAREN || strcmp(peek(0).value, "(") != 0) {
                error("Error: expected '(varName)' after register in asm inputs\n");
            }
            advance();
            Token var_tok = advance();
            if (var_tok.type != TOK_IDENT && var_tok.type != TOK_INT) {
                error("Error: expected variable name or integer constant in asm input, got '%s'\n", var_tok.value);
            }
            if (peek(0).type == TOK_PAREN && strcmp(peek(0).value, ")") == 0) advance();
            if (inputs_str[0]) strncat(inputs_str, ",", sizeof(inputs_str) - strlen(inputs_str) - 1);
            strncat(inputs_str, reg_tok.value, sizeof(inputs_str) - strlen(inputs_str) - 1);
            strncat(inputs_str, ":",           sizeof(inputs_str) - strlen(inputs_str) - 1);
            
            if (var_tok.type == TOK_INT) {
                strncat(inputs_str, "#", sizeof(inputs_str) - strlen(inputs_str) - 1);
            }
            strncat(inputs_str, var_tok.value, sizeof(inputs_str) - strlen(inputs_str) - 1);
        }
    }
    
    
    {
        snprintf(asmnode.str, 2047, "%s\x01%s\x01%s\x01%s",
                 instr, out_var, clobbers_str, inputs_str);
        asmnode.str[2047] = '\0';
    }

    
    Node out_node;
    out_node.type   = NODE_IDENT;
    out_node.childs = malloc(sizeof(vector_node));
    vn_init(out_node.childs, 1);
    out_node.str = strdup(out_var[0] ? out_var : "");
    vn_push_back(asmnode.childs, out_node);

    Token cur = peek(0);
    if (cur.type == TOK_SEMICOLON)
        advance();
    else if (!is_block) {
        error("Error in %s:\n\tat [line: %i; col: %i] => expected ';' after ASM, got '%s'\n",
               cur.file, cur.line, cur.col, cur.value);
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
    assign.begin = t;
    if (t.type != TOK_IDENT) {
        error("Error: expected identifier after '*'\n");
    }

    Node *ident = make_node(NODE_IDENT, t.value);
    vn_push_back(assign.childs, *ident);
    free(ident);

    if (strcmp(peek(0).value, "=") == 0)
        advance();
    else {
        error("Error: expected '=' after '*%s'\n", t.value);
    }

    Node *expr = parseExpr();
    if (expr) {
        vn_push_back(assign.childs, *expr);
        free(expr);
    }

    if (peek(0).type == TOK_SEMICOLON) advance();
    else {
        error("Error: semicolon expected");
        
    }

    assign.type = NODE_PTR_ASSIGN;
    return assign;
}

static int infer_expr_type_str(Node *n, char *out, int out_size);

static int needs_cast(Node *expr, const char *target_type) {
    if (!target_type || target_type[0] == '\0') return 0;
    const char *tt = target_type;
    if (strncmp(tt, "autodel:", 8) == 0) tt += 8;
    if (strncmp(tt, "const:",   6) == 0) tt += 6;
    if (tt[0] == '\0') return 0;

    char src_type[64] = "";
    if (!infer_expr_type_str(expr, src_type, sizeof(src_type))) return 0;
    return strcmp(src_type, tt) != 0;
}

void **parse(int it, vector_token* tokenss, char *flags, void *overloads, int *overloads_c) {
    g_flags = flags;
    i = it;
    tokens = tokenss;
    var_type_count    = 0;
    known_class_count = 0;

    if (overloads && overloads_c) {
        overload_table = (OverloadEntry*)overloads;
        overload_count = *overloads_c;
    } else {
        overload_table = malloc(sizeof(OverloadEntry) * MAX_OVERLOADS);
        overload_count = 0;
    }

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

    if ((*flags) & 0b0001) {
        void **tmp = malloc(sizeof(void*) * 3);
        tmp[0] = nodes;
        tmp[1] = overload_table;
        
        int *ret_count = malloc(sizeof(int));
        *ret_count = overload_count;
        tmp[2] = ret_count;
        
        return tmp;
    }
    else {
        void **tmp = malloc(sizeof(void*) * 1);
        tmp[0] = nodes;
        return tmp;
    }
}

static Node *make_cast_node(Node *expr, const char *type_str) {
    Node *cast = make_node(NODE_CAST, type_str);
    vn_push_back(cast->childs, *expr);
    return cast;
}


Node parseAssign(Token t) {
    const char *vt = var_type_lookup(t.value);
    if (vt && strncmp(vt, "const:", 6) == 0) {
        error("Error: cannot assign to constant variable '%s' at line %d, col %d\n",
               t.value, t.line, t.col);
         
    }

    Node assign;
    assign.childs = malloc(sizeof(vector_node));
    vn_init(assign.childs, 4);
    assign.str = NULL;
    assign.begin = t;
    
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
            error("Error: expected ']'\n");
            
        }
        
        assign.type = NODE_INDEX_ASSIGN;
    }
    else if (peek(0).type == TOK_OP &&
        (strcmp(peek(0).value, ".") == 0 || strcmp(peek(0).value, "->") == 0)) {
        Node *var = make_node(NODE_VAR, t.value);
        var->begin = t;
        
        while (peek(0).type == TOK_OP &&
            (strcmp(peek(0).value, ".") == 0 || strcmp(peek(0).value, "->") == 0)) {
            Token op = advance();
            Token field = peek(0);
            if (field.type != TOK_IDENT) {
                error("Error: expected field name\n");
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
            else { error("Error: expected ']'\n");  }

            
            if (peek(0).type == TOK_OP &&
                (strcmp(peek(0).value, ".") == 0 ||
                strcmp(peek(0).value, "->") == 0))
            {
                
                Node *index_node = make_node(NODE_INDEX, "[]");
                index_node->begin = peek(0);
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
                        error("Error: expected field name\n");
                         break;
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
                    else { error("Error: expected ']'\n");  }
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
                if (vt) {
                    strncpy(class_name_buf, vt, 63);
                    int len = strlen(class_name_buf);
                    if (len > 0 && class_name_buf[len-1] == '*') class_name_buf[len-1] = '\0';
                }
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
                
                const char *vt = var_type_lookup(t.value);
                if (vt && needs_cast(expr, vt)) {
                    Node *cast = make_cast_node(expr, vt);
                    vn_push_back(assign.childs, *cast);
                    free(cast);
                } else {
                    vn_push_back(assign.childs, *expr);
                }
                free(expr);
            }
        }
    }
    else {
        error("Error: expected '='\n");
        
    }

    if (peek(0).type == TOK_SEMICOLON) advance();
    else {
        error("Error: semicolon expected");
        
    }

    return assign;
}

Node parseReturn(Token kw) {
    Node ret;
    ret.childs = malloc(sizeof(vector_node));
    vn_init(ret.childs, 2);
    ret.str = NULL;
    ret.begin = kw;

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
            error("Error: expected ';' after return value\n");
            
        }
    }

    ret.type = NODE_RETURN;
    return ret;
}

static Node parseFuncCallInner(Token t, int is_stmt) {
    debug("DEBUG funcall: '%s' is_stmt=%d peek='%s'\n",
           t.value, is_stmt, peek(0).value);
    Node call;
    call.childs = malloc(sizeof(vector_node));
    vn_init(call.childs, 4);
    call.str = malloc(strlen(t.value) + 1);
    strcpy(call.str, t.value);
    call.begin = t;

    Node *name_node = make_node(NODE_IDENT, t.value);
    vn_push_back(call.childs, *name_node);
    free(name_node);

    Node *args = make_node(NODE_ARGS, "");
    if (strcmp(peek(0).value, "(") == 0) {
        advance();
        while (strcmp(peek(0).value, ")") != 0) {
            debug("DEBUG funcall arg: peek='%s'\n", peek(0).value);
            if (peek(0).type == TOK_EOF) break;
            Node *thing = parseExpr();
            if (thing) { vn_push_back(args->childs, *thing); free(thing); }
            if (peek(0).type == TOK_COMMA) advance();
            else break;
        }
        if (strcmp(peek(0).value, ")") == 0)
            advance();
        else {
            error("Error: expected ')' in function call\n");
            
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
            debug("DEBUG funcall: resolved '%s' -> '%s'\n", t.value, resolved);

            
            OverloadEntry *oe = NULL;
            for (int oi = 0; oi < overload_count; oi++) {
                if (strcmp(overload_table[oi].mangled, resolved) == 0) {
                    oe = &overload_table[oi];
                    break;
                }
            }
            
            if (!oe) {
                for (int m = 0; m < num_imported_modules && !oe; m++) {
                    for (int oi = 0; oi < global_modules_counts[m]; oi++) {
                        if (strcmp(global_modules_overloads[m][oi].mangled, resolved) == 0) {
                            oe = &global_modules_overloads[m][oi];
                            break;
                        }
                    }
                }
            }
            if (oe) {
                Node *argsn = &call.childs->data[1];
                for (int ai = 0; ai < (int)argsn->childs->size && ai < oe->param_count; ai++) {
                    const char *ptype = oe->param_types[ai];
                    Node *arg = &argsn->childs->data[ai];
                    if (needs_cast(arg, ptype)) {
                        
                        Node *cast = make_cast_node(arg, ptype);
                        argsn->childs->data[ai] = *cast;
                        free(cast);
                    }
                }
            }
        }
    }

    if (is_stmt) {
        if (peek(0).type == TOK_SEMICOLON) advance();
        else {
            error("Error: semicolon expected");
            
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
    ifexpr.begin = current;

    if (strcmp(current.value, "(") == 0) {
        Node *expr = parseExpr();
        if (expr) { vn_push_back(ifexpr.childs, *expr); free(expr); }
        current = advance();
        if (strcmp(current.value, ")") != 0) {
            error("Error: expected ')' after if condition\n");
            
        }
    } else {
        error("Error: expected '(' after if\n");
        
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
    whilex.begin = current;

    if (strcmp(current.value, "(") == 0) {
        Node *expr = parseExpr();
        if (expr) { vn_push_back(whilex.childs, *expr); free(expr); }
        current = advance();
        if (strcmp(current.value, ")") != 0) {
            error("Error: expected ')' after while condition\n");
            
        }
    } else {
        error("Error: expected '(' after while\n");
        
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
    dowhile.begin = t;

    if (strcmp(t.value, "while") != 0) {
        error("Error: expected 'while' after 'do'\n");
        
        dowhile.type = NODE_ERROR;
        return dowhile;
    }
    t = advance();
    if (strcmp(t.value, "(") != 0) {
        error("Error: expected '(' after 'while'\n");
        
        dowhile.type = NODE_ERROR;
        return dowhile;
    }

    Node *cond = parseExpr();
    if (!cond) {
        error("Error: expected condition in do while\n");
        
        dowhile.type = NODE_ERROR;
        return dowhile;
    }
    vn_push_back(dowhile.childs, *cond);
    free(cond);

    t = advance();
    if (strcmp(t.value, ")") != 0) {
        error("Error: expected ')' after do while condition\n");
        
    }

    Node *body = parse_body();
    if (body) { vn_push_back(dowhile.childs, *body); free(body); }

    if (peek(0).type == TOK_SEMICOLON) advance();
    else {
        error("Error: semicolon expected");
        
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
    fornode.begin = t;

    if (strcmp(t.value, "(") != 0) {
        error("Error: expected '(' after 'for'\n");
        
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
                error("Error: expected step expression in for\n");
                
                step.type = NODE_ERROR; step.childs = NULL; step.str = NULL;
            }
        } else {
            error("Error: expected identifier in for step\n");
            
            step.type = NODE_ERROR; step.childs = NULL; step.str = NULL;
        }
        if (step.type == NODE_ERROR) { fornode.type = NODE_ERROR; return fornode; }
        vn_push_back(fornode.childs, step);
    }

    t = advance();
    if (strcmp(t.value, ")") != 0) {
        error("Error: expected ')' after for header\n");
        
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
    func.begin = t;

    Token current = t;
    if (current.type == TOK_TYPE || current.type == TOK_IDENT) {
        char ret_str[80];
        strncpy(ret_str, current.value, 79);
        ret_str[79] = '\0';
        collect_stars(ret_str, 80);
        Node *type = make_node(NODE_TYPE, ret_str);
        vn_push_back(func.childs, *type);
        free(type);
    } else {
        error("Error: unknown function return type\n");
        
    }

    current = advance();
    if (current.type == TOK_IDENT) {
        Node *ident = make_node(NODE_IDENT, current.value);
        vn_push_back(func.childs, *ident);
        free(ident);
    } else {
        error("Error: expected function name\n");
        
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
            error("Error in %s:\n\tat [line: %i; col: %i] => expected ')' after params\n",
                   current.file, current.line, current.col);
        }
    } else {
        error("Error: expected '(' in function def\n");
        
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
                if (param->childs && param->childs->size >= 1) {
                    const char *pt = param->childs->data[0].str;
                    if (strncmp(pt, "notdel:", 7) == 0) pt += 7;
                    strncpy(param_types[param_count++], pt, 63);
                }
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
                error("Error: unexpected EOF in function body\n");
                
                break;
            }
            Node temp = parsing();
            if (temp.type != NODE_ERROR) {
                Node *temp_copy = malloc(sizeof(Node));
                *temp_copy = temp;
                vn_push_back(scope->childs, *temp_copy);
                free(temp_copy);
            } else {
                error("Error in function scope\n");
                
            }
        }
        advance();
        vn_push_back(func.childs, *scope);
        free(scope);
    } else {
        error("Error: expected '{' in function def\n");
        
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

    params.begin = peek(0);

    while (1) {
        Node param;
        param.childs = malloc(sizeof(vector_node));
        vn_init(param.childs, 2);
        param.type = NODE_PARAM;
        param.str  = NULL;

        int is_notdel = 0;
        if (peek(0).type == TOK_KEYWORD && strcmp(peek(0).value, "notdel") == 0) {
            advance();
            is_notdel = 1;
        }

        char type_buf[128] = "";
        int is_ftype = parse_type_token(type_buf, sizeof(type_buf));

        if (is_notdel) {
            char tmp[164];
            snprintf(tmp, sizeof(tmp), "notdel:%s", type_buf);
            strncpy(type_buf, tmp, sizeof(type_buf) - 1);
            type_buf[sizeof(type_buf) - 1] = '\0';
        }

        Node *type = make_node(NODE_TYPE, type_buf);
        vn_push_back(param.childs, *type);

        if (!is_ftype) {
            Token name_tok = advance();
            if (name_tok.type == TOK_IDENT) {
                Node *ident = make_node(NODE_IDENT, name_tok.value);
                vn_push_back(param.childs, *ident);
                free(ident);
            }

            int has_ptr = (strchr(type_buf, '*') != NULL);
            if (has_ptr && peek(0).type == TOK_OP) {
                char lt = '\0';
                if (strcmp(peek(0).value, "?") == 0) lt = 'a';
                else if (strcmp(peek(0).value, "!") == 0) lt = 'b';
                if (lt) {
                    advance();
                    int tlen = (int)strlen(type_buf);
                    if (tlen < 126) { type_buf[tlen] = lt; type_buf[tlen+1] = '\0'; }
                } else {
                    int tlen = (int)strlen(type_buf);
                    if (tlen < 126) { type_buf[tlen] = 'c'; type_buf[tlen+1] = '\0'; }
                }
                free(type->str);
                type->str = malloc(strlen(type_buf) + 1);
                strcpy(type->str, type_buf);
            } else if (has_ptr) {
                int tlen = (int)strlen(type_buf);
                if (tlen < 126) { type_buf[tlen] = 'c'; type_buf[tlen+1] = '\0'; }
                free(type->str);
                type->str = malloc(strlen(type_buf) + 1);
                strcpy(type->str, type_buf);
            }
            free(type);
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
static int type_rank(const char *t) {
    if (!t) return -1;
    if (strcmp(t, "char")   == 0 || strcmp(t, "uchar")  == 0) return 1;
    if (strcmp(t, "short")  == 0 || strcmp(t, "ushort") == 0) return 2;
    if (strcmp(t, "int")    == 0 || strcmp(t, "uint")   == 0) return 3;
    if (strcmp(t, "long")   == 0 || strcmp(t, "ulong")  == 0) return 4;
    if (strcmp(t, "float")  == 0)                              return 5;
    if (strcmp(t, "double") == 0)                              return 6;
    return -1;
}


static const char *promote_type(const char *a, const char *b) {
    int ra = type_rank(a);
    int rb = type_rank(b);
    if (ra < 0 && rb < 0) return NULL;
    if (ra < 0) return b;
    if (rb < 0) return a;
    return (ra >= rb) ? a : b;
}

static int infer_expr_type_str(Node *n, char *out, int out_size) {
    if (!n) return 0;
    switch (n->type) {
        case NODE_I32:    strncpy(out, "int",    out_size-1); out[out_size-1]='\0'; return 1;
        case NODE_I64:    strncpy(out, "long",   out_size-1); out[out_size-1]='\0'; return 1;
        case NODE_I16:    strncpy(out, "short",  out_size-1); out[out_size-1]='\0'; return 1;
        case NODE_I8:     strncpy(out, "char",   out_size-1); out[out_size-1]='\0'; return 1;
        case NODE_FLOAT:  strncpy(out, "float",  out_size-1); out[out_size-1]='\0'; return 1;
        case NODE_DOUBLE: strncpy(out, "double", out_size-1); out[out_size-1]='\0'; return 1;
        case NODE_STRING: strncpy(out, "char*",  out_size-1); out[out_size-1]='\0'; return 1;
        case NODE_CAST:
            if (n->str && n->str[0]) {
                strncpy(out, n->str, out_size-1); out[out_size-1]='\0';
                return 1;
            }
            return 0;
        case NODE_UNOP:
            
            if (n->str && (strcmp(n->str, "&") == 0 || strcmp(n->str, "!!") == 0)) {
                strncpy(out, "ptr", out_size-1); out[out_size-1]='\0'; return 1;
            }
            if (n->childs && n->childs->size >= 1)
                return infer_expr_type_str(&n->childs->data[0], out, out_size);
            return 0;
        case NODE_VAR:
        case NODE_IDENT: {
            const char *vt = var_type_lookup(n->str);
            if (vt) { strncpy(out, vt, out_size-1); out[out_size-1]='\0'; return 1; }
            return 0;
        }
        case NODE_FUNC_CALL: {
            
            for (int oi = 0; oi < overload_count; oi++) {
                if (strcmp(overload_table[oi].mangled, n->str) == 0) {
                    strncpy(out, overload_table[oi].ret_type, out_size-1);
                    out[out_size-1] = '\0';
                    return 1;
                }
            }
            for (int m = 0; m < num_imported_modules; m++) {
                for (int oi = 0; oi < global_modules_counts[m]; oi++) {
                    if (strcmp(global_modules_overloads[m][oi].mangled, n->str) == 0) {
                        strncpy(out, global_modules_overloads[m][oi].ret_type, out_size-1);
                        out[out_size-1] = '\0';
                        return 1;
                    }
                }
            }
            return 0;
        }
        case NODE_BINOP: {
            
            char lt[64] = "", rt[64] = "";
            int lk = (n->childs && n->childs->size >= 1)
                     ? infer_expr_type_str(&n->childs->data[0], lt, sizeof(lt)) : 0;
            int rk = (n->childs && n->childs->size >= 2)
                     ? infer_expr_type_str(&n->childs->data[1], rt, sizeof(rt)) : 0;
            if (!lk && !rk) return 0;
            const char *promoted = promote_type(lk ? lt : NULL, rk ? rt : NULL);
            if (!promoted) return 0;
            strncpy(out, promoted, out_size-1); out[out_size-1]='\0';
            return 1;
        }
        default: return 0;
    }
}

Node parseVarDef(Token t, int m) {
    Node var;
    var.childs = malloc(sizeof(vector_node));
    vn_init(var.childs, 4);
    var.str = NULL;
    var.begin = t;

    int is_autodel = m;

    
    if (strcmp(t.value, "F") == 0 &&
        peek(0).type == TOK_OP && strcmp(peek(0).value, "<") == 0)
    {
        i--;
        char ftype_buf[128] = "";
        parse_type_token(ftype_buf, sizeof(ftype_buf));
        

        
        Token name_tok = advance();
        if (name_tok.type != TOK_IDENT) {
            error("Error: expected variable name after F<> type\n");
            
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

        
        if (strcmp(peek(0).value, "[") == 0) {
            advance();
            Node *size_expr = parseExpr();
            if (!size_expr) {
                error("Error: expected array size\n");
                
            }
            vn_push_back(var.childs, *size_expr);
            free(size_expr);
            if (strcmp(peek(0).value, "]") == 0) advance();
            else { error("Error: expected ']'\n");  }
            Node *undef = make_node(NODE_UNDEF, "");
            vn_push_back(var.childs, *undef);
            free(undef);
            if (peek(0).type == TOK_SEMICOLON) advance();
            var.type = NODE_ARRAY_DEF;
            return var;
        }

        
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

    
    Token current = t;

    char base_type[64];
    base_type[0] = '\0';

    if (current.type == TOK_TYPE || current.type == TOK_IDENT) {
        char stars[16] = "";
        {
            int sc = 0;
            while (strcmp(peek(sc).value, "*") == 0) sc++;
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
        error("Error: unknown variable type '%s'\n", current.value);
        
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
        error("Error: expected identifier, got '%s'\n", current.value);
        
    }

    var_type_push(var_name, base_type);

    if (strcmp(peek(0).value, "[") == 0) {
        advance();
        Node *size_expr = parseExpr();
        if (!size_expr) {
            error("Error: expected array size expression\n");
            
        }
        vn_push_back(var.childs, *size_expr);
        free(size_expr);
        if (strcmp(peek(0).value, "]") == 0)
            advance();
        else {
            error("Error: expected ']'\n");
            
        }
        Node *undef = make_node(NODE_UNDEF, "");
        vn_push_back(var.childs, *undef);
        free(undef);
        if (peek(0).type == TOK_SEMICOLON) advance();
        var.type = NODE_ARRAY_DEF;
        return var;
    }

    debug("Type assign: %i, got '%s' in line %i\n", peek(0).type, peek(0).value, peek(0).line);

    if (strcmp(peek(0).value, "=") == 0) {
        advance();

        if (peek(0).type == TOK_KEYWORD && strcmp(peek(0).value, "new") == 0) {
            advance();
            char alloc_type[96];
            const char *raw_type = var.childs->data[0].str;
            const char *type_to_copy = raw_type;
            
            
            if (strncmp(type_to_copy, "autodel:", 8) == 0) type_to_copy += 8;
            if (strncmp(type_to_copy, "const:", 6) == 0) type_to_copy += 6;
            
            strncpy(alloc_type, type_to_copy, 95);
            alloc_type[95] = '\0';
            
            
            int len = strlen(alloc_type);
            if (len > 0 && alloc_type[len-1] == '*') alloc_type[len-1] = '\0';
            
            Node *new_node = make_node(NODE_NEW, alloc_type);
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
                
                const char *raw_type = var.childs->data[0].str;
                if (strncmp(raw_type, "autodel:", 8) == 0) raw_type += 8;
                if (strncmp(raw_type, "const:",   6) == 0) raw_type += 6;
                if (needs_cast(expr, raw_type)) {
                    Node *cast = make_cast_node(expr, raw_type);
                    vn_push_back(var.childs, *cast);
                    free(cast);
                } else {
                    vn_push_back(var.childs, *expr);
                }
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
    str.begin = name;

    if (name.type != TOK_IDENT) {
        error("Error: expected struct name\n");
        
        str.type = NODE_ERROR;
        return str;
    }

    class_push(name.value);
    
    str.str = malloc(strlen(name.value) + 1);
    strcpy(str.str, name.value);

    Token brace = advance();
    if (brace.type == TOK_SEMICOLON) {
        str.type = NODE_STRUCT_DEF;
        return str;
    }

    if (strcmp(brace.value, "{") != 0) {
        error("Error: expected '{' after struct name\n");
        
        str.type = NODE_ERROR;
        return str;
    }

    while (strcmp(peek(0).value, "}") != 0) {
        if (peek(0).type == TOK_EOF) {
            error("Error: unexpected EOF in struct body\n");
            
            break;
        }

        if (peek(0).type != TOK_TYPE &&
            !(peek(0).type == TOK_IDENT && class_lookup(peek(0).value))) {
            error("Error: expected type in struct body\n");
            
            str.type = NODE_ERROR;
            return str;
        }

        Token field_type = advance();
        Node field = parseVarDef(field_type, 0);
        if (field.type == NODE_ERROR) {
            error("Error in struct field\n");
            
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
    cls.begin = name;

    if (name.type != TOK_IDENT) {
        error("Error: expected class name\n");
        
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
            error("Error: expected parent class name\n");
            
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

    char ctxs[64] = "";
    if (strcmp(peek(0).value, ":") == 0) {
        advance();
        while (peek(0).type == TOK_IDENT) {
            strcat(ctxs, advance().value);
            if (peek(0).type == TOK_IDENT) strcat(ctxs, "|");
        }

        if (ctxs[0] != '\0') {
            char full_name[128];
            snprintf(full_name, sizeof(full_name), "%s:%s", cls.str, ctxs);
            free(cls.str);
            cls.str = malloc(strlen(full_name) + 1);
            strcpy(cls.str, full_name);
        }
        else {
            error("parser: expected T after ':'\n"); 
        }
    }

    Token brace = advance();
    debug("%i\n", peek(0).type);
    if (brace.type == TOK_SEMICOLON) {
        cls.type = NODE_STRUCT_DEF;
        return cls;
    }
    if (strcmp(brace.value, "{") != 0) {
        error("Error: expected '{' after class name\n");
        
        cls.type = NODE_ERROR;
        return cls;
    }
    
    char autodel_fields[32][64];
    char autodel_field_types[32][64];
    int  autodel_field_count = 0;
    
    int has_explicit_dtor = 0;

    while (strcmp(peek(0).value, "}") != 0) {
        if (peek(0).type == TOK_EOF) {
            error("Error: unexpected EOF in class body\n");
            
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
            while (strcmp(peek(ptr_off + 1).value, "*") == 0)
                ptr_off++;
            Token after_name = peek(ptr_off + 2);
            Token type_tok = advance();

            if (strcmp(after_name.value, "(") == 0) {
                var_type_push("self", name.value);
                Node method = parseFuncDefInner(type_tok, 0);
                if (method.type == NODE_ERROR) {
                    error("Error in class method\n");
                    
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

                debug("DEBUG parser: method '%s' -> mangled '%s' static=%d\n",
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
                    error("Error in class field\n");
                    
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
                                debug("DEBUG parseClass: autodel field '%s' of type '%s'\n",
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
                error("Error: expected '(' after 'new'\n");
                
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
                error("Error: expected ')' after constructor params\n");
                
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
                debug("DEBUG parser: ctor '%s' -> mangled '%s'\n", ctor_name, mangled_ctor);
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
                error("Error: expected '(' after 'delete'\n");
                
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
                error("Error: expected ')' after destructor params\n");
                
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
                debug("DEBUG parser: dtor '%s' -> mangled '%s'\n", dtor_name, mangled_dtor);
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
                    debug("DEBUG parseClass: auto-delete field '%s' appended to explicit dtor\n",
                           autodel_fields[fi]);
                }
                vn_push_back(method.childs, *body);
                free(body);
            }

            method.type = NODE_FUNC_DEF;
            vn_push_back(cls.childs, method);
        }
        else {
            error("Error: expected type, 'new', 'delete', or 'static' in class body, got '%s'\n",
                   peek(0).value);
            
            cls.type = NODE_ERROR;
            return cls;
        }
    }

    advance();

    if (!has_explicit_dtor && autodel_field_count > 0) {
        debug("DEBUG parseClass: generating auto-dtor for '%s' (%d fields)\n",
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
            debug("DEBUG parseClass: auto-delete field '%s'\n", autodel_fields[fi]);
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

        debug("DEBUG parseClass: auto-dtor '%s' -> '%s'\n", dtor_base, mangled_dtor);

        vn_push_back(cls.childs, method);
    }

    if (peek(0).type == TOK_SEMICOLON) advance();

    cls.type = NODE_STRUCT_DEF;
    return cls;
}

typedef enum {
    PREC_NONE,
    PREC_ASSIGN,
    PREC_OR,
    PREC_AND,
    PREC_BITOR,
    PREC_BITXOR,
    PREC_BITAND,
    PREC_EQ,
    PREC_CMP,
    PREC_SHIFT,
    PREC_ADD,
    PREC_MUL,
    PREC_UNARY,
    PREC_CALL,
    PREC_PRIMARY
} Precedence;

static Node *parse_expression(int precedence);

static int get_precedence(Token t) {
    if (t.type != TOK_OP) {
        if (strcmp(t.value, "(") == 0 || strcmp(t.value, "[") == 0) return PREC_CALL;
        return PREC_NONE;
    }
    if (strcmp(t.value, "||") == 0) return PREC_OR;
    if (strcmp(t.value, "&&") == 0) return PREC_AND;
    if (strcmp(t.value, "|")  == 0) return PREC_BITOR;
    if (strcmp(t.value, "^")  == 0) return PREC_BITXOR;
    if (strcmp(t.value, "&")  == 0) return PREC_BITAND;
    if (strcmp(t.value, "==") == 0 || strcmp(t.value, "!=") == 0) return PREC_EQ;
    if (strcmp(t.value, "<")  == 0 || strcmp(t.value, ">")  == 0 ||
        strcmp(t.value, "<=") == 0 || strcmp(t.value, ">=") == 0) return PREC_CMP;
    if (strcmp(t.value, "<<") == 0 || strcmp(t.value, ">>") == 0) return PREC_SHIFT;
    if (strcmp(t.value, "+")  == 0 || strcmp(t.value, "-")  == 0) return PREC_ADD;
    if (strcmp(t.value, "*")  == 0 || strcmp(t.value, "/")  == 0 || strcmp(t.value, "%") == 0) return PREC_MUL;
    if (strcmp(t.value, ".")  == 0 || strcmp(t.value, "->") == 0) return PREC_CALL;
    return PREC_NONE;
}


static Node *parse_prefix() {
    Token t = advance();

    
    if (t.type == TOK_OP && (strcmp(t.value, "-") == 0  || strcmp(t.value, "!") == 0 ||
                             strcmp(t.value, "~") == 0  || strcmp(t.value, "*") == 0 ||
                             strcmp(t.value, "&") == 0)) {
        Node *operand = parse_expression(PREC_UNARY);
        Node *node = make_node(NODE_UNOP, t.value);
        vn_push_back(node->childs, *operand);
        free(operand);
        node->begin = t;
        
        return node;
    }

    
    if (t.type == TOK_INT) {
        
        const char *v = t.value;
        int len = (int)strlen(v);
        if (len > 0) {
            char last = v[len - 1];
            if (last == 'c' || last == 'C') {
                Node *val = make_node(NODE_I8, t.value);
                val->begin = t;
                return val;
            }
            if (last == 's' || last == 'S') {
                Node *val = make_node(NODE_I16, t.value);
                val->begin = t;
                return val;
            }
            if (last == 'l' || last == 'L') {
                Node *val = make_node(NODE_I64, t.value);
                val->begin = t;
                return val;
            }
        }
        Node *val = make_node(NODE_I32, t.value);
        val->begin = t;
        return val;
    }

    if (t.type == TOK_STRING) {
        Node *str_node = make_node(NODE_STRING, t.value);
        str_node->begin = t;
        return str_node;
    }

    
    if (t.type == TOK_IDENT) {
        Node *var = make_node(NODE_VAR, t.value);
        var->begin = t;
        return var;
    }

    
    if (strcmp(t.value, "(") == 0) {
        int is_cast = 0;
        char cast_type[80] = "";

        if (peek(0).type == TOK_TYPE ||
            (peek(0).type == TOK_IDENT && class_lookup(peek(0).value)))
        {
            int star_offset = 1;
            while (strcmp(peek(star_offset).value, "*") == 0)
                    star_offset++;
            
            if (strcmp(peek(star_offset).value, ")") == 0) {
                is_cast = 1;
                strncpy(cast_type, peek(0).value, 79);
                advance();
                
                while (strcmp(peek(0).value, "*") == 0) {
                    strncat(cast_type, "*", sizeof(cast_type) - strlen(cast_type) - 1);
                    advance();
                }
                advance();
            }
        }

        if (is_cast) {
            Node *operand = parse_expression(PREC_UNARY);
            Node *cast = make_node(NODE_CAST, cast_type);
            if (operand) {
                vn_push_back(cast->childs, *operand);
                free(operand);
            }
            cast->begin = t;
            return cast;
        }

        
        Node *inner = parse_expression(PREC_NONE);
        if (strcmp(advance().value, ")") != 0) {
            error("Error: expected ')'\n");
        }
        inner->begin = t;
        return inner;
    }

    error("Error: unexpected token in expression '%s'\n", t.value);
    return NULL;
}


static Node *parse_infix(Node *left, Token op) {
    
    if (strcmp(op.value, "(") == 0) {
        Node *call = make_node(NODE_FUNC_CALL, left->str);

        
        Node *name_node = make_node(NODE_IDENT, left->str);
        vn_push_back(call->childs, *name_node); free(name_node);

        Node *args = make_node(NODE_ARGS, "");
        while (strcmp(peek(0).value, ")") != 0) {
            Node *arg = parse_expression(PREC_NONE);
            vn_push_back(args->childs, *arg); free(arg);
            if (peek(0).type == TOK_COMMA) advance();
            else break;
        }
        advance();
        vn_push_back(call->childs, *args); free(args);

        
        {
            const char *base = left->str;
            Node *args_in_call = &call->childs->data[1];
            const char *resolved = resolve_overload(base, args_in_call);
            if (resolved) {
                free(call->str);
                call->str = malloc(strlen(resolved) + 1);
                strcpy(call->str, resolved);
                free(call->childs->data[0].str);
                call->childs->data[0].str = malloc(strlen(resolved) + 1);
                strcpy(call->childs->data[0].str, resolved);
                debug("DEBUG parse_infix funcall: resolved '%s' -> '%s'\n", base, resolved);

                
                OverloadEntry *oe = NULL;
                for (int oi = 0; oi < overload_count; oi++) {
                    if (strcmp(overload_table[oi].mangled, resolved) == 0) {
                        oe = &overload_table[oi];
                        break;
                    }
                }
                if (!oe) {
                    for (int m = 0; m < num_imported_modules && !oe; m++) {
                        for (int oi = 0; oi < global_modules_counts[m]; oi++) {
                            if (strcmp(global_modules_overloads[m][oi].mangled, resolved) == 0) {
                                oe = &global_modules_overloads[m][oi];
                                break;
                            }
                        }
                    }
                }
                if (oe) {
                    Node *argsn = &call->childs->data[1];
                    for (int ai = 0; ai < (int)argsn->childs->size && ai < oe->param_count; ai++) {
                        const char *ptype = oe->param_types[ai];
                        Node *arg = &argsn->childs->data[ai];
                        if (needs_cast(arg, ptype)) {
                            Node *cast = make_cast_node(arg, ptype);
                            argsn->childs->data[ai] = *cast;
                            free(cast);
                        }
                    }
                }
            }
        }

        return call;
    }

    if (strcmp(op.value, "[") == 0) {
        Node *idx = parse_expression(PREC_NONE);
        if (strcmp(advance().value, "]") != 0) {
            error("Error: expected ']'\n");
        }
        Node *node = make_node(NODE_INDEX, "[]");
        node->begin = op;
        vn_push_back(node->childs, *left);
        vn_push_back(node->childs, *idx);
        free(idx);
        return node;
    }

    int precedence = get_precedence(op);
    Node *right = parse_expression(precedence);

    int is_arith = (strcmp(op.value, "+")  == 0 || strcmp(op.value, "-")  == 0 ||
                    strcmp(op.value, "*")  == 0 || strcmp(op.value, "/")  == 0 ||
                    strcmp(op.value, "%")  == 0 ||
                    strcmp(op.value, "<")  == 0 || strcmp(op.value, ">")  == 0 ||
                    strcmp(op.value, "<=") == 0 || strcmp(op.value, ">=") == 0 ||
                    strcmp(op.value, "==") == 0 || strcmp(op.value, "!=") == 0);
    if (is_arith && left && right) {
        char lt[64] = "", rt[64] = "";
        int lk = infer_expr_type_str(left,  lt, sizeof(lt));
        int rk = infer_expr_type_str(right, rt, sizeof(rt));
        if (lk && rk && strcmp(lt, rt) != 0) {
            const char *wider = promote_type(lt, rt);
            if (wider) {
                
                if (type_rank(wider) >= 1) {
                    if (strcmp(lt, wider) != 0) {
                        
                        Node *cast = make_cast_node(left, wider);
                        free(left);
                        left = cast;
                    } else {
                        
                        Node *cast = make_cast_node(right, wider);
                        free(right);
                        right = cast;
                    }
                }
            }
        }
    }

    Node *node = make_node(NODE_BINOP, op.value);
    node->begin = op;
    vn_push_back(node->childs, *left);
    vn_push_back(node->childs, *right);
    free(right);

    return node;
}

static Node *parse_expression(int precedence) {
    Node *left = parse_prefix();
    if (!left) return NULL;
    left->begin = peek(0);

    while (precedence < get_precedence(peek(0))) {
        Token op = advance();
        left = parse_infix(left, op);
    }

    return left;
}

Node *parseExpr(void) {
    return parse_expression(PREC_NONE);
}