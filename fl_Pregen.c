#include "fl_Pregen.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_AUTODEL 64
#define MAX_OWNERSHIP_FUNCS 64
#define MAX_ALIASES 16

static int  autodel_counter      = 0;
static int  ownership_func_count = 0;
static char ownership_funcs[MAX_OWNERSHIP_FUNCS][64];

static void err() {
    long ret;
    __asm__ __volatile__ (
        "syscall"
        : "=a"(ret)
        : "a"(60), "D"(1)
    );
}

static int has_inline_new(Node *n) {
    if (!n) return 0;
    if (n->type == NODE_NEW) return 1;
    if (n->childs)
        for (unsigned long long j = 0; j < n->childs->size; j++)
            if (has_inline_new(&n->childs->data[j])) return 1;
    return 0;
}

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

static void vn_insert_at(vector_node *nodes, int pos, Node n) {
    vn_push_back(nodes, n);
    for (int j = (int)nodes->size - 1; j > pos; j--)
        nodes->data[j] = nodes->data[j - 1];
    nodes->data[pos] = n;
}

static Node *make_autodel_tempvar(const char *tmp_name, const char *class_name, Node *new_node) {
    Node *var = make_node(NODE_VAR_DEF, "");
    
    char type_str[80];
    snprintf(type_str, sizeof(type_str), "autodel:%s*", class_name);
    Node *type  = make_node(NODE_TYPE,  type_str);
    Node *ident = make_node(NODE_IDENT, tmp_name);
    
    vn_push_back(var->childs, *type);     free(type);
    vn_push_back(var->childs, *ident);    free(ident);
    vn_push_back(var->childs, *new_node);
    
    var->type = NODE_VAR_DEF;
    return var;
}

static void replace_inline_new(Node *n, 
                                char tmp_names[64][64],
                                Node *new_nodes[64],
                                int *count)
{
    if (!n || !n->childs) return;
    for (unsigned long long j = 0; j < n->childs->size; j++) {
        Node *child = &n->childs->data[j];
        if (child->type == NODE_NEW) {
            char tmp_name[64];
            snprintf(tmp_name, sizeof(tmp_name), "__inew_%d", autodel_counter++);
            
            Node *new_copy = malloc(sizeof(Node));
            *new_copy = *child;
            new_nodes[*count] = new_copy;
            strncpy(tmp_names[*count], tmp_name, 63);
            (*count)++;
            
            free(child->str);
            child->str   = malloc(strlen(tmp_name) + 1);
            strcpy(child->str, tmp_name);
            child->type  = NODE_VAR;
            child->childs = malloc(sizeof(vector_node));
            vn_init(child->childs, 1);
        } else {
            replace_inline_new(child, tmp_names, new_nodes, count);
        }
    }
}

static void pregen_inline_new(vector_node *nodes, int start, int end) {
    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];
        
        if (n->type == NODE_SCOPE) {
            pregen_inline_new(n->childs, 0, (int)n->childs->size);
            continue;
        }
        if ((n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF)
             && n->childs->size >= 4) {
            Node *scope = &n->childs->data[3];
            if (scope->type == NODE_SCOPE)
                pregen_inline_new(scope->childs, 0, (int)scope->childs->size);
            continue;
        }
        if ((n->type == NODE_IF    || n->type == NODE_WHILE ||
             n->type == NODE_FOR   || n->type == NODE_DO_WHILE) && n->childs) {
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if (child->type == NODE_SCOPE && child->childs)
                    pregen_inline_new(child->childs, 0, (int)child->childs->size);
            }
            continue;
        }

        if (n->type != NODE_FUNC_CALL) continue;
        if (!has_inline_new(n)) continue;

        char tmp_names[64][64];
        Node *new_nodes[64];
        int   count = 0;
        memset(tmp_names, 0, sizeof(tmp_names));
        memset(new_nodes, 0, sizeof(new_nodes));

        replace_inline_new(n, tmp_names, new_nodes, &count);
        if (count == 0) continue;

        for (int k = count - 1; k >= 0; k--) {
            const char *class_name = new_nodes[k]->str;
            Node *tmpvar = make_autodel_tempvar(tmp_names[k], class_name, new_nodes[k]);
            vn_insert_at(nodes, j, *tmpvar);
            free(tmpvar);
            end++;
            j++;
        }
    }
}

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
    char aliases[MAX_ALIASES][64];
    int  alias_count;
} AutodelVar;

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

