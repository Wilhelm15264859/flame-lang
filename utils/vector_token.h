#ifndef VECTOR_TOKEN_H
#define VECTOR_TOKEN_H

#include "elements.h"
#include <stdlib.h>

typedef struct {
    Token             *data;
    unsigned long long size;
    unsigned long long capacity;
} vector_token;

int  vt_init      (vector_token *v, unsigned long long init_capac);
void vt_free      (vector_token *v);
int  vt_push_back (vector_token *v, Token t);
int  vt_pop_back  (vector_token *v, Token *out);  /* кладёт результат в out */

#endif /* VECTOR_TOKEN_H */