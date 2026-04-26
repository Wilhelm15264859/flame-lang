#include "fl_Pregen.h"
#include "fl_Parser.h"   /* OverloadEntry (with param_lifetimes), global_modules_* */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>   /* uint64_t — нужен для CFG-битсетов */

extern OverloadEntry* global_modules_overloads[];
extern int global_modules_counts[];
extern int num_imported_modules;

/* ═══════════════════════════════════════════════════════════════════
 * Константы
 * ═══════════════════════════════════════════════════════════════════ */
#define MAX_AUTODEL         64
#define MAX_OWNERSHIP_FUNCS 64
#define MAX_VADDR_VARS      64 
#define MAX_VADDR_NAMES     64 
#define MAX_FIELD_ALIASES   46
#define MAX_INDEX_ALIASES   64
#define MAX_VADDR_POOL      64 
#define MAX_GLOBAL_SLOTS    64
#define MAX_FUNC_PARAMS     32
#define MAX_FUNC_INDEX      64 
#define MAX_PARAM_ALIASES   16
#define MAX_PARAM_FIELDS    16

static size_t max_auotdel = MAX_AUTODEL;
static size_t max_ownership_funcs = MAX_OWNERSHIP_FUNCS;

/* ═══════════════════════════════════════════════════════════════════
 * Счётчик глобальных индексных переменных (_idx_N)
 * ═══════════════════════════════════════════════════════════════════ */
static int g_idx_counter = 0;

/* ═══════════════════════════════════════════════════════════════════
 * Глобальные счётчики
 * ═══════════════════════════════════════════════════════════════════ */
static int  autodel_counter     = 0;
static size_t  ownership_func_count = 0;
static char (*ownership_funcs)[64];

/* ═══════════════════════════════════════════════════════════════════
 * Пул виртуальных адресов
 * ═══════════════════════════════════════════════════════════════════ */
static int vaddr_pool_free[MAX_VADDR_POOL];
static int vaddr_pool_top  = 0;
static int vaddr_next_new  = 1;

static void vaddr_pool_init(void) {
    vaddr_pool_top = 0;
    vaddr_next_new = 1;
}

static int vaddr_pop(void) {
    if (vaddr_pool_top > 0)
        return vaddr_pool_free[--vaddr_pool_top];
    return vaddr_next_new++;
}

static void vaddr_push_free(int addr) {
    if (addr <= 0) return;
    if (vaddr_pool_top < MAX_VADDR_POOL)
        vaddr_pool_free[vaddr_pool_top++] = addr;
}

/* ═══════════════════════════════════════════════════════════════════
 * Индекс функций
 * ═══════════════════════════════════════════════════════════════════ */
typedef struct {
    char name[64];
    int  is_ptr;
} FuncParam;

typedef struct {
    char       name[64];
    int        index;
    FuncParam  params[MAX_FUNC_PARAMS];
    int        param_count;
    int        returns_ptr;
} FuncRecord;

static FuncRecord g_func_index[MAX_FUNC_INDEX];
static int        g_func_index_count = 0;

static void func_index_reset(void) {
    g_func_index_count = 0;
}

static FuncRecord *func_index_get_or_add(const char *name) {
    for (int i = 0; i < g_func_index_count; i++)
        if (strcmp(g_func_index[i].name, name) == 0)
            return &g_func_index[i];
    if (g_func_index_count >= MAX_FUNC_INDEX) {
        fprintf(stderr, "Error: buffer overflow -> out of memory\n");
        exit(1);
    };
    FuncRecord *r = &g_func_index[g_func_index_count];
    strncpy(r->name, name, 63); r->name[63] = '\0';
    r->index       = g_func_index_count;
    r->param_count = 0;
    r->returns_ptr = 0;
    g_func_index_count++;
    return r;
}

static FuncRecord *func_index_find(const char *name) __attribute__((unused));
static FuncRecord *func_index_find(const char *name) {
    for (int i = 0; i < g_func_index_count; i++)
        if (strcmp(g_func_index[i].name, name) == 0)
            return &g_func_index[i];
    return NULL;
}

