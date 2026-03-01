#include "fl_Pregen.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_AUTODEL 64

static Node *make_node(NodeType type, const char *str) {
    Node *n = malloc(sizeof(Node));
    n->type = type;
    n->childs = malloc(sizeof(vector_node));
    vn_init(n->childs, 2);
    
    if (str) {
        n->str = malloc(strlen(str) + 1);
        strcpy(n->str, str);
    } else {
        n->str = malloc(1);
        n->str[0] = '\0';
    }
    return n;
}

static int node_uses_var(Node *n, const char *name) {
    if (!n) return 0;
    if ((n->type == NODE_VAR || n->type == NODE_ADDR ||
         n->type == NODE_ASSIGN || n->type == NODE_IDENT) &&
        n->str && strcmp(n->str, name) == 0)
        return 1;
    if (n->childs) {
        for (unsigned long long j = 0; j < n->childs->size; j++)
            if (node_uses_var(&n->childs->data[j], name))
                return 1;
    }
    return 0;
}

static Node make_delete_node(const char *name) {
    Node del;
    del.childs = malloc(sizeof(vector_node));
    vn_init(del.childs, 2);
    del.str  = NULL;
    del.type = NODE_DELETE;

    Node *args = make_node(NODE_ARGS, "");
    vn_push_back(del.childs, *args);
    free(args);

    Node *var = make_node(NODE_VAR, name);
    vn_push_back(del.childs, *var);
    free(var);

    return del;
}

static void vn_insert_after(vector_node *nodes, int pos, Node n) {
    vn_push_back(nodes, n);
    for (int j = (int)nodes->size - 1; j > pos + 1; j--)
        nodes->data[j] = nodes->data[j - 1];
    nodes->data[pos + 1] = n;
}

static void pregen_scope(vector_node *nodes, int start, int end);

static int node_is_block(Node *n) {
    return n->type == NODE_FOR    ||
           n->type == NODE_WHILE  ||
           n->type == NODE_DO_WHILE;
}

static void pregen_scope(vector_node *nodes, int start, int end) {
    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];

        if (n->type == NODE_FUNC_DEF && n->childs->size >= 4) {
            Node *scope = &n->childs->data[3];
            if (scope->type == NODE_SCOPE)
                pregen_scope(scope->childs, 0, (int)scope->childs->size);
        }
        else if (n->type == NODE_SCOPE) {
            pregen_scope(n->childs, 0, (int)n->childs->size);
        }
    }

    char autodel_in_scope[MAX_AUTODEL][64];
    int  autodel_in_scope_count = 0;

    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];
        if (n->type != NODE_VAR_DEF) continue;
        if (n->childs->size < 2) continue;

        const char *type_str = n->childs->data[0].str;
        if (strncmp(type_str, "autodel:", 8) != 0) continue;

        const char *var_name = n->childs->data[1].str;
        strncpy(autodel_in_scope[autodel_in_scope_count++], var_name, 63);
        printf("DEBUG pregen: found autodel var '%s' type '%s'\n", var_name, type_str);
    }

    for (int ai = 0; ai < autodel_in_scope_count; ai++) {
        const char *name = autodel_in_scope[ai];

        int last = -1;
        for (int j = start; j < end; j++) {
            Node *n = &nodes->data[j];

            if (node_is_block(n)) {
                if (node_uses_var(n, name))
                    last = j;
            } else {
                if (node_uses_var(n, name))
                    last = j;
            }
        }

        if (last < 0) continue;

        printf("DEBUG pregen: autodel '%s' last use at index %d\n", name, last);

        Node del = make_delete_node(name);
        vn_insert_after(nodes, last, del);
        end++;
    }
}

void pregen(vector_node *nodes) {
    pregen_scope(nodes, 0, (int)nodes->size);
}