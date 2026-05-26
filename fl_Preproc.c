#include "fl_Preproc.h"
#include "utils/elements.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#ifdef _WIN32
  #include <windows.h>
  #define realpath(p,r) _fullpath(r,p,512)
#else
  #include <stdlib.h>
#endif


#define MAX_DEFINES      64
#define MAX_INCLUDED     64
#define OUTPUT_SIZE      (1024 * 1024 * 4)
#define MAX_IMPORTS      16
#define MAX_IF_DEPTH     64
#define MAX_MACRO_PARAMS 32
#define MAX_EXPAND_DEPTH 64


int compile_file(const char *input_file, const char *link_flags,
                 const char *target, int is_import, void *sym_buffer, int *sym_c, void* overls, int* overls_c);


static char (*g_import_objects)[512] = NULL;
static const char **g_import_ptrs   = NULL;
static int  g_import_count          = 0;
static char g_target[256]           = "";


const char **preprocess_get_imports(int *count) {
    *count = g_import_count;
    if (g_import_count == 0 || !g_import_objects) return NULL;
    for (int i = 0; i < g_import_count; i++)
        g_import_ptrs[i] = g_import_objects[i];
    return g_import_ptrs;
}

const char *preprocess_get_target(void) {
    return g_target[0] ? g_target : NULL;
}

void preprocess_set_target(const char *triple) {
    if (triple && triple[0]) {
        strncpy(g_target, triple, sizeof(g_target) - 1);
        g_target[sizeof(g_target) - 1] = '\0';
    } else {
        g_target[0] = '\0';
    }
}


static int triple_matches(const char *name) {
    if (!g_target[0]) return 0;
    if (strcmp(g_target, name) == 0) return 1;
    size_t nlen = strlen(name);
    if (strncmp(g_target, name, nlen) == 0 &&
        (g_target[nlen] == '-' || g_target[nlen] == '\0'))
        return 1;
    const char *p = g_target;
    while (*p) {
        if (strncmp(p, name, nlen) == 0 &&
            (p[nlen] == '-' || p[nlen] == '\0'))
            return 1;
        p = strchr(p, '-');
        if (!p) break;
        p++;
    }
    return 0;
}


typedef struct {
    int active;
    int done;
    int parent_active;
} IfFrame;


typedef struct {
    char  name[64];
    char  value[512];
    char  params[MAX_MACRO_PARAMS][64];
    int   param_count;
    int   is_variadic;
} Define;

typedef struct {
    char *out;
    size_t output_size;
    size_t out_i;
    Define *defines;
    size_t define_count;
    size_t max_defines;
    IfFrame *if_stack;
    size_t if_depth;
    size_t max_if_depth;
    char (*included_files)[512];
    size_t included_count;
    size_t max_includes;
    char (*pragma_once_files)[512];
    size_t pragma_once_count;
    size_t max_pragma_once;
    char current_file[512];
    int  current_line;
    int  counter;
} PreprocCtx;


static int currently_active(PreprocCtx *ctx) {
    for (size_t k = 0; k < ctx->if_depth; k++)
        if (!ctx->if_stack[k].active) return 0;
    return 1;
}

static int already_included(PreprocCtx *ctx, const char *canon_path) {
    for (size_t i = 0; i < ctx->included_count; i++)
        if (strcmp(ctx->included_files[i], canon_path) == 0)
            return 1;
    return 0;
}

static int pragma_once_included(PreprocCtx *ctx, const char *canon_path) {
    for (size_t i = 0; i < ctx->pragma_once_count; i++)
        if (strcmp(ctx->pragma_once_files[i], canon_path) == 0)
            return 1;
    return 0;
}

static void mark_pragma_once(PreprocCtx *ctx, const char *canon_path) {
    if (ctx->pragma_once_count >= ctx->max_pragma_once) {
        ctx->max_pragma_once *= 2;
        char (*tmp)[512] = realloc(ctx->pragma_once_files, ctx->max_pragma_once * 512);
        if (!tmp) { error("preprocessor: out of memory\n");  }
        ctx->pragma_once_files = tmp;
    }
    strncpy(ctx->pragma_once_files[ctx->pragma_once_count++], canon_path, 511);
    ctx->pragma_once_files[ctx->pragma_once_count - 1][511] = '\0';
}

static void mark_included(PreprocCtx *ctx, const char *canon_path) {
    if (ctx->included_count >= ctx->max_includes) {
        ctx->max_includes *= 2;
        char (*tmp)[512] = realloc(ctx->included_files, ctx->max_includes * 512);
        if (!tmp) { error("preprocessor: out of memory\n");  }
        ctx->included_files = tmp;
    }
    strncpy(ctx->included_files[ctx->included_count++], canon_path, 511);
    ctx->included_files[ctx->included_count - 1][511] = '\0';
}


static void define_push(PreprocCtx *ctx, const char *name, const char *value,
                         char params[][64], int param_count, int is_variadic) {
    
    for (size_t i = 0; i < ctx->define_count; i++) {
        if (strcmp(ctx->defines[i].name, name) == 0) {
            strncpy(ctx->defines[i].value, value, sizeof(ctx->defines[i].value) - 1);
            ctx->defines[i].param_count = param_count;
            ctx->defines[i].is_variadic = is_variadic;
            if (param_count > 0 && params) {
                for (int j = 0; j < param_count && j < MAX_MACRO_PARAMS; j++)
                    strncpy(ctx->defines[i].params[j], params[j], 63);
            }
            return;
        }
    }
    if (ctx->define_count >= ctx->max_defines) {
        ctx->max_defines *= 2;
        Define *tmp = realloc(ctx->defines, ctx->max_defines * sizeof(Define));
        if (!tmp) { error("preprocessor: define table overflow\n");  }
        ctx->defines = tmp;
    }
    Define *d = &ctx->defines[ctx->define_count++];
    memset(d, 0, sizeof(Define));
    strncpy(d->name,  name,  63);
    strncpy(d->value, value, sizeof(d->value) - 1);
    d->param_count = param_count;
    d->is_variadic = is_variadic;
    if (param_count > 0 && params) {
        for (int j = 0; j < param_count && j < MAX_MACRO_PARAMS; j++)
            strncpy(d->params[j], params[j], 63);
    }
}

static void define_simple(PreprocCtx *ctx, const char *name, const char *value) {
    define_push(ctx, name, value, NULL, -1, 0);
}

static void define_remove(PreprocCtx *ctx, const char *name) {
    for (size_t i = 0; i < ctx->define_count; i++) {
        if (strcmp(ctx->defines[i].name, name) == 0) {
            for (size_t j = i; j < ctx->define_count - 1; j++)
                ctx->defines[j] = ctx->defines[j + 1];
            ctx->define_count--;
            return;
        }
    }
}

