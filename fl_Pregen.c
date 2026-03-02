#include "fl_Pregen.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_AUTODEL 64
#define MAX_OWNERSHIP_FUNCS 64

static int  autodel_counter      = 0;
static int  ownership_func_count = 0;
static char ownership_funcs[MAX_OWNERSHIP_FUNCS][64];

static void ownership_func_push(const char *name) {
    for (int i = 0; i < ownership_func_count; i++)
        if (strcmp(ownership_funcs[i], name) == 0) return;
    if (ownership_func_count >= MAX_OWNERSHIP_FUNCS) return;
    strncpy(ownership_funcs[ownership_func_count++], name, 63);
    printf("DEBUG pregen: function '%s' transfers ownership\n", name);
}

static int ownership_func_lookup(const char *name) {
    for (int i = 0; i < ownership_func_count; i++)
        if (strcmp(ownership_funcs[i], name) == 0) return 1;
    return 0;
}

typedef struct {
    char name[64];
    char base_type[64];
} AutodelVar;

static Node *make_node(NodeType type, const char *str) {
    Node *n = malloc(sizeof(Node));
    n->type   = type;
    n->childs = malloc(sizeof(vector_node));
    vn_init(n->childs, 2);
    if (str) {
        n->str = malloc(strlen(str) + 1);
        strcpy(n->str, str);
    } else {
        n->str    = malloc(1);
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
    if (n->childs)
        for (unsigned long long j = 0; j < n->childs->size; j++)
            if (node_uses_var(&n->childs->data[j], name))
                return 1;
    return 0;
}

static Node make_delete_node(const char *name) {
    Node del;
    del.childs = malloc(sizeof(vector_node));
    vn_init(del.childs, 2);
    del.str  = NULL;
    del.type = NODE_DELETE;
    Node *args = make_node(NODE_ARGS, "");
    vn_push_back(del.childs, *args); free(args);
    Node *var  = make_node(NODE_VAR, name);
    vn_push_back(del.childs, *var);  free(var);
    return del;
}

static Node make_tempvar_node(const char *tmp_name, const char *type_str, Node *expr) {
    Node var;
    var.childs = malloc(sizeof(vector_node));
    vn_init(var.childs, 3);
    var.str  = NULL;
    var.type = NODE_VAR_DEF;
    Node *type  = make_node(NODE_TYPE,  type_str); vn_push_back(var.childs, *type);  free(type);
    Node *ident = make_node(NODE_IDENT, tmp_name); vn_push_back(var.childs, *ident); free(ident);
    vn_push_back(var.childs, *expr);
    return var;
}

static Node make_return_var_node(const char *tmp_name) {
    Node ret;
    ret.childs = malloc(sizeof(vector_node));
    vn_init(ret.childs, 1);
    ret.str  = NULL;
    ret.type = NODE_RETURN;
    Node *var = make_node(NODE_VAR, tmp_name);
    vn_push_back(ret.childs, *var); free(var);
    return ret;
}

static void vn_insert_at(vector_node *nodes, int pos, Node n) {
    vn_push_back(nodes, n);
    for (int j = (int)nodes->size - 1; j > pos; j--)
        nodes->data[j] = nodes->data[j - 1];
    nodes->data[pos] = n;
}

static int node_is_block(Node *n) {
    return n->type == NODE_FOR     ||
           n->type == NODE_WHILE   ||
           n->type == NODE_DO_WHILE;
}

static void collect_autodel_names_in_scope(vector_node *nodes,
                                            char out[MAX_AUTODEL][64],
                                            int *count)
{
    for (unsigned long long j = 0; j < nodes->size; j++) {
        Node *n = &nodes->data[j];
        if (n->type == NODE_VAR_DEF && n->childs->size >= 2) {
            const char *ts = n->childs->data[0].str;
            if (strncmp(ts, "autodel:", 8) == 0 && *count < MAX_AUTODEL)
                strncpy(out[(*count)++], n->childs->data[1].str, 63);
        }
        if (n->type == NODE_SCOPE && n->childs)
            collect_autodel_names_in_scope(n->childs, out, count);
        if ((n->type == NODE_IF    || n->type == NODE_WHILE ||
             n->type == NODE_FOR   || n->type == NODE_DO_WHILE) && n->childs)
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if (child->type == NODE_SCOPE && child->childs)
                    collect_autodel_names_in_scope(child->childs, out, count);
            }
    }
}

