#include "utils/vector_node.h"

void **codegen(vector_node *nodes, const char *out_file, void *sym, int *sym_c,
             const char *extra_link_flags, const char *target_triple, const char *passes, char is_import);