static Define *define_lookup(PreprocCtx *ctx, const char *name) {
    for (int i = (int)ctx->define_count - 1; i >= 0; i--)
        if (strcmp(ctx->defines[i].name, name) == 0)
            return &ctx->defines[i];
    return NULL;
}

static int define_defined(PreprocCtx *ctx, const char *name) {
    return define_lookup(ctx, name) != NULL;
}


static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { error("preprocessor: cannot open '%s'\n", path);  }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);
    char *buf = malloc(size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, size, f);
    buf[rd] = '\0';
    fclose(f);
    return buf;
}


static void inject_target_defines(PreprocCtx *ctx) {
    if (!g_target[0]) return;
    char buf[256];
    strncpy(buf, g_target, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char *saveptr = NULL;
#ifdef _WIN32
    char *tok = strtok_s(buf, "-", &saveptr);
#else
    char *tok = strtok_r(buf, "-", &saveptr);
#endif
    while (tok) {
        define_simple(ctx, tok, tok);
#ifdef _WIN32
        tok = strtok_s(NULL, "-", &saveptr);
#else
        tok = strtok_r(NULL, "-", &saveptr);
#endif
    }
    define_simple(ctx, g_target, g_target);
}

static char *expand_macros(PreprocCtx *ctx, const char *text, int depth);

static int parse_macro_args(const char *src, size_t *pos, int src_len,
                             char args[][512], int max_args) {
    int count = 0;
    
    if (src[*pos] != '(') return 0;
    (*pos)++;

    int depth = 0;
    int buf_i = 0;
    char buf[512];

    while (*pos < (size_t)src_len) {
        char c = src[*pos];
        if (c == '(' ) depth++;
        if (c == ')' ) {
            if (depth == 0) {
                buf[buf_i] = '\0';
                
                int s = 0; while (buf[s] == ' ' || buf[s] == '\t') s++;
                int e = buf_i - 1; while (e >= s && (buf[e] == ' ' || buf[e] == '\t')) e--;
                buf[e + 1] = '\0';
                if (count < max_args) strncpy(args[count], buf + s, 511);
                count++;
                (*pos)++;
                break;
            }
            depth--;
        }
        if (c == ',' && depth == 0) {
            buf[buf_i] = '\0';
            int s = 0; while (buf[s] == ' ' || buf[s] == '\t') s++;
            int e = buf_i - 1; while (e >= s && (buf[e] == ' ' || buf[e] == '\t')) e--;
            buf[e + 1] = '\0';
            if (count < max_args) strncpy(args[count], buf + s, 511);
            count++;
            buf_i = 0;
            (*pos)++;
            continue;
        }
        if (buf_i < 511) buf[buf_i++] = c;
        (*pos)++;
    }
    return count;
}


static char *substitute_params(const char *body, char params[][64], char args[][512],
                                int param_count, int is_variadic, char va_args_val[][512],
                                int va_count) {
    static char result[4096];
    int ri = 0;
    int bi = 0;
    int blen = (int)strlen(body);

    while (bi < blen && ri < (int)sizeof(result) - 1) {
        
        if (body[bi] == '#' && bi + 1 < blen && body[bi + 1] == '#') {
            
            while (ri > 0 && (result[ri-1] == ' ' || result[ri-1] == '\t')) ri--;
            bi += 2;
            
            while (bi < blen && (body[bi] == ' ' || body[bi] == '\t')) bi++;
            continue;
        }

        
        if (body[bi] == '#' && bi + 1 < blen && body[bi + 1] != '#') {
            bi++;
            while (bi < blen && (body[bi] == ' ' || body[bi] == '\t')) bi++;
            
            char pname[64]; int pi = 0;
            while (bi < blen && (isalnum((unsigned char)body[bi]) || body[bi] == '_'))
                pname[pi++] = body[bi++];
            pname[pi] = '\0';

            
            const char *arg_val = pname;
            for (int p = 0; p < param_count; p++) {
                if (strcmp(params[p], pname) == 0) {
                    arg_val = args[p];
                    break;
                }
            }
            
            result[ri++] = '"';
            for (const char *av = arg_val; *av && ri < (int)sizeof(result) - 3; av++) {
                if (*av == '"' || *av == '\\') result[ri++] = '\\';
                result[ri++] = *av;
            }
            result[ri++] = '"';
            continue;
        }

        
        if (isalpha((unsigned char)body[bi]) || body[bi] == '_') {
            char word[64]; int wi = 0;
            while (bi < blen && (isalnum((unsigned char)body[bi]) || body[bi] == '_'))
                word[wi++] = body[bi++];
            word[wi] = '\0';

            
            if (strcmp(word, "__VA_ARGS__") == 0 && is_variadic) {
                for (int v = 0; v < va_count; v++) {
                    if (v > 0 && ri < (int)sizeof(result) - 3) { result[ri++] = ','; result[ri++] = ' '; }
                    const char *vv = va_args_val[v];
                    while (*vv && ri < (int)sizeof(result) - 1) result[ri++] = *vv++;
                }
                continue;
            }

            int found = 0;
            for (int p = 0; p < param_count; p++) {
                if (strcmp(params[p], word) == 0) {
                    const char *av = args[p];
                    while (*av && ri < (int)sizeof(result) - 1) result[ri++] = *av++;
                    found = 1;
                    break;
                }
            }
            if (!found) {
                int wlen = (int)strlen(word);
                if (ri + wlen < (int)sizeof(result) - 1) {
                    memcpy(result + ri, word, wlen);
                    ri += wlen;
                }
            }
            continue;
        }

        result[ri++] = body[bi++];
    }
    result[ri] = '\0';
    return result;
}


static char *expand_macros(PreprocCtx *ctx, const char *text, int depth) {
    if (depth > MAX_EXPAND_DEPTH) return strdup(text);

    size_t   tlen = (int)strlen(text);
    size_t   ri   = 0;
    size_t   ti   = 0;
    
    size_t result_cap = tlen * 2 + 256;
    char  *result     = malloc(result_cap);
    if (!result) return strdup(text);

#define ROUT(ch) do { \
    if ((size_t)ri >= result_cap - 1) { \
        result_cap *= 2; \
        char *tmp = realloc(result, result_cap); \
        if (!tmp) { free(result); return strdup(text); } \
        result = tmp; \
    } \
    result[ri++] = (ch); \
} while(0)

    while (ti < tlen) {
        
        if (text[ti] == '"') {
            ROUT(text[ti++]);
            while (ti < tlen && text[ti] != '"') {
                if (text[ti] == '\\' && ti + 1 < tlen) ROUT(text[ti++]);
                ROUT(text[ti++]);
            }
            if (ti < tlen) ROUT(text[ti++]);
            continue;
        }
        if (text[ti] == '\'') {
            ROUT(text[ti++]);
            while (ti < tlen && text[ti] != '\'') {
                if (text[ti] == '\\' && ti + 1 < tlen) ROUT(text[ti++]);
                ROUT(text[ti++]);
            }
            if (ti < tlen) ROUT(text[ti++]);
            continue;
        }

        if (isalpha((unsigned char)text[ti]) || text[ti] == '_') {
            char word[64]; int wi = 0;
            int  word_start = ti;
            while (ti < tlen && (isalnum((unsigned char)text[ti]) || text[ti] == '_'))
                word[wi++] = text[ti++];
            word[wi] = '\0';

            Define *def = define_lookup(ctx, word);
            if (!def) {
                
                if ((ri + wi) >= result_cap - 1) {
                    result_cap = (result_cap * 2 > ri + wi + 256) ? result_cap * 2 : (ri + wi + 256);
                    char *tmp = realloc(result, result_cap);
                    if (!tmp) { free(result); return strdup(text); }
                    result = tmp;
                }
                memcpy(result + ri, word, wi);
                ri += wi;
                continue;
            }

            
            if (def->param_count < 0) {
                char *expanded = expand_macros(ctx, def->value, depth + 1);
                size_t elen = strlen(expanded);
                while ((size_t)(ri + elen + 1) >= result_cap) {
                    result_cap *= 2;
                    char *tmp = realloc(result, result_cap);
                    if (!tmp) { free(result); free(expanded); return strdup(text); }
                    result = tmp;
                }
                memcpy(result + ri, expanded, elen);
                ri += (int)elen;
                free(expanded);
                continue;
            }

            
            int saved_ti = ti;
            while (ti < tlen && (text[ti] == ' ' || text[ti] == '\t')) ti++;
            if (ti >= tlen || text[ti] != '(') {
                
                memcpy(result + ri, word, wi);
                ri += wi;
                if ((size_t)ri >= result_cap - 1) {
                    result_cap *= 2;
                    char *tmp = realloc(result, result_cap);
                    if (!tmp) { free(result); return strdup(text); }
                    result = tmp;
                }
                ti = saved_ti;
                (void)word_start;
                continue;
            }

            
            char args[MAX_MACRO_PARAMS + 8][512];
            memset(args, 0, sizeof(args));
            int arg_count = parse_macro_args(text, &ti, tlen, args, MAX_MACRO_PARAMS + 8);

            
            char va_args[MAX_MACRO_PARAMS][512];
            int  va_count = 0;
            memset(va_args, 0, sizeof(va_args));
            int normal_params = def->param_count;
            if (def->is_variadic && arg_count > normal_params) {
                va_count = arg_count - normal_params;
                for (int v = 0; v < va_count && v < MAX_MACRO_PARAMS; v++)
                    strncpy(va_args[v], args[normal_params + v], 511);
            }

            
            char *substituted = substitute_params(def->value,
                def->params, args, normal_params,
                def->is_variadic, va_args, va_count);

            char *expanded = expand_macros(ctx, substituted, depth + 1);
            size_t elen = strlen(expanded);
            while ((size_t)(ri + elen + 1) >= result_cap) {
                result_cap *= 2;
                char *tmp = realloc(result, result_cap);
                if (!tmp) { free(result); free(expanded); return strdup(text); }
                result = tmp;
            }
            memcpy(result + ri, expanded, elen);
            ri += (int)elen;
            free(expanded);
            continue;
        }

        ROUT(text[ti++]);
    }

    result[ri] = '\0';
#undef ROUT
    return result;
}