static void collect_returns_in_scope(vector_node *nodes,
                                      char autodel_names[MAX_AUTODEL][64],
                                      int autodel_count,
                                      const char *fname)
{
    for (unsigned long long j = 0; j < nodes->size; j++) {
        Node *n = &nodes->data[j];

        if (n->type == NODE_RETURN && n->childs->size > 0) {
            Node *ret_expr = &n->childs->data[0];
            if (ret_expr->type == NODE_VAR)
                for (int ai = 0; ai < autodel_count; ai++)
                    if (strcmp(ret_expr->str, autodel_names[ai]) == 0) {
                        ownership_func_push(fname);
                        return;
                    }
        }
        if (n->type == NODE_SCOPE && n->childs)
            collect_returns_in_scope(n->childs, autodel_names, autodel_count, fname);
        if ((n->type == NODE_IF    || n->type == NODE_WHILE ||
             n->type == NODE_FOR   || n->type == NODE_DO_WHILE) && n->childs)
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if (child->type == NODE_SCOPE && child->childs)
                    collect_returns_in_scope(child->childs, autodel_names,
                                             autodel_count, fname);
            }
    }
}

static void collect_ownership_func(Node *func_node) {
    if (func_node->childs->size < 4) return;
    const char *fname = func_node->childs->data[1].str;
    Node *scope = &func_node->childs->data[3];
    if (scope->type != NODE_SCOPE) return;

    char autodel_names[MAX_AUTODEL][64];
    int  autodel_count = 0;
    collect_autodel_names_in_scope(scope->childs, autodel_names, &autodel_count);
    if (autodel_count == 0) return;

    collect_returns_in_scope(scope->childs, autodel_names, autodel_count, fname);
}

static void collect_ownership(vector_node *nodes, int start, int end) {
    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];
        if (n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF)
            collect_ownership_func(n);
        else if (n->type == NODE_SCOPE)
            collect_ownership(n->childs, 0, (int)n->childs->size);
    }
}

static void patch_ownership_vardefs(vector_node *nodes, int start, int end) {
    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];

        if (n->type == NODE_SCOPE) {
            patch_ownership_vardefs(n->childs, 0, (int)n->childs->size);
            continue;
        }
        if ((n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF)
             && n->childs->size >= 4) {
            Node *scope = &n->childs->data[3];
            if (scope->type == NODE_SCOPE)
                patch_ownership_vardefs(scope->childs, 0, (int)scope->childs->size);
            continue;
        }
        if ((n->type == NODE_IF    || n->type == NODE_WHILE ||
             n->type == NODE_FOR   || n->type == NODE_DO_WHILE) && n->childs) {
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if (child->type == NODE_SCOPE && child->childs)
                    patch_ownership_vardefs(child->childs, 0, (int)child->childs->size);
            }
            continue;
        }

        if (n->type != NODE_VAR_DEF || n->childs->size < 3) continue;

        Node *type_node = &n->childs->data[0];
        Node *init_node = &n->childs->data[2];

        if (strncmp(type_node->str, "autodel:", 8) == 0) continue;
        if (init_node->type != NODE_FUNC_CALL) continue;

        const char *callee = init_node->str;
        if (!callee || !ownership_func_lookup(callee)) continue;

        char new_type[80];
        snprintf(new_type, sizeof(new_type), "autodel:%s", type_node->str);
        free(type_node->str);
        type_node->str = malloc(strlen(new_type) + 1);
        strcpy(type_node->str, new_type);

        printf("DEBUG pregen: patched var '%s' -> '%s' (from '%s')\n",
               n->childs->data[1].str, new_type, callee);
    }
}

