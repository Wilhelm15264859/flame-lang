#include "fl_Pregen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>   /* uint64_t — нужен для CFG-битсетов */

/* ═══════════════════════════════════════════════════════════════════
 * Константы
 * ═══════════════════════════════════════════════════════════════════ */
#define MAX_AUTODEL        64
#define MAX_OWNERSHIP_FUNCS 64
#define MAX_VADDR_VARS     128
#define MAX_VADDR_NAMES    32
#define MAX_FIELD_ALIASES  32
#define MAX_INDEX_ALIASES  32
#define MAX_VADDR_POOL     512
#define MAX_GLOBAL_SLOTS   256

/* ═══════════════════════════════════════════════════════════════════
 * Счётчик глобальных индексных переменных (_idx_N)
 * ═══════════════════════════════════════════════════════════════════ */
static int g_idx_counter = 0;

/* ═══════════════════════════════════════════════════════════════════
 * Глобальные счётчики
 * ═══════════════════════════════════════════════════════════════════ */
static int  autodel_counter     = 0;
static int  ownership_func_count = 0;
static char ownership_funcs[MAX_OWNERSHIP_FUNCS][64];

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
 * Индекс функций — таблица всех известных функций и их параметров
 * ═══════════════════════════════════════════════════════════════════ */
#define MAX_FUNC_PARAMS  32
#define MAX_FUNC_INDEX   128

/* Запись об одном параметре функции: имя + является ли указателем */
typedef struct {
    char name[64];
    int  is_ptr;   /* 1 если тип заканчивается на '*' */
} FuncParam;

/* Запись об одной функции */
typedef struct {
    char       name[64];
    int        index;            /* порядковый индекс функции в таблице */
    FuncParam  params[MAX_FUNC_PARAMS];
    int        param_count;
    int        returns_ptr;      /* 1 если ownership-функция */
} FuncRecord;

static FuncRecord g_func_index[MAX_FUNC_INDEX];
static int        g_func_index_count = 0;

static void func_index_reset(void) {
    g_func_index_count = 0;
}