typedef struct { const char *s; int pos; } ExprParser;

static long long eval_expr(PreprocCtx *ctx, ExprParser *ep);

static void expr_skip_ws(ExprParser *ep) {
    while (ep->s[ep->pos] == ' ' || ep->s[ep->pos] == '\t') ep->pos++;
}

static long long eval_primary(PreprocCtx *ctx, ExprParser *ep) {
    expr_skip_ws(ep);
    char c = ep->s[ep->pos];

    
    if (c == '!') { ep->pos++; return !eval_primary(ctx, ep); }
    if (c == '~') { ep->pos++; return ~eval_primary(ctx, ep); }
    if (c == '-') { ep->pos++; return -eval_primary(ctx, ep); }
    if (c == '+') { ep->pos++; return  eval_primary(ctx, ep); }

    
    if (c == '(') {
        ep->pos++;
        long long v = eval_expr(ctx, ep);
        expr_skip_ws(ep);
        if (ep->s[ep->pos] == ')') ep->pos++;
        return v;
    }

    
    if (isdigit((unsigned char)c)) {
        long long v = 0;
        if (c == '0' && (ep->s[ep->pos+1] == 'x' || ep->s[ep->pos+1] == 'X')) {
            ep->pos += 2;
            while (isxdigit((unsigned char)ep->s[ep->pos])) {
                char hc = ep->s[ep->pos++];
                v = v * 16 + (isdigit((unsigned char)hc) ? hc - '0' :
                              tolower((unsigned char)hc) - 'a' + 10);
            }
        } else {
            while (isdigit((unsigned char)ep->s[ep->pos]))
                v = v * 10 + (ep->s[ep->pos++] - '0');
        }
        
        while (ep->s[ep->pos] == 'L' || ep->s[ep->pos] == 'l' ||
               ep->s[ep->pos] == 'U' || ep->s[ep->pos] == 'u')
            ep->pos++;
        return v;
    }

    
    if (isalpha((unsigned char)c) || c == '_') {
        char word[64]; int wi = 0;
        while (isalnum((unsigned char)ep->s[ep->pos]) || ep->s[ep->pos] == '_')
            word[wi++] = ep->s[ep->pos++];
        word[wi] = '\0';

        if (strcmp(word, "defined") == 0) {
            expr_skip_ws(ep);
            int paren = (ep->s[ep->pos] == '(');
            if (paren) ep->pos++;
            expr_skip_ws(ep);
            char name[64]; int ni = 0;
            while (isalnum((unsigned char)ep->s[ep->pos]) || ep->s[ep->pos] == '_')
                name[ni++] = ep->s[ep->pos++];
            name[ni] = '\0';
            if (paren) { expr_skip_ws(ep); if (ep->s[ep->pos] == ')') ep->pos++; }
            return define_defined(ctx, name) ? 1 : 0;
        }

        
        Define *def = define_lookup(ctx, word);
        if (def && def->param_count < 0) {
            ExprParser inner = { def->value, 0 };
            return eval_expr(ctx, &inner);
        }
        return 0;
    }

    ep->pos++;
    return 0;
}

