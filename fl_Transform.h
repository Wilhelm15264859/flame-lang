#pragma once
#include "fl_Parser.h"

void ssa_transform(vector_node *nodes);

void ssa_base_name(const char *versioned, char *buf, int buf_len);

static inline const char *ssa_strip(const char *name) {
    if (!name) return name;
    const char *h = name;
    while (*h && *h != '#') h++;
    return (*h == '#') ? name : name;
}

#define SSA_BASE_EQ(a, b)                          \
    ({ char _a[64], _b[64];                        \
       ssa_base_name((a), _a, 64);                 \
       ssa_base_name((b), _b, 64);                 \
       strcmp(_a, _b) == 0; })
