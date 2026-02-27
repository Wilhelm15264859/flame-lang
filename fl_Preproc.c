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

static void preprocess_internal(const char *source, const char *base_dir,
                                 char *out, int *out_i)
{
    int src_i   = 0;
    int src_len = (int)strlen(source);

    while (src_i < src_len) {

        if (source[src_i] == '/' && source[src_i + 1] == '/') {
            while (src_i < src_len && source[src_i] != '\n')
                out[(*out_i)++] = source[src_i++];
            continue;
        }

        if (source[src_i] == '/' && source[src_i + 1] == '*') {
            out[(*out_i)++] = source[src_i++];
            out[(*out_i)++] = source[src_i++];
            while (src_i < src_len) {
                if (source[src_i] == '*' && source[src_i + 1] == '/') {
                    out[(*out_i)++] = source[src_i++];
                    out[(*out_i)++] = source[src_i++];
                    break;
                }
                out[(*out_i)++] = source[src_i++];
            }
            continue;
        }

        if (source[src_i] == '"') {
            out[(*out_i)++] = source[src_i++];
            while (src_i < src_len && source[src_i] != '"') {
                if (source[src_i] == '\\' && src_i + 1 < src_len)
                    out[(*out_i)++] = source[src_i++];
                out[(*out_i)++] = source[src_i++];
            }
            if (src_i < src_len)
                out[(*out_i)++] = source[src_i++];
            continue;
        }

        if (source[src_i] == '#') {
            src_i++;
            while (source[src_i] == ' ' || source[src_i] == '\t') src_i++;

            char directive[32] = {0};
            int  di = 0;
            while (src_i < src_len &&
                   source[src_i] != ' ' &&
                   source[src_i] != '\t' &&
                   source[src_i] != '\n')
                directive[di++] = source[src_i++];

            while (source[src_i] == ' ' || source[src_i] == '\t') src_i++;

            if (strcmp(directive, "include") == 0) {
                char filename[256] = {0};
                int  fi = 0;
                char delim = source[src_i++];
                char end   = (delim == '"') ? '"' : '>';
                while (src_i < src_len && source[src_i] != end)
                    filename[fi++] = source[src_i++];
                if (src_i < src_len) src_i++;

                char path[512];
                if (base_dir && base_dir[0])
                    snprintf(path, sizeof(path), "%s/%s", base_dir, filename);
                else
                    snprintf(path, sizeof(path), "%s", filename);

                char canon[512] = {0};
                if (!realpath(path, canon))
                    strncpy(canon, path, 511);

                if (already_included(canon)) {
                    
                } else {
                    mark_included(canon);
                    char *included = read_file(path);
                    if (included) {
                        char canon_copy[512];
                        strncpy(canon_copy, canon, 511);
                        char new_base[512] = {0};
                        char *last_slash = strrchr(canon_copy, '/');
                        if (last_slash) {
                            *last_slash = '\0';
                            strncpy(new_base, canon_copy, 511);
                        } else {
                            strncpy(new_base, ".", 511);
                        }
                        preprocess_internal(included, new_base, out, out_i);
                        free(included);
                        out[(*out_i)++] = '\n';
                    }
                }
            }
            else if (strcmp(directive, "define") == 0) {
                char name[64]   = {0};
                char value[256] = {0};
                int  ni = 0;

                while (src_i < src_len &&
                       source[src_i] != ' ' &&
                       source[src_i] != '\t' &&
                       source[src_i] != '\n')
                    name[ni++] = source[src_i++];

                while (source[src_i] == ' ' || source[src_i] == '\t') src_i++;

                int vi = 0;
                while (src_i < src_len && source[src_i] != '\n')
                    value[vi++] = source[src_i++];

                while (vi > 0 && (value[vi-1] == ' ' || value[vi-1] == '\t'))
                    value[--vi] = '\0';

                define_push(name, value);
            }
            else if (strcmp(directive, "undef") == 0) {
                char name[64] = {0};
                int  ni = 0;
                while (src_i < src_len &&
                       source[src_i] != ' ' &&
                       source[src_i] != '\t' &&
                       source[src_i] != '\n')
                    name[ni++] = source[src_i++];
                define_remove(name);
            }
            else {
                fprintf(stderr, "preprocessor: unknown directive '#%s'\n", directive);
                while (src_i < src_len && source[src_i] != '\n') src_i++;
            }

            if (src_i < src_len && source[src_i] == '\n') src_i++;
            continue;
        }

        if (isalpha((unsigned char)source[src_i]) || source[src_i] == '_') {
            char word[64] = {0};
            int  wi = 0;
            while (src_i < src_len &&
                   (isalnum((unsigned char)source[src_i]) || source[src_i] == '_'))
                word[wi++] = source[src_i++];

            const char *repl = define_lookup(word);
            if (repl) {
                int rlen = (int)strlen(repl);
                memcpy(out + *out_i, repl, rlen);
                *out_i += rlen;
            } else {
                memcpy(out + *out_i, word, wi);
                *out_i += wi;
            }
            continue;
        }

        out[(*out_i)++] = source[src_i++];
    }
}

char *preprocess(const char *source, const char *base_dir) {
    define_count   = 0;
    included_count = 0;

    char *out = malloc(OUTPUT_SIZE);
    if (!out) {
        fprintf(stderr, "preprocessor: out of memory\n");
        return NULL;
    }

    int out_i = 0;
    preprocess_internal(source, base_dir, out, &out_i);
    out[out_i] = '\0';

    return out;
}