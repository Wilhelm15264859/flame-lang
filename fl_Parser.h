#ifndef PARSER_H
#define PARSER_H

#include "utils/vector_node.h"
#include "utils/vector_token.h"
#include "utils/elements.h"

#define MAX_OVERLOAD_PARAMS  16

typedef struct {
    char base_name[64];
    char mangled[128];
    char param_types[MAX_OVERLOAD_PARAMS][64];
    int  param_count;
    char ret_type[64];
    int  is_static;
} OverloadEntry;

void **parse(int it, vector_token* tokenss, int is_import, void *overloads, int *overloads_c);
void register_imported_module(OverloadEntry *overloads, int count);
OverloadEntry* find_global_overload(const char* base_name, int* out_count);

#endif