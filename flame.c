#include "fl_Preproc.h"
#include "fl_Lexer.h"
#include "fl_Exception.h"
#include "fl_Parser.h"
#include "fl_Pregen.h"
#include "fl_Builder.h"
#include <llvm-c/Target.h>
#include <llvm-c/Error.h>
#include <llvm-c/TargetMachine.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>

#define MAX_IMPORTED_MODULES 64

OverloadEntry* global_modules_overloads[MAX_IMPORTED_MODULES];
int global_modules_counts[MAX_IMPORTED_MODULES];
int num_imported_modules = 0;

void register_imported_module(OverloadEntry *overloads, int count) {
    if (num_imported_modules >= MAX_IMPORTED_MODULES) return;
    global_modules_overloads[num_imported_modules] = overloads;
    global_modules_counts[num_imported_modules] = count;
    num_imported_modules++;
}

OverloadEntry* get_global_modules(int *out_num_modules, int **out_counts) {
    *out_num_modules = num_imported_modules;
    *out_counts = global_modules_counts;
    return (OverloadEntry*)global_modules_overloads; 
}

char pass_list[2048] = "";
char g_flags = 0;
void *g_sym = NULL;
int *g_sym_c = NULL;

void *g_overs = NULL;
int *g_overs_c = NULL;

int compile_file(const char *input_file, const char *link_flags,
                        const char *target, char flags, void *sym_buffer, int *sym_c, void* overls, int* overls_c) {
    flags |= (g_flags & 0b0010);
    char filen[256];
    snprintf(filen, sizeof(filen), "%s.fl", input_file);

    FILE *file = fopen(filen, "rb");
    if (!file) { perror("fopen"); return 1; }

    fseek(file, 0, SEEK_END);
    unsigned long long length = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *buffer = malloc(length + 1);
    if (!buffer) { fclose(file); return 1; }
    fread(buffer, 1, length, file);
    buffer[length] = '\0';
    fclose(file);

    char filen_copy[256];
    strncpy(filen_copy, filen, 255);
    char *base_dir = dirname(filen_copy);

    if (target && target[0] != '\0') {
        preprocess_set_target(target);
    } else {
        char *host = LLVMGetDefaultTargetTriple();
        preprocess_set_target(host);
        LLVMDisposeErrorMessage(host);
    }

    char *processed = preprocess(buffer, base_dir);
    free(buffer);
    if (!processed) return 1;

    if (g_sym && g_sym_c) {
        sym_buffer = g_sym;
        sym_c = g_sym_c;
    }
    if (overls && overls_c) {
        g_overs = overls;
        g_overs_c = overls_c;
    }

    if (!target || target[0] == '\0') {
        const char *src_target = preprocess_get_target();
        if (src_target) target = src_target;
    }

    vector_token *tokens       = lexing(processed);
    free(processed);
    vector_token *tokens_clean = extract_exceptions(tokens);
    vt_free(tokens); free(tokens);
    vector_token *tokens_ready = preparse(tokens_clean);
    vt_free(tokens_clean); free(tokens_clean);

    void **tmpp = parse(0, tokens_ready, flags & 0b0001, overls, overls_c);
    vector_node *nodes = tmpp[0];

    if (flags & 0b0001) {
        overls = tmpp[1];
        overls_c = tmpp[2];
        register_imported_module((OverloadEntry*)overls, *(int*)overls_c);
    }

    vt_free(tokens_ready); free(tokens_ready);

    pregen(nodes);
    void **tmp = codegen(nodes, input_file, sym_buffer, sym_c, link_flags, target, pass_list, flags);
    if (tmp != NULL && tmp != (void**)-1) { 
        g_sym = tmp[0];
        g_sym_c = tmp[1];
    } else {
        return 1; 
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Flame language\n\tNo arguments\n");
        return 1;
    }

    if (strcmp(argv[1], "-v") == 0) {
        printf("Flame language\n\t--%s\n", VERSION);
        return 0;
    }

    if (strcmp(argv[1], "-c") == 0) {
        if (argc < 3) {
            printf("Flame language\n\tUncorrect request\n");
            return 1;
        }

        const char *input_file = argv[2];
        char link_flags[512]   = "";
        const char *target     = NULL;

        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
                strncat(link_flags, " ",      sizeof(link_flags) - strlen(link_flags) - 1);
                strncat(link_flags, argv[++i], sizeof(link_flags) - strlen(link_flags) - 1);
            } else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
                strncat(link_flags, " -l",    sizeof(link_flags) - strlen(link_flags) - 1);
                strncat(link_flags, argv[++i], sizeof(link_flags) - strlen(link_flags) - 1);
            } else if (strncmp(argv[i], "-l", 2) == 0) {
                strncat(link_flags, " ",      sizeof(link_flags) - strlen(link_flags) - 1);
                strncat(link_flags, argv[i],  sizeof(link_flags) - strlen(link_flags) - 1);
            } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
                target = argv[++i];
            } else if (strcmp(argv[i], "-O") == 0) {
                g_flags |= 0b0100;
            } else if (strncmp(argv[i], "-d", 2) == 0) {
                g_flags |= 0b0010;
            } else if (strcmp(argv[i], "-p") == 0) {
                i++;
                int first_pass = 1;
                
                while (i < argc && argv[i][0] != '-') {
                    if (!first_pass) {
                        strncat(pass_list, ",", sizeof(pass_list) - strlen(pass_list) - 1);
                    }
                    char *op = argv[i];
                    if (strcmp(op, "O3") == 0) op = "default<O3>";
                    if (strcmp(op, "O2") == 0) op = "default<O2>";
                    if (strcmp(op, "O1") == 0) op = "default<O1>";
                    if (strcmp(op, "O0") == 0) op = "default<O0>";
                    strncat(pass_list, op, sizeof(pass_list) - strlen(pass_list) - 1);
                    first_pass = 0;
                    i++;
                }
                i--; // Возвращаемся на шаг назад, чтобы внешний цикл `for` корректно обработал следующий флаг
            } else {
                printf("Flame language\n\tUnknown flag '%s'\n", argv[i]);
                return 1;
            }
        }

        if (!target) target = "";

        return compile_file(input_file, link_flags, target, g_flags, g_sym, g_sym_c, g_overs, g_overs_c);
    }

    printf("Flame language\n\tUnknown command\n");
    return 1;
}