static int node_uses_any_alias(Node *n, AutodelVar *av) {
    if (node_uses_var(n, av->name)) return 1;
    for (int k = 0; k < av->alias_count; k++)
        if (node_uses_var(n, av->aliases[k])) return 1;
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

static int find_autodel_by_name(AutodelVar *vars, int count, const char *name) {
    for (int i = 0; i < count; i++)
        if (vars[i].name[0] != '\0' && strcmp(vars[i].name, name) == 0)
            return i;
    return -1;
}

static void register_alias(AutodelVar *vars, int count, int ai, const char *alias_name) {
    for (int i = 0; i < count; i++) {
        if (i == ai) continue;
        for (int k = 0; k < vars[i].alias_count; k++) {
            if (strcmp(vars[i].aliases[k], alias_name) == 0) {
                printf("DEBUG pregen: merging alias group '%s' into '%s'\n",
                       vars[i].name, vars[ai].name);
                for (int m = 0; m < vars[i].alias_count; m++) {
                    int dup = 0;
                    for (int n2 = 0; n2 < vars[ai].alias_count; n2++)
                        if (strcmp(vars[ai].aliases[n2], vars[i].aliases[m]) == 0) { dup = 1; break; }
                    if (!dup && vars[ai].alias_count < MAX_ALIASES)
                        strncpy(vars[ai].aliases[vars[ai].alias_count++], vars[i].aliases[m], 63);
                }
                if (vars[ai].alias_count < MAX_ALIASES)
                    strncpy(vars[ai].aliases[vars[ai].alias_count++], vars[i].name, 63);
                vars[i].name[0] = '\0';
                return;
            }
        }
    }

    for (int k = 0; k < vars[ai].alias_count; k++)
        if (strcmp(vars[ai].aliases[k], alias_name) == 0) return;
    if (vars[ai].alias_count < MAX_ALIASES) {
        strncpy(vars[ai].aliases[vars[ai].alias_count++], alias_name, 63);
        printf("DEBUG pregen: '%s' is now an alias of autodel '%s'\n",
               alias_name, vars[ai].name);
    }
}

static int node_is_simple_var_assign(Node *n) {
    if (n->type != NODE_ASSIGN) return 0;
    if (n->childs->size < 1) return 0;
    const char *dest = n->childs->data[0].str;
    if (!dest || dest[0] == '\0') return 0;
    return 1;
}

static void collect_aliases(vector_node *nodes, int start, int end,
                             AutodelVar *vars, int count)
{
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int j = start; j < end; j++) {
            Node *n = &nodes->data[j];

            if (n->type == NODE_VAR_DEF && n->childs->size >= 3) {
                Node *init = &n->childs->data[2];
                if (init->type == NODE_VAR && init->str && init->str[0]) {
                    const char *src  = init->str;
                    const char *dest = n->childs->data[1].str;
                    if (!dest || dest[0] == '\0') continue;

                    int ai = find_autodel_by_name(vars, count, src);
                    if (ai < 0) {
                        for (int i = 0; i < count && ai < 0; i++)
                            for (int k = 0; k < vars[i].alias_count; k++)
                                if (strcmp(vars[i].aliases[k], src) == 0) { ai = i; break; }
                    }
                    if (ai >= 0) {
                        int already = 0;
                        for (int k = 0; k < vars[ai].alias_count; k++)
                            if (strcmp(vars[ai].aliases[k], dest) == 0) { already = 1; break; }
                        if (!already) {
                            register_alias(vars, count, ai, dest);
                            changed = 1;
                        }
                    }
                }
            }

            if (n->type == NODE_ASSIGN && node_is_simple_var_assign(n) && n->childs->size >= 3) {
                Node *rhs = &n->childs->data[2];
                if (rhs->type == NODE_VAR && rhs->str && rhs->str[0]) {
                    const char *src  = rhs->str;
                    const char *dest = n->childs->data[0].str;
                    if (!dest || dest[0] == '\0') continue;

                    int ai = find_autodel_by_name(vars, count, src);
                    if (ai < 0) {
                        for (int i = 0; i < count && ai < 0; i++)
                            for (int k = 0; k < vars[i].alias_count; k++)
                                if (strcmp(vars[i].aliases[k], src) == 0) { ai = i; break; }
                    }
                    if (ai >= 0) {
                        int already = 0;
                        for (int k = 0; k < vars[ai].alias_count; k++)
                            if (strcmp(vars[ai].aliases[k], dest) == 0) { already = 1; break; }
                        if (!already) {
                            register_alias(vars, count, ai, dest);
                            changed = 1;
                        }
                    }
                }
            }
        }
    }
}

