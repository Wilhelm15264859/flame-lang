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

#define MAX_DEFINES  256
#define MAX_INCLUDED 256
#define OUTPUT_SIZE  (1024 * 1024 * 4)

/* ── #import: список .o файлов для линковки ── */
#define MAX_IMPORTS 64
static char g_import_objects[MAX_IMPORTS][512];
static int  g_import_count = 0;

const char **preprocess_get_imports(int *count) {
    *count = g_import_count;
    return (const char **)g_import_objects;
}

static char g_target[256] = "";

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

#define MAX_IF_DEPTH 64

typedef struct {
    int active;
    int done;
    int parent_active;
} IfFrame;

static IfFrame if_stack[MAX_IF_DEPTH];
static int     if_depth = 0;

static int currently_active(void) {
    for (int k = 0; k < if_depth; k++)
        if (!if_stack[k].active) return 0;
    return 1;
}

typedef struct {
    char name[64];
    char value[256];
} Define;

static Define defines[MAX_DEFINES];
static int    define_count = 0;

static char included_files[MAX_INCLUDED][512];
static int  included_count = 0;

static int already_included(const char *canon_path) {
    for (int i = 0; i < included_count; i++)
        if (strcmp(included_files[i], canon_path) == 0)
            return 1;
    return 0;
}

static void mark_included(const char *canon_path) {
    if (included_count >= MAX_INCLUDED) return;
    strncpy(included_files[included_count++], canon_path, 511);
}

static void define_push(const char *name, const char *value) {
    for (int i = 0; i < define_count; i++) {
        if (strcmp(defines[i].name, name) == 0) {
            strncpy(defines[i].value, value, 255);
            return;
        }
    }
    if (define_count >= MAX_DEFINES) {
        fprintf(stderr, "preprocessor: define table overflow\n");
        return;
    }
    strncpy(defines[define_count].name,  name,  63);
    strncpy(defines[define_count].value, value, 255);
    define_count++;
}

static void define_remove(const char *name) {
    for (int i = 0; i < define_count; i++) {
        if (strcmp(defines[i].name, name) == 0) {
            for (int j = i; j < define_count - 1; j++)
                defines[j] = defines[j + 1];
            define_count--;
            return;
        }
    }
}

static const char *define_lookup(const char *name) {
    for (int i = define_count - 1; i >= 0; i--)
        if (strcmp(defines[i].name, name) == 0)
            return defines[i].value;
    return NULL;
}

static int define_defined(const char *name) {
    for (int i = 0; i < define_count; i++)
        if (strcmp(defines[i].name, name) == 0)
            return 1;
    return 0;
}

