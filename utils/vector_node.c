#include "vector_node.h"

int vn_init(vector_node *v, unsigned long long init_capac) {
    v->size     = 0;
    v->capacity = (init_capac > 0) ? init_capac : 4;
    v->data     = malloc(v->capacity * sizeof(Node));
    return (v->data != NULL) ? 0 : 1;
}

void vn_free(vector_node *v) {
    free(v->data);
    v->data     = NULL;
    v->size     = 0;
    v->capacity = 0;
}

int vn_push_back(vector_node *v, Node t) {
    if (v->size == v->capacity) {
        unsigned long long new_cap = v->capacity * 2;
        Node *temp = realloc(v->data, new_cap * sizeof(Node));
        if (temp == NULL) return 1;
        v->data     = temp;
        v->capacity = new_cap;
    }
    v->data[v->size++] = t;
    return 0;
}

int vn_pop_back(vector_node *v, Node *out) {
    if (v->size == 0) return 1;
    v->size--;
    *out = v->data[v->size];
    return 0;
}