static void func_index_register_params(FuncRecord *r, Node *params_node) {
    if (!params_node || params_node->type != NODE_PARAMS) return;
    r->param_count = 0;
    for (unsigned long long i = 0; i < params_node->childs->size && r->param_count < MAX_FUNC_PARAMS; i++) {
        Node *p = &params_node->childs->data[i];
        if (p->type != NODE_VAR_DEF || !p->childs || p->childs->size < 2) continue;
        const char *type_str = p->childs->data[0].str ? p->childs->data[0].str : "";
        const char *nm       = p->childs->data[1].str ? p->childs->data[1].str : "";
        FuncParam *fp = &r->params[r->param_count++];
        strncpy(fp->name, nm, 63);     fp->name[63] = '\0';
        int tlen = (int)strlen(type_str);
        fp->is_ptr = (tlen > 0 && type_str[tlen - 1] == '*') ? 1 : 0;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Глобальный граф алиасов
 * ═══════════════════════════════════════════════════════════════════ */
typedef struct {
    char alias[64];
} ParamAliasEffect;

typedef struct {
    char obj[64];
    char field[64];
    int  is_arrow;
} ParamFieldEffect;

typedef struct {
    char func_name[64];
    int  param_idx;
    int  vaddr;
    int  escapes_out;
    char resolved_lifetime;
    ParamAliasEffect alias_effects[MAX_PARAM_ALIASES];
    int              alias_effect_count;
    ParamFieldEffect field_effects[MAX_PARAM_FIELDS];
    int              field_effect_count;
} GlobalArgAlias;

static GlobalArgAlias g_arg_aliases[MAX_GLOBAL_SLOTS];
static int            g_arg_alias_count = 0;

static void gaa_reset(void) {
    g_arg_alias_count = 0;
}

static GlobalArgAlias *gaa_get_or_push(const char *fn, int pidx, int vaddr) {
    for (int i = 0; i < g_arg_alias_count; i++)
        if (strcmp(g_arg_aliases[i].func_name, fn) == 0 &&
            g_arg_aliases[i].param_idx == pidx &&
            g_arg_aliases[i].vaddr     == vaddr)
            return &g_arg_aliases[i];
    if (g_arg_alias_count >= MAX_GLOBAL_SLOTS) return NULL;
    GlobalArgAlias *e = &g_arg_aliases[g_arg_alias_count++];
    strncpy(e->func_name, fn, 63); e->func_name[63] = '\0';
    e->param_idx          = pidx;
    e->vaddr              = vaddr;
    e->escapes_out        = 0;
    e->resolved_lifetime  = '\0';
    e->alias_effect_count = 0;
    e->field_effect_count = 0;
    return e;
}

static void gaa_push(const char *fn, int pidx, int vaddr) {
    gaa_get_or_push(fn, pidx, vaddr);
}

static void gaa_mark_escape(const char *fn, int vaddr) {
    for (int i = 0; i < g_arg_alias_count; i++)
        if (strcmp(g_arg_aliases[i].func_name, fn) == 0 &&
            g_arg_aliases[i].vaddr == vaddr)
            g_arg_aliases[i].escapes_out = 1;
}

static void gaa_add_alias_effect(const char *fn, int pidx, int vaddr, const char *alias_name) {
    GlobalArgAlias *e = gaa_get_or_push(fn, pidx, vaddr);
    if (!e) return;
    for (int i = 0; i < e->alias_effect_count; i++)
        if (strcmp(e->alias_effects[i].alias, alias_name) == 0) return;
    if (e->alias_effect_count >= MAX_PARAM_ALIASES) return;
    strncpy(e->alias_effects[e->alias_effect_count++].alias, alias_name, 63);
}

static void gaa_add_field_effect(const char *fn, int pidx, int vaddr,
                                  const char *obj, const char *field, int is_arrow) {
    GlobalArgAlias *e = gaa_get_or_push(fn, pidx, vaddr);
    if (!e) return;
    for (int i = 0; i < e->field_effect_count; i++)
        if (strcmp(e->field_effects[i].obj, obj) == 0 &&
            strcmp(e->field_effects[i].field, field) == 0 &&
            e->field_effects[i].is_arrow == is_arrow) return;
    if (e->field_effect_count >= MAX_PARAM_FIELDS) return;
    ParamFieldEffect *pfe = &e->field_effects[e->field_effect_count++];
    strncpy(pfe->obj,   obj,   63);
    strncpy(pfe->field, field, 63);
    pfe->is_arrow = is_arrow;
}

/* ═══════════════════════════════════════════════════════════════════
 * Вспомогательные узлы AST
 * ═══════════════════════════════════════════════════════════════════ */
static Node *make_node(NodeType type, const char *str) {
    Node *n   = malloc(sizeof(Node));
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

static Node *make_autodel_tempvar(const char *tmp_name, const char *class_name,
                                  Node *new_node) {
    Node *var = make_node(NODE_VAR_DEF, "");
    char type_str[80];
    snprintf(type_str, sizeof(type_str), "autodel:%s*", class_name);
    Node *type  = make_node(NODE_TYPE,  type_str);
    Node *ident = make_node(NODE_IDENT, tmp_name);
    vn_push_back(var->childs, *type);  free(type);
    vn_push_back(var->childs, *ident); free(ident);
    vn_push_back(var->childs, *new_node);
    return var;
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

/* ═══════════════════════════════════════════════════════════════════
 * Inline-new
 * ═══════════════════════════════════════════════════════════════════ */
static int has_inline_new(Node *n) {
    if (!n) return 0;
    if (n->type == NODE_NEW) return 1;
    if (n->childs)
        for (unsigned long long j = 0; j < n->childs->size; j++)
            if (has_inline_new(&n->childs->data[j])) return 1;
    return 0;
}

static void replace_inline_new(Node *n, char tmp_names[64][64],
                               Node *new_nodes[64], int *count) {
    if (!n || !n->childs) return;
    for (unsigned long long j = 0; j < n->childs->size; j++) {
        Node *child = &n->childs->data[j];
        if (child->type == NODE_NEW) {
            char tmp_name[64];
            snprintf(tmp_name, sizeof(tmp_name), "__inew_%d", autodel_counter++);
            
            Node *new_copy = malloc(sizeof(Node));
            *new_copy = *child;
            /* FIX 1: Глубокое копирование строки во избежание UB */
            if (child->str) {
                new_copy->str = malloc(strlen(child->str) + 1);
                strcpy(new_copy->str, child->str);
            }

            new_nodes[*count] = new_copy;
            strncpy(tmp_names[*count], tmp_name, 63);
            (*count)++;
            
            free(child->str);
            child->str    = malloc(strlen(tmp_name) + 1);
            strcpy(child->str, tmp_name);
            child->type   = NODE_VAR;
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
        if ((n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF || 
             n->type == NODE_ASYNC_FUNC_DEF || n->type == NODE_AWAIT_FUNC_DEF) &&
            n->childs->size >= 4) {
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
        if (n->type != NODE_FUNC_CALL || !has_inline_new(n))
            continue;

        char  tmp_names[64][64];
        Node *new_nodes[64];
        int   count = 0;
        memset(tmp_names, 0, sizeof(tmp_names));
        memset(new_nodes, 0, sizeof(new_nodes));

        replace_inline_new(n, tmp_names, new_nodes, &count);
        if (count == 0) continue;

        for (int k = count - 1; k >= 0; k--) {
            const char *class_name = new_nodes[k]->str;
            Node *tmpvar = make_autodel_tempvar(tmp_names[k], class_name,
                                               new_nodes[k]);
            vn_insert_at(nodes, j, *tmpvar);
            free(tmpvar);
            end++;
            j++;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Ownership-функции
 * ═══════════════════════════════════════════════════════════════════ */
static void ownership_func_push(const char *name) {
    for (size_t i = 0; i < ownership_func_count; i++)
        if (strcmp(ownership_funcs[i], name) == 0) return;
    if (ownership_func_count >= max_ownership_funcs) {
        max_ownership_funcs *= 2;
        char (*re_ownership_funcs)[64] = realloc(ownership_funcs, max_ownership_funcs * 64);
        if (!re_ownership_funcs) {
            fprintf(stderr, "Error: buffer overflow -> out of memory\n");
            exit(1);
        }
        ownership_funcs = re_ownership_funcs;
    }
    strncpy(ownership_funcs[ownership_func_count++], name, 63);
}

static int ownership_func_lookup(const char *name) {
    for (size_t i = 0; i < ownership_func_count; i++)
        if (strcmp(ownership_funcs[i], name) == 0) return 1;
    return 0;
}

typedef char (*char_arr_ptr_8)[64];
static char_arr_ptr_8 collect_autodel_names_in_scope(vector_node *nodes, char (*out)[64], size_t *count) {
    for (unsigned long long j = 0; j < nodes->size; j++) {
        Node *n = &nodes->data[j];
        if (n->type == NODE_VAR_DEF && n->childs->size >= 2) {
            const char *ts = n->childs->data[0].str;

            if (*count >= max_auotdel) {
                max_auotdel *= 2;
                char (*re_out)[64] = realloc(out, max_auotdel * 64);
                if (!re_out) {
                    fprintf(stderr, "Error: buffer overflow -> out of memory\n");
                    exit(1);
                }
                out = re_out;
            }

            if (strncmp(ts, "autodel:", 8) == 0)
                strncpy(out[(*count)++], n->childs->data[1].str, 63);
        }
        if (n->type == NODE_SCOPE && n->childs)
            collect_autodel_names_in_scope(n->childs, out, count);
        if ((n->type == NODE_IF || n->type == NODE_WHILE || n->type == NODE_FOR || n->type == NODE_DO_WHILE) && n->childs)
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if (child->type == NODE_SCOPE && child->childs)
                    collect_autodel_names_in_scope(child->childs, out, count);
            }
    }
    return out;
}

static void collect_returns_in_scope(vector_node *nodes, char (*autodel_names)[64], int autodel_count, const char *fname) {
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
        if ((n->type == NODE_IF || n->type == NODE_WHILE || n->type == NODE_FOR || n->type == NODE_DO_WHILE) && n->childs)
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if (child->type == NODE_SCOPE && child->childs)
                    collect_returns_in_scope(child->childs, autodel_names, autodel_count, fname);
            }
    }
}

static void collect_ownership_func(Node *func_node) {
    if (func_node->childs->size < 4) return;
    const char *fname = func_node->childs->data[1].str;
    Node *scope = &func_node->childs->data[3];
    if (scope->type != NODE_SCOPE) return;
    char (*autodel_names)[64] = malloc(max_auotdel * 64);
    size_t  autodel_count = 0;
    autodel_names = collect_autodel_names_in_scope(scope->childs, autodel_names, &autodel_count);
    if (autodel_count == 0) {
        free(autodel_names); /* FIX 2: Утечка памяти компилятора */
        return;
    }
    collect_returns_in_scope(scope->childs, autodel_names, autodel_count, fname);
    free(autodel_names); /* FIX 2 */
}

static void collect_ownership(vector_node *nodes, int start, int end) {
    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];
        if (n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF || 
            n->type == NODE_ASYNC_FUNC_DEF || n->type == NODE_AWAIT_FUNC_DEF)
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
        if ((n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF || 
             n->type == NODE_ASYNC_FUNC_DEF || n->type == NODE_AWAIT_FUNC_DEF) &&
            n->childs->size >= 4) {
            Node *scope = &n->childs->data[3];
            if (scope->type == NODE_SCOPE)
                patch_ownership_vardefs(scope->childs, 0, (int)scope->childs->size);
            continue;
        }
        if ((n->type == NODE_IF || n->type == NODE_WHILE || n->type == NODE_FOR || n->type == NODE_DO_WHILE) && n->childs) {
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
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * AST Helpers
 * ═══════════════════════════════════════════════════════════════════ */
static const char *get_base_var(Node *n) {
    if (!n) return NULL;
    if (n->type == NODE_VAR || n->type == NODE_IDENT) return n->str;
    if (n->type == NODE_ADDR && n->str) return n->str;
    if (n->type == NODE_MEMBER_ARROW || n->type == NODE_MEMBER_DOT ||
        n->type == NODE_INDEX || n->type == NODE_ADDR_INDEX) {
        if (n->childs && n->childs->size > 0)
            return get_base_var(&n->childs->data[0]);
    }
    return NULL;
}

static int node_uses_var(Node *n, const char *name) {
    if (!n) return 0;
    if ((n->type == NODE_VAR  || n->type == NODE_ADDR  ||
         n->type == NODE_ASSIGN || n->type == NODE_IDENT) &&
        n->str && strcmp(n->str, name) == 0)
        return 1;
    if (n->childs)
        for (unsigned long long j = 0; j < n->childs->size; j++)
            if (node_uses_var(&n->childs->data[j], name))
                return 1;
    return 0;
}

static int node_is_block(Node *n) {
    return n->type == NODE_FOR || n->type == NODE_WHILE || n->type == NODE_DO_WHILE;
}

/* ═══════════════════════════════════════════════════════════════════
 * VAddrSlot — слот виртуального адреса
 * ═══════════════════════════════════════════════════════════════════ */
typedef struct {
    char obj[64];
    char field[64];
    int  is_arrow;
} FieldAlias;

typedef struct {
    char arr[64];
    char idx_var[64];
} IndexAlias;

#define MAX_MIRROR_ALIASES 32
typedef struct {
    char alias_var[64];
    char owner_obj[64];
    char owner_field[64];
    int  is_arrow;
    int  is_addr_of; 
} MirrorAlias;

typedef struct {
    int        vaddr;
    char       names[MAX_VADDR_NAMES][64];
    int        name_count;
    FieldAlias fields[MAX_FIELD_ALIASES];
    int        field_count;
    IndexAlias idx_aliases[MAX_INDEX_ALIASES];
    int        idx_alias_count;
    MirrorAlias mirrors[MAX_MIRROR_ALIASES];
    int         mirror_count;
    char       base_type[64];
    int        escaped;    
    vector_node *birth_vec;
    int          birth_idx;
} VAddrSlot;

/* ─── Операции над слотом ─────────────────────────────────────────── */

static void slot_add_name(VAddrSlot *s, const char *name) {
    for (int k = 0; k < s->name_count; k++)
        if (strcmp(s->names[k], name) == 0) return;
    if (s->name_count < MAX_VADDR_NAMES)
        strncpy(s->names[s->name_count++], name, 63);
    else {
        fprintf(stderr, "Error: buffer overflow -> out of memory\n");
        exit(1);
    }
}

static void slot_add_field(VAddrSlot *s, const char *obj, const char *field, int is_arrow) {
    for (int k = 0; k < s->field_count; k++)
        if (strcmp(s->fields[k].obj, obj) == 0 &&
            strcmp(s->fields[k].field, field) == 0 &&
            s->fields[k].is_arrow == is_arrow) return;
    if (s->field_count < MAX_FIELD_ALIASES) {
        strncpy(s->fields[s->field_count].obj,   obj,   63);
        strncpy(s->fields[s->field_count].field, field, 63);
        s->fields[s->field_count].is_arrow = is_arrow;
        s->field_count++;
    }
}

static void slot_add_idx_alias(VAddrSlot *s, const char *arr, const char *idx_var) {
    for (int k = 0; k < s->idx_alias_count; k++)
        if (strcmp(s->idx_aliases[k].arr, arr) == 0 &&
            strcmp(s->idx_aliases[k].idx_var, idx_var) == 0) return;
    if (s->idx_alias_count < MAX_INDEX_ALIASES) {
        strncpy(s->idx_aliases[s->idx_alias_count].arr,     arr,     63);
        strncpy(s->idx_aliases[s->idx_alias_count].idx_var, idx_var, 63);
        s->idx_alias_count++;
    }
}

static void slot_remove_idx_aliases_for_idx(VAddrSlot *s, const char *idx_var) {
    int w = 0;
    for (int k = 0; k < s->idx_alias_count; k++) {
        if (strcmp(s->idx_aliases[k].idx_var, idx_var) != 0)
            s->idx_aliases[w++] = s->idx_aliases[k];
    }
    s->idx_alias_count = w;
}

static void slot_add_mirror(VAddrSlot *s, const char *alias_var,
                             const char *owner_obj, const char *owner_field,
                             int is_arrow, int is_addr_of) {
    for (int k = 0; k < s->mirror_count; k++)
        if (strcmp(s->mirrors[k].alias_var, alias_var) == 0 &&
            strcmp(s->mirrors[k].owner_obj, owner_obj) == 0 &&
            strcmp(s->mirrors[k].owner_field, owner_field) == 0) return;
    if (s->mirror_count >= MAX_MIRROR_ALIASES) return;
    MirrorAlias *m = &s->mirrors[s->mirror_count++];
    strncpy(m->alias_var,   alias_var,   63);
    strncpy(m->owner_obj,   owner_obj,   63);
    strncpy(m->owner_field, owner_field, 63);
    m->is_arrow   = is_arrow;
    m->is_addr_of = is_addr_of;
}

static int find_slot_by_name(VAddrSlot *slots, int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (slots[i].escaped) continue;
        for (int k = 0; k < slots[i].name_count; k++)
            if (strcmp(slots[i].names[k], name) == 0)
                return i;
    }
    return -1;
}

static int find_slot_by_name_any(VAddrSlot *slots, int count, const char *name) {
    for (int i = 0; i < count; i++)
        for (int k = 0; k < slots[i].name_count; k++)
            if (strcmp(slots[i].names[k], name) == 0)
                return i;
    return -1;
}

static int slot_used_in_node(Node *n, VAddrSlot *s) {
    for (int k = 0; k < s->name_count; k++)
        if (node_uses_var(n, s->names[k])) return 1;

    for (int k = 0; k < s->field_count; k++) {
        if (!n || !n->childs) continue;
        NodeType mtype = s->fields[k].is_arrow ? NODE_MEMBER_ARROW : NODE_MEMBER_DOT;
        if (n->type == mtype && n->str &&
            strcmp(n->str, s->fields[k].field) == 0 &&
            n->childs->size > 0 && n->childs->data[0].str &&
            strcmp(n->childs->data[0].str, s->fields[k].obj) == 0)
            return 1;
        for (unsigned long long ci = 0; ci < n->childs->size; ci++) {
            Node *c = &n->childs->data[ci];
            if ((c->type == mtype || n->type == NODE_MEMBER_ASSIGN) &&
                c->str && strcmp(c->str, s->fields[k].field) == 0 &&
                c->childs && c->childs->size > 0 && c->childs->data[0].str &&
                strcmp(c->childs->data[0].str, s->fields[k].obj) == 0)
                return 1;
        }
    }

    for (int k = 0; k < s->idx_alias_count; k++) {
        if (!n || !n->childs) continue;
        if ((n->type == NODE_INDEX || n->type == NODE_INDEX_ASSIGN ||
             n->type == NODE_ADDR_INDEX) && n->childs->size >= 2) {
            Node *arr_n = &n->childs->data[0];
            Node *idx_n = &n->childs->data[1];
            if (arr_n->str && strcmp(arr_n->str, s->idx_aliases[k].arr) == 0 &&
                idx_n->str && strcmp(idx_n->str, s->idx_aliases[k].idx_var) == 0)
                return 1;
        }
    }
    return 0;
}


/* ═══════════════════════════════════════════════════════════════════
 * Рекурсивный поиск Алиасов (глубокий проход)
 * ═══════════════════════════════════════════════════════════════════ */
static void collect_aliases_in_node(Node *n, VAddrSlot *slots, int slot_count,
                                    int *changed, const char *func_name) {
    if (!n) return;

    if (n->type == NODE_VAR_DEF && n->childs->size >= 3) {
        Node *init = &n->childs->data[2];
        const char *lhs_name = n->childs->data[1].str;

        if ((init->type == NODE_MEMBER_ARROW || init->type == NODE_MEMBER_DOT) &&
            init->childs && init->childs->size >= 1 &&
            init->str && init->str[0] &&
            init->childs->data[0].str && init->childs->data[0].str[0] &&
            lhs_name && lhs_name[0]) {
            const char *owner_obj   = init->childs->data[0].str;
            const char *owner_field = init->str;
            int is_arrow = (init->type == NODE_MEMBER_ARROW);
            for (int si = 0; si < slot_count; si++) {
                int has_owner = 0;
                for (int k = 0; k < slots[si].name_count; k++)
                    if (strcmp(slots[si].names[k], owner_obj) == 0) { has_owner = 1; break; }
                if (!has_owner)
                    for (int k = 0; k < slots[si].field_count; k++)
                        if (strcmp(slots[si].fields[k].obj, owner_obj) == 0) { has_owner = 1; break; }
                if (has_owner) {
                    int before = slots[si].mirror_count;
                    slot_add_mirror(&slots[si], lhs_name, owner_obj, owner_field,
                                    is_arrow, 0);
                    if (slots[si].mirror_count != before) *changed = 1;
                }
            }
        }
        else if (init->type == NODE_ADDR && init->childs && init->childs->size >= 1) {
            Node *inner = &init->childs->data[0];
            if ((inner->type == NODE_MEMBER_ARROW || inner->type == NODE_MEMBER_DOT) &&
                inner->childs && inner->childs->size >= 1 &&
                inner->str && inner->str[0] &&
                inner->childs->data[0].str && inner->childs->data[0].str[0] &&
                lhs_name && lhs_name[0]) {
                const char *owner_obj   = inner->childs->data[0].str;
                const char *owner_field = inner->str;
                int is_arrow = (inner->type == NODE_MEMBER_ARROW);
                for (int si = 0; si < slot_count; si++) {
                    int has_owner = 0;
                    for (int k = 0; k < slots[si].name_count; k++)
                        if (strcmp(slots[si].names[k], owner_obj) == 0) { has_owner = 1; break; }
                    if (has_owner) {
                        int before = slots[si].mirror_count;
                        slot_add_mirror(&slots[si], lhs_name, owner_obj, owner_field,
                                        is_arrow, 1);
                        if (slots[si].mirror_count != before) *changed = 1;
                    }
                }
            }
        }
    }
    else if (n->type == NODE_ASSIGN && n->childs->size >= 3) {
        Node *rhs = &n->childs->data[2];
        const char *lhs_name = n->childs->data[0].str;

        if ((rhs->type == NODE_MEMBER_ARROW || rhs->type == NODE_MEMBER_DOT) &&
            rhs->childs && rhs->childs->size >= 1 &&
            rhs->str && rhs->str[0] &&
            rhs->childs->data[0].str && rhs->childs->data[0].str[0] &&
            lhs_name && lhs_name[0]) {
            const char *owner_obj   = rhs->childs->data[0].str;
            const char *owner_field = rhs->str;
            int is_arrow = (rhs->type == NODE_MEMBER_ARROW);
            for (int si = 0; si < slot_count; si++) {
                int has_owner = 0;
                for (int k = 0; k < slots[si].name_count; k++)
                    if (strcmp(slots[si].names[k], owner_obj) == 0) { has_owner = 1; break; }
                if (!has_owner)
                    for (int k = 0; k < slots[si].field_count; k++)
                        if (strcmp(slots[si].fields[k].obj, owner_obj) == 0) { has_owner = 1; break; }
                if (has_owner) {
                    int before = slots[si].mirror_count;
                    slot_add_mirror(&slots[si], lhs_name, owner_obj, owner_field,
                                    is_arrow, 0);
                    if (slots[si].mirror_count != before) *changed = 1;
                }
            }
        }
        else if (rhs->type == NODE_ADDR && rhs->childs && rhs->childs->size >= 1) {
            Node *inner = &rhs->childs->data[0];
            if ((inner->type == NODE_MEMBER_ARROW || inner->type == NODE_MEMBER_DOT) &&
                inner->childs && inner->childs->size >= 1 &&
                inner->str && inner->str[0] &&
                inner->childs->data[0].str && inner->childs->data[0].str[0] &&
                lhs_name && lhs_name[0]) {
                const char *owner_obj   = inner->childs->data[0].str;
                const char *owner_field = inner->str;
                int is_arrow = (inner->type == NODE_MEMBER_ARROW);
                for (int si = 0; si < slot_count; si++) {
                    int has_owner = 0;
                    for (int k = 0; k < slots[si].name_count; k++)
                        if (strcmp(slots[si].names[k], owner_obj) == 0) { has_owner = 1; break; }
                    if (has_owner) {
                        int before = slots[si].mirror_count;
                        slot_add_mirror(&slots[si], lhs_name, owner_obj, owner_field,
                                        is_arrow, 1);
                        if (slots[si].mirror_count != before) *changed = 1;
                    }
                }
            }
        }
    }
    else if (n->type == NODE_MEMBER_ASSIGN && n->childs->size >= 3) {
        Node *member = &n->childs->data[0];
        Node *rhs    = &n->childs->data[2];
        int is_arrow = (member->type == NODE_MEMBER_ARROW);
        int is_dot   = (member->type == NODE_MEMBER_DOT);
        if ((is_arrow || is_dot) && member->childs && member->childs->size >= 1 &&
            member->str && member->str[0] &&
            member->childs->data[0].str && member->childs->data[0].str[0] &&
            rhs->type == NODE_VAR && rhs->str && rhs->str[0]) {
            int si = find_slot_by_name(slots, slot_count, rhs->str);
            if (si >= 0) {
                int before = slots[si].field_count;
                slot_add_field(&slots[si], member->childs->data[0].str, member->str, is_arrow);
                if (slots[si].field_count != before) *changed = 1;
            }
        }
    }
    else if (n->type == NODE_INDEX_ASSIGN || n->type == NODE_MEMBER_INDEX_ASSIGN) {
        if (n->childs->size >= 3) {
            Node *rhs = &n->childs->data[n->childs->size - 1];
            const char *rhs_base = get_base_var(rhs);
            if (rhs_base) {
                int si = find_slot_by_name(slots, slot_count, rhs_base);
                if (si >= 0 && !slots[si].escaped) {
                    Node *lhs = &n->childs->data[0];
                    const char *arr_name = NULL;
                    const char *idx_name = NULL;
                    if (lhs->type == NODE_INDEX && lhs->childs && lhs->childs->size >= 2) {
                        arr_name = get_base_var(&lhs->childs->data[0]);
                        Node *idx_nd = &lhs->childs->data[1];
                        if (idx_nd->type == NODE_VAR || idx_nd->type == NODE_IDENT)
                            idx_name = idx_nd->str;
                    }
                    if (arr_name && idx_name) {
                        int before = slots[si].idx_alias_count;
                        slot_add_idx_alias(&slots[si], arr_name, idx_name);
                        if (slots[si].idx_alias_count != before) *changed = 1;
                    } else {
                        if (!slots[si].escaped) {
                            slots[si].escaped = 1;
                            *changed = 1;
                        }
                    }
                }
            }
        }
    }
    else if ((n->type == NODE_ASSIGN || n->type == NODE_VAR_DEF) && n->childs->size >= 2) {
        const char *dest = NULL;
        if (n->type == NODE_ASSIGN && n->childs->data[0].str)
            dest = n->childs->data[0].str;
        else if (n->type == NODE_VAR_DEF && n->childs->size >= 2 && n->childs->data[1].str)
            dest = n->childs->data[1].str;
        if (dest && dest[0]) {
            for (int si = 0; si < slot_count; si++) {
                if (slots[si].idx_alias_count == 0) continue;
                int before = slots[si].idx_alias_count;
                slot_remove_idx_aliases_for_idx(&slots[si], dest);
                if (slots[si].idx_alias_count != before) *changed = 1;
            }
        }
    }
    
    if (n->childs) {
        for (unsigned long long i = 0; i < n->childs->size; i++) {
            collect_aliases_in_node(&n->childs->data[i], slots, slot_count, changed, func_name);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Структура для анти-сдвига AST при вставках
 * ═══════════════════════════════════════════════════════════════════ */
typedef struct {
    int slot_idx;
    vector_node *vec;
    int idx;
} Insertion;

/* ═══════════════════════════════════════════════════════════════════
 * Control Flow Graph (CFG)
 * ═══════════════════════════════════════════════════════════════════ */
/* FIX 3: Увеличиваем лимит CFG блоков */
#define CFG_MAX_BLOCKS  4096
#define CFG_MAX_SUCCS     4
#define CFG_SLOT_WORDS  ((MAX_VADDR_VARS + 63) / 64) 

typedef struct {
    vector_node *vec;   
    int          start; 
    int          end;   

    int succ[CFG_MAX_SUCCS];
    int succ_count;
    int pred[CFG_MAX_SUCCS * 4]; 
    int pred_count;

    uint64_t use [CFG_SLOT_WORDS]; 
    uint64_t def [CFG_SLOT_WORDS]; 
    uint64_t live_in [CFG_SLOT_WORDS];
    uint64_t live_out[CFG_SLOT_WORDS];
} CFGBlock;

typedef struct {
    CFGBlock blocks[CFG_MAX_BLOCKS];
    int      count;
    int      entry; 
    int      exit;  
} CFG;

static void bs_clear(uint64_t *w) {
    for (int i = 0; i < CFG_SLOT_WORDS; i++) w[i] = 0;
}
static void bs_set(uint64_t *w, int bit) {
    if (bit < 0 || bit >= MAX_VADDR_VARS) return;
    w[bit >> 6] |= (uint64_t)1 << (bit & 63);
}
static int bs_test(const uint64_t *w, int bit) {
    if (bit < 0) return 0;
    if (bit >= MAX_VADDR_VARS) {
        fprintf(stderr, "Error: buffer overflow -> out of memory\n");
        exit(1);
    }
    return (w[bit >> 6] >> (bit & 63)) & 1;
}
static int bs_or_changed(uint64_t *dst, const uint64_t *src) {
    int ch = 0;
    for (int i = 0; i < CFG_SLOT_WORDS; i++) {
        uint64_t prev = dst[i];
        dst[i] |= src[i];
        if (dst[i] != prev) ch = 1;
    }
    return ch;
}
static void bs_andnot(uint64_t *dst, const uint64_t *src2, const uint64_t *src3) {
    for (int i = 0; i < CFG_SLOT_WORDS; i++)
        dst[i] = src2[i] & ~src3[i];
}

static int cfg_build_vec(CFG *cfg, vector_node *vec, int start, int end, int after_block);

static int cfg_new_block(CFG *cfg) {
    /* FIX 3: Защита от переполнения графа без "тихих падений" */
    if (cfg->count >= CFG_MAX_BLOCKS) {
        fprintf(stderr, "Error: CFG limit exceeded! Function is too complex. Increase CFG_MAX_BLOCKS.\n");
        exit(1);
    }
    int id = cfg->count++;
    CFGBlock *b = &cfg->blocks[id];
    b->vec        = NULL;
    b->start      = 0;
    b->end        = -1;
    b->succ_count = 0;
    b->pred_count = 0;
    bs_clear(b->use); bs_clear(b->def);
    bs_clear(b->live_in); bs_clear(b->live_out);
    return id;
}

static void cfg_add_edge(CFG *cfg, int from, int to) {
    if (from < 0 || to < 0 || from >= cfg->count || to >= cfg->count) return;
    CFGBlock *f = &cfg->blocks[from];
    for (int i = 0; i < f->succ_count; i++) if (f->succ[i] == to) return;
    if (f->succ_count < CFG_MAX_SUCCS) f->succ[f->succ_count++] = to;
    CFGBlock *t = &cfg->blocks[to];
    for (int i = 0; i < t->pred_count; i++) if (t->pred[i] == from) return;
    if (t->pred_count < CFG_MAX_SUCCS * 4) t->pred[t->pred_count++] = from;
}

static int cfg_build_vec(CFG *cfg, vector_node *vec, int start, int end, int after_block) {
    int cur = cfg_new_block(cfg);
    if (cur < 0) return -1;
    cfg->blocks[cur].vec   = vec;
    cfg->blocks[cur].start = start;
    cfg->blocks[cur].end   = start - 1; 

    for (int j = start; j < end; j++) {
        Node *n = &vec->data[j];

        if (n->type == NODE_IF && n->childs) {
            cfg->blocks[cur].end = j - 1;
            int cond_blk = cfg_new_block(cfg);
            if (cond_blk < 0) break;
            cfg->blocks[cond_blk].vec   = vec;
            cfg->blocks[cond_blk].start = j;
            cfg->blocks[cond_blk].end   = j;
            cfg_add_edge(cfg, cur, cond_blk);

            int merge_blk = cfg_new_block(cfg);
            if (merge_blk < 0) break;
            cfg->blocks[merge_blk].vec   = vec;
            cfg->blocks[merge_blk].start = j + 1;
            cfg->blocks[merge_blk].end   = j;     

            int has_else = 0;
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if (child->type == NODE_SCOPE && child->childs) {
                    int then_exit = cfg_build_vec(cfg, child->childs, 0,
                                                   (int)child->childs->size, merge_blk);
                    cfg_add_edge(cfg, cond_blk, cfg->blocks[cond_blk + 1].vec ? cond_blk + 1 : merge_blk);
                    (void)then_exit;
                } else if (child->type == NODE_ELSE && child->childs) {
                    has_else = 1;
                    for (unsigned long long m = 0; m < child->childs->size; m++) {
                        Node *ec = &child->childs->data[m];
                        if (ec->type == NODE_SCOPE && ec->childs) {
                            cfg_build_vec(cfg, ec->childs, 0,
                                          (int)ec->childs->size, merge_blk);
                        }
                    }
                }
            }
            if (!has_else) cfg_add_edge(cfg, cond_blk, merge_blk);
            cur = merge_blk;
            cfg->blocks[cur].start = j + 1;
            continue;
        }

        if ((n->type == NODE_WHILE || n->type == NODE_FOR ||
             n->type == NODE_DO_WHILE) && n->childs) {
            cfg->blocks[cur].end = j - 1;
            int hdr = cfg_new_block(cfg);
            if (hdr < 0) break;
            cfg->blocks[hdr].vec   = vec;
            cfg->blocks[hdr].start = j;
            cfg->blocks[hdr].end   = j;

            int after = cfg_new_block(cfg);
            if (after < 0) break;
            cfg->blocks[after].vec   = vec;
            cfg->blocks[after].start = j + 1;
            cfg->blocks[after].end   = j;

            cfg_add_edge(cfg, cur, hdr);

            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if (child->type == NODE_SCOPE && child->childs) {
                    int body_exit = cfg_build_vec(cfg, child->childs, 0,
                                                   (int)child->childs->size, hdr);
                    (void)body_exit;
                }
            }

            cfg_add_edge(cfg, hdr, after);
            cur = after;
            cfg->blocks[cur].start = j + 1;
            continue;
        }

        cfg->blocks[cur].end = j;
        if (n->type == NODE_RETURN) {
            int ab = (after_block >= 0) ? after_block : cfg->exit;
            cfg_add_edge(cfg, cur, ab);
            int dead = cfg_new_block(cfg);
            if (dead < 0) break;
            cfg->blocks[dead].vec   = vec;
            cfg->blocks[dead].start = j + 1;
            cfg->blocks[dead].end   = j;
            cur = dead;
        }
    }

    int ab = (after_block >= 0) ? after_block : cfg->exit;
    cfg_add_edge(cfg, cur, ab);
    return cur;
}

static void cfg_init(CFG *cfg) {
    cfg->count = 0;
    cfg->entry = cfg_new_block(cfg); 
    cfg->exit  = cfg_new_block(cfg); 
}

static void cfg_build(CFG *cfg, vector_node *scope_vec) {
    cfg_init(cfg);
    cfg_build_vec(cfg, scope_vec, 0, (int)scope_vec->size, cfg->exit);
    cfg_add_edge(cfg, cfg->entry,
                 cfg->count > 2 ? 2 : cfg->exit); 
}

static void cfg_fill_use_def(CFG *cfg, VAddrSlot *slots, int slot_count) {
    for (int bi = 0; bi < cfg->count; bi++) {
        CFGBlock *b = &cfg->blocks[bi];
        if (!b->vec || b->start > b->end) continue;
        for (int j = b->start; j <= b->end && j < (int)b->vec->size; j++) {
            Node *n = &b->vec->data[j];
            for (int si = 0; si < slot_count; si++) {
                if (slot_used_in_node(n, &slots[si])) {
                    if (!bs_test(b->def, si))
                        bs_set(b->use, si);
                }
                if (n->type == NODE_VAR_DEF && n->childs && n->childs->size >= 2) {
                    const char *ts = n->childs->data[0].str ? n->childs->data[0].str : "";
                    if (strncmp(ts, "autodel:", 8) == 0) {
                        const char *nm = n->childs->data[1].str ? n->childs->data[1].str : "";
                        for (int k = 0; k < slots[si].name_count; k++) {
                            if (strcmp(slots[si].names[k], nm) == 0) {
                                bs_set(b->def, si);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

static void cfg_liveness(CFG *cfg) {
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int bi = cfg->count - 1; bi >= 0; bi--) {
            CFGBlock *b = &cfg->blocks[bi];
            uint64_t new_out[CFG_SLOT_WORDS];
            bs_clear(new_out);
            for (int s = 0; s < b->succ_count; s++) {
                int sid = b->succ[s];
                if (sid >= 0 && sid < cfg->count)
                    bs_or_changed(new_out, cfg->blocks[sid].live_in);
            }
            if (bs_or_changed(b->live_out, new_out)) changed = 1;

            uint64_t new_in[CFG_SLOT_WORDS];
            bs_andnot(new_in, b->live_out, b->def);
            for (int i = 0; i < CFG_SLOT_WORDS; i++) new_in[i] |= b->use[i];
            if (bs_or_changed(b->live_in, new_in)) changed = 1;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * STATIC GC: Анализ графа и вставка DELETE
 * ═══════════════════════════════════════════════════════════════════ */

/* 1. Поиск путей без return (Reachability Analysis) */
static int dfs_reach_exit_without_return(CFGBlock *b, uint8_t *visited, CFGBlock *exit_node, CFG *cfg) {
    if (b == exit_node) return 1;
    int id = b - cfg->blocks;
    if (visited[id]) return 0;
    visited[id] = 1;

    if (b->vec && b->start <= b->end) {
        Node *last_node = &b->vec->data[b->end];
        if (last_node->type == NODE_RETURN) return 0; /* Путь безопасно оборван */
    }

    for (int i = 0; i < b->succ_count; i++) {
        int next_id = b->succ[i];
        if (next_id >= 0 && next_id < cfg->count) {
            if (dfs_reach_exit_without_return(&cfg->blocks[next_id], visited, exit_node, cfg)) return 1;
        }
    }
    return 0;
}

static void check_return_paths(CFG *cfg, int is_void, const char *func_name) {
    if (is_void || !func_name || !func_name[0]) return;
    uint8_t visited[CFG_MAX_BLOCKS] = {0};
    if (dfs_reach_exit_without_return(&cfg->blocks[cfg->entry], visited, &cfg->blocks[cfg->exit], cfg)) {
        fprintf(stderr, "\n[Compile Error]: Control reaches end of non-void function '%s'\n", func_name);
        exit(1);
    }
}

/* 2. Отложенная вставка (Защита от смещения индексов AST) */
typedef struct {
    vector_node *vec;
    int idx;
    char name[64];
    int insert_after;
} SGCInsertion;

static SGCInsertion sgc_ins[1024];
static int sgc_ins_count = 0;

static void sgc_add_ins(vector_node *vec, int idx, const char *name, int after) {
    if (sgc_ins_count >= 1024) return;
    sgc_ins[sgc_ins_count].vec = vec;
    sgc_ins[sgc_ins_count].idx = idx;
    strncpy(sgc_ins[sgc_ins_count].name, name, 63);
    sgc_ins[sgc_ins_count].insert_after = after;
    sgc_ins_count++;
}

static int get_slot_idx(Node *n, VAddrSlot *slots, int count) {
    const char *base = get_base_var(n);
    if (!base) return -1;
    return find_slot_by_name(slots, count, base);
}

/* 3. Главный движок SGC */
static void sgc_apply_deletes(CFG *cfg, VAddrSlot *slots, int slot_count) {
    sgc_ins_count = 0;

    for (int i = 0; i < cfg->count; i++) {
        CFGBlock *b = &cfg->blocks[i];
        if (!b->vec || b->start > b->end) continue;

        uint64_t current_live[CFG_SLOT_WORDS];
        for (int w = 0; w < CFG_SLOT_WORDS; w++) current_live[w] = b->live_in[w];

        for (int j = b->start; j <= b->end && j < (int)b->vec->size; j++) {
            Node *n = &b->vec->data[j];

            /* ПРАВИЛО 1: Kill Points (Переприсваивание) */
            if (n->type == NODE_ASSIGN || n->type == NODE_VAR_DEF) {
                Node *lhs = (n->type == NODE_ASSIGN) ? &n->childs->data[0] : &n->childs->data[1];
                int si = get_slot_idx(lhs, slots, slot_count);
                if (si >= 0 && !slots[si].escaped) {
                    if (bs_test(current_live, si)) {
                        /* Убиваем старый объект строго ПЕРЕД присваиванием */
                        sgc_add_ins(b->vec, j, slots[si].names[0], 0); 
                    }
                    bs_set(current_live, si); /* Объект снова жив */
                }
            }

            for (int si = 0; si < slot_count; si++) {
                if (slot_used_in_node(n, &slots[si])) bs_set(current_live, si);
            }

            /* ПРАВИЛО 2: Return Points */
            if (n->type == NODE_RETURN) {
                int ret_si = (n->childs && n->childs->size > 0) ? get_slot_idx(&n->childs->data[0], slots, slot_count) : -1;
                for (int si = 0; si < slot_count; si++) {
                    if (si == ret_si) continue; /* Не удаляем то, что возвращаем */
                    if (bs_test(current_live, si) && !slots[si].escaped) {
                        sgc_add_ins(b->vec, j, slots[si].names[0], 0); /* ПЕРЕД return */
                    }
                }
                break;
            }
        }

        /* ПРАВИЛО 3: Death Points (Конец жизни в блоке) */
        for (int si = 0; si < slot_count; si++) {
            if (bs_test(current_live, si) && !bs_test(b->live_out, si) && !slots[si].escaped) {
                int last_j = b->start;
                for (int k = b->end; k >= b->start; k--) {
                    if (k < (int)b->vec->size && slot_used_in_node(&b->vec->data[k], &slots[si])) {
                        last_j = k; break;
                    }
                }
                sgc_add_ins(b->vec, last_j, slots[si].names[0], 1); /* ПОСЛЕ последнего использования */
            }
        }
    }

    /* Применяем вставки с конца, чтобы не съехали индексы */
    for (int i = 0; i < sgc_ins_count - 1; i++) {
        for (int k = i + 1; k < sgc_ins_count; k++) {
            if (sgc_ins[i].vec < sgc_ins[k].vec || 
               (sgc_ins[i].vec == sgc_ins[k].vec && sgc_ins[i].idx < sgc_ins[k].idx)) {
                SGCInsertion tmp = sgc_ins[i]; sgc_ins[i] = sgc_ins[k]; sgc_ins[k] = tmp;
            }
        }
    }
    for (int i = 0; i < sgc_ins_count; i++) {
        Node del = make_delete_node(sgc_ins[i].name);
        vn_insert_at(sgc_ins[i].vec, sgc_ins[i].idx + sgc_ins[i].insert_after, del);
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * pregen_scope 
 * ═══════════════════════════════════════════════════════════════════ */
static void pregen_scope(vector_node *nodes, int start, int end,
                         const char *func_name, Node *params_node, int insert_deletes, int is_void) {

    /* ── 0. Регистрируем функцию в индексе (только в Pass 1) ────────── */
    if (!insert_deletes && func_name && func_name[0]) {
        FuncRecord *fr = func_index_get_or_add(func_name);
        if (fr && params_node)
            func_index_register_params(fr, params_node);
        if (fr)
            fr->returns_ptr = ownership_func_lookup(func_name);
    }

    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];
        if (n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF || 
            n->type == NODE_ASYNC_FUNC_DEF || n->type == NODE_AWAIT_FUNC_DEF) {
            Node *scope = &n->childs->data[3];
            const char *fn = n->childs->data[1].str ? n->childs->data[1].str : "";
            Node *p = (n->childs->size >= 3 && n->childs->data[2].type == NODE_PARAMS) ? &n->childs->data[2] : NULL;
            if (scope->type == NODE_SCOPE) pregen_scope(scope->childs, 0, (int)scope->childs->size, fn, p, insert_deletes, is_void);
        } else if (n->type == NODE_SCOPE) {
            pregen_scope(n->childs, 0, (int)n->childs->size, func_name, NULL, insert_deletes, is_void);
        } else if (node_is_block(n) && n->childs) {
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if (child->type == NODE_SCOPE && child->childs)
                    pregen_scope(child->childs, 0, (int)child->childs->size, func_name, NULL, insert_deletes, is_void);
            }
        } else if (n->type == NODE_IF && n->childs) {
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if (child->type == NODE_SCOPE && child->childs)
                    pregen_scope(child->childs, 0, (int)child->childs->size, func_name, NULL, insert_deletes, is_void);
                else if (child->type == NODE_ELSE && child->childs)
                    for (unsigned long long m = 0; m < child->childs->size; m++) {
                        Node *ec = &child->childs->data[m];
                        if (ec->type == NODE_SCOPE && ec->childs)
                            pregen_scope(ec->childs, 0, (int)ec->childs->size, func_name, NULL, insert_deletes, is_void);
                    }
            }
        }
    }

    VAddrSlot slots[MAX_VADDR_VARS];
    int slot_count = 0;

    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];
        if (n->type != NODE_VAR_DEF || n->childs->size < 2) continue;
        const char *type_str = n->childs->data[0].str;
        if (strncmp(type_str, "autodel:", 8) != 0) continue;
        if (slot_count >= MAX_VADDR_VARS) {
            fprintf(stderr, "Error: buffer overflow -> out of memory\n");
            exit(1);
        };

        VAddrSlot *s   = &slots[slot_count++];
        s->vaddr       = vaddr_pop();
        s->name_count  = 0;
        s->field_count = 0;
        s->idx_alias_count = 0;
        s->mirror_count = 0;
        s->escaped     = 0;
        
        s->birth_vec   = nodes;
        s->birth_idx   = j;

        strncpy(s->base_type, type_str + 8, 63);
        s->base_type[63] = '\0';
        int blen = (int)strlen(s->base_type);
        if (blen > 0 && s->base_type[blen - 1] == '*') s->base_type[blen - 1] = '\0';
        slot_add_name(s, n->childs->data[1].str);
    }

    if (slot_count == 0) return;

    /* 3. Глубокое распространение field-алиасов, зеркал и escapes */
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int j = start; j < end; j++) {
            collect_aliases_in_node(&nodes->data[j], slots, slot_count, &changed, func_name);
        }
    }

    if (func_name && func_name[0] && params_node && params_node->type == NODE_PARAMS) {
        int pidx = 0;
        for (unsigned long long pi = 0; pi < params_node->childs->size; pi++) {
            Node *p = &params_node->childs->data[pi];
            if (p->type != NODE_VAR_DEF || !p->childs || p->childs->size < 2) continue;
            const char *pname    = p->childs->data[1].str ? p->childs->data[1].str : "";
            const char *type_str = p->childs->data[0].str ? p->childs->data[0].str : "";
            int tlen = (int)strlen(type_str);
            int is_ptr = (tlen > 0 && type_str[tlen - 1] == '*') ? 1 : 0;
            if (is_ptr && pname[0]) {
                int si = find_slot_by_name_any(slots, slot_count, pname);
                if (si >= 0) {
                    VAddrSlot *s = &slots[si];
                    gaa_push(func_name, pidx, s->vaddr);
                    for (int k = 0; k < s->name_count; k++) {
                        if (strcmp(s->names[k], pname) != 0)
                            gaa_add_alias_effect(func_name, pidx, s->vaddr, s->names[k]);
                    }
                    for (int k = 0; k < s->field_count; k++) {
                        gaa_add_field_effect(func_name, pidx, s->vaddr,
                                             s->fields[k].obj, s->fields[k].field,
                                             s->fields[k].is_arrow);
                    }
                    if (s->escaped)
                        gaa_mark_escape(func_name, s->vaddr);
                }
            }
            pidx++;
        }
    }

    if (func_name && func_name[0] && params_node && params_node->type == NODE_PARAMS) {
        int pidx = 0;
        for (unsigned long long pi = 0; pi < params_node->childs->size; pi++) {
            Node *p = &params_node->childs->data[pi];
            if (p->type != NODE_VAR_DEF || !p->childs || p->childs->size < 2) continue;
            const char *pname    = p->childs->data[1].str ? p->childs->data[1].str : "";
            const char *type_str = p->childs->data[0].str ? p->childs->data[0].str : "";
            int tlen = (int)strlen(type_str);

            int is_ptr = 0;
            for (int ti = 0; ti < tlen; ti++)
                if (type_str[ti] == '*') { is_ptr = 1; break; }

            char embedded_lt = '\0';
            if (is_ptr) {
                for (int ti = tlen - 1; ti >= 0; ti--) {
                    char c = type_str[ti];
                    if (c == 'a' || c == 'b' || c == 'c') { embedded_lt = c; break; }
                    if (c == '*') break; 
                }
                if (!embedded_lt && tlen > 0) {
                    char last = type_str[tlen - 1];
                    if (last == 'a' || last == 'b' || last == 'c') embedded_lt = last;
                }
            }

            if (is_ptr && embedded_lt == 'c' && pname[0]) {
                int si = find_slot_by_name_any(slots, slot_count, pname);
                char resolved = 'a';
                if (si >= 0 && slots[si].escaped) resolved = 'b';
                GlobalArgAlias *gae = gaa_get_or_push(func_name, pidx,
                                          si >= 0 ? slots[si].vaddr : 0);
                if (gae) gae->resolved_lifetime = resolved;
            }
            pidx++;
        }
    }

    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];
        if (n->type != NODE_FUNC_CALL || !n->str || !n->str[0]) continue;
        if (!n->childs) continue;

        const char *callee = n->str;
        int param_idx = 0;
        for (unsigned long long ci = 0; ci < n->childs->size; ci++) {
            Node *arg = &n->childs->data[ci];
            if (arg->type == NODE_ARGS) continue;
            const char *base = get_base_var(arg);
            if (base) {
                int si = find_slot_by_name(slots, slot_count, base);
                if (si >= 0) {
                    gaa_push(callee, param_idx, slots[si].vaddr);
                    for (int gi = 0; gi < g_arg_alias_count; gi++) {
                        GlobalArgAlias *gae = &g_arg_aliases[gi];
                        if (strcmp(gae->func_name, callee) != 0) continue;
                        if (gae->param_idx != param_idx) continue;
                        for (int ai = 0; ai < gae->alias_effect_count; ai++) {
                            int before = slots[si].name_count;
                            slot_add_name(&slots[si], gae->alias_effects[ai].alias);
                            (void)before; 
                        }
                        for (int fi = 0; fi < gae->field_effect_count; fi++) {
                            slot_add_field(&slots[si],
                                           gae->field_effects[fi].obj,
                                           gae->field_effects[fi].field,
                                           gae->field_effects[fi].is_arrow);
                        }
                        if (gae->escapes_out) {
                            slots[si].escaped = 1;
                            gaa_mark_escape(func_name ? func_name : "", slots[si].vaddr);
                        }
                    }
                }
            }
            param_idx++;
        }
    }

    if (insert_deletes) {
        for (int j = end - 1; j >= start; j--) {
            Node *n = &nodes->data[j];
            if (n->type != NODE_MEMBER_ASSIGN || n->childs->size < 3) continue;
            Node *member = &n->childs->data[0];
            if (!member->childs || member->childs->size < 1) continue;
            if (!member->str || !member->str[0]) continue;
            const char *written_obj   = member->childs->data[0].str;
            const char *written_field = member->str;
            int written_arrow = (member->type == NODE_MEMBER_ARROW);
            if (!written_obj || !written_field) continue;

            for (int si = 0; si < slot_count; si++) {
                for (int mi = 0; mi < slots[si].mirror_count; mi++) {
                    MirrorAlias *ma = &slots[si].mirrors[mi];
                    if (ma->is_addr_of) continue; 
                    if (strcmp(ma->owner_obj,   written_obj)   != 0) continue;
                    if (strcmp(ma->owner_field, written_field) != 0) continue;
                    if (ma->is_arrow != written_arrow)               continue;

                    Node rhs_member;
                    rhs_member.type   = ma->is_arrow ? NODE_MEMBER_ARROW : NODE_MEMBER_DOT;
                    rhs_member.str    = malloc(strlen(ma->owner_field) + 1);
                    strcpy(rhs_member.str, ma->owner_field);
                    rhs_member.childs = malloc(sizeof(vector_node));
                    vn_init(rhs_member.childs, 1);
                    Node owner_var = {NODE_VAR, NULL, malloc(strlen(ma->owner_obj) + 1), NULL};
                    strcpy(owner_var.str, ma->owner_obj);
                    owner_var.childs = NULL;
                    vn_push_back(rhs_member.childs, owner_var);

                    Node mirror_assign;
                    mirror_assign.type   = NODE_ASSIGN;
                    mirror_assign.str    = NULL;
                    mirror_assign.childs = malloc(sizeof(vector_node));
                    vn_init(mirror_assign.childs, 3);

                    Node lhs_node = {NODE_VAR, NULL, malloc(strlen(ma->alias_var) + 1), NULL};
                    strcpy(lhs_node.str, ma->alias_var);
                    lhs_node.childs = NULL;

                    Node op_node = {NODE_IDENT, NULL, malloc(2), NULL};
                    strcpy(op_node.str, "=");
                    op_node.childs = NULL;

                    vn_push_back(mirror_assign.childs, lhs_node);
                    vn_push_back(mirror_assign.childs, op_node);
                    vn_push_back(mirror_assign.childs, rhs_member);

                    vn_insert_at(nodes, j + 1, mirror_assign);
                    end++; 
                }
            }
        }
    }

    if (insert_deletes) {
        for (int j = start; j < end; j++) {
            Node *n = &nodes->data[j];
            if (n->type != NODE_FUNC_CALL || !n->str || !n->str[0]) continue;
            if (!n->childs) continue;

            OverloadEntry *ext = NULL;
            for (int m = 0; m < num_imported_modules && !ext; m++) {
                OverloadEntry *mod = global_modules_overloads[m];
                int cnt = global_modules_counts[m];
                for (int ei = 0; ei < cnt; ei++) {
                    if (strcmp(mod[ei].mangled, n->str) == 0) {
                        ext = &mod[ei];
                        break;
                    }
                }
            }
            if (!ext) continue;

            int param_idx = 0;
            for (unsigned long long ci = 0; ci < n->childs->size; ci++) {
                Node *arg = &n->childs->data[ci];
                if (arg->type == NODE_ARGS) continue;
                if (param_idx >= ext->param_count) { param_idx++; continue; }

                char lt = ext->param_lifetimes[param_idx];

                if (lt == 'c') {
                    for (int gi = 0; gi < g_arg_alias_count; gi++) {
                        if (strcmp(g_arg_aliases[gi].func_name, n->str) == 0 &&
                            g_arg_aliases[gi].param_idx == param_idx &&
                            g_arg_aliases[gi].resolved_lifetime != '\0') {
                            lt = g_arg_aliases[gi].resolved_lifetime;
                            break;
                        }
                    }
                }

                if (lt == 'b' || lt == 'c') {
                    const char *arg_base = get_base_var(arg);
                    if (arg_base) {
                        int used_after = 0;
                        for (int k = j + 1; k < end && !used_after; k++) {
                            Node *stack[256];
                            int top = 0;
                            stack[top++] = &nodes->data[k];
                            while (top > 0 && !used_after) {
                                Node *cur = stack[--top];
                                if (!cur) continue;
                                if ((cur->type == NODE_VAR || cur->type == NODE_IDENT) &&
                                    cur->str && strcmp(cur->str, arg_base) == 0) {
                                    used_after = 1;
                                    break;
                                }
                                if (cur->childs)
                                    for (unsigned long long ci2 = 0;
                                         ci2 < cur->childs->size && top < 255; ci2++)
                                        stack[top++] = &cur->childs->data[ci2];
                            }
                        }
                        if (used_after) {
                            fprintf(stderr,
                                "pregen ERROR: pointer '%s' passed to extern function"
                                " '%s' (param %d) with lifetime '%c' —"
                                " pointer may be invalid after call but is used later.\n",
                                arg_base, ext->base_name, param_idx, lt);
                            exit(1);
                        }
                    }
                }
                param_idx++;
            }
        }
    }

    if (insert_deletes) {
        CFG cfg;
        cfg_build(&cfg, nodes);
        cfg_fill_use_def(&cfg, slots, slot_count);
        cfg_liveness(&cfg);

        /* Проверяем, что все ветви не-void функции имеют return */
        check_return_paths(&cfg, is_void, func_name);

        /* Генерируем и вставляем DELETE */
        sgc_apply_deletes(&cfg, slots, slot_count);

        /* Освобождаем виртуальные адреса */
        for (int si = 0; si < slot_count; si++) {
            vaddr_push_free(slots[si].vaddr);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Глобальные переменные-индексы (_idx_N)
 * ═══════════════════════════════════════════════════════════════════ */

#define MAX_IDX_VARS 256
typedef struct {
    char name[64];      
    char idx_type[64];  
} IdxVarRecord;

static IdxVarRecord g_idx_vars[MAX_IDX_VARS];
static int          g_idx_var_count = 0;

static void idx_var_reset(void) {
    g_idx_var_count = 0;
    g_idx_counter   = 0;
}

#define MAX_ARR_SIZES 256
typedef struct { char name[96]; char size[32]; } ArrSizeEntry;
static ArrSizeEntry g_arr_sizes[MAX_ARR_SIZES];
static int          g_arr_size_count = 0;

static void arr_size_reset(void) { g_arr_size_count = 0; }

static void arr_size_register(const char *scope_func, const char *name, const char *size) {
    char key[96];
    if (scope_func && scope_func[0])
        snprintf(key, sizeof(key), "%s:%s", scope_func, name);
    else
        strncpy(key, name, 95);
    key[95] = '\0';
    for (int i = 0; i < g_arr_size_count; i++)
        if (strcmp(g_arr_sizes[i].name, key) == 0) return;
    if (g_arr_size_count >= MAX_ARR_SIZES) return;
    strncpy(g_arr_sizes[g_arr_size_count].name, key, 95);
    strncpy(g_arr_sizes[g_arr_size_count].size, size, 31);
    g_arr_size_count++;
}

static void collect_array_sizes_in_scope(vector_node *nodes, const char *scope_func) {
    for (unsigned long long j = 0; j < nodes->size; j++) {
        Node *n = &nodes->data[j];
        if (n->type == NODE_ARRAY_DEF && n->childs && n->childs->size >= 3) {
            const char *nm = n->childs->data[1].str ? n->childs->data[1].str : "";
            const char *sz = n->childs->data[2].str ? n->childs->data[2].str : "";
            if (nm[0] && sz[0]) arr_size_register(scope_func, nm, sz);
        }
        if ((n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF || 
             n->type == NODE_ASYNC_FUNC_DEF || n->type == NODE_AWAIT_FUNC_DEF) &&
            n->childs && n->childs->size >= 4) {
            const char *fn = n->childs->data[1].str ? n->childs->data[1].str : "";
            Node *sc = &n->childs->data[3];
            if (sc->type == NODE_SCOPE && sc->childs)
                collect_array_sizes_in_scope(sc->childs, fn);
            continue;
        }
        if (n->childs) collect_array_sizes_in_scope(n->childs, scope_func);
    }
}

static void collect_array_sizes(vector_node *nodes) {
    collect_array_sizes_in_scope(nodes, "");
}

static void inject_idx_vars(vector_node *nodes, int *start_p, int *end_p,
                            vector_node *root_nodes,
                            vector_node *scope_nodes,   
                            const char  *scope_func)    
{
    int j = *start_p;
    while (j < *end_p) {
        Node *n = &nodes->data[j];

        if (n->type == NODE_SCOPE && n->childs) {
            int s = 0, e = (int)n->childs->size;
            inject_idx_vars(n->childs, &s, &e, root_nodes, scope_nodes, scope_func);
            j++; continue;
        }
        if ((n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF || 
             n->type == NODE_ASYNC_FUNC_DEF || n->type == NODE_AWAIT_FUNC_DEF) &&
            n->childs && n->childs->size >= 4) {
            Node *scope = &n->childs->data[3];
            const char *fn = n->childs->data[1].str ? n->childs->data[1].str : "";
            if (scope->type == NODE_SCOPE && scope->childs) {
                int s = 0, e = (int)scope->childs->size;
                inject_idx_vars(scope->childs, &s, &e, root_nodes, scope->childs, fn);
            }
            j++; continue;
        }
        if ((n->type == NODE_IF    || n->type == NODE_WHILE ||
             n->type == NODE_FOR   || n->type == NODE_DO_WHILE) && n->childs) {
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *ch = &n->childs->data[k];
                if (ch->type == NODE_SCOPE && ch->childs) {
                    int s = 0, e = (int)ch->childs->size;
                    inject_idx_vars(ch->childs, &s, &e, root_nodes, scope_nodes, scope_func);
                } else if (ch->type == NODE_ELSE && ch->childs) {
                    for (unsigned long long m = 0; m < ch->childs->size; m++) {
                        Node *ec = &ch->childs->data[m];
                        if (ec->type == NODE_SCOPE && ec->childs) {
                            int s = 0, e = (int)ec->childs->size;
                            inject_idx_vars(ec->childs, &s, &e, root_nodes, scope_nodes, scope_func);
                        }
                    }
                }
            }
            j++; continue;
        }

        int found_index = 0;
        Node *stack[256];
        int   top = 0;
        if (n->childs) {
            for (unsigned long long ci = 0; ci < n->childs->size; ci++) {
                stack[top]      = &n->childs->data[ci];
                top++;
                if (top >= 256) break;
            }
        }
        while (top > 0 && !found_index) {
            top--;
            Node *cur      = stack[top];

            if (cur->type == NODE_INDEX && cur->childs && cur->childs->size >= 2) {
                Node *idx_nd  = &cur->childs->data[1];

                if ((idx_nd->type == NODE_VAR || idx_nd->type == NODE_IDENT) &&
                    idx_nd->str && idx_nd->str[0]) {

                    printf("Error: dynamic indexation in autodel\n");
                    exit(1);
                }
            }

            if (!found_index && cur->childs) {
                for (unsigned long long ci = 0; ci < cur->childs->size && top < 255; ci++) {
                    stack[top]      = &cur->childs->data[ci];
                    top++;
                }
            }
        }

        j++;
    }
}

static void emit_idx_global_defs(vector_node *nodes) {
    for (int i = g_idx_var_count - 1; i >= 0; i--) {
        Node *vdef = make_node(NODE_STATIC_VAR_DEF, "");
        Node *type = make_node(NODE_TYPE,  g_idx_vars[i].idx_type);
        Node *name = make_node(NODE_IDENT, g_idx_vars[i].name);
        Node *init = make_node(NODE_I64,   "0");
        vn_push_back(vdef->childs, *type); free(type);
        vn_push_back(vdef->childs, *name); free(name);
        vn_push_back(vdef->childs, *init); free(init);
        vn_insert_at(nodes, 0, *vdef);
        free(vdef);
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Контроль доступа к памяти в корутинах
 * ═══════════════════════════════════════════════════════════════════ */
typedef struct {
    char names[256][64];
    int count;
    int is_active;
} CoroutineContext;

static CoroutineContext current_coro = {0};

static void coro_add_allowed_var(const char* name) {
    if (!name || !name[0]) return;
    for (int i = 0; i < current_coro.count; i++) {
        if (strcmp(current_coro.names[i], name) == 0) return;
    }
    if (current_coro.count < 256) {
        strncpy(current_coro.names[current_coro.count++], name, 63);
    }
}

static int coro_is_var_allowed(const char* name) {
    for (int i = 0; i < current_coro.count; i++) {
        if (strcmp(current_coro.names[i], name) == 0) return 1;
    }
    return 0;
}

// Рекурсивно достаем базу: a.b.c -> a, ptr[i] -> ptr, *ptr -> ptr
static const char* get_base_ident(Node* lhs) {
    if (!lhs) return NULL;
    if (lhs->type == NODE_IDENT || lhs->type == NODE_VAR) return lhs->str;
    if ((lhs->type == NODE_MEMBER_DOT || lhs->type == NODE_MEMBER_ARROW ||
         lhs->type == NODE_INDEX || lhs->type == NODE_ADDR_INDEX) &&
        lhs->childs && lhs->childs->size > 0) {
        return get_base_ident(&lhs->childs->data[0]);
    }
    if (lhs->type == NODE_DEREF || lhs->type == NODE_ADDR) {
        if (lhs->childs && lhs->childs->size > 0) return get_base_ident(&lhs->childs->data[0]);
    }
    return NULL;
}

static void validate_coroutine_memory(vector_node *nodes) {
    if (!nodes) return;
    for (size_t i = 0; i < nodes->size; i++) {
        Node *n = &nodes->data[i];

        // 1. Вход в корутину
        if (n->type == NODE_ASYNC_FUNC_DEF || n->type == NODE_AWAIT_FUNC_DEF) {
            current_coro.is_active = 1;
            current_coro.count = 0;

            // Добавляем параметры (обычно они в childs[2])
            if (n->childs->size >= 3 && n->childs->data[2].type == NODE_PARAMS) {
                Node *params = &n->childs->data[2];
                for (size_t p = 0; p < params->childs->size; p++) {
                    Node *param = &params->childs->data[p];
                    if (param->childs && param->childs->size >= 2 && param->childs->data[1].str) {
                        coro_add_allowed_var(param->childs->data[1].str);
                    }
                }
            }

            // Обходим тело (childs[3])
            if (n->childs->size >= 4 && n->childs->data[3].type == NODE_SCOPE) {
                validate_coroutine_memory(n->childs->data[3].childs);
            }

            current_coro.is_active = 0;
            continue; // Тело уже обошли, идем к следующему узлу
        }

        if (current_coro.is_active) {
            // 2. Регистрируем локальные переменные
            if (n->type == NODE_VAR_DEF || n->type == NODE_CHANNEL_VAR_DEF) {
                if (n->childs && n->childs->size >= 2 && n->childs->data[1].str) {
                    coro_add_allowed_var(n->childs->data[1].str);
                }
            }

            // 3. Проверяем запись
            if (n->type == NODE_ASSIGN || n->type == NODE_MEMBER_ASSIGN ||
                n->type == NODE_INDEX_ASSIGN || n->type == NODE_PTR_ASSIGN ||
                n->type == NODE_MEMBER_INDEX_ASSIGN) {
                
                if (n->childs && n->childs->size > 0) {
                    Node *lhs = &n->childs->data[0];
                    const char* base_name = get_base_ident(lhs);
                    
                    if (base_name && !coro_is_var_allowed(base_name)) {
                        fprintf(stderr, "\n[Compile Error]: Memory safety violation in coroutine.\n"
                                        "Attempt to write to '%s'. Coroutines can only write to their own arguments, "
                                        "local variables, or variables explicitly marked as 'channel'.\n", base_name);
                        exit(1);
                    }
                }
            }
        }

        // Рекурсивный обход для вложенных скоупов (if, while, for)
        if (n->childs) {
            validate_coroutine_memory(n->childs);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Точка входа
 * ═══════════════════════════════════════════════════════════════════ */
void pregen(vector_node *nodes) {
    autodel_counter      = 0;
    ownership_func_count = 0;
    ownership_funcs = malloc(max_ownership_funcs * 64);
    vaddr_pool_init();
    gaa_reset();
    func_index_reset();
    idx_var_reset();
    arr_size_reset();

    validate_coroutine_memory(nodes);
    collect_array_sizes(nodes);

    {
        int s = 0, e = (int)nodes->size;
        inject_idx_vars(nodes, &s, &e, nodes, nodes, "");
    }

    emit_idx_global_defs(nodes);

    pregen_inline_new(nodes, 0, (int)nodes->size);

    size_t prev = -1;
    while (prev != ownership_func_count) {
        prev = ownership_func_count;
        collect_ownership(nodes, 0, (int)nodes->size);
        patch_ownership_vardefs(nodes, 0, (int)nodes->size);
    }

    /* Pass 1: собираем все алиасы и граф вызовов (без вставки delete) */
    for (int j = 0; j < (int)nodes->size; j++) {
        Node *n = &nodes->data[j];
        if ((n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF || 
             n->type == NODE_ASYNC_FUNC_DEF || n->type == NODE_AWAIT_FUNC_DEF) &&
            n->childs->size >= 4) {
            Node *scope       = &n->childs->data[3];
            const char *fname = n->childs->data[1].str ? n->childs->data[1].str : "";
            Node *params      = (n->childs->size >= 3 && n->childs->data[2].type == NODE_PARAMS) ? &n->childs->data[2] : NULL;
            if (scope->type == NODE_SCOPE) {
                int is_void = (n->childs->data[0].str && strstr(n->childs->data[0].str, "void") != NULL);
                pregen_scope(scope->childs, 0, (int)scope->childs->size, fname, params, 0, is_void); /* insert_deletes = 0 */
            }
        } else if (n->type == NODE_STRUCT_DEF) {
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if ((n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF || 
                     n->type == NODE_ASYNC_FUNC_DEF || n->type == NODE_AWAIT_FUNC_DEF) && child->childs->size >= 4) {
                    Node *scope       = &child->childs->data[3];
                    const char *fname = child->childs->data[1].str ? child->childs->data[1].str : "";
                    Node *params      = (child->childs->size >= 3 && child->childs->data[2].type == NODE_PARAMS) ? &child->childs->data[2] : NULL;
                    if (scope->type == NODE_SCOPE) {
                        int is_void = (n->childs->data[0].str && strstr(n->childs->data[0].str, "void") != NULL);
                        pregen_scope(scope->childs, 0, (int)scope->childs->size, fname, params, 0, is_void); /* insert_deletes = 0 */
                    }
                }
            }
        }
    }

    /* Pass 2: вставляем delete, используя полную картину */
    for (int j = 0; j < (int)nodes->size; j++) {
        Node *n = &nodes->data[j];
        if ((n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF || 
             n->type == NODE_ASYNC_FUNC_DEF || n->type == NODE_AWAIT_FUNC_DEF) &&
            n->childs->size >= 4) {
            Node *scope       = &n->childs->data[3];
            const char *fname = n->childs->data[1].str ? n->childs->data[1].str : "";
            Node *params      = (n->childs->size >= 3 && n->childs->data[2].type == NODE_PARAMS) ? &n->childs->data[2] : NULL;
            if (scope->type == NODE_SCOPE) {
                int is_void = (n->childs->data[0].str && strstr(n->childs->data[0].str, "void") != NULL);
                pregen_scope(scope->childs, 0, (int)scope->childs->size, fname, params, 1, is_void); /* insert_deletes = 1 */
            }
        } else if (n->type == NODE_STRUCT_DEF) {
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if ((n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF || 
                     n->type == NODE_ASYNC_FUNC_DEF || n->type == NODE_AWAIT_FUNC_DEF) && child->childs->size >= 4) {
                    Node *scope       = &child->childs->data[3];
                    const char *fname = child->childs->data[1].str ? child->childs->data[1].str : "";
                    Node *params      = (child->childs->size >= 3 && child->childs->data[2].type == NODE_PARAMS) ? &child->childs->data[2] : NULL;
                    if (scope->type == NODE_SCOPE) {
                        int is_void = (n->childs->data[0].str && strstr(n->childs->data[0].str, "void") != NULL);
                        pregen_scope(scope->childs, 0, (int)scope->childs->size, fname, params, 1, is_void); /* insert_deletes = 1 */
                    }
                }
            }
        }
    }

    for (int i = 0; i < g_func_index_count; i++) {
        FuncRecord *r = &g_func_index[i];
        fprintf(stderr, "pregen[IDX]: func[%d]='%s' params=%d%s\n",
            r->index, r->name, r->param_count,
            r->returns_ptr ? " [ownership]" : "");
        for (int p = 0; p < r->param_count; p++)
            fprintf(stderr, "  param[%d]: '%s'%s\n",
                p, r->params[p].name, r->params[p].is_ptr ? " *ptr*" : "");
    }

    for (int i = 0; i < g_arg_alias_count; i++) {
        GlobalArgAlias *e = &g_arg_aliases[i];
        if (e->escapes_out)
            fprintf(stderr, "pregen[GAA]: func='%s' param=%d vaddr=0x%x → escapes caller",
                e->func_name, e->param_idx, e->vaddr);
        else
            fprintf(stderr, "pregen[GAA]: func='%s' param=%d vaddr=0x%x → dies in callee",
                e->func_name, e->param_idx, e->vaddr);
        if (e->alias_effect_count > 0) {
            fprintf(stderr, " aliases=[");
            for (int k = 0; k < e->alias_effect_count; k++)
                fprintf(stderr, "%s%s", k ? "," : "", e->alias_effects[k].alias);
            fprintf(stderr, "]");
        }
        if (e->field_effect_count > 0) {
            fprintf(stderr, " fields=[");
            for (int k = 0; k < e->field_effect_count; k++)
                fprintf(stderr, "%s%s%s%s", k ? "," : "",
                    e->field_effects[k].obj,
                    e->field_effects[k].is_arrow ? "->" : ".",
                    e->field_effects[k].field);
            fprintf(stderr, "]");
        }
        fprintf(stderr, "\n");
    }
}