void preprocess_set_target(const char *triple) {
    if (triple && triple[0]) {
        strncpy(g_target, triple, sizeof(g_target) - 1);
        g_target[sizeof(g_target) - 1] = '\0';

        /* Регистрируем каждый компонент triple как дефайн.
         * strtok_r / strtok_s — портируемый вариант без strsep. */
        char buf[256];
        strncpy(buf, triple, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
#ifdef _WIN32
        char *saveptr = NULL;
        char *tok = strtok_s(buf, "-", &saveptr);
#else
        char *saveptr = NULL;
        char *tok = strtok_r(buf, "-", &saveptr);
#endif
        while (tok) {
            define_push(tok, tok);
#ifdef _WIN32
            tok = strtok_s(NULL, "-", &saveptr);
#else
            tok = strtok_r(NULL, "-", &saveptr);
#endif
        }
        define_push(triple, triple);
    } else {
        g_target[0] = '\0';
    }
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

static int preprocess_internal(const char *source, const char *base_dir,
                                char *out, int *out_i)
{
    int src_i   = 0;
    int src_len = (int)strlen(source);

    /* Макрос безопасной записи в out: проверяет границу перед каждым байтом */
#define OUT_PUSH(ch) do { \
    if (*out_i >= OUTPUT_SIZE - 1) { \
        fprintf(stderr, "preprocessor: output buffer overflow\n"); \
        return -1; \
    } \
    out[(*out_i)++] = (ch); \
} while (0)

    /* Макрос безопасного накопления строки: обрезает с ошибкой */
#define STR_PUSH(buf, idx, maxlen, ch) do { \
    if ((idx) < (maxlen) - 1) { (buf)[(idx)++] = (ch); } \
} while (0)

    while (src_i < src_len) {

        /* --- однострочный комментарий --- */
        if (source[src_i] == '/' &&
            src_i + 1 < src_len && source[src_i + 1] == '/') {
            if (currently_active()) {
                while (src_i < src_len && source[src_i] != '\n')
                    OUT_PUSH(source[src_i++]);
            } else {
                while (src_i < src_len && source[src_i] != '\n')
                    src_i++;
            }
            continue;
        }

        /* --- блочный комментарий --- */
        if (source[src_i] == '/' &&
            src_i + 1 < src_len && source[src_i + 1] == '*') {
            int comment_line = 0; /* для диагностики — не отслеживаем номер строки здесь */
            (void)comment_line;
            if (currently_active()) {
                OUT_PUSH(source[src_i++]);
                OUT_PUSH(source[src_i++]);
                int closed = 0;
                while (src_i < src_len) {
                    if (source[src_i] == '*' &&
                        src_i + 1 < src_len && source[src_i + 1] == '/') {
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
                    if (source[src_i] == '*' &&
                        src_i + 1 < src_len && source[src_i + 1] == '/') {
                        src_i += 2; closed = 1; break;
                    }
                    src_i++;
                }
                if (!closed)
                    fprintf(stderr, "preprocessor: warning: unclosed block comment\n");
            }
            continue;
        }

        /* --- строковый литерал (не раскрываем макросы внутри) --- */
        if (source[src_i] == '"') {
            if (currently_active()) {
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

        /* --- символьный литерал (не раскрываем макросы внутри) --- */
        if (source[src_i] == '\'') {
            if (currently_active()) {
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
            while (src_i < src_len &&
                   (source[src_i] == ' ' || source[src_i] == '\t')) src_i++;

            char directive[32] = {0};
            int  di = 0;
            while (src_i < src_len &&
                   source[src_i] != ' '  &&
                   source[src_i] != '\t' &&
                   source[src_i] != '\n') {
                STR_PUSH(directive, di, (int)sizeof(directive), source[src_i]);
                src_i++;
            }
            directive[di] = '\0';

            while (src_i < src_len &&
                   (source[src_i] == ' ' || source[src_i] == '\t')) src_i++;

            if (strcmp(directive, "ifdef") == 0) {
                char name[256] = {0}; int ni = 0;
                while (src_i < src_len &&
                       source[src_i] != ' ' &&
                       source[src_i] != '\t' &&
                       source[src_i] != '\n') {
                    STR_PUSH(name, ni, (int)sizeof(name), source[src_i]);
                    src_i++;
                }
                name[ni] = '\0';

                if (if_depth >= MAX_IF_DEPTH) {
                    fprintf(stderr, "preprocessor: #ifdef nesting too deep\n");
                } else {
                    int parent = currently_active();
                    int cond = (strchr(name, '-') || strchr(name, '.'))
                               ? triple_matches(name)
                               : define_defined(name);
                    if_stack[if_depth].active        = parent && cond;
                    if_stack[if_depth].done          = parent && cond;
                    if_stack[if_depth].parent_active = parent;
                    if_depth++;
                }
            }
            else if (strcmp(directive, "ifndef") == 0) {
                char name[256] = {0}; int ni = 0;
                while (src_i < src_len &&
                       source[src_i] != ' ' &&
                       source[src_i] != '\t' &&
                       source[src_i] != '\n') {
                    STR_PUSH(name, ni, (int)sizeof(name), source[src_i]);
                    src_i++;
                }
                name[ni] = '\0';

                if (if_depth >= MAX_IF_DEPTH) {
                    fprintf(stderr, "preprocessor: #ifndef nesting too deep\n");
                } else {
                    int parent = currently_active();
                    int cond = (strchr(name, '-') || strchr(name, '.'))
                               ? !triple_matches(name)
                               : !define_defined(name);
                    if_stack[if_depth].active        = parent && cond;
                    if_stack[if_depth].done          = parent && cond;
                    if_stack[if_depth].parent_active = parent;
                    if_depth++;
                }
            }
            else if (strcmp(directive, "else") == 0) {
                if (if_depth == 0) {
                    fprintf(stderr, "preprocessor: #else without #ifdef/#ifndef\n");
                } else {
                    IfFrame *fr = &if_stack[if_depth - 1];
                    fr->active = fr->parent_active && !fr->done;
                    if (fr->active) fr->done = 1;
                }
            }
            else if (strcmp(directive, "endif") == 0) {
                if (if_depth == 0)
                    fprintf(stderr, "preprocessor: #endif without #ifdef/#ifndef\n");
                else
                    if_depth--;
            }
            else if (strcmp(directive, "import") == 0) {
                /* #import "module" — компилирует module.fl → module.o, линкует статически */
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

                if (currently_active()) {
                    /* Строим путь к .fl файлу */
                    char src_path[512];
                    if (base_dir && base_dir[0])
                        snprintf(src_path, sizeof(src_path), "%s/%s.fl", base_dir, filename);
                    else
                        snprintf(src_path, sizeof(src_path), "%s.fl", filename);

                    /* Объектный файл — рядом с исходником */
                    char obj_path[512];
                    if (base_dir && base_dir[0])
                        snprintf(obj_path, sizeof(obj_path), "%s/%s.o", base_dir, filename);
                    else
                        snprintf(obj_path, sizeof(obj_path), "%s.o", filename);

                    /* Проверяем, не импортировали ли уже */
                    int already = 0;
                    for (int ii = 0; ii < g_import_count; ii++)
                        if (strcmp(g_import_objects[ii], obj_path) == 0) { already = 1; break; }

                    if (!already) {
                        /* Компилируем импортируемый файл */
                        char cmd[1024];
                        snprintf(cmd, sizeof(cmd), "flame -c \"%s\"", src_path);
                        int rc2 = system(cmd);
                        if (rc2 != 0)
                            fprintf(stderr, "preprocessor: #import: failed to compile '%s'\n", src_path);
                        else if (g_import_count < MAX_IMPORTS) {
                            strncpy(g_import_objects[g_import_count++], obj_path,
                                    sizeof(g_import_objects[0]) - 1);
                            fprintf(stderr, "preprocessor: #import: queued '%s'\n", obj_path);
                        }
                    }
                }
            }
            else if (strcmp(directive, "include") == 0) {                char filename[256] = {0}; int fi = 0;
                if (src_i >= src_len) goto skip_newline;
                char delim = source[src_i++];
                char end   = (delim == '"') ? '"' : '>';
                while (src_i < src_len && source[src_i] != end && source[src_i] != '\n') {
                    STR_PUSH(filename, fi, (int)sizeof(filename), source[src_i]);
                    src_i++;
                }
                filename[fi] = '\0';
                if (src_i < src_len && source[src_i] == end) src_i++;

                if (currently_active()) {
                    char path[512];
                    if (base_dir && base_dir[0])
                        snprintf(path, sizeof(path), "%s/%s", base_dir, filename);
                    else
                        snprintf(path, sizeof(path), "%s", filename);

                    char canon[512] = {0};
                    if (!realpath(path, canon)) {
                        /* realpath завершился с ошибкой — файл может не существовать,
                         * используем нормализованный путь как есть */
                        strncpy(canon, path, sizeof(canon) - 1);
                        canon[sizeof(canon) - 1] = '\0';
                    }

                    if (included_count >= MAX_INCLUDED) {
                        fprintf(stderr,
                            "preprocessor: #include table full, cannot include '%s'\n",
                            canon);
                    } else if (!already_included(canon)) {
                        /* Регистрируем ДО рекурсии — защита от косвенных циклов */
                        mark_included(canon);
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
                            int rc = preprocess_internal(included_src, new_base,
                                                         out, out_i);
                            free(included_src);
                            if (rc < 0) return -1;
                            OUT_PUSH('\n');
                        }
                    }
                    /* else: файл уже включён — тихо пропускаем (include guard) */
                }
            }
            else if (strcmp(directive, "define") == 0) {
                char name[256]  = {0}; int ni = 0;
                while (src_i < src_len &&
                       source[src_i] != ' ' &&
                       source[src_i] != '\t' &&
                       source[src_i] != '\n') {
                    STR_PUSH(name, ni, (int)sizeof(name), source[src_i]);
                    src_i++;
                }
                name[ni] = '\0';

                while (src_i < src_len &&
                       (source[src_i] == ' ' || source[src_i] == '\t')) src_i++;

                char value[256] = {0}; int vi = 0;
                while (src_i < src_len && source[src_i] != '\n') {
                    STR_PUSH(value, vi, (int)sizeof(value), source[src_i]);
                    src_i++;
                }
                value[vi] = '\0';
                /* убираем trailing пробелы */
                while (vi > 0 && (value[vi-1] == ' ' || value[vi-1] == '\t'))
                    value[--vi] = '\0';

                if (currently_active()) {
                    if (strcmp(name, "__target__") == 0 && value[0] != '\0') {
                        if (g_target[0] == '\0') {
                            strncpy(g_target, value, sizeof(g_target) - 1);
                            g_target[sizeof(g_target) - 1] = '\0';
                            fprintf(stderr, "preprocessor: target triple = '%s'\n",
                                    g_target);
                        }
                    } else {
                        define_push(name, value);
                    }
                }
            }
            else if (strcmp(directive, "undef") == 0) {
                char name[256] = {0}; int ni = 0;
                while (src_i < src_len &&
                       source[src_i] != ' ' &&
                       source[src_i] != '\t' &&
                       source[src_i] != '\n') {
                    STR_PUSH(name, ni, (int)sizeof(name), source[src_i]);
                    src_i++;
                }
                name[ni] = '\0';

                if (currently_active())
                    define_remove(name);
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
        if (!currently_active()) {
            src_i++;
            continue;
        }

        /* --- идентификатор: возможное раскрытие макроса --- */
        if (isalpha((unsigned char)source[src_i]) || source[src_i] == '_') {
            char word[256] = {0}; int wi = 0;
            while (src_i < src_len &&
                   (isalnum((unsigned char)source[src_i]) || source[src_i] == '_')) {
                STR_PUSH(word, wi, (int)sizeof(word), source[src_i]);
                src_i++;
            }
            word[wi] = '\0';

            const char *repl = define_lookup(word);
            if (repl && strcmp(repl, word) != 0) {
                /* Раскрываем только если замена отличается от имени —
                 * это предотвращает бесконечную рекурсию для A→A */
                int rlen = (int)strlen(repl);
                if (*out_i + rlen >= OUTPUT_SIZE - 1) {
                    fprintf(stderr, "preprocessor: output buffer overflow "
                            "during macro expansion of '%s'\n", word);
                    return -1;
                }
                memcpy(out + *out_i, repl, rlen);
                *out_i += rlen;
            } else {
                /* нет замены или замена совпадает с именем — выводим как есть */
                if (*out_i + wi >= OUTPUT_SIZE - 1) {
                    fprintf(stderr, "preprocessor: output buffer overflow\n");
                    return -1;
                }
                memcpy(out + *out_i, word, wi);
                *out_i += wi;
            }
            continue;
        }

        OUT_PUSH(source[src_i++]);
    }

    if (if_depth > 0)
        fprintf(stderr, "preprocessor: warning: %d unclosed #ifdef/#ifndef\n",
                if_depth);

#undef OUT_PUSH
#undef STR_PUSH
    return 0;
}

char *preprocess(const char *source, const char *base_dir) {
    included_count = 0;
    if_depth       = 0;
    define_count   = 0;   /* сброс дефайнов между вызовами */
    g_import_count = 0;   /* сброс списка #import */

    /* target-defines выставляются снаружи через preprocess_set_target()
     * до вызова preprocess(), поэтому их нужно восстановить после сброса. */
    if (g_target[0]) {
        char saved[256];
        strncpy(saved, g_target, sizeof(saved) - 1);
        saved[sizeof(saved) - 1] = '\0';
        preprocess_set_target(saved);
    }

    char *out = malloc(OUTPUT_SIZE);
    if (!out) {
        fprintf(stderr, "preprocessor: out of memory\n");
        return NULL;
    }

    int out_i = 0;
    int rc = preprocess_internal(source, base_dir, out, &out_i);
    if (rc < 0) {
        free(out);
        return NULL;
    }
    out[out_i] = '\0';

    return out;
}