static long long eval_mul(PreprocCtx *ctx, ExprParser *ep) {
    long long v = eval_primary(ctx, ep);
    expr_skip_ws(ep);
    while (ep->s[ep->pos] == '*' || ep->s[ep->pos] == '/' || ep->s[ep->pos] == '%') {
        char op = ep->s[ep->pos++];
        long long r = eval_primary(ctx, ep);
        if (op == '*') v *= r;
        else if (op == '/') v = r ? v / r : 0;
        else v = r ? v % r : 0;
        expr_skip_ws(ep);
    }
    return v;
}

static long long eval_add(PreprocCtx *ctx, ExprParser *ep) {
    long long v = eval_mul(ctx, ep);
    expr_skip_ws(ep);
    while (ep->s[ep->pos] == '+' || ep->s[ep->pos] == '-') {
        char op = ep->s[ep->pos++];
        long long r = eval_mul(ctx, ep);
        v = (op == '+') ? v + r : v - r;
        expr_skip_ws(ep);
    }
    return v;
}

static long long eval_shift(PreprocCtx *ctx, ExprParser *ep) {
    long long v = eval_add(ctx, ep);
    expr_skip_ws(ep);
    while ((ep->s[ep->pos] == '<' && ep->s[ep->pos+1] == '<') ||
           (ep->s[ep->pos] == '>' && ep->s[ep->pos+1] == '>')) {
        int left = (ep->s[ep->pos] == '<');
        ep->pos += 2;
        long long r = eval_add(ctx, ep);
        v = left ? (v << r) : (v >> r);
        expr_skip_ws(ep);
    }
    return v;
}

static long long eval_relational(PreprocCtx *ctx, ExprParser *ep) {
    long long v = eval_shift(ctx, ep);
    expr_skip_ws(ep);
    for (;;) {
        if (ep->s[ep->pos] == '<' && ep->s[ep->pos+1] == '=') { ep->pos += 2; v = v <= eval_shift(ctx, ep); }
        else if (ep->s[ep->pos] == '>' && ep->s[ep->pos+1] == '=') { ep->pos += 2; v = v >= eval_shift(ctx, ep); }
        else if (ep->s[ep->pos] == '<' && ep->s[ep->pos+1] != '<') { ep->pos++; v = v < eval_shift(ctx, ep); }
        else if (ep->s[ep->pos] == '>' && ep->s[ep->pos+1] != '>') { ep->pos++; v = v > eval_shift(ctx, ep); }
        else break;
        expr_skip_ws(ep);
    }
    return v;
}

static long long eval_equality(PreprocCtx *ctx, ExprParser *ep) {
    long long v = eval_relational(ctx, ep);
    expr_skip_ws(ep);
    while ((ep->s[ep->pos] == '=' && ep->s[ep->pos+1] == '=') ||
           (ep->s[ep->pos] == '!' && ep->s[ep->pos+1] == '=')) {
        int eq = (ep->s[ep->pos] == '=');
        ep->pos += 2;
        long long r = eval_relational(ctx, ep);
        v = eq ? (v == r) : (v != r);
        expr_skip_ws(ep);
    }
    return v;
}

static long long eval_bitand(PreprocCtx *ctx, ExprParser *ep) {
    long long v = eval_equality(ctx, ep);
    expr_skip_ws(ep);
    while (ep->s[ep->pos] == '&' && ep->s[ep->pos+1] != '&') { ep->pos++; v &= eval_equality(ctx, ep); expr_skip_ws(ep); }
    return v;
}

static long long eval_bitxor(PreprocCtx *ctx, ExprParser *ep) {
    long long v = eval_bitand(ctx, ep);
    expr_skip_ws(ep);
    while (ep->s[ep->pos] == '^') { ep->pos++; v ^= eval_bitand(ctx, ep); expr_skip_ws(ep); }
    return v;
}

static long long eval_bitor(PreprocCtx *ctx, ExprParser *ep) {
    long long v = eval_bitxor(ctx, ep);
    expr_skip_ws(ep);
    while (ep->s[ep->pos] == '|' && ep->s[ep->pos+1] != '|') { ep->pos++; v |= eval_bitxor(ctx, ep); expr_skip_ws(ep); }
    return v;
}

static long long eval_logand(PreprocCtx *ctx, ExprParser *ep) {
    long long v = eval_bitor(ctx, ep);
    expr_skip_ws(ep);
    while (ep->s[ep->pos] == '&' && ep->s[ep->pos+1] == '&') { ep->pos += 2; long long r = eval_bitor(ctx, ep); v = v && r; expr_skip_ws(ep); }
    return v;
}

static long long eval_logor(PreprocCtx *ctx, ExprParser *ep) {
    long long v = eval_logand(ctx, ep);
    expr_skip_ws(ep);
    while (ep->s[ep->pos] == '|' && ep->s[ep->pos+1] == '|') { ep->pos += 2; long long r = eval_logand(ctx, ep); v = v || r; expr_skip_ws(ep); }
    return v;
}


static long long eval_expr(PreprocCtx *ctx, ExprParser *ep) {
    long long v = eval_logor(ctx, ep);
    expr_skip_ws(ep);
    if (ep->s[ep->pos] == '?') {
        ep->pos++;
        long long t = eval_expr(ctx, ep);
        expr_skip_ws(ep);
        if (ep->s[ep->pos] == ':') ep->pos++;
        long long f = eval_expr(ctx, ep);
        return v ? t : f;
    }
    return v;
}

static long long eval_if_expr(PreprocCtx *ctx, const char *expr) {
    
    char *expanded = expand_macros(ctx, expr, 0);
    ExprParser ep = { expanded, 0 };
    long long result = eval_expr(ctx, &ep);
    free(expanded);
    return result;
}

static void if_push(PreprocCtx *ctx, int cond) {
    if (ctx->if_depth >= ctx->max_if_depth) {
        ctx->max_if_depth *= 2;
        IfFrame *tmp = realloc(ctx->if_stack, sizeof(IfFrame) * ctx->max_if_depth);
        if (!tmp) { error("preprocessor: out of memory in #if stack\n");  }
        ctx->if_stack = tmp;
    }
    int parent = currently_active(ctx);
    ctx->if_stack[ctx->if_depth].active        = parent && cond;
    ctx->if_stack[ctx->if_depth].done          = parent && cond;
    ctx->if_stack[ctx->if_depth].parent_active = parent;
    ctx->if_depth++;
}