static void pregen_scope(vector_node *nodes, int start, int end) {
    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];
        if (n->type == NODE_FUNC_DEF && n->childs->size >= 4) {
            Node *scope = &n->childs->data[3];
            if (scope->type == NODE_SCOPE)
                pregen_scope(scope->childs, 0, (int)scope->childs->size);
        } else if (n->type == NODE_SCOPE) {
            pregen_scope(n->childs, 0, (int)n->childs->size);
        } else if (node_is_block(n) && n->childs) {
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if (child->type == NODE_SCOPE && child->childs)
                    pregen_scope(child->childs, 0, (int)child->childs->size);
                else if (child->type == NODE_VAR_DEF)
                    pregen_scope(n->childs, (int)k, (int)k + 1);
            }
        } else if (n->type == NODE_IF && n->childs) {
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if (child->type == NODE_SCOPE && child->childs)
                    pregen_scope(child->childs, 0, (int)child->childs->size);
                else if (child->type == NODE_ELSE && child->childs)
                    for (unsigned long long m = 0; m < child->childs->size; m++) {
                        Node *ec = &child->childs->data[m];
                        if (ec->type == NODE_SCOPE && ec->childs)
                            pregen_scope(ec->childs, 0, (int)ec->childs->size);
                    }
            }
        }
    }

    AutodelVar autodel_vars[MAX_AUTODEL];
    int autodel_count = 0;

    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];
        if (n->type != NODE_VAR_DEF || n->childs->size < 2) continue;
        const char *type_str = n->childs->data[0].str;
        if (strncmp(type_str, "autodel:", 8) != 0) continue;

        const char *var_name = n->childs->data[1].str;
        char base[64] = {0};
        strncpy(base, type_str + 8, 63);
        int blen = strlen(base);
        if (blen > 0 && base[blen - 1] == '*') base[blen - 1] = '\0';

        strncpy(autodel_vars[autodel_count].name,      var_name, 63);
        strncpy(autodel_vars[autodel_count].base_type, base,     63);
        autodel_count++;
        printf("DEBUG pregen: autodel var '%s' base '%s'\n", var_name, base);
    }

    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];
        if (n->type != NODE_RETURN || n->childs->size == 0) continue;
        Node *ret_expr = &n->childs->data[0];
        if (ret_expr->type == NODE_UNDEF) continue;

        for (int ai = 0; ai < autodel_count; ai++) {
            const char *aname = autodel_vars[ai].name;
            if (aname[0] == '\0') continue;
            if (!node_uses_var(ret_expr, aname)) continue;

            if (ret_expr->type == NODE_VAR && strcmp(ret_expr->str, aname) == 0) {
                printf("DEBUG pregen: '%s' escapes\n", aname);
                autodel_vars[ai].name[0] = '\0';
                break;
            }

            char tmp_name[64];
            snprintf(tmp_name, sizeof(tmp_name), "__ret_%d", autodel_counter++);
            printf("DEBUG pregen: expanding return '%s' -> '%s'\n", aname, tmp_name);

            Node ret_expr_copy = *ret_expr;
            Node tmpvar  = make_tempvar_node(tmp_name, autodel_vars[ai].base_type, &ret_expr_copy);
            Node del     = make_delete_node(aname);
            Node new_ret = make_return_var_node(tmp_name);

            nodes->data[j] = new_ret;
            vn_insert_at(nodes, j, del);
            vn_insert_at(nodes, j, tmpvar);
            end += 2;
            j   += 2;

            autodel_vars[ai].name[0] = '\0';
            break;
        }
    }

    for (int ai = 0; ai < autodel_count; ai++) {
        const char *name = autodel_vars[ai].name;
        if (name[0] == '\0') continue;

        int last = -1;
        for (int j = start; j < end; j++)
            if (node_uses_var(&nodes->data[j], name)) last = j;
        if (last < 0) continue;

        printf("DEBUG pregen: delete '%s' after index %d\n", name, last);
        Node del = make_delete_node(name);
        vn_insert_at(nodes, last + 1, del);
        end++;
    }
}

void pregen(vector_node *nodes) {
    autodel_counter      = 0;
    ownership_func_count = 0;

    int prev = -1;
    while (prev != ownership_func_count) {
        prev = ownership_func_count;
        collect_ownership(nodes, 0, (int)nodes->size);
        patch_ownership_vardefs(nodes, 0, (int)nodes->size);
    }

    pregen_scope(nodes, 0, (int)nodes->size);
}