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

const char* version = "2.3.0-STABLE";

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Flame language\n\tNo arguments\n");
        return 1;
    }

    if (strcmp(argv[1], "-v") == 0) {
        printf("Flame language\n\t--%s\n", version);
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
            } else {
                printf("Flame language\n\tUnknown flag '%s'\n", argv[i]);
                return 1;
            }
        }

        if (!target) target = "";

        char filen[256];
        snprintf(filen, sizeof(filen), "%s.fl", input_file);

        FILE *file = fopen(filen, "rb");
        if (!file) {
            perror("fopen");
            printf("Flame language\n\tCannot open file\n");
            return 1;
        }

        fseek(file, 0, SEEK_END);
        unsigned long long length = ftell(file);
        fseek(file, 0, SEEK_SET);
        
        char *buffer = malloc(length + 1);
        if (!buffer) {
            fclose(file);
            printf("Flame language\n\tCannot read file\n");
            return 1;
        }
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
        if (!processed) {
            printf("Flame language\n\tPreprocessor failed\n");
            return 1;
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

        vector_node *nodes = parse(0, tokens_ready);
        vt_free(tokens_ready); free(tokens_ready);

        pregen(nodes);
        codegen(nodes, input_file, link_flags, target);

        return 0;
    }

    printf("Flame language\n\tUnknown command\n");
    return 1;
}