static int preprocess_internal(PreprocCtx *ctx, const char *source, const char *base_dir) {
    int src_i   = 0;
    int src_len = (int)strlen(source);

#define OUT_PUSH(ch) do { \
    if (ctx->out_i >= ctx->output_size - 1) { \
        ctx->output_size *= 2; \
        char *re_out = realloc(ctx->out, ctx->output_size); \
        if (!re_out) { error("preprocessor: output buffer overflow\n");  } \
        ctx->out = re_out; \
    } \
    ctx->out[(ctx->out_i)++] = (ch); \
} while(0)

#define OUT_STR(s) do { \
    const char *_s = (s); \
    while (*_s) OUT_PUSH(*_s++); \
} while(0)

#define STR_PUSH(buf, idx, maxlen, ch) do { \
    if ((idx) < (maxlen) - 1) { (buf)[(idx)++] = (ch); } \
} while(0)

    

    while (src_i < src_len) {

        
        if (source[src_i] == '/' && src_i + 1 < src_len && source[src_i+1] == '/') {
            
            if (currently_active(ctx)) {
                while (src_i < src_len && source[src_i] != '\n')
                    OUT_PUSH(source[src_i++]);
            } else {
                while (src_i < src_len && source[src_i] != '\n') src_i++;
            }
            continue;
        }

        
        if (source[src_i] == '/' && src_i + 1 < src_len && source[src_i+1] == '*') {
            src_i += 2;
            while (src_i < src_len) {
                if (source[src_i] == '\n') { ctx->current_line++; if (currently_active(ctx)) OUT_PUSH('\n'); }
                if (source[src_i] == '*' && src_i + 1 < src_len && source[src_i+1] == '/') {
                    src_i += 2; break;
                }
                src_i++;
            }
            continue;
        }

        
        if (source[src_i] == '"') {
            if (currently_active(ctx)) {
                OUT_PUSH(source[src_i++]);
                while (src_i < src_len && source[src_i] != '"') {
                    if (source[src_i] == '\\' && src_i + 1 < src_len) OUT_PUSH(source[src_i++]);
                    OUT_PUSH(source[src_i++]);
                }
                if (src_i < src_len) OUT_PUSH(source[src_i++]);
            } else {
                src_i++;
                while (src_i < src_len && source[src_i] != '"') {
                    if (source[src_i] == '\\' && src_i + 1 < src_len) src_i++;
                    src_i++;
                }
                if (src_i < src_len) src_i++;
            }
            continue;
        }

        
        if (source[src_i] == '\'') {
            if (currently_active(ctx)) {
                OUT_PUSH(source[src_i++]);
                while (src_i < src_len && source[src_i] != '\'') {
                    if (source[src_i] == '\\' && src_i + 1 < src_len) OUT_PUSH(source[src_i++]);
                    OUT_PUSH(source[src_i++]);
                }
                if (src_i < src_len) OUT_PUSH(source[src_i++]);
            } else {
                src_i++;
                while (src_i < src_len && source[src_i] != '\'') {
                    if (source[src_i] == '\\' && src_i + 1 < src_len) src_i++;
                    src_i++;
                }
                if (src_i < src_len) src_i++;
            }
            continue;
        }

        
        if (source[src_i] == '\\' && src_i + 1 < src_len && source[src_i+1] == '\n') {
            src_i += 2;
            ctx->current_line++;
            continue;
        }

        
        if (source[src_i] == '#') {
            src_i++;
            while (src_i < src_len && (source[src_i] == ' ' || source[src_i] == '\t')) src_i++;

            char directive[32] = {0}; int di = 0;
            while (src_i < src_len && source[src_i] != ' ' && source[src_i] != '\t' && source[src_i] != '\n') {
                STR_PUSH(directive, di, (int)sizeof(directive), source[src_i]);
                src_i++;
            }
            directive[di] = '\0';
            while (src_i < src_len && (source[src_i] == ' ' || source[src_i] == '\t')) src_i++;

            
            if (strcmp(directive, "if") == 0) {
                char expr[512] = {0}; int ei = 0;
                while (src_i < src_len && source[src_i] != '\n')
                    STR_PUSH(expr, ei, (int)sizeof(expr), source[src_i++]);
                expr[ei] = '\0';
                long long cond = currently_active(ctx) ? eval_if_expr(ctx, expr) : 0;
                if_push(ctx, cond != 0);
            }
            
            else if (strcmp(directive, "ifdef") == 0) {
                char name[256] = {0}; int ni = 0;
                while (src_i < src_len && source[src_i] != ' ' && source[src_i] != '\t' && source[src_i] != '\n')
                    STR_PUSH(name, ni, (int)sizeof(name), source[src_i++]);
                name[ni] = '\0';
                int cond = (strchr(name, '-') || strchr(name, '.'))
                           ? triple_matches(name) : define_defined(ctx, name);
                if_push(ctx, cond);
            }
            
            else if (strcmp(directive, "ifndef") == 0) {
                char name[256] = {0}; int ni = 0;
                while (src_i < src_len && source[src_i] != ' ' && source[src_i] != '\t' && source[src_i] != '\n')
                    STR_PUSH(name, ni, (int)sizeof(name), source[src_i++]);
                name[ni] = '\0';
                int cond = (strchr(name, '-') || strchr(name, '.'))
                           ? !triple_matches(name) : !define_defined(ctx, name);
                if_push(ctx, cond);
            }
            
            else if (strcmp(directive, "elif") == 0) {
                char expr[512] = {0}; int ei = 0;
                while (src_i < src_len && source[src_i] != '\n')
                    STR_PUSH(expr, ei, (int)sizeof(expr), source[src_i++]);
                expr[ei] = '\0';
                if (ctx->if_depth == 0) {
                    error("preprocessor: #elif without #if at line %d\n", ctx->current_line);
                } else {
                    IfFrame *fr = &ctx->if_stack[ctx->if_depth - 1];
                    if (!fr->done && fr->parent_active) {
                        long long cond = eval_if_expr(ctx, expr);
                        fr->active = (cond != 0);
                        if (fr->active) fr->done = 1;
                    } else {
                        fr->active = 0;
                    }
                }
            }
            
            else if (strcmp(directive, "else") == 0) {
                if (ctx->if_depth == 0) {
                    error("preprocessor: #else without #if at line %d\n", ctx->current_line);
                    
                } else {
                    IfFrame *fr = &ctx->if_stack[ctx->if_depth - 1];
                    fr->active = fr->parent_active && !fr->done;
                    if (fr->active) fr->done = 1;
                }
            }
            
            else if (strcmp(directive, "endif") == 0) {
                if (ctx->if_depth == 0) {
                    error("preprocessor: #endif without #if at line %d\n", ctx->current_line);
                    
                } else
                    ctx->if_depth--;
            }
            
            else if (strcmp(directive, "define") == 0) {
                if (!currently_active(ctx)) goto skip_newline;

                
                char name[64] = {0}; int ni = 0;
                while (src_i < src_len && (isalnum((unsigned char)source[src_i]) || source[src_i] == '_'))
                    STR_PUSH(name, ni, (int)sizeof(name), source[src_i++]);
                name[ni] = '\0';

                char params[MAX_MACRO_PARAMS][64];
                int  param_count = -1;
                int  is_variadic = 0;

                
                if (src_i < src_len && source[src_i] == '(') {
                    src_i++;
                    param_count = 0;
                    while (src_i < src_len && source[src_i] != ')') {
                        while (src_i < src_len && (source[src_i] == ' ' || source[src_i] == '\t')) src_i++;
                        if (source[src_i] == ')') break;
                        
                        if (src_i + 2 < src_len && source[src_i] == '.' &&
                            source[src_i+1] == '.' && source[src_i+2] == '.') {
                            is_variadic = 1;
                            src_i += 3;
                            while (src_i < src_len && (source[src_i] == ' ' || source[src_i] == '\t')) src_i++;
                            break;
                        }
                        char pname[64] = {0}; int pi = 0;
                        while (src_i < src_len && (isalnum((unsigned char)source[src_i]) || source[src_i] == '_'))
                            STR_PUSH(pname, pi, (int)sizeof(pname), source[src_i++]);
                        pname[pi] = '\0';
                        if (pi > 0 && param_count < MAX_MACRO_PARAMS)
                            strncpy(params[param_count++], pname, 63);
                        while (src_i < src_len && (source[src_i] == ' ' || source[src_i] == '\t')) src_i++;
                        if (src_i < src_len && source[src_i] == ',') src_i++;
                    }
                    if (src_i < src_len && source[src_i] == ')') src_i++;
                }

                while (src_i < src_len && (source[src_i] == ' ' || source[src_i] == '\t')) src_i++;

                
                char value[512] = {0}; int vi = 0;
                while (src_i < src_len && source[src_i] != '\n') {
                    if (source[src_i] == '\\' && src_i + 1 < src_len && source[src_i+1] == '\n') {
                        src_i += 2;
                        ctx->current_line++;
                        
                        STR_PUSH(value, vi, (int)sizeof(value), ' ');
                        continue;
                    }
                    STR_PUSH(value, vi, (int)sizeof(value), source[src_i++]);
                }
                value[vi] = '\0';
                
                while (vi > 0 && (value[vi-1] == ' ' || value[vi-1] == '\t')) value[--vi] = '\0';

                
                if (strcmp(name, "__target__") == 0 && value[0] != '\0') {
                    if (g_target[0] == '\0') {
                        strncpy(g_target, value, sizeof(g_target) - 1);
                        g_target[sizeof(g_target) - 1] = '\0';
                        error("preprocessor: target triple = '%s'\n", g_target);
                        inject_target_defines(ctx);
                    }
                } else {
                    define_push(ctx, name, value, params, param_count, is_variadic);
                }
            }
            
            else if (strcmp(directive, "undef") == 0) {
                if (!currently_active(ctx)) goto skip_newline;
                char name[256] = {0}; int ni = 0;
                while (src_i < src_len && source[src_i] != ' ' && source[src_i] != '\t' && source[src_i] != '\n')
                    STR_PUSH(name, ni, (int)sizeof(name), source[src_i++]);
                name[ni] = '\0';
                define_remove(ctx, name);
            }
            
            else if (strcmp(directive, "include") == 0) {
                char filename[256] = {0}; int fi = 0;
                if (src_i >= src_len) goto skip_newline;
                char delim = source[src_i++];
                char end   = (delim == '"') ? '"' : '>';
                while (src_i < src_len && source[src_i] != end && source[src_i] != '\n')
                    STR_PUSH(filename, fi, (int)sizeof(filename), source[src_i++]);
                filename[fi] = '\0';
                if (src_i < src_len && source[src_i] == end) src_i++;

                if (!currently_active(ctx)) goto skip_newline;

                char path[512];
                if (base_dir && base_dir[0])
                    snprintf(path, sizeof(path), "%s/%s", base_dir, filename);
                else
                    snprintf(path, sizeof(path), "%s", filename);

                char canon[512] = {0};
                if (!realpath(path, canon))
                    strncpy(canon, path, sizeof(canon) - 1);

                
                if (pragma_once_included(ctx, canon)) goto skip_newline;

                if (!already_included(ctx, canon)) {
                    mark_included(ctx, canon);
                    char *included_src = read_file(path);
                    if (included_src) {
                        char new_base[512] = {0};
                        char canon_copy[512];
                        strncpy(canon_copy, canon, sizeof(canon_copy) - 1);
                        char *last_slash = strrchr(canon_copy, '/');
                        if (last_slash) { *last_slash = '\0'; strncpy(new_base, canon_copy, sizeof(new_base) - 1); }
                        else strncpy(new_base, ".", sizeof(new_base) - 1);

                        char saved_file[512];
                        int  saved_line = ctx->current_line;
                        strncpy(saved_file, ctx->current_file, sizeof(saved_file) - 1);

                        {
                            char lm[640];
                            snprintf(lm, sizeof(lm), "\n#line 1 \"%s\"\n", canon);
                            OUT_STR(lm);
                        }

                        strncpy(ctx->current_file, canon, sizeof(ctx->current_file) - 1);
                        ctx->current_line = 1;

                        int rc = preprocess_internal(ctx, included_src, new_base);
                        free(included_src);

                        strncpy(ctx->current_file, saved_file, sizeof(ctx->current_file) - 1);
                        ctx->current_line = saved_line;

                        if (rc < 0) return -1;

                        {
                            char lm[640];
                            snprintf(lm, sizeof(lm), "\n#line %d \"%s\"\n",
                                     saved_line, saved_file);
                            OUT_STR(lm);
                        }
                    }
                    else {
                        error("Error: cannot open file '%s'", path);
                    }
                }
                else {
                    warning("Warning: '%s' is already included", canon);
                }
            }
            
            else if (strcmp(directive, "import") == 0) {
                char filename[256] = {0}; int fi = 0;
                if (src_i >= src_len) goto skip_newline;
                char delim = source[src_i++];
                char end   = (delim == '"') ? '"' : '>';
                while (src_i < src_len && source[src_i] != end && source[src_i] != '\n')
                    STR_PUSH(filename, fi, (int)sizeof(filename), source[src_i++]);
                filename[fi] = '\0';
                if (src_i < src_len && source[src_i] == end) src_i++;

                if (!currently_active(ctx)) goto skip_newline;

                char src_path[512], obj_path[512];
                if (base_dir && base_dir[0]) {
                    snprintf(src_path, sizeof(src_path), "%s/%s", base_dir, filename);
                    snprintf(obj_path, sizeof(obj_path), "%s/%s.o", base_dir, filename);
                } else {
                    snprintf(src_path, sizeof(src_path), "%s", filename);
                    snprintf(obj_path, sizeof(obj_path), "%s.o", filename);
                }

                int already = 0;
                for (int ii = 0; ii < g_import_count; ii++)
                    if (strcmp(g_import_objects[ii], obj_path) == 0) { already = 1; break; }

                if (!already) {
                    char src_no_ext[512];
                    strncpy(src_no_ext, src_path, sizeof(src_no_ext) - 1);
                    char *dot = strrchr(src_no_ext, '.');
                    if (dot && strcmp(dot, ".fl") == 0) *dot = '\0';

                    int tmp = compile_file(src_no_ext, "", g_target, 1, NULL, NULL, NULL, NULL);
                    if (tmp != 0)
                        {error("preprocessor: #import: failed to compile '%s'\n", src_path); }
                    else if (g_import_count < MAX_IMPORTS)
                        strncpy(g_import_objects[g_import_count++], obj_path, sizeof(g_import_objects[0]) - 1);
                }
            }
            
            else if (strcmp(directive, "error") == 0) {
                char msg[256] = {0}; int mi = 0;
                while (src_i < src_len && source[src_i] != '\n')
                    STR_PUSH(msg, mi, (int)sizeof(msg), source[src_i++]);
                msg[mi] = '\0';
                if (currently_active(ctx)) {
                    error("preprocessor: error at line %d: %s\n", ctx->current_line, msg);
                    return -1;
                }
            }
            
            else if (strcmp(directive, "warning") == 0) {
                char msg[256] = {0}; int mi = 0;
                while (src_i < src_len && source[src_i] != '\n')
                    STR_PUSH(msg, mi, (int)sizeof(msg), source[src_i++]);
                msg[mi] = '\0';
                if (currently_active(ctx))
                    debug("preprocessor: warning at line %d: %s\n", ctx->current_line, msg);
            }
            
            else if (strcmp(directive, "line") == 0) {
                char rest[256] = {0}; int ri2 = 0;
                while (src_i < src_len && source[src_i] != '\n')
                    STR_PUSH(rest, ri2, (int)sizeof(rest), source[src_i++]);
                rest[ri2] = '\0';
                if (currently_active(ctx)) {
                    int new_line = atoi(rest);
                    if (new_line > 0) ctx->current_line = new_line - 1;
                    
                    char *q = strchr(rest, '"');
                    if (q) {
                        char *q2 = strchr(q + 1, '"');
                        if (q2) {
                            int flen = (int)(q2 - q - 1);
                            if (flen < (int)sizeof(ctx->current_file) - 1) {
                                memcpy(ctx->current_file, q + 1, flen);
                                ctx->current_file[flen] = '\0';
                            }
                        }
                    }
                }
            }
            
            else if (strcmp(directive, "pragma") == 0) {
                char pragma_arg[64] = {0}; int pi = 0;
                while (src_i < src_len && source[src_i] != '\n' && source[src_i] != ' ' && source[src_i] != '\t')
                    STR_PUSH(pragma_arg, pi, (int)sizeof(pragma_arg), source[src_i++]);
                pragma_arg[pi] = '\0';
                while (src_i < src_len && source[src_i] != '\n') src_i++;

                if (currently_active(ctx) && strcmp(pragma_arg, "once") == 0) {
                    
                    if (ctx->current_file[0]) {
                        char canon[512] = {0};
                        if (!realpath(ctx->current_file, canon))
                            strncpy(canon, ctx->current_file, sizeof(canon) - 1);
                        mark_pragma_once(ctx, canon);
                    }
                }
                
            }
            else {
                if (currently_active(ctx))
                    {error("preprocessor: unknown directive '#%s' at line %d\n",
                            directive, ctx->current_line); }
                while (src_i < src_len && source[src_i] != '\n') src_i++;
            }

            skip_newline:
            if (src_i < src_len && source[src_i] == '\n') {
                src_i++;
                ctx->current_line++;
            }
            continue;
        }

        
        if (!currently_active(ctx)) {
            if (source[src_i] == '\n') ctx->current_line++;
            src_i++;
            continue;
        }

        
        if (isalpha((unsigned char)source[src_i]) || source[src_i] == '_') {
            
            char word[64]; int wi = 0;
            int word_start = src_i;
            while (src_i < src_len && (isalnum((unsigned char)source[src_i]) || source[src_i] == '_'))
                word[wi++] = source[src_i++];
            word[wi] = '\0';

            
            if (strcmp(word, "__FILE__") == 0) {
                OUT_PUSH('"');
                for (const char *fp = ctx->current_file; *fp; fp++) {
                    if (*fp == '\\' || *fp == '"') OUT_PUSH('\\');
                    OUT_PUSH(*fp);
                }
                OUT_PUSH('"');
                continue;
            }
            if (strcmp(word, "__LINE__") == 0) {
                char lbuf[32]; snprintf(lbuf, sizeof(lbuf), "%d", ctx->current_line);
                OUT_STR(lbuf);
                continue;
            }
            if (strcmp(word, "__COUNTER__") == 0) {
                char lbuf[32]; snprintf(lbuf, sizeof(lbuf), "%d", ctx->counter++);
                OUT_STR(lbuf);
                continue;
            }
            if (strcmp(word, "__DATE__") == 0) {
                static char date_buf[32] = "";
                if (!date_buf[0]) {
                    time_t t = time(NULL);
                    struct tm *tm = localtime(&t);
                    const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                            "Jul","Aug","Sep","Oct","Nov","Dec"};
                    snprintf(date_buf, sizeof(date_buf), "\"%s %2d %4d\"",
                             months[tm->tm_mon], tm->tm_mday, 1900 + tm->tm_year);
                }
                OUT_STR(date_buf);
                continue;
            }
            if (strcmp(word, "__TIME__") == 0) {
                static char time_buf[32] = "";
                if (!time_buf[0]) {
                    time_t t = time(NULL);
                    struct tm *tm = localtime(&t);
                    snprintf(time_buf, sizeof(time_buf), "\"%02d:%02d:%02d\"",
                             tm->tm_hour, tm->tm_min, tm->tm_sec);
                }
                OUT_STR(time_buf);
                continue;
            }

            
            Define *def = define_lookup(ctx, word);
            if (!def) {
                
                memcpy(ctx->out + ctx->out_i, word, wi);
                ctx->out_i += wi;
                if (ctx->out_i >= ctx->output_size - 1) {
                    ctx->output_size *= 2;
                    char *tmp = realloc(ctx->out, ctx->output_size);
                    if (!tmp) { error("preprocessor: output buffer overflow\n");  }
                    ctx->out = tmp;
                }
                continue;
            }

            
            if (def->param_count < 0) {
                char *expanded = expand_macros(ctx, def->value, 0);
                OUT_STR(expanded);
                free(expanded);
                continue;
            }

            
            while (src_i < src_len && (source[src_i] == ' ' || source[src_i] == '\t')) src_i++;
            if (src_i >= src_len || source[src_i] != '(') {
                
                memcpy(ctx->out + ctx->out_i, word, wi);
                ctx->out_i += wi;
                if (ctx->out_i >= ctx->output_size - 1) {
                    ctx->output_size *= 2;
                    char *tmp = realloc(ctx->out, ctx->output_size);
                    if (!tmp) { error("preprocessor: output buffer overflow\n");  }
                    ctx->out = tmp;
                }
                src_i = word_start + wi;
                continue;
            }

            
            
            char call_buf[2048]; int cb_i = 0;
            int depth = 0;
            while (src_i < src_len && cb_i < (int)sizeof(call_buf) - 1) {
                char c2 = source[src_i];
                if (c2 == '(') depth++;
                if (c2 == ')') { depth--; if (depth == 0) { call_buf[cb_i++] = c2; src_i++; break; } }
                if (c2 == '\n') ctx->current_line++;
                call_buf[cb_i++] = c2;
                src_i++;
            }
            call_buf[cb_i] = '\0';

            
            char args[MAX_MACRO_PARAMS + 8][512];
            memset(args, 0, sizeof(args));
            size_t pos2 = 0;
            int arg_count = parse_macro_args(call_buf, &pos2, cb_i, args, MAX_MACRO_PARAMS + 8);

            
            char va_args[MAX_MACRO_PARAMS][512]; int va_count = 0;
            memset(va_args, 0, sizeof(va_args));
            int normal_params = def->param_count;
            if (def->is_variadic && arg_count > normal_params) {
                va_count = arg_count - normal_params;
                for (int v = 0; v < va_count && v < MAX_MACRO_PARAMS; v++)
                    strncpy(va_args[v], args[normal_params + v], 511);
            }

            char *substituted = substitute_params(def->value, def->params, args,
                                                   normal_params, def->is_variadic, va_args, va_count);
            char *expanded = expand_macros(ctx, substituted, 0);
            OUT_STR(expanded);
            free(expanded);
            continue;
        }

        
        if (source[src_i] == '\n') ctx->current_line++;
        OUT_PUSH(source[src_i++]);
    }

