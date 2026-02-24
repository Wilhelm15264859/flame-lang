#ifndef VECTOR_NODE_H
#define VECTOR_NODE_H

#include "elements.h"
#include <stdlib.h>

typedef struct vector_node {
    Node             *data;
    unsigned long long size;
    unsigned long long capacity;
} vector_node;

int  vn_init      (vector_node *v, unsigned long long init_capac);
void vn_free      (vector_node *v);
int  vn_push_back (vector_node *v, Node t);
int  vn_pop_back  (vector_node *v, Node *out);

#endif