static void pregen_scope(vector_node *nodes, int start, int end) {
    pregen_inline_new(nodes, start, end);
    end = (int)nodes->size;

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
        autodel_vars[autodel_count].alias_count = 0;
        autodel_count++;
        printf("DEBUG pregen: autodel var '%s' base '%s'\n", var_name, base);
    }

    collect_aliases(nodes, start, end, autodel_vars, autodel_count);

    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];

        if (n->type == NODE_VAR_DEF && n->childs->size >= 3) {
            Node *type_node = &n->childs->data[0];
            Node *init_node = &n->childs->data[2];

            if (strncmp(type_node->str, "autodel:", 8) == 0) continue;
            if (init_node->type != NODE_VAR) continue;
            if (!n->childs->data[1].str || n->childs->data[1].str[0] == '\0') continue;

            if (find_autodel_by_name(autodel_vars, autodel_count,
                                    init_node->str) >= 0) {
                printf("Error [pregen]: cannot assign autodel variable '%s' "
                    "to non-owning 'notdel' variable '%s' — "
                    "use autodel or ensure lifetime is safe\n",
                    init_node->str,
                    n->childs->data[1].str);
                err();
            }
        }

        if (n->type == NODE_ASSIGN && node_is_simple_var_assign(n) && n->childs->size >= 3) {
            Node *rhs = &n->childs->data[2];
            if (rhs->type != NODE_VAR) continue;

            if (find_autodel_by_name(autodel_vars, autodel_count, rhs->str) < 0)
                continue;

            const char *dest = n->childs->data[0].str;
            if (!dest || dest[0] == '\0') continue;

            if (find_autodel_by_name(autodel_vars, autodel_count, dest) < 0) {
                printf("Error [pregen]: cannot assign autodel variable '%s' "
                    "to non-owning variable '%s'\n",
                    rhs->str, dest);
                err();
            }
        }
    }

    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];
        if (n->type != NODE_RETURN || n->childs->size == 0) continue;
        Node *ret_expr = &n->childs->data[0];
        if (ret_expr->type == NODE_UNDEF) continue;

        for (int ai = 0; ai < autodel_count; ai++) {
            const char *aname = autodel_vars[ai].name;
            if (!aname || aname[0] == '\0') continue;
            if (!node_uses_any_alias(ret_expr, &autodel_vars[ai])) continue;

            int direct_escape = 0;
            if (ret_expr->type == NODE_VAR) {
                if (strcmp(ret_expr->str, aname) == 0) direct_escape = 1;
                for (int k = 0; k < autodel_vars[ai].alias_count && !direct_escape; k++)
                    if (strcmp(ret_expr->str, autodel_vars[ai].aliases[k]) == 0)
                        direct_escape = 1;
            }

            if (direct_escape) {
                printf("DEBUG pregen: '%s' (or alias) escapes via return\n", aname);
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
        if (!name || name[0] == '\0') continue;
        int last = -1;
        for (int j = start; j < end; j++)
            if (node_uses_any_alias(&nodes->data[j], &autodel_vars[ai]))
                last = j;
        if (last < 0) continue;

        printf("DEBUG pregen: delete '%s' after index %d (aliases: %d)\n",
               name, last, autodel_vars[ai].alias_count);
        for (int k = 0; k < autodel_vars[ai].alias_count; k++)
            printf("  alias: '%s'\n", autodel_vars[ai].aliases[k]);

        Node del = make_delete_node(name);
        vn_insert_at(nodes, last + 1, del);
        end++;
    }
}

void pregen(vector_node *nodes) {
    autodel_counter      = 0;
    ownership_func_count = 0;

    pregen_inline_new(nodes, 0, (int)nodes->size);

    int prev = -1;
    while (prev != ownership_func_count) {
        prev = ownership_func_count;
        collect_ownership(nodes, 0, (int)nodes->size);
        patch_ownership_vardefs(nodes, 0, (int)nodes->size);
    }

    pregen_scope(nodes, 0, (int)nodes->size);
}