#undef OUT_PUSH
#undef OUT_STR
#undef STR_PUSH
    return 0;
}

char *preprocess(const char *source, const char *base_dir, const char *input_file) {
    
    if (!g_import_objects) {
        g_import_objects = malloc(MAX_IMPORTS * 512);
        g_import_ptrs    = malloc(MAX_IMPORTS * sizeof(const char *));
        g_import_count   = 0;
    }

    PreprocCtx ctx;
    memset(&ctx, 0, sizeof(ctx));

    ctx.max_defines      = MAX_DEFINES;
    ctx.max_includes     = MAX_INCLUDED;
    ctx.output_size      = OUTPUT_SIZE;
    ctx.max_if_depth     = MAX_IF_DEPTH;
    ctx.max_pragma_once  = MAX_INCLUDED;

    ctx.defines           = malloc(sizeof(Define) * MAX_DEFINES);
    ctx.included_files    = malloc(MAX_INCLUDED * 512);
    ctx.pragma_once_files = malloc(MAX_INCLUDED * 512);
    ctx.out               = malloc(OUTPUT_SIZE);
    ctx.if_stack          = malloc(sizeof(IfFrame) * MAX_IF_DEPTH);

    if (!ctx.out || !ctx.defines || !ctx.included_files || !ctx.if_stack || !ctx.pragma_once_files) {
        error("preprocessor: out of memory\n");
        
    }

    if (input_file && input_file[0]) {
        char canon[512] = {0};
        if (!realpath(input_file, canon))
            strncpy(canon, input_file, sizeof(canon) - 1);
        strncpy(ctx.current_file, canon, sizeof(ctx.current_file) - 1);
    } else {
        ctx.current_file[0] = '\0';
    }
    ctx.current_line = 1;

    define_simple(&ctx, "__FLAME__", VERSION);

    inject_target_defines(&ctx);

    
    if (ctx.current_file[0]) {
        char lm[640];
        snprintf(lm, sizeof(lm), "#line 1 \"%s\"\n", ctx.current_file);
        const char *p = lm;
        while (*p) {
            if (ctx.out_i >= ctx.output_size - 1) {
                ctx.output_size *= 2;
                char *tmp = realloc(ctx.out, ctx.output_size);
                if (!tmp) { error("preprocessor: out of memory\n"); }
                ctx.out = tmp;
            }
            ctx.out[ctx.out_i++] = *p++;
        }
    }

    int rc = preprocess_internal(&ctx, source, base_dir);

    if (ctx.if_depth > 0) {
        error("preprocessor: unclosed #if/#ifdef/#ifndef (%zu levels)\n", ctx.if_depth);
        
        rc = -1;
    }

    if (rc < 0) {
        free(ctx.out);
        free(ctx.defines);
        free(ctx.included_files);
        free(ctx.pragma_once_files);
        free(ctx.if_stack);
        return NULL;
    }

    ctx.out[ctx.out_i] = '\0';

    extern int D;
    if (D) {
        debug("\n--- PREPROCESSOR OUTPUT ---\n%s\n--- END PREPROCESSOR OUTPUT ---\n", ctx.out);
        fflush(stdout);
    }

    free(ctx.defines);
    free(ctx.included_files);
    free(ctx.pragma_once_files);
    free(ctx.if_stack);
    return ctx.out;
}