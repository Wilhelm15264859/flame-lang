#include "fl_Lexer.h"
#include "fl_Parser.h"
#include "fl_Builder.h"
#include "fl_Preproc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libgen.h>

const char* version = "0.1.0-BETA";

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
        char link_flags[512] = "";

        // собираем -l флаги начиная с argv[3]
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
                strncat(link_flags, " -l", sizeof(link_flags) - strlen(link_flags) - 1);
                strncat(link_flags, argv[++i], sizeof(link_flags) - strlen(link_flags) - 1);
            } else if (strncmp(argv[i], "-l", 2) == 0) {
                strncat(link_flags, " ", sizeof(link_flags) - strlen(link_flags) - 1);
                strncat(link_flags, argv[i], sizeof(link_flags) - strlen(link_flags) - 1);
            } else {
                printf("Flame language\n\tUnknown flag '%s'\n", argv[i]);
                return 1;
            }
        }

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

        char *processed = preprocess(buffer, base_dir);
        free(buffer);
        if (!processed) {
            printf("Flame language\n\tPreprocessor failed\n");
            return 1;
        }

        vector_token *tokens = lexing(processed);
        free(processed);

        vector_node *nodes = parse(0, tokens);
        free(tokens);

        codegen(nodes, input_file, link_flags);
        return 0;
    }

    printf("Flame language\n\tUnknown command\n");
    return 1;
}