/* Регистрирует функцию (идемпотентно) и возвращает указатель на запись */
static FuncRecord *func_index_get_or_add(const char *name) {
    for (int i = 0; i < g_func_index_count; i++)
        if (strcmp(g_func_index[i].name, name) == 0)
            return &g_func_index[i];
    if (g_func_index_count >= MAX_FUNC_INDEX) return NULL;
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

/* Заполняет индекс параметров из AST-узла NODE_PARAMS */
static void func_index_register_params(FuncRecord *r, Node *params_node) {
    if (!params_node || params_node->type != NODE_PARAMS) return;
    r->param_count = 0;
    for (unsigned long long i = 0; i < params_node->childs->size && r->param_count < MAX_FUNC_PARAMS; i++) {
        Node *p = &params_node->childs->data[i];
        /* Ожидаем NODE_VAR_DEF с childs[0]=type, childs[1]=name */
        if (p->type != NODE_VAR_DEF || !p->childs || p->childs->size < 2) continue;
        const char *type_str = p->childs->data[0].str ? p->childs->data[0].str : "";
        const char *nm       = p->childs->data[1].str ? p->childs->data[1].str : "";
        FuncParam *fp = &r->params[r->param_count++];
        strncpy(fp->name, nm, 63);     fp->name[63] = '\0';
        /* Указатель: тип заканчивается на '*' или содержит '*' */
        int tlen = (int)strlen(type_str);
        fp->is_ptr = (tlen > 0 && type_str[tlen - 1] == '*') ? 1 : 0;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Глобальный граф алиасов (межпроцедурный анализ)
 * ═══════════════════════════════════════════════════════════════════ */

/* Эффект, который функция производит над одним параметром-указателем */
#define MAX_PARAM_ALIASES  16
#define MAX_PARAM_FIELDS   16

typedef struct {
    char alias[64];      /* имя нового алиаса внутри тела функции */
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
    /* Эффекты, которые были собраны при анализе тела функции */
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
    e->param_idx         = pidx;
    e->vaddr             = vaddr;
    e->escapes_out       = 0;
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

/* Записывает, что параметр param_idx функции fn получил новый алиас alias_name */
static void gaa_add_alias_effect(const char *fn, int pidx, int vaddr, const char *alias_name) {
    GlobalArgAlias *e = gaa_get_or_push(fn, pidx, vaddr);
    if (!e) return;
    for (int i = 0; i < e->alias_effect_count; i++)
        if (strcmp(e->alias_effects[i].alias, alias_name) == 0) return;
    if (e->alias_effect_count >= MAX_PARAM_ALIASES) return;
    strncpy(e->alias_effects[e->alias_effect_count++].alias, alias_name, 63);
}

/* Записывает, что параметр получил новый field-alias */
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

static Node make_tempvar_node(const char *tmp_name, const char *type_str,
                              Node *expr) {
    Node var;
    var.childs = malloc(sizeof(vector_node));
    vn_init(var.childs, 3);
    var.str  = NULL;
    var.type = NODE_VAR_DEF;
    Node *type  = make_node(NODE_TYPE,  type_str);
    vn_push_back(var.childs, *type);  free(type);
    Node *ident = make_node(NODE_IDENT, tmp_name);
    vn_push_back(var.childs, *ident); free(ident);
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
        if ((n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF) &&
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
    for (int i = 0; i < ownership_func_count; i++)
        if (strcmp(ownership_funcs[i], name) == 0) return;
    if (ownership_func_count >= MAX_OWNERSHIP_FUNCS) return;
    strncpy(ownership_funcs[ownership_func_count++], name, 63);
}

static int ownership_func_lookup(const char *name) {
    for (int i = 0; i < ownership_func_count; i++)
        if (strcmp(ownership_funcs[i], name) == 0) return 1;
    return 0;
}

static void collect_autodel_names_in_scope(vector_node *nodes, char out[MAX_AUTODEL][64], int *count) {
    for (unsigned long long j = 0; j < nodes->size; j++) {
        Node *n = &nodes->data[j];
        if (n->type == NODE_VAR_DEF && n->childs->size >= 2) {
            const char *ts = n->childs->data[0].str;
            if (strncmp(ts, "autodel:", 8) == 0 && *count < MAX_AUTODEL)
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
}

static void collect_returns_in_scope(vector_node *nodes, char autodel_names[MAX_AUTODEL][64], int autodel_count, const char *fname) {
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
        if ((n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF) &&
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

/* Алиас через индекс массива: arr[idx_var] = ptr  */
typedef struct {
    char arr[64];      /* имя массива                  */
    char idx_var[64];  /* имя переменной-индекса       */
} IndexAlias;

typedef struct {
    int        vaddr;
    char       names[MAX_VADDR_NAMES][64];
    int        name_count;
    FieldAlias fields[MAX_FIELD_ALIASES];
    int        field_count;
    IndexAlias idx_aliases[MAX_INDEX_ALIASES];
    int        idx_alias_count;
    char       base_type[64];
    int        escaped;    /* 1 = убежал через return или массив */
    
    vector_node *birth_vec;
    int          birth_idx;
    
    vector_node *limit_vec;
    int          limit_idx;
} VAddrSlot;

/* ─── Операции над слотом ─────────────────────────────────────────── */

static void slot_add_name(VAddrSlot *s, const char *name) {
    for (int k = 0; k < s->name_count; k++)
        if (strcmp(s->names[k], name) == 0) return;
    if (s->name_count < MAX_VADDR_NAMES)
        strncpy(s->names[s->name_count++], name, 63);
}

static void slot_remove_name(VAddrSlot *s, const char *name) {
    for (int k = 0; k < s->name_count; k++) {
        if (strcmp(s->names[k], name) == 0) {
            for (int m = k; m < s->name_count - 1; m++)
                strncpy(s->names[m], s->names[m + 1], 63);
            s->name_count--;
            return;
        }
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
    /* Удаляем все IndexAlias у которых переменная-индекс совпадает с idx_var */
    int w = 0;
    for (int k = 0; k < s->idx_alias_count; k++) {
        if (strcmp(s->idx_aliases[k].idx_var, idx_var) != 0)
            s->idx_aliases[w++] = s->idx_aliases[k];
    }
    s->idx_alias_count = w;
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

/* Версия без фильтра по escaped — нужна при сборе эффектов callee */
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

    /* Проверяем idx_aliases: arr[idx_var] — использование слота */
    for (int k = 0; k < s->idx_alias_count; k++) {
        if (!n || !n->childs) continue;
        /* Прямое использование: NODE_INDEX с arr и idx_var */
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
static void collect_aliases_in_node(Node *n, VAddrSlot *slots, int slot_count, int *changed) {
    if (!n) return;
    
    if (n->type == NODE_VAR_DEF && n->childs->size >= 3) {
        Node *init = &n->childs->data[2];
        if (init->type == NODE_VAR && init->str && init->str[0]) {
            int si = find_slot_by_name(slots, slot_count, init->str);
            if (si >= 0 && n->childs->data[1].str) {
                int before = slots[si].name_count;
                slot_add_name(&slots[si], n->childs->data[1].str);
                if (slots[si].name_count != before) *changed = 1;
            }
        }
    }
    else if (n->type == NODE_ASSIGN && n->childs->size >= 3) {
        Node *rhs = &n->childs->data[2];
        if (rhs->type == NODE_VAR && rhs->str && rhs->str[0]) {
            int si = find_slot_by_name(slots, slot_count, rhs->str);
            if (si >= 0 && n->childs->data[0].str) {
                int before = slots[si].name_count;
                slot_add_name(&slots[si], n->childs->data[0].str);
                if (slots[si].name_count != before) *changed = 1;
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
        /* arr[idx] = ptr  — добавляем idx_alias вместо немедленного escaped */
        if (n->childs->size >= 3) {
            Node *rhs = &n->childs->data[n->childs->size - 1];
            const char *rhs_base = get_base_var(rhs);
            if (rhs_base) {
                int si = find_slot_by_name(slots, slot_count, rhs_base);
                if (si >= 0 && !slots[si].escaped) {
                    /* Определяем имя массива и переменную-индекс */
                    Node *lhs = &n->childs->data[0]; /* NODE_INDEX или цепочка */
                    const char *arr_name = NULL;
                    const char *idx_name = NULL;
                    if (lhs->type == NODE_INDEX && lhs->childs && lhs->childs->size >= 2) {
                        arr_name = get_base_var(&lhs->childs->data[0]);
                        /* Индекс должен быть переменной — тогда можем отследить */
                        Node *idx_nd = &lhs->childs->data[1];
                        if (idx_nd->type == NODE_VAR || idx_nd->type == NODE_IDENT)
                            idx_name = idx_nd->str;
                    }
                    if (arr_name && idx_name) {
                        int before = slots[si].idx_alias_count;
                        slot_add_idx_alias(&slots[si], arr_name, idx_name);
                        if (slots[si].idx_alias_count != before) *changed = 1;
                    } else {
                        /* Не можем отследить — помечаем как escaped */
                        if (!slots[si].escaped) {
                            slots[si].escaped = 1;
                            *changed = 1;
                        }
                    }
                }
            }
        }
    }
    /* Переприсвоение переменной-индекса: idx = ... — сбрасываем idx_alias в иерархии */
    else if ((n->type == NODE_ASSIGN || n->type == NODE_VAR_DEF) && n->childs->size >= 2) {
        const char *dest = NULL;
        if (n->type == NODE_ASSIGN && n->childs->data[0].str)
            dest = n->childs->data[0].str;
        else if (n->type == NODE_VAR_DEF && n->childs->size >= 2 && n->childs->data[1].str)
            dest = n->childs->data[1].str;
        if (dest && dest[0]) {
            /* Проверяем, не является ли dest переменной-индексом какого-либо слота */
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
            collect_aliases_in_node(&n->childs->data[i], slots, slot_count, changed);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Рекурсивный поиск Split-Lifetime (a = new)
 * ═══════════════════════════════════════════════════════════════════ */
static void split_lifetime_recursive(vector_node *vec, int start, int end, VAddrSlot *slots, int *slot_count) {
    for (int i = start; i < end; i++) {
        Node *n = &vec->data[i];
        if (n->type == NODE_ASSIGN && n->childs->size >= 3) {
            Node *rhs = &n->childs->data[2];
            const char *dest = n->childs->data[0].str;
            if (dest && dest[0]) {
                int is_reinit = (rhs->type == NODE_NEW) ||
                                (rhs->type == NODE_FUNC_CALL && rhs->str && ownership_func_lookup(rhs->str));
                if (is_reinit) {
                    int old_si = find_slot_by_name(slots, *slot_count, dest);
                    if (old_si >= 0) {
                        VAddrSlot *old_slot = &slots[old_si];
                        slot_remove_name(old_slot, dest);
                        if (old_slot->name_count == 0 && !old_slot->escaped) {
                            old_slot->limit_vec = vec;
                            old_slot->limit_idx = i;
                        }
                        if (*slot_count < MAX_VADDR_VARS) {
                            VAddrSlot *ns = &slots[(*slot_count)++];
                            ns->vaddr = vaddr_pop();
                            ns->name_count = 0;
                            ns->field_count = 0;
                            ns->escaped = 0;
                            ns->birth_vec = vec;
                            ns->birth_idx = i;
                            ns->limit_vec = NULL;
                            ns->limit_idx = -1;
                            strncpy(ns->base_type, old_slot->base_type, 63);
                            slot_add_name(ns, dest);
                        }
                    }
                }
            }
        }
        if (n->childs) {
            split_lifetime_recursive(n->childs, 0, n->childs->size, slots, slot_count);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Рекурсивная мутация Return (Escape)
 * ═══════════════════════════════════════════════════════════════════ */
static void mutate_returns_recursive(vector_node *vec, int start, int end, VAddrSlot *slots, int slot_count, const char *func_name) {
    for (int i = start; i < end; i++) {
        Node *n = &vec->data[i];
        if (n->type == NODE_RETURN && n->childs->size > 0) {
            Node *ret_expr = &n->childs->data[0];
            if (ret_expr->type != NODE_UNDEF) {
                for (int si = 0; si < slot_count; si++) {
                    if (slots[si].escaped) continue;
                    if (!slot_used_in_node(ret_expr, &slots[si])) continue;

                    if (ret_expr->type == NODE_VAR) {
                        int direct = 0;
                        for (int k = 0; k < slots[si].name_count && !direct; k++)
                            if (strcmp(ret_expr->str, slots[si].names[k]) == 0) direct = 1;
                        if (direct) {
                            slots[si].escaped = 1;
                            if (func_name && func_name[0]) gaa_mark_escape(func_name, slots[si].vaddr);
                            break;
                        }
                    }

                    char tmp_name[64];
                    snprintf(tmp_name, sizeof(tmp_name), "__ret_%d", autodel_counter++);

                    Node ret_expr_copy = *ret_expr;
                    Node tmpvar  = make_tempvar_node(tmp_name, slots[si].base_type, &ret_expr_copy);
                    Node del     = make_delete_node(slots[si].names[0]);
                    Node new_ret = make_return_var_node(tmp_name);

                    vec->data[i] = new_ret;
                    vn_insert_at(vec, i, del);
                    vn_insert_at(vec, i, tmpvar);
                    end += 2;
                    i   += 2;

                    slots[si].escaped = 1;
                    if (func_name && func_name[0]) gaa_mark_escape(func_name, slots[si].vaddr);
                    break;
                }
            }
        }
        if (n->childs && n->type != NODE_RETURN) {
            mutate_returns_recursive(n->childs, 0, n->childs->size, slots, slot_count, func_name);
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
 *
 * Каждый CFGBlock соответствует одному «прямолинейному» участку AST:
 *   – телу функции между операторами ветвления
 *   – телу if / else / for / while / do-while
 *
 * Рёбра: succ[] — список преемников (обычно ≤2).
 * Данные liveness хранятся побитово: bit[i] ↔ слот i.
 * Максимальное число слотов = MAX_VADDR_VARS (128).
 *
 * Алгоритм (backward dataflow, May-analysis):
 *   live_out[B] = ⋃  live_in[S]   для каждого преемника S
 *   live_in[B]  = use[B] ∪ (live_out[B] \ def[B])
 * Итерируем до fixpoint.
 * ═══════════════════════════════════════════════════════════════════ */

#define CFG_MAX_BLOCKS  512
#define CFG_MAX_SUCCS     4
#define CFG_SLOT_WORDS  ((MAX_VADDR_VARS + 63) / 64)   /* 2 uint64 для 128 слотов */

typedef struct {
    /* Ссылка на фрагмент AST */
    vector_node *vec;   /* вектор, содержащий операторы блока   */
    int          start; /* индекс первого оператора (включ.)    */
    int          end;   /* индекс последнего оператора (включ.) */

    /* Рёбра */
    int succ[CFG_MAX_SUCCS];
    int succ_count;
    int pred[CFG_MAX_SUCCS * 4]; /* простой список предшественников */
    int pred_count;

    /* Liveness битсеты */
    uint64_t use [CFG_SLOT_WORDS]; /* используется в блоке до определения  */
    uint64_t def [CFG_SLOT_WORDS]; /* определяется (убивается) в блоке     */
    uint64_t live_in [CFG_SLOT_WORDS];
    uint64_t live_out[CFG_SLOT_WORDS];
} CFGBlock;

typedef struct {
    CFGBlock blocks[CFG_MAX_BLOCKS];
    int      count;
    int      entry; /* индекс entry-блока  */
    int      exit;  /* индекс virtual exit */
} CFG;

/* ─── Битсет-хелперы ──────────────────────────────────────────────── */

static void bs_clear(uint64_t *w) {
    for (int i = 0; i < CFG_SLOT_WORDS; i++) w[i] = 0;
}
static void bs_set(uint64_t *w, int bit) {
    if (bit < 0 || bit >= MAX_VADDR_VARS) return;
    w[bit >> 6] |= (uint64_t)1 << (bit & 63);
}
static int bs_test(const uint64_t *w, int bit) {
    if (bit < 0 || bit >= MAX_VADDR_VARS) return 0;
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
/* dst = src2 & ~src3  (set difference) */
static void bs_andnot(uint64_t *dst, const uint64_t *src2, const uint64_t *src3) {
    for (int i = 0; i < CFG_SLOT_WORDS; i++)
        dst[i] = src2[i] & ~src3[i];
}

/* ─── Построение CFG ──────────────────────────────────────────────── */

/* Прямое объявление: cfg_build_vec заполняет блоки из вектора */
static int cfg_build_vec(CFG *cfg, vector_node *vec, int start, int end, int after_block);

static int cfg_new_block(CFG *cfg) {
    if (cfg->count >= CFG_MAX_BLOCKS) return -1;
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

/*
 * cfg_build_vec: рекурсивно строит CFG из вектора [start, end).
 * Возвращает id блока, который «выходит» из этого фрагмента,
 * чтобы вызывающий мог добавить ребро к продолжению.
 * after_block — id блока, куда должны идти «fall-through» рёбра
 * из конца данного фрагмента (-1 = виртуальный exit).
 */
static int cfg_build_vec(CFG *cfg, vector_node *vec, int start, int end, int after_block) {
    /* Создаём текущий «линейный» блок */
    int cur = cfg_new_block(cfg);
    if (cur < 0) return -1;
    cfg->blocks[cur].vec   = vec;
    cfg->blocks[cur].start = start;
    cfg->blocks[cur].end   = start - 1; /* пока пуст */

    for (int j = start; j < end; j++) {
        Node *n = &vec->data[j];

        if (n->type == NODE_IF && n->childs) {
            /* Закрываем текущий линейный блок */
            cfg->blocks[cur].end = j - 1;

            /* Блок для условия (сам if — одна инструкция) */
            int cond_blk = cfg_new_block(cfg);
            if (cond_blk < 0) break;
            cfg->blocks[cond_blk].vec   = vec;
            cfg->blocks[cond_blk].start = j;
            cfg->blocks[cond_blk].end   = j;
            cfg_add_edge(cfg, cur, cond_blk);

            /* Блок-слияние после if */
            int merge_blk = cfg_new_block(cfg);
            if (merge_blk < 0) break;
            cfg->blocks[merge_blk].vec   = vec;
            cfg->blocks[merge_blk].start = j + 1;
            cfg->blocks[merge_blk].end   = j;     /* пуст, заполнится дальше */

            /* Ветви then / else */
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
            /* Если else нет — прямое ребро cond → merge */
            if (!has_else) cfg_add_edge(cfg, cond_blk, merge_blk);

            /* Продолжаем в merge_blk */
            cur = merge_blk;
            cfg->blocks[cur].start = j + 1;
            continue;
        }

        if ((n->type == NODE_WHILE || n->type == NODE_FOR ||
             n->type == NODE_DO_WHILE) && n->childs) {
            /* Закрываем линейный блок */
            cfg->blocks[cur].end = j - 1;

            /* Блок заголовка цикла */
            int hdr = cfg_new_block(cfg);
            if (hdr < 0) break;
            cfg->blocks[hdr].vec   = vec;
            cfg->blocks[hdr].start = j;
            cfg->blocks[hdr].end   = j;

            /* Блок после цикла */
            int after = cfg_new_block(cfg);
            if (after < 0) break;
            cfg->blocks[after].vec   = vec;
            cfg->blocks[after].start = j + 1;
            cfg->blocks[after].end   = j;

            cfg_add_edge(cfg, cur, hdr);

            /* Тело цикла */
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if (child->type == NODE_SCOPE && child->childs) {
                    int body_exit = cfg_build_vec(cfg, child->childs, 0,
                                                   (int)child->childs->size, hdr);
                    (void)body_exit;
                }
            }

            /* hdr → тело и hdr → after (exit condition) */
            cfg_add_edge(cfg, hdr, after);

            cur = after;
            cfg->blocks[cur].start = j + 1;
            continue;
        }

        /* Обычная инструкция — расширяем текущий линейный блок */
        cfg->blocks[cur].end = j;

        /* Early exit: return / break / continue */
        if (n->type == NODE_RETURN) {
            int ab = (after_block >= 0) ? after_block : cfg->exit;
            cfg_add_edge(cfg, cur, ab);
            /* Начинаем мёртвый блок для кода после return */
            int dead = cfg_new_block(cfg);
            if (dead < 0) break;
            cfg->blocks[dead].vec   = vec;
            cfg->blocks[dead].start = j + 1;
            cfg->blocks[dead].end   = j;
            cur = dead;
        }
    }

    /* Fall-through из конца блока */
    int ab = (after_block >= 0) ? after_block : cfg->exit;
    cfg_add_edge(cfg, cur, ab);
    return cur;
}

/* Инициализация и построение полного CFG функции */
static void cfg_init(CFG *cfg) {
    cfg->count = 0;
    cfg->entry = cfg_new_block(cfg); /* 0 = entry */
    cfg->exit  = cfg_new_block(cfg); /* 1 = virtual exit */
}

static void cfg_build(CFG *cfg, vector_node *scope_vec) {
    cfg_init(cfg);
    /* entry → тело → exit */
    cfg_build_vec(cfg, scope_vec, 0, (int)scope_vec->size, cfg->exit);
    cfg_add_edge(cfg, cfg->entry,
                 cfg->count > 2 ? 2 : cfg->exit); /* entry → первый реальный блок */
}

/* ─── Заполнение use/def для одного блока ────────────────────────── */

/*
 * Для целей liveness:
 *   use[B][i] = 1  если слот i читается в блоке до своего определения
 *   def[B][i] = 1  если слот i определяется (delete-вставка или new) в блоке
 *
 * Мы заполняем use/def *по слотам* (не по переменным), используя
 * уже вычисленную таблицу slots[].
 */
static void cfg_fill_use_def(CFG *cfg, VAddrSlot *slots, int slot_count) {
    for (int bi = 0; bi < cfg->count; bi++) {
        CFGBlock *b = &cfg->blocks[bi];
        if (!b->vec || b->start > b->end) continue;
        for (int j = b->start; j <= b->end && j < (int)b->vec->size; j++) {
            Node *n = &b->vec->data[j];
            for (int si = 0; si < slot_count; si++) {
                if (slot_used_in_node(n, &slots[si])) {
                    /* Если слот ещё не определён в блоке — это USE */
                    if (!bs_test(b->def, si))
                        bs_set(b->use, si);
                }
                /* NODE_VAR_DEF autodel: считается определением */
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

/* ─── Итеративный backward liveness ──────────────────────────────── */
static void cfg_liveness(CFG *cfg) {
    int changed = 1;
    while (changed) {
        changed = 0;
        /* Backward: проходим в обратном порядке блоков */
        for (int bi = cfg->count - 1; bi >= 0; bi--) {
            CFGBlock *b = &cfg->blocks[bi];

            /* live_out[B] = ⋃ live_in[S] для всех преемников S */
            uint64_t new_out[CFG_SLOT_WORDS];
            bs_clear(new_out);
            for (int s = 0; s < b->succ_count; s++) {
                int sid = b->succ[s];
                if (sid >= 0 && sid < cfg->count)
                    bs_or_changed(new_out, cfg->blocks[sid].live_in);
            }
            if (bs_or_changed(b->live_out, new_out)) changed = 1;

            /* live_in[B] = use[B] ∪ (live_out[B] \ def[B]) */
            uint64_t new_in[CFG_SLOT_WORDS];
            bs_andnot(new_in, b->live_out, b->def);
            for (int i = 0; i < CFG_SLOT_WORDS; i++) new_in[i] |= b->use[i];
            if (bs_or_changed(b->live_in, new_in)) changed = 1;
        }
    }
}

/*
 * cfg_last_use_point: возвращает (через out-параметры) вектор и индекс,
 * ПОСЛЕ которого нужно вставить delete для слота si.
 *
 * Алгоритм:
 *   Ищем последний basic block B такой, что:
 *     bs_test(live_in[B], si) == 1  ИЛИ  slot_used_in_node в блоке
 *   и хотя бы в одном из его преемников live_in[succ] не содержит si.
 *   Это — «выход» слота.
 *   Внутри блока ищем последнее физическое использование.
 */
static void cfg_last_use_point(CFG *cfg, int si,
                                VAddrSlot *slots,
                                vector_node **out_vec, int *out_idx) {
    *out_vec = NULL;
    *out_idx = -1;

    /* Блоки, в которых слот живой (live_in или live_out) */
    for (int bi = cfg->count - 1; bi >= 0; bi--) {
        CFGBlock *b = &cfg->blocks[bi];
        if (!b->vec || b->start > b->end) continue;

        int live_here = bs_test(b->live_in,  si) ||
                        bs_test(b->live_out, si);
        if (!live_here) continue;

        /* Проверяем, что хотя бы один преемник НЕ держит слот живым */
        int dies_after = 0;
        if (b->succ_count == 0) {
            dies_after = 1;
        } else {
            for (int s = 0; s < b->succ_count; s++) {
                int sid = b->succ[s];
                if (sid < 0 || sid >= cfg->count ||
                    !bs_test(cfg->blocks[sid].live_in, si))
                    dies_after = 1;
            }
        }
        if (!dies_after) continue;

        /* Нашли последний живой блок. Ищем последнее использование внутри. */
        *out_vec = b->vec;
        *out_idx = b->start; /* fallback — начало блока */

        int lim = b->end;
        if (lim >= (int)b->vec->size) lim = (int)b->vec->size - 1;
        for (int j = b->start; j <= lim; j++) {
            if (slot_used_in_node(&b->vec->data[j], &slots[si]))
                *out_idx = j;
        }
        return; /* Достаточно первого найденного блока (reverse order) */
    }

    /* Fallback: если слот вообще нигде не используется — точка рождения */
    if (*out_vec == NULL) {
        *out_vec = slots[si].birth_vec;
        *out_idx = slots[si].birth_idx;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * pregen_scope
 * ═══════════════════════════════════════════════════════════════════ */
static void pregen_scope(vector_node *nodes, int start, int end,
                         const char *func_name, Node *params_node) {

    /* ── 0. Регистрируем функцию в индексе ───────────────────────────── */
    if (func_name && func_name[0]) {
        FuncRecord *fr = func_index_get_or_add(func_name);
        if (fr && params_node)
            func_index_register_params(fr, params_node);
        if (fr)
            fr->returns_ptr = ownership_func_lookup(func_name);
    }

    /* Рекурсивный спуск в блоки */
    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];
        if (n->type == NODE_FUNC_DEF && n->childs->size >= 4) {
            Node *scope = &n->childs->data[3];
            const char *fn = n->childs->data[1].str ? n->childs->data[1].str : "";
            Node *p = (n->childs->size >= 3 && n->childs->data[2].type == NODE_PARAMS) ? &n->childs->data[2] : NULL;
            if (scope->type == NODE_SCOPE) pregen_scope(scope->childs, 0, (int)scope->childs->size, fn, p);
        } else if (n->type == NODE_SCOPE) {
            pregen_scope(n->childs, 0, (int)n->childs->size, func_name, NULL);
        } else if (node_is_block(n) && n->childs) {
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if (child->type == NODE_SCOPE && child->childs)
                    pregen_scope(child->childs, 0, (int)child->childs->size, func_name, NULL);
            }
        } else if (n->type == NODE_IF && n->childs) {
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if (child->type == NODE_SCOPE && child->childs)
                    pregen_scope(child->childs, 0, (int)child->childs->size, func_name, NULL);
                else if (child->type == NODE_ELSE && child->childs)
                    for (unsigned long long m = 0; m < child->childs->size; m++) {
                        Node *ec = &child->childs->data[m];
                        if (ec->type == NODE_SCOPE && ec->childs)
                            pregen_scope(ec->childs, 0, (int)ec->childs->size, func_name, NULL);
                    }
            }
        }
    }

    VAddrSlot slots[MAX_VADDR_VARS];
    int slot_count = 0;

    /* 1. Локальные autodel-переменные (ПАРАМЕТРОВ БОЛЬШЕ НЕТ) */
    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];
        if (n->type != NODE_VAR_DEF || n->childs->size < 2) continue;
        const char *type_str = n->childs->data[0].str;
        if (strncmp(type_str, "autodel:", 8) != 0) continue;
        if (slot_count >= MAX_VADDR_VARS) break;

        VAddrSlot *s   = &slots[slot_count++];
        s->vaddr       = vaddr_pop();
        s->name_count  = 0;
        s->field_count = 0;
        s->escaped     = 0;
        
        s->birth_vec   = nodes;
        s->birth_idx   = j;
        s->limit_vec   = NULL;
        s->limit_idx   = -1;

        strncpy(s->base_type, type_str + 8, 63);
        s->base_type[63] = '\0';
        int blen = (int)strlen(s->base_type);
        if (blen > 0 && s->base_type[blen - 1] == '*') s->base_type[blen - 1] = '\0';
        slot_add_name(s, n->childs->data[1].str);
    }

    if (slot_count == 0) return;

    /* 2. ГЛУБОКОЕ Распространение алиасов */
    int changed = 1;
    while (changed) {
        changed = 0;
        for (int j = start; j < end; j++) {
            collect_aliases_in_node(&nodes->data[j], slots, slot_count, &changed);
        }
    }

    /* 2b. Записываем эффекты параметров-указателей в глобальный граф.
     *     Для каждого параметра-указателя смотрим, какие алиасы и field-aliases
     *     накопились у его слота, и сохраняем их как эффекты callee. */
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
                /* Ищем слот по имени параметра (включая escaped — параметр мог убежать) */
                int si = find_slot_by_name_any(slots, slot_count, pname);
                if (si >= 0) {
                    VAddrSlot *s = &slots[si];
                    gaa_push(func_name, pidx, s->vaddr);
                    /* Алиасы (все имена кроме самого параметра) */
                    for (int k = 0; k < s->name_count; k++) {
                        if (strcmp(s->names[k], pname) != 0)
                            gaa_add_alias_effect(func_name, pidx, s->vaddr, s->names[k]);
                    }
                    /* Field-aliases */
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

    /* 3. ГЛУБОКИЙ Split-Lifetime */
    split_lifetime_recursive(nodes, start, end, slots, &slot_count);

    /* 4. Глобальный граф вызовов + применение эффектов callee к слотам caller */
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
                    /* Регистрируем передачу */
                    gaa_push(callee, param_idx, slots[si].vaddr);

                    /* Применяем сохранённые эффекты callee к нашему слоту.
                     * Ищем запись о callee/param_idx в глобальном графе.
                     * Там могут быть алиасы и field-aliases собранные из тела callee —
                     * добавляем их в слот caller, чтобы дальнейший анализ
                     * (split-lifetime, escape, delete-insertion) их учёл. */
                    for (int gi = 0; gi < g_arg_alias_count; gi++) {
                        GlobalArgAlias *gae = &g_arg_aliases[gi];
                        if (strcmp(gae->func_name, callee) != 0) continue;
                        if (gae->param_idx != param_idx) continue;
                        /* Алиасы */
                        for (int ai = 0; ai < gae->alias_effect_count; ai++) {
                            int before = slots[si].name_count;
                            slot_add_name(&slots[si], gae->alias_effects[ai].alias);
                            (void)before; /* изменения учитываются при след. итерации */
                        }
                        /* Field-aliases */
                        for (int fi = 0; fi < gae->field_effect_count; fi++) {
                            slot_add_field(&slots[si],
                                           gae->field_effects[fi].obj,
                                           gae->field_effects[fi].field,
                                           gae->field_effects[fi].is_arrow);
                        }
                        /* Escape */
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

    /* 5. Рекурсивная мутация возвратов (Escape) */
    mutate_returns_recursive(nodes, start, end, slots, slot_count, func_name);

    /* 6. Строим CFG скопа и вычисляем liveness */
    CFG cfg;
    cfg_build(&cfg, nodes);
    cfg_fill_use_def(&cfg, slots, slot_count);
    cfg_liveness(&cfg);

    /* 6b. Подготовка и сортировка вставок delete */
    Insertion ins[MAX_VADDR_VARS];
    int ins_count = 0;

    for (int si = 0; si < slot_count; si++) {
        if (slots[si].escaped) {
            vaddr_push_free(slots[si].vaddr);
            continue;
        }

        vector_node *l_vec = NULL;
        int l_idx = -1;

        if (slots[si].limit_vec != NULL) {
            /* Слот разделён: удаление перед новым a = new() */
            l_vec = slots[si].limit_vec;
            l_idx = slots[si].limit_idx - 1;
        } else {
            /* Естественная смерть: спрашиваем CFG */
            cfg_last_use_point(&cfg, si, slots, &l_vec, &l_idx);
            /* Fallback на старое поведение, если CFG не нашёл блок */
            if (!l_vec) {
                l_vec = slots[si].birth_vec;
                l_idx = slots[si].birth_idx;
                for (unsigned long long i = (unsigned long long)slots[si].birth_idx;
                     i < l_vec->size; i++) {
                    if (slot_used_in_node(&l_vec->data[i], &slots[si]))
                        l_idx = (int)i;
                }
            }
        }

        ins[ins_count].slot_idx = si;
        ins[ins_count].vec      = l_vec;
        ins[ins_count].idx      = l_idx;
        ins_count++;
    }

    /* Сортируем вставки (от конца к началу), чтобы индексы при вставке не сбивались */
    for (int i = 0; i < ins_count - 1; i++) {
        for (int j = i + 1; j < ins_count; j++) {
            if (ins[i].vec < ins[j].vec || 
               (ins[i].vec == ins[j].vec && ins[i].idx < ins[j].idx)) {
                Insertion tmp = ins[i];
                ins[i] = ins[j];
                ins[j] = tmp;
            }
        }
    }

    /* 7. Безопасная вставка узлов зануления и delete */
    for (int i = 0; i < ins_count; i++) {
        int si = ins[i].slot_idx;
        vector_node *v = ins[i].vec;
        int insert_pos = ins[i].idx + 1;

        /* Проверяем, не вставлен ли delete уже (например, ручной delete в коде) */
        int already_deleted = 0;
        for (unsigned long long jj = insert_pos; jj < v->size; jj++) {
            Node *nn = &v->data[jj];
            if (nn->type == NODE_DELETE && nn->childs && nn->childs->size >= 2) {
                Node *vn2 = &nn->childs->data[1];
                if (vn2->type == NODE_VAR && vn2->str &&
                    slots[si].name_count > 0 &&
                    strcmp(vn2->str, slots[si].names[0]) == 0) {
                    already_deleted = 1;
                    /* Запоминаем позицию существующего delete, чтобы вставить
                     * зануление алиасов СРАЗУ ПОСЛЕ него */
                    insert_pos = (int)jj + 1;
                    break;
                }
            }
        }

        if (!already_deleted) {
            /* Вставляем delete перед зануляющими присвоениями, т.е. на insert_pos
             * (зануления уже сдвинуты вперёд вставками выше — вставляем снова
             * на исходную позицию, чтобы порядок был: delete, zero, zero, ...) */
            int del_pos = ins[i].idx + 1; /* оригинальная позиция */
            Node del = make_delete_node(slots[si].names[0]);
            vn_insert_at(v, del_pos, del);
        }

        vaddr_push_free(slots[si].vaddr);
    }
}

/* ═══════════════════════════════════════════════════════════════════
 * Глобальные переменные-индексы (_idx_N)
 *
 * Для каждого arr[i] в AST:
 *   1. Создаём NODE_STATIC_VAR_DEF  _idx_N  с типом переменной i.
 *   2. Заменяем NODE_INDEX так, чтобы перед ним было присвоение
 *      _idx_N = i, а сам индекс использует _idx_N.
 *   3. Вставляем проверку границ: if (_idx_N < 0 || _idx_N >= size) abort()
 *      для массивов с известным размером (NODE_ARRAY_DEF).
 * ═══════════════════════════════════════════════════════════════════ */

/* Таблица глобальных индексных переменных, которые были сгенерированы */
#define MAX_IDX_VARS 256
typedef struct {
    char name[64];      /* _idx_N                              */
    char idx_type[64];  /* тип (int, long, …)                  */
} IdxVarRecord;

static IdxVarRecord g_idx_vars[MAX_IDX_VARS];
static int          g_idx_var_count = 0;

static void idx_var_reset(void) {
    g_idx_var_count = 0;
    g_idx_counter   = 0;
}

/* ─── Таблица известных размеров массивов (скоуп:имя → размер) ──── *
 * Ключ: "<func_name>:<arr_name>" для локальных, "<arr_name>" для    *
 * глобальных (func_name == NULL или "").  Это предотвращает коллизию *
 * одноимённых массивов в разных функциях.                            */
#define MAX_ARR_SIZES 256
typedef struct { char name[96]; char size[32]; } ArrSizeEntry;
static ArrSizeEntry g_arr_sizes[MAX_ARR_SIZES];
static int          g_arr_size_count = 0;

static void arr_size_reset(void) { g_arr_size_count = 0; }

/* scope_func: имя функции-владельца или "" для глобального уровня */
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

/* ─── Первый проход: собираем NODE_ARRAY_DEF размеры ─────────────── */
static void collect_array_sizes_in_scope(vector_node *nodes, const char *scope_func) {
    for (unsigned long long j = 0; j < nodes->size; j++) {
        Node *n = &nodes->data[j];
        if (n->type == NODE_ARRAY_DEF && n->childs && n->childs->size >= 3) {
            const char *nm = n->childs->data[1].str ? n->childs->data[1].str : "";
            const char *sz = n->childs->data[2].str ? n->childs->data[2].str : "";
            if (nm[0] && sz[0]) arr_size_register(scope_func, nm, sz);
        }
        /* Спускаемся в тела функций со сменой скоупа */
        if ((n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF) &&
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

/* ─── Второй проход: заменяем arr[expr] → _idx_N=expr + bounds + arr[_idx_N] ── */
static void inject_idx_vars(vector_node *nodes, int *start_p, int *end_p,
                            vector_node *root_nodes,
                            vector_node *scope_nodes,   /* текущий скоуп функции */
                            const char  *scope_func)    /* имя функции или ""   */
{
    int j = *start_p;
    while (j < *end_p) {
        Node *n = &nodes->data[j];

        /* Рекурсия в блоки */
        if (n->type == NODE_SCOPE && n->childs) {
            int s = 0, e = (int)n->childs->size;
            inject_idx_vars(n->childs, &s, &e, root_nodes, scope_nodes, scope_func);
            j++; continue;
        }
        if ((n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF) &&
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

        /* Ищем NODE_INDEX внутри текущего оператора */
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

                /* Только если индекс — переменная (не константа) */
                if ((idx_nd->type == NODE_VAR || idx_nd->type == NODE_IDENT) &&
                    idx_nd->str && idx_nd->str[0]) {

                    printf("Error: dynamic indexation in autodel");
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

/* ─── Эмит глобальных NODE_STATIC_VAR_DEF для _idx_N переменных ──── */
static void emit_idx_global_defs(vector_node *nodes) {
    /* Вставляем в начало вектора верхнего уровня */
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
 * Точка входа
 * ═══════════════════════════════════════════════════════════════════ */
void pregen(vector_node *nodes) {
    autodel_counter      = 0;
    ownership_func_count = 0;
    vaddr_pool_init();
    gaa_reset();
    func_index_reset();
    idx_var_reset();
    arr_size_reset();

    /* Собираем размеры массивов до любых трансформаций */
    collect_array_sizes(nodes);

    /* Инжектируем _idx_N переменные и проверки границ */
    {
        int s = 0, e = (int)nodes->size;
        inject_idx_vars(nodes, &s, &e, nodes, nodes, "");
    }

    /* Эмитим глобальные определения _idx_N в начало модуля */
    emit_idx_global_defs(nodes);

    pregen_inline_new(nodes, 0, (int)nodes->size);

    int prev = -1;
    while (prev != ownership_func_count) {
        prev = ownership_func_count;
        collect_ownership(nodes, 0, (int)nodes->size);
        patch_ownership_vardefs(nodes, 0, (int)nodes->size);
    }

    for (int j = 0; j < (int)nodes->size; j++) {
        Node *n = &nodes->data[j];
        if ((n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF) &&
            n->childs->size >= 4) {
            Node *scope       = &n->childs->data[3];
            const char *fname = n->childs->data[1].str ? n->childs->data[1].str : "";
            Node *params      = (n->childs->size >= 3 && n->childs->data[2].type == NODE_PARAMS) ? &n->childs->data[2] : NULL;
            if (scope->type == NODE_SCOPE)
                pregen_scope(scope->childs, 0, (int)scope->childs->size, fname, params);
        } else if (n->type == NODE_STRUCT_DEF) {
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if ((child->type == NODE_FUNC_DEF || child->type == NODE_STATIC_FUNC_DEF) && child->childs->size >= 4) {
                    Node *scope       = &child->childs->data[3];
                    const char *fname = child->childs->data[1].str ? child->childs->data[1].str : "";
                    Node *params      = (child->childs->size >= 3 && child->childs->data[2].type == NODE_PARAMS) ? &child->childs->data[2] : NULL;
                    if (scope->type == NODE_SCOPE)
                        pregen_scope(scope->childs, 0, (int)scope->childs->size, fname, params);
                }
            }
        }
    }

    /* Дамп индекса функций */
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