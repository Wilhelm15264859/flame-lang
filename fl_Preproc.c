#include "fl_Preproc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#ifdef _WIN32
  #include <windows.h>
  #define realpath(p,r) _fullpath(r,p,512)
#else
  #include <stdlib.h>
#endif

#define MAX_DEFINES  16
#define MAX_INCLUDED 16
#define OUTPUT_SIZE  (1024 * 1024 * 4)
#define MAX_IMPORTS 16
#define MAX_IF_DEPTH 64

int compile_file(const char *input_file, const char *link_flags,
                 const char *target, int is_import, void *sym_buffer, int *sym_c, void* overls, int* overls_c);

/* ── Глобальные переменные для линковки всего проекта ── */
static char (*g_import_objects)[512] = NULL;
static const char **g_import_ptrs = NULL;
static int  g_import_count = 0;
static char g_target[256] = "";

const char **preprocess_get_imports(int *count) {
    *count = g_import_count;
    if (g_import_count == 0 || !g_import_objects) return NULL;
    for (int i = 0; i < g_import_count; i++) {
        g_import_ptrs[i] = g_import_objects[i];
    }
    return g_import_ptrs;
}

const char *preprocess_get_target(void) {
    return g_target[0] ? g_target : NULL;
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

/* ── Контекст препроцессора (изолирует состояние каждого файла) ── */
typedef struct {
    int active;
    int done;
    int parent_active;
} IfFrame;

typedef struct {
    char name[64];
    char value[256];
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

static void mark_included(PreprocCtx *ctx, const char *canon_path) {
    if (ctx->included_count >= ctx->max_includes) {
        ctx->max_includes *= 2;
        char (*re_included_files)[512] = realloc(ctx->included_files, ctx->max_includes * 512);
        if (!re_included_files) {
            fprintf(stderr, "preprocessor: out of memory in includes\n");
            exit(1);
        }
        ctx->included_files = re_included_files;
    }
    strncpy(ctx->included_files[ctx->included_count++], canon_path, 511);
}

static void define_push(PreprocCtx *ctx, const char *name, const char *value) {
    for (size_t i = 0; i < ctx->define_count; i++) {
        if (strcmp(ctx->defines[i].name, name) == 0) {
            strncpy(ctx->defines[i].value, value, 255);
            return;
        }
    }
    if (ctx->define_count >= ctx->max_defines) {
        ctx->max_defines *= 2;
        Define *re_defines = realloc(ctx->defines, ctx->max_defines * sizeof(Define));
        if (!re_defines) {
            fprintf(stderr, "preprocessor: define table overflow\n");
            exit(1);
        }
        ctx->defines = re_defines;
    }
    strncpy(ctx->defines[ctx->define_count].name,  name,  63);
    strncpy(ctx->defines[ctx->define_count].value, value, 255);
    ctx->define_count++;
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

static const char *define_lookup(PreprocCtx *ctx, const char *name) {
    for (int i = ctx->define_count - 1; i >= 0; i--)
        if (strcmp(ctx->defines[i].name, name) == 0)
            return ctx->defines[i].value;
    return NULL;
}

static int define_defined(PreprocCtx *ctx, const char *name) {
    for (size_t i = 0; i < ctx->define_count; i++)
        if (strcmp(ctx->defines[i].name, name) == 0)
            return 1;
    return 0;
}

void preprocess_set_target(const char *triple) {
    if (triple && triple[0]) {
        strncpy(g_target, triple, sizeof(g_target) - 1);
        g_target[sizeof(g_target) - 1] = '\0';
    } else {
        g_target[0] = '\0';
    }
}

static void inject_target_defines(PreprocCtx *ctx) {
    if (!g_target[0]) return;
    char buf[256];
    strncpy(buf, g_target, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
#ifdef _WIN32
    char *saveptr = NULL;
    char *tok = strtok_s(buf, "-", &saveptr);
#else
    char *saveptr = NULL;
    char *tok = strtok_r(buf, "-", &saveptr);
#endif
    while (tok) {
        define_push(ctx, tok, tok);
#ifdef _WIN32
        tok = strtok_s(NULL, "-", &saveptr);
#else
        tok = strtok_r(NULL, "-", &saveptr);
#endif
    }
    define_push(ctx, g_target, g_target);
}

static char *read_file(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "preprocessor: cannot open '%s'\n", path);
        return NULL;
    }
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

static int preprocess_internal(PreprocCtx *ctx, const char *source, const char *base_dir)
{
    int src_i   = 0;
    int src_len = (int)strlen(source);

#define OUT_PUSH(ch) do {            \
    if (ctx->out_i >= ctx->output_size - 1) { \
        ctx->output_size *= 2;            \
        char *re_out = realloc(ctx->out, ctx->output_size * sizeof(char));      \
        if (!re_out) {                                                 \
            fprintf(stderr, "preprocessor: output buffer overflow\n"); \
            exit(1);        \
        }                   \
        ctx->out = re_out;       \
    }                       \
    ctx->out[(ctx->out_i)++] = (ch); \
} while (0)

#define STR_PUSH(buf, idx, maxlen, ch) do { \
    if ((idx) < (maxlen) - 1) { (buf)[(idx)++] = (ch); } \
} while (0)

    while (src_i < src_len) {

        /* --- однострочный комментарий --- */
        if (source[src_i] == '/' && src_i + 1 < src_len && source[src_i + 1] == '/') {
            if (currently_active(ctx)) {
                while (src_i < src_len && source[src_i] != '\n')
                    OUT_PUSH(source[src_i++]);
            } else {
                while (src_i < src_len && source[src_i] != '\n')
                    src_i++;
            }
            continue;
        }

        /* --- блочный комментарий --- */
        if (source[src_i] == '/' && src_i + 1 < src_len && source[src_i + 1] == '*') {
            if (currently_active(ctx)) {
                OUT_PUSH(source[src_i++]);
                OUT_PUSH(source[src_i++]);
                int closed = 0;
                while (src_i < src_len) {
                    if (source[src_i] == '*' && src_i + 1 < src_len && source[src_i + 1] == '/') {
                        OUT_PUSH(source[src_i++]);
                        OUT_PUSH(source[src_i++]);
                        closed = 1;
                        break;
                    }
                    OUT_PUSH(source[src_i++]);
                }
                if (!closed)
                    fprintf(stderr, "preprocessor: warning: unclosed block comment\n");
            } else {
                src_i += 2;
                int closed = 0;
                while (src_i < src_len) {
                    if (source[src_i] == '*' && src_i + 1 < src_len && source[src_i + 1] == '/') {
                        src_i += 2; closed = 1; break;
                    }
                    src_i++;
                }
                if (!closed)
                    fprintf(stderr, "preprocessor: warning: unclosed block comment\n");
            }
            continue;
        }

        /* --- строковый литерал --- */
        if (source[src_i] == '"') {
            if (currently_active(ctx)) {
                OUT_PUSH(source[src_i++]);
                while (src_i < src_len && source[src_i] != '"') {
                    if (source[src_i] == '\\' && src_i + 1 < src_len)
                        OUT_PUSH(source[src_i++]);
                    OUT_PUSH(source[src_i++]);
                }
                if (src_i < src_len)
                    OUT_PUSH(source[src_i++]);
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

        /* --- символьный литерал --- */
        if (source[src_i] == '\'') {
            if (currently_active(ctx)) {
                OUT_PUSH(source[src_i++]);
                while (src_i < src_len && source[src_i] != '\'') {
                    if (source[src_i] == '\\' && src_i + 1 < src_len)
                        OUT_PUSH(source[src_i++]);
                    OUT_PUSH(source[src_i++]);
                }
                if (src_i < src_len)
                    OUT_PUSH(source[src_i++]);
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

        /* --- директива # --- */
        if (source[src_i] == '#') {
            src_i++;
            while (src_i < src_len && (source[src_i] == ' ' || source[src_i] == '\t')) src_i++;

            char directive[32] = {0};
            int  di = 0;
            while (src_i < src_len && source[src_i] != ' ' && source[src_i] != '\t' && source[src_i] != '\n') {
                STR_PUSH(directive, di, (int)sizeof(directive), source[src_i]);
                src_i++;
            }
            directive[di] = '\0';

            while (src_i < src_len && (source[src_i] == ' ' || source[src_i] == '\t')) src_i++;

            if (strcmp(directive, "ifdef") == 0) {
                char name[256] = {0}; int ni = 0;
                while (src_i < src_len && source[src_i] != ' ' && source[src_i] != '\t' && source[src_i] != '\n') {
                    STR_PUSH(name, ni, (int)sizeof(name), source[src_i]);
                    src_i++;
                }
                name[ni] = '\0';

                if (ctx->if_depth >= ctx->max_if_depth) {
                    ctx->max_if_depth *= 2;
                    IfFrame *re_if_stack = realloc(ctx->if_stack, sizeof(IfFrame) * ctx->max_if_depth);
                    if (!re_if_stack) {
                        fprintf(stderr, "preprocessor: out of memory in #if* stack");
                        exit(1);
                    }
                    ctx->if_stack = re_if_stack;
                }
                int parent = currently_active(ctx);
                int cond = (strchr(name, '-') || strchr(name, '.'))
                           ? triple_matches(name)
                           : define_defined(ctx, name);
                ctx->if_stack[ctx->if_depth].active        = parent && cond;
                ctx->if_stack[ctx->if_depth].done          = parent && cond;
                ctx->if_stack[ctx->if_depth].parent_active = parent;
                ctx->if_depth++;
            }
            else if (strcmp(directive, "ifndef") == 0) {
                char name[256] = {0}; int ni = 0;
                while (src_i < src_len && source[src_i] != ' ' && source[src_i] != '\t' && source[src_i] != '\n') {
                    STR_PUSH(name, ni, (int)sizeof(name), source[src_i]);
                    src_i++;
                }
                name[ni] = '\0';

                if (ctx->if_depth >= ctx->max_if_depth) {
                    ctx->max_if_depth *= 2;
                    IfFrame *re_if_stack = realloc(ctx->if_stack, sizeof(IfFrame) * ctx->max_if_depth);
                    if (!re_if_stack) {
                        fprintf(stderr, "preprocessor: out of memory in #if* stack");
                        exit(1);
                    }
                    ctx->if_stack = re_if_stack;
                }
                int parent = currently_active(ctx);
                int cond = (strchr(name, '-') || strchr(name, '.'))
                           ? !triple_matches(name)
                           : !define_defined(ctx, name);
                ctx->if_stack[ctx->if_depth].active        = parent && cond;
                ctx->if_stack[ctx->if_depth].done          = parent && cond;
                ctx->if_stack[ctx->if_depth].parent_active = parent;
                ctx->if_depth++;
            }
            else if (strcmp(directive, "else") == 0) {
                if (ctx->if_depth == 0) {
                    fprintf(stderr, "preprocessor: #else without #ifdef/#ifndef\n");
                } else {
                    IfFrame *fr = &ctx->if_stack[ctx->if_depth - 1];
                    fr->active = fr->parent_active && !fr->done;
                    if (fr->active) fr->done = 1;
                }
            }
            else if (strcmp(directive, "endif") == 0) {
                if (ctx->if_depth == 0)
                    fprintf(stderr, "preprocessor: #endif without #ifdef/#ifndef\n");
                else
                    ctx->if_depth--;
            }
            else if (strcmp(directive, "import") == 0) {
                char filename[256] = {0}; int fi = 0;
                if (src_i >= src_len) goto skip_newline;
                char delim = source[src_i++];
                char end   = (delim == '"') ? '"' : '>';
                while (src_i < src_len && source[src_i] != end && source[src_i] != '\n') {
                    STR_PUSH(filename, fi, (int)sizeof(filename), source[src_i]);
                    src_i++;
                }
                filename[fi] = '\0';
                if (src_i < src_len && source[src_i] == end) src_i++;

                if (currently_active(ctx)) {
                    char src_path[512];
                    if (base_dir && base_dir[0])
                        snprintf(src_path, sizeof(src_path), "%s/%s", base_dir, filename);
                    else
                        snprintf(src_path, sizeof(src_path), "%s", filename);

                    char obj_path[512];
                    if (base_dir && base_dir[0])
                        snprintf(obj_path, sizeof(obj_path), "%s/%s.o", base_dir, filename);
                    else
                        snprintf(obj_path, sizeof(obj_path), "%s.o", filename);

                    int already = 0;
                    for (int ii = 0; ii < g_import_count; ii++)
                        if (strcmp(g_import_objects[ii], obj_path) == 0) { already = 1; break; }

                    if (!already) {
                        char src_no_ext[512];
                        strncpy(src_no_ext, src_path, sizeof(src_no_ext) - 1);
                        src_no_ext[sizeof(src_no_ext) - 1] = '\0';
                        char *dot = strrchr(src_no_ext, '.');
                        if (dot && strcmp(dot, ".fl") == 0) *dot = '\0';

                        int tmp = compile_file(src_no_ext, "", g_target, 1, 
                                            NULL, NULL, NULL, NULL);

                        if (tmp != 0) {
                            fprintf(stderr, "preprocessor: #import: failed to compile '%s'\n", src_path);
                        } else {
                            if (g_import_count < MAX_IMPORTS) {
                                strncpy(g_import_objects[g_import_count++], obj_path, sizeof(g_import_objects[0]) - 1);
                                fprintf(stderr, "preprocessor: #import: queued '%s'\n", obj_path);
                            }
                        }
                    }
                }
            }
            else if (strcmp(directive, "include") == 0) {
                char filename[256] = {0}; int fi = 0;
                if (src_i >= src_len) goto skip_newline;
                char delim = source[src_i++];
                char end   = (delim == '"') ? '"' : '>';
                while (src_i < src_len && source[src_i] != end && source[src_i] != '\n') {
                    STR_PUSH(filename, fi, (int)sizeof(filename), source[src_i]);
                    src_i++;
                }
                filename[fi] = '\0';
                if (src_i < src_len && source[src_i] == end) src_i++;

                if (currently_active(ctx)) {
                    char path[512];
                    if (base_dir && base_dir[0])
                        snprintf(path, sizeof(path), "%s/%s", base_dir, filename);
                    else
                        snprintf(path, sizeof(path), "%s", filename);

                    char canon[512] = {0};
                    if (!realpath(path, canon)) {
                        strncpy(canon, path, sizeof(canon) - 1);
                        canon[sizeof(canon) - 1] = '\0';
                    }

                    if (ctx->included_count >= ctx->max_includes) {
                        ctx->max_includes *= 2;
                        char (*re_included_files)[512] = realloc(ctx->included_files, ctx->max_includes * 512);
                        if (!re_included_files) {
                            fprintf(stderr, "preprocessor: out of memory in includes\n");
                            exit(1);
                        }
                        ctx->included_files = re_included_files;
                    } else if (!already_included(ctx, canon)) {
                        mark_included(ctx, canon);
                        char *included_src = read_file(path);
                        if (included_src) {
                            char canon_copy[512];
                            strncpy(canon_copy, canon, sizeof(canon_copy) - 1);
                            canon_copy[sizeof(canon_copy) - 1] = '\0';
                            char new_base[512] = {0};
                            char *last_slash = strrchr(canon_copy, '/');
                            if (last_slash) {
                                *last_slash = '\0';
                                strncpy(new_base, canon_copy, sizeof(new_base) - 1);
                            } else {
                                strncpy(new_base, ".", sizeof(new_base) - 1);
                            }
                            /* Делим контекст с includ-ами, чтобы макросы шарились */
                            int rc = preprocess_internal(ctx, included_src, new_base);
                            free(included_src);
                            if (rc < 0) return -1;
                            OUT_PUSH('\n');
                        }
                    }
                }
            }
            else if (strcmp(directive, "define") == 0) {
                char name[256]  = {0}; int ni = 0;
                while (src_i < src_len && source[src_i] != ' ' && source[src_i] != '\t' && source[src_i] != '\n') {
                    STR_PUSH(name, ni, (int)sizeof(name), source[src_i]);
                    src_i++;
                }
                name[ni] = '\0';

                while (src_i < src_len && (source[src_i] == ' ' || source[src_i] == '\t')) src_i++;

                char value[256] = {0}; int vi = 0;
                while (src_i < src_len && source[src_i] != '\n') {
                    STR_PUSH(value, vi, (int)sizeof(value), source[src_i]);
                    src_i++;
                }
                value[vi] = '\0';
                while (vi > 0 && (value[vi-1] == ' ' || value[vi-1] == '\t'))
                    value[--vi] = '\0';

                if (currently_active(ctx)) {
                    if (strcmp(name, "__target__") == 0 && value[0] != '\0') {
                        if (g_target[0] == '\0') {
                            strncpy(g_target, value, sizeof(g_target) - 1);
                            g_target[sizeof(g_target) - 1] = '\0';
                            fprintf(stderr, "preprocessor: target triple = '%s'\n", g_target);
                            inject_target_defines(ctx);
                        }
                    } else {
                        define_push(ctx, name, value);
                    }
                }
            }
            else if (strcmp(directive, "undef") == 0) {
                char name[256] = {0}; int ni = 0;
                while (src_i < src_len && source[src_i] != ' ' && source[src_i] != '\t' && source[src_i] != '\n') {
                    STR_PUSH(name, ni, (int)sizeof(name), source[src_i]);
                    src_i++;
                }
                name[ni] = '\0';

                if (currently_active(ctx))
                    define_remove(ctx, name);
            }
            else {
                fprintf(stderr, "preprocessor: unknown directive '#%s'\n", directive);
                while (src_i < src_len && source[src_i] != '\n') src_i++;
            }

            skip_newline:
            if (src_i < src_len && source[src_i] == '\n') src_i++;
            continue;
        }

        /* --- неактивная ветка --- */
        if (!currently_active(ctx)) {
            src_i++;
            continue;
        }

        /* --- идентификатор --- */
        if (isalpha((unsigned char)source[src_i]) || source[src_i] == '_') {
            char word[256] = {0}; int wi = 0;
            while (src_i < src_len && (isalnum((unsigned char)source[src_i]) || source[src_i] == '_')) {
                STR_PUSH(word, wi, (int)sizeof(word), source[src_i]);
                src_i++;
            }
            word[wi] = '\0';

            const char *repl = define_lookup(ctx, word);
            if (repl && strcmp(repl, word) != 0) {
                int rlen = (int)strlen(repl);
                if (ctx->out_i + rlen >= ctx->output_size - 1) {
                    ctx->output_size *= 2;
                    char *re_out = realloc(ctx->out, ctx->output_size * sizeof(char));
                    if (!re_out) {
                        fprintf(stderr, "preprocessor: output buffer overflow\n");
                        exit(1);
                    }
                    ctx->out = re_out;
                }
                memcpy(ctx->out + ctx->out_i, repl, rlen);
                ctx->out_i += rlen;
            } else {
                if (ctx->out_i + wi >= ctx->output_size - 1) {
                    ctx->output_size *= 2;
                    char *re_out = realloc(ctx->out, ctx->output_size * sizeof(char));
                    if (!re_out) {
                        fprintf(stderr, "preprocessor: output buffer overflow\n");
                        exit(1);
                    }
                    ctx->out = re_out;
                }
                memcpy(ctx->out + ctx->out_i, word, wi);
                ctx->out_i += wi;
            }
            continue;
        }

        OUT_PUSH(source[src_i++]);
    }

#undef OUT_PUSH
#undef STR_PUSH
    return 0;
}

char *preprocess(const char *source, const char *base_dir) {
    /* Ленивая инициализация глобального массива импортов (один раз на весь компилятор) */
    if (!g_import_objects) {
        g_import_objects = malloc(MAX_IMPORTS * 512);
        g_import_ptrs    = malloc(MAX_IMPORTS * 512);
        g_import_count   = 0;
    }

    /* Изолированный контекст конкретно для этого файла */
    PreprocCtx ctx;
    ctx.included_count = 0;
    ctx.if_depth       = 0;
    ctx.define_count   = 0;
    ctx.out_i          = 0;

    ctx.max_defines  = MAX_DEFINES;
    ctx.max_includes = MAX_INCLUDED;
    ctx.output_size  = OUTPUT_SIZE;
    ctx.max_if_depth = MAX_IF_DEPTH;

    ctx.defines        = malloc(sizeof(Define) * MAX_DEFINES);
    ctx.included_files = malloc(MAX_INCLUDED * 512);
    ctx.out            = malloc(OUTPUT_SIZE * sizeof(char));
    ctx.if_stack       = malloc(sizeof(IfFrame) * MAX_IF_DEPTH);

    if (!ctx.out || !ctx.defines || !ctx.included_files || !ctx.if_stack) {
        fprintf(stderr, "preprocessor: out of memory\n");
        return NULL;
    }

    /* Подхватываем таргет-макросы (если они уже установлены) */
    inject_target_defines(&ctx);

    int rc = preprocess_internal(&ctx, source, base_dir);
    
    if (ctx.if_depth > 0) {
        fprintf(stderr, "preprocessor: unclosed #ifdef/#ifndef\n");
        rc = -1;
    }

    if (rc < 0) {
        free(ctx.out);
        free(ctx.defines);
        free(ctx.included_files);
        free(ctx.if_stack);
        return NULL;
    }
    
    ctx.out[ctx.out_i] = '\0';

    /* Очищаем структуры данных, принадлежавшие ТОЛЬКО этому контексту */
    free(ctx.defines);
    free(ctx.included_files);
    free(ctx.if_stack);

    /* Обрати внимание: мы НЕ делаем free(g_import_ptrs) и free(g_import_objects).
     * Они живут до завершения всей программы, чтобы линковщик мог их забрать. */

    return ctx.out;
}