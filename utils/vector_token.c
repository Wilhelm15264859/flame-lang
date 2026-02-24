#include "vector_token.h"

int vt_init(vector_token *v, unsigned long long init_capac) {
    v->size     = 0;
    v->capacity = (init_capac > 0) ? init_capac : 4;
    v->data     = malloc(v->capacity * sizeof(Token));
    return (v->data != NULL) ? 0 : 1;
}

void vt_free(vector_token *v) {
    free(v->data);
    v->data     = NULL;
    v->size     = 0;
    v->capacity = 0;
}

int vt_push_back(vector_token *v, Token t) {
    if (v->size == v->capacity) {
        unsigned long long new_cap = v->capacity * 2;
        Token *temp = realloc(v->data, new_cap * sizeof(Token));
        if (temp == NULL) return 1;  /* старый v->data не тронут */
        v->data     = temp;
        v->capacity = new_cap;
    }
    v->data[v->size++] = t;
    return 0;
}

/* возвращает 0 и кладёт токен в *out
   возвращает 1 если вектор пуст       */
int vt_pop_back(vector_token *v, Token *out) {
    if (v->size == 0) return 1;
    v->size--;
    *out = v->data[v->size];
    return 0;
}