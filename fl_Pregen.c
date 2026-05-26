#include "fl_Pregen.h"
#include "fl_Parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

extern OverloadEntry* global_modules_overloads[];
extern int global_modules_counts[];
extern int num_imported_modules;

#define MAX_AUTODEL         64
#define MAX_OWNERSHIP_FUNCS 64
#define MAX_VADDR_VARS      64
#define MAX_VADDR_POOL      64
#define MAX_SLOT_VARS       32
#define MAX_SLOT_FIELDS     16
#define MAX_SLOT_INDICES    16

static size_t max_auotdel = MAX_AUTODEL;
static size_t max_ownership_funcs = MAX_OWNERSHIP_FUNCS;

static int g_idx_counter = 0;

static int  autodel_counter     = 0;
static size_t  ownership_func_count = 0;
static char (*ownership_funcs)[64];

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

static Node make_delete_node(const char *name, Token begin) {
    Node del;
    del.childs = malloc(sizeof(vector_node));
    vn_init(del.childs, 2);
    del.str  = NULL;
    del.type = NODE_DELETE;
    del.begin = begin;
    Node *args = make_node(NODE_ARGS, "");
    vn_push_back(del.childs, *args); free(args);
    Node *var  = make_node(NODE_VAR, name);
    var->begin = begin;
    vn_push_back(del.childs, *var);  free(var);
    return del;
}

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

static void ownership_func_push(const char *name) {
    for (size_t i = 0; i < ownership_func_count; i++)
        if (strcmp(ownership_funcs[i], name) == 0) return;
    if (ownership_func_count >= max_ownership_funcs) {
        max_ownership_funcs *= 2;
        char (*re_ownership_funcs)[64] = realloc(ownership_funcs, max_ownership_funcs * 64);
        if (!re_ownership_funcs) {
            error("Error: buffer overflow -> out of memory\n");
            
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
                    error("Error: buffer overflow -> out of memory\n");
                    
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
        free(autodel_names);
        return;
    }
    collect_returns_in_scope(scope->childs, autodel_names, autodel_count, fname);
    free(autodel_names);
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

typedef struct { char var[64]; }                        SlotVar;
typedef struct { char obj[64]; char field[64]; int is_arrow; } SlotField;
typedef struct { char arr[64]; char idx[64]; }          SlotIndex;

typedef struct {
    int        vaddr;
    char       base_type[64];
    int        escaped;
    SlotVar    vars[MAX_SLOT_VARS];
    int        var_count;
    SlotField  fields[MAX_SLOT_FIELDS];
    int        field_count;
    SlotIndex  indices[MAX_SLOT_INDICES];
    int        index_count;
} VAddrSlot;



static void slot_add_var(VAddrSlot *s, const char *name) {
    if (!name || !name[0]) return;
    for (int k = 0; k < s->var_count; k++)
        if (strcmp(s->vars[k].var, name) == 0) return;
    if (s->var_count >= MAX_SLOT_VARS) {
        error("Error: slot var overflow\n");
    }
    strncpy(s->vars[s->var_count++].var, name, 63);
}

static void slot_add_field(VAddrSlot *s, const char *obj, const char *field, int is_arrow) {
    if (!obj || !field) return;
    for (int k = 0; k < s->field_count; k++)
        if (strcmp(s->fields[k].obj, obj)     == 0 &&
            strcmp(s->fields[k].field, field) == 0 &&
            s->fields[k].is_arrow == is_arrow) return;
    if (s->field_count >= MAX_SLOT_FIELDS) return;
    strncpy(s->fields[s->field_count].obj,   obj,   63);
    strncpy(s->fields[s->field_count].field, field, 63);
    s->fields[s->field_count].is_arrow = is_arrow;
    s->field_count++;
}

static void slot_add_index(VAddrSlot *s, const char *arr, const char *idx) {
    if (!arr || !idx) return;
    for (int k = 0; k < s->index_count; k++)
        if (strcmp(s->indices[k].arr, arr) == 0 &&
            strcmp(s->indices[k].idx, idx) == 0) return;
    if (s->index_count >= MAX_SLOT_INDICES) return;
    strncpy(s->indices[s->index_count].arr, arr, 63);
    strncpy(s->indices[s->index_count].idx, idx, 63);
    s->index_count++;
}


static int find_slot_by_var(VAddrSlot *slots, int count, const char *name) {
    if (!name) return -1;
    for (int i = 0; i < count; i++)
        for (int k = 0; k < slots[i].var_count; k++)
            if (strcmp(slots[i].vars[k].var, name) == 0) return i;
    return -1;
}


static int find_slot_by_field(VAddrSlot *slots, int count,
                               const char *obj, const char *field, int is_arrow) {
    if (!obj || !field) return -1;
    for (int i = 0; i < count; i++)
        for (int k = 0; k < slots[i].field_count; k++)
            if (strcmp(slots[i].fields[k].obj, obj)     == 0 &&
                strcmp(slots[i].fields[k].field, field) == 0 &&
                slots[i].fields[k].is_arrow == is_arrow) return i;
    return -1;
}


static int find_slot_by_index(VAddrSlot *slots, int count,
                               const char *arr, const char *idx) {
    if (!arr || !idx) return -1;
    for (int i = 0; i < count; i++)
        for (int k = 0; k < slots[i].index_count; k++)
            if (strcmp(slots[i].indices[k].arr, arr) == 0 &&
                strcmp(slots[i].indices[k].idx, idx) == 0) return i;
    return -1;
}


static int slot_used_in_node(Node *n, VAddrSlot *s) {
    if (!n) return 0;

    
    if (n->type == NODE_VAR || n->type == NODE_IDENT || n->type == NODE_ADDR) {
        for (int k = 0; k < s->var_count; k++)
            if (n->str && strcmp(n->str, s->vars[k].var) == 0) return 1;
    }

    
    if (n->type == NODE_MEMBER_ARROW || n->type == NODE_MEMBER_DOT) {
        int is_arrow = (n->type == NODE_MEMBER_ARROW);
        for (int k = 0; k < s->field_count; k++) {
            if (s->fields[k].is_arrow != is_arrow) continue;
            if (!n->str || strcmp(n->str, s->fields[k].field) != 0) continue;
            if (!n->childs || n->childs->size < 1) continue;
            if (n->childs->data[0].str &&
                strcmp(n->childs->data[0].str, s->fields[k].obj) == 0) return 1;
        }
    }

    
    if (n->type == NODE_INDEX || n->type == NODE_INDEX_ASSIGN ||
        n->type == NODE_ADDR_INDEX) {
        if (n->childs && n->childs->size >= 2) {
            for (int k = 0; k < s->index_count; k++) {
                if (n->childs->data[0].str &&
                    strcmp(n->childs->data[0].str, s->indices[k].arr) == 0 &&
                    n->childs->data[1].str &&
                    strcmp(n->childs->data[1].str, s->indices[k].idx) == 0) return 1;
            }
        }
    }

    
    if (n->childs)
        for (unsigned long long ci = 0; ci < n->childs->size; ci++)
            if (slot_used_in_node(&n->childs->data[ci], s)) return 1;
    return 0;
}


static int find_slot_for_node(Node *n, VAddrSlot *slots, int count) {
    if (!n) return -1;
    
    if (n->type == NODE_VAR || n->type == NODE_IDENT)
        return find_slot_by_var(slots, count, n->str);
    
    if ((n->type == NODE_MEMBER_ARROW || n->type == NODE_MEMBER_DOT) &&
        n->childs && n->childs->size >= 1 && n->childs->data[0].str)
        return find_slot_by_field(slots, count,
                                   n->childs->data[0].str, n->str,
                                   n->type == NODE_MEMBER_ARROW);
    
    if (n->type == NODE_INDEX && n->childs && n->childs->size >= 2 &&
        n->childs->data[0].str && n->childs->data[1].str)
        return find_slot_by_index(slots, count,
                                   n->childs->data[0].str,
                                   n->childs->data[1].str);
    return -1;
}

typedef struct {
    int slot_idx;
    vector_node *vec;
    int idx;
} Insertion;

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
        error("Error: buffer overflow -> out of memory\n");
        
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
    
    if (cfg->count >= CFG_MAX_BLOCKS) {
        error("Error: CFG limit exceeded! Function is too complex. Increase CFG_MAX_BLOCKS.\n");
        
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
                    const char *nm = n->childs->data[1].str ? n->childs->data[1].str : "";
                    for (int k = 0; k < slots[si].var_count; k++) {
                        if (strcmp(slots[si].vars[k].var, nm) == 0) {
                            bs_set(b->def, si);
                            break;
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

static int dfs_reach_exit_without_return(CFGBlock *b, uint8_t *visited, CFGBlock *exit_node, CFG *cfg) {
    if (b == exit_node) return 1;
    int id = b - cfg->blocks;
    if (visited[id]) return 0;
    visited[id] = 1;

    if (b->vec && b->start <= b->end) {
        Node *last_node = &b->vec->data[b->end];
        if (last_node->type == NODE_RETURN) return 0;
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
        error("\n[Compile Error]: Control reaches end of non-void function '%s'\n", func_name);
        
    }
}


typedef struct {
    vector_node *vec;
    int idx;
    char name[64];
    int insert_after;
    Token begin;
} SGCInsertion;

static SGCInsertion sgc_ins[1024];
static int sgc_ins_count = 0;

static void sgc_add_ins(vector_node *vec, int idx, const char *name, int after, Token begin) {
    if (sgc_ins_count >= 1024) return;
    sgc_ins[sgc_ins_count].vec = vec;
    sgc_ins[sgc_ins_count].idx = idx;
    strncpy(sgc_ins[sgc_ins_count].name, name, 63);
    sgc_ins[sgc_ins_count].insert_after = after;
    sgc_ins[sgc_ins_count].begin = begin;
    sgc_ins_count++;
}

static const char *slot_live_name(VAddrSlot *s, vector_node *vec,
                                   int block_start, int block_end) {
    for (int k = block_end; k >= block_start; k--) {
        if (k >= (int)vec->size) continue;
        for (int vi = s->var_count - 1; vi >= 0; vi--) {
            Node *stk[128]; int top = 0;
            stk[top++] = &vec->data[k];
            while (top > 0) {
                Node *cur = stk[--top];
                if (!cur) continue;
                if ((cur->type == NODE_VAR || cur->type == NODE_IDENT) &&
                    cur->str && strcmp(cur->str, s->vars[vi].var) == 0)
                    return s->vars[vi].var;
                if (cur->childs)
                    for (unsigned long long ci = 0;
                         ci < cur->childs->size && top < 127; ci++)
                        stk[top++] = &cur->childs->data[ci];
            }
        }
    }
    return s->var_count > 0 ? s->vars[s->var_count - 1].var : "";
}


static void sgc_apply_deletes(CFG *cfg, VAddrSlot *slots, int slot_count) {
    sgc_ins_count = 0;

    for (int i = 0; i < cfg->count; i++) {
        CFGBlock *b = &cfg->blocks[i];
        if (!b->vec || b->start > b->end) continue;

        uint64_t current_live[CFG_SLOT_WORDS];
        for (int w = 0; w < CFG_SLOT_WORDS; w++) current_live[w] = b->live_in[w];

        for (int j = b->start; j <= b->end && j < (int)b->vec->size; j++) {
            Node *n = &b->vec->data[j];

            
            for (int si = 0; si < slot_count; si++)
                if (slot_used_in_node(n, &slots[si])) bs_set(current_live, si);
            
            if (n->type == NODE_RETURN) {
                int ret_si = -1;
                if (n->childs && n->childs->size > 0)
                    ret_si = find_slot_for_node(&n->childs->data[0], slots, slot_count);
                for (int si = 0; si < slot_count; si++) {
                    if (si == ret_si) continue;
                    if (bs_test(current_live, si) && !slots[si].escaped)
                        sgc_add_ins(b->vec, j,
                                    slot_live_name(&slots[si], b->vec, b->start, j), 0,
                                    n->begin);
                }
                break;
            }
        }

        
        for (int si = 0; si < slot_count; si++) {
            if (bs_test(current_live, si) && !bs_test(b->live_out, si) && !slots[si].escaped) {
                
                int last_j = b->start;
                for (int k = b->end; k >= b->start; k--) {
                    if (k < (int)b->vec->size && slot_used_in_node(&b->vec->data[k], &slots[si])) {
                        last_j = k; break;
                    }
                }
                Token ref_begin = (last_j < (int)b->vec->size)
                                  ? b->vec->data[last_j].begin
                                  : b->vec->data[b->start].begin;
                sgc_add_ins(b->vec, last_j,
                            slot_live_name(&slots[si], b->vec, b->start, last_j), 1,
                            ref_begin);
            }
        }
    }
    for (int i = 0; i < sgc_ins_count - 1; i++) {
        for (int k = i + 1; k < sgc_ins_count; k++) {
            int swap = 0;
            if (sgc_ins[i].vec < sgc_ins[k].vec) {
                swap = 1;
            } else if (sgc_ins[i].vec == sgc_ins[k].vec) {
                if (sgc_ins[i].idx < sgc_ins[k].idx) {
                    swap = 1;
                } else if (sgc_ins[i].idx == sgc_ins[k].idx &&
                           sgc_ins[i].insert_after < sgc_ins[k].insert_after) {
                    
                    swap = 1;
                }
            }
            if (swap) {
                SGCInsertion tmp = sgc_ins[i]; sgc_ins[i] = sgc_ins[k]; sgc_ins[k] = tmp;
            }
        }
    }
    for (int i = 0; i < sgc_ins_count; i++) {
        Node del = make_delete_node(sgc_ins[i].name, sgc_ins[i].begin);
        vn_insert_at(sgc_ins[i].vec, sgc_ins[i].idx + sgc_ins[i].insert_after, del);
    }
}

static void collect_assign_aliases(Node *n, VAddrSlot *slots, int slot_count) {
    if (!n) return;
    if (n->type == NODE_MEMBER_ASSIGN && n->childs && n->childs->size >= 3) {
        Node *member = &n->childs->data[0];
        Node *rhs    = &n->childs->data[2];
        int is_arrow = (member->type == NODE_MEMBER_ARROW);
        int is_dot   = (member->type == NODE_MEMBER_DOT);
        if ((is_arrow || is_dot) && member->childs && member->childs->size >= 1 &&
            member->str && member->childs->data[0].str) {
            int si = -1;
            if (rhs->type == NODE_VAR || rhs->type == NODE_IDENT)
                si = find_slot_by_var(slots, slot_count, rhs->str);
            if (si >= 0)
                slot_add_field(&slots[si], member->childs->data[0].str,
                               member->str, is_arrow);
        }
    }
    if ((n->type == NODE_INDEX_ASSIGN || n->type == NODE_MEMBER_INDEX_ASSIGN) &&
        n->childs && n->childs->size >= 3) {
        Node *lhs = &n->childs->data[0];
        Node *rhs = &n->childs->data[n->childs->size - 1];
        int si = -1;
        if (rhs->type == NODE_VAR || rhs->type == NODE_IDENT)
            si = find_slot_by_var(slots, slot_count, rhs->str);
        if (si >= 0 && lhs->type == NODE_INDEX &&
            lhs->childs && lhs->childs->size >= 2 &&
            lhs->childs->data[0].str && lhs->childs->data[1].str) {
            slot_add_index(&slots[si], lhs->childs->data[0].str,
                           lhs->childs->data[1].str);
        }
    }
    
    if (n->childs &&
        n->type != NODE_FUNC_DEF && n->type != NODE_STATIC_FUNC_DEF &&
        n->type != NODE_ASYNC_FUNC_DEF && n->type != NODE_AWAIT_FUNC_DEF)
        for (unsigned long long ci = 0; ci < n->childs->size; ci++)
            collect_assign_aliases(&n->childs->data[ci], slots, slot_count);
}

static void collect_slots_in_scope(vector_node *nodes, int start, int end,
                                    VAddrSlot *slots, int *slot_count) {
    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];

        if (n->type == NODE_SCOPE && n->childs) {
            collect_slots_in_scope(n->childs, 0, (int)n->childs->size,
                                   slots, slot_count);
            continue;
        }
        if ((n->type == NODE_IF || n->type == NODE_WHILE ||
             n->type == NODE_FOR || n->type == NODE_DO_WHILE) && n->childs) {
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *child = &n->childs->data[k];
                if (child->type == NODE_SCOPE && child->childs)
                    collect_slots_in_scope(child->childs, 0, (int)child->childs->size,
                                           slots, slot_count);
                else if (child->type == NODE_ELSE && child->childs)
                    for (unsigned long long m = 0; m < child->childs->size; m++) {
                        Node *ec = &child->childs->data[m];
                        if (ec->type == NODE_SCOPE && ec->childs)
                            collect_slots_in_scope(ec->childs, 0, (int)ec->childs->size,
                                                   slots, slot_count);
                    }
            }
            continue;
        }
        
        if (n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF ||
            n->type == NODE_ASYNC_FUNC_DEF || n->type == NODE_AWAIT_FUNC_DEF) continue;

        if (n->type != NODE_VAR_DEF || !n->childs || n->childs->size < 2) continue;

        const char *type_str = n->childs->data[0].str ? n->childs->data[0].str : "";
        const char *varname  = n->childs->data[1].str ? n->childs->data[1].str : "";
        if (!varname[0]) continue;

        if (strncmp(type_str, "notdel:", 7) == 0) continue; // notdel — не наш, не трогаем
        int is_autodel = (strncmp(type_str, "autodel:", 8) == 0);
        Node *init = (n->childs->size >= 3) ? &n->childs->data[2] : NULL;

        
        if (init && (init->type == NODE_VAR || init->type == NODE_IDENT) && init->str) {
            int si = find_slot_by_var(slots, *slot_count, init->str);
            if (si >= 0) {
                slot_add_var(&slots[si], varname);
                continue;
            }
        }

        if (init && init->type == NODE_DEREF &&
            init->childs && init->childs->size >= 1) {
            Node *inner = &init->childs->data[0];
            if ((inner->type == NODE_VAR || inner->type == NODE_IDENT) && inner->str) {
                int si = find_slot_by_var(slots, *slot_count, inner->str);
                if (si >= 0) {
                    slot_add_var(&slots[si], varname);
                    continue;
                }
            }
        }

        
        if (init && (init->type == NODE_MEMBER_ARROW || init->type == NODE_MEMBER_DOT) &&
            init->childs && init->childs->size >= 1 &&
            init->childs->data[0].str && init->str) {
            int si = find_slot_by_field(slots, *slot_count,
                                         init->childs->data[0].str, init->str,
                                         init->type == NODE_MEMBER_ARROW);
            if (si >= 0) {
                slot_add_var(&slots[si], varname);
                continue;
            }
        }

        
        if (init && init->type == NODE_INDEX &&
            init->childs && init->childs->size >= 2 &&
            init->childs->data[0].str && init->childs->data[1].str) {
            int si = find_slot_by_index(slots, *slot_count,
                                         init->childs->data[0].str,
                                         init->childs->data[1].str);
            if (si >= 0) {
                slot_add_var(&slots[si], varname);
                continue;
            }
        }

        
        if (init && init->type == NODE_PHI && init->childs) {
            int found_si = -1;
            for (unsigned long long pi = 0; pi < init->childs->size && found_si < 0; pi++) {
                Node *op = &init->childs->data[pi];
                if ((op->type == NODE_VAR || op->type == NODE_IDENT) && op->str)
                    found_si = find_slot_by_var(slots, *slot_count, op->str);
            }
            if (found_si >= 0) {
                slot_add_var(&slots[found_si], varname);
                continue;
            }
        }

        
        if (is_autodel ||
            (init && (init->type == NODE_NEW ||
                      (init->type == NODE_FUNC_CALL && init->str &&
                       ownership_func_lookup(init->str))))) {
            if (*slot_count >= MAX_VADDR_VARS) {
                error("Error: too many autodel slots\n");
            }
            VAddrSlot *s  = &slots[(*slot_count)++];
            s->vaddr      = vaddr_pop();
            s->escaped    = 0;
            s->var_count  = 0;
            s->field_count  = 0;
            s->index_count  = 0;
            
            const char *bt = is_autodel ? type_str + 8 : type_str;
            strncpy(s->base_type, bt, 63); s->base_type[63] = '\0';
            int blen = (int)strlen(s->base_type);
            if (blen > 0 && s->base_type[blen - 1] == '*') s->base_type[blen - 1] = '\0';
            slot_add_var(s, varname);
            continue;
        }
    }

    for (int j = start; j < end; j++)
        collect_assign_aliases(&nodes->data[j], slots, *slot_count);
}


static void mark_escaped_call(Node *n, VAddrSlot *slots, int slot_count) {
    if (!n || !n->str || !n->str[0] || !n->childs) return;

    OverloadEntry *ext = NULL;
    for (int m = 0; m < num_imported_modules && !ext; m++) {
        OverloadEntry *mod = global_modules_overloads[m];
        int cnt = global_modules_counts[m];
        for (int ei = 0; ei < cnt; ei++)
            if (strcmp(mod[ei].mangled, n->str) == 0) { ext = &mod[ei]; break; }
    }

    int param_idx = 0;
    for (unsigned long long ci = 0; ci < n->childs->size; ci++) {
        Node *arg = &n->childs->data[ci];
        if (arg->type == NODE_ARGS) continue;

        int si = find_slot_for_node(arg, slots, slot_count);
        if (si >= 0) {
            char lt = 'a';
            if (ext && param_idx < ext->param_count)
                lt = ext->param_lifetimes[param_idx];
            else if (ext)
                lt = 'c';

            if (lt == 'b' || lt == 'c')
                slots[si].escaped = 1;

            if ((lt == 'b' || lt == 'c') && ext &&
                (arg->type == NODE_VAR || arg->type == NODE_IDENT) && arg->str) {
                
            }
        }
        param_idx++;
    }
}


static void mark_escaped_node(Node *n, VAddrSlot *slots, int slot_count) {
    if (!n) return;
    
    if (n->type == NODE_FUNC_DEF    || n->type == NODE_STATIC_FUNC_DEF ||
        n->type == NODE_ASYNC_FUNC_DEF || n->type == NODE_AWAIT_FUNC_DEF) return;

    if (n->type == NODE_FUNC_CALL)
        mark_escaped_call(n, slots, slot_count);

    if (n->childs)
        for (unsigned long long ci = 0; ci < n->childs->size; ci++)
            mark_escaped_node(&n->childs->data[ci], slots, slot_count);
}

typedef struct {
    Node       *call_node;
    const char *arg_name;
    const char *callee;
    int         param_idx;
    char        lt;
} EscapedCall;

#define MAX_ESCAPED_CALLS 256
static EscapedCall g_escaped_calls[MAX_ESCAPED_CALLS];
static int         g_escaped_call_count = 0;


static void collect_escaped_calls_r(Node *n) {
    if (!n) return;
    if (n->type == NODE_FUNC_DEF    || n->type == NODE_STATIC_FUNC_DEF ||
        n->type == NODE_ASYNC_FUNC_DEF || n->type == NODE_AWAIT_FUNC_DEF) return;

    if (n->type == NODE_FUNC_CALL && n->str && n->str[0] && n->childs) {
        OverloadEntry *ext = NULL;
        for (int m = 0; m < num_imported_modules && !ext; m++) {
            OverloadEntry *mod = global_modules_overloads[m];
            int cnt = global_modules_counts[m];
            for (int ei2 = 0; ei2 < cnt; ei2++)
                if (strcmp(mod[ei2].mangled, n->str) == 0) { ext = &mod[ei2]; break; }
        }
        if (ext) {
            int pidx = 0;
            for (unsigned long long ci = 0; ci < n->childs->size; ci++) {
                Node *arg = &n->childs->data[ci];
                if (arg->type == NODE_ARGS) continue;
                if (pidx < ext->param_count) {
                    char lt = ext->param_lifetimes[pidx];
                    if ((lt == 'b' || lt == 'c') &&
                        (arg->type == NODE_VAR || arg->type == NODE_IDENT) &&
                        arg->str && g_escaped_call_count < MAX_ESCAPED_CALLS) {
                        EscapedCall *ec = &g_escaped_calls[g_escaped_call_count++];
                        ec->call_node  = n;
                        ec->arg_name   = arg->str;
                        ec->callee     = ext->base_name;
                        ec->param_idx  = pidx;
                        ec->lt         = lt;
                    }
                }
                pidx++;
            }
        }
    }

    if (n->childs)
        for (unsigned long long ci = 0; ci < n->childs->size; ci++)
            collect_escaped_calls_r(&n->childs->data[ci]);
}

static void check_use_r(Node *n, Node *call_node, const char *arg_name,
                         const char *callee, int param_idx, char lt,
                         int *past_call) {
    if (!n) return;
    if (n->type == NODE_FUNC_DEF    || n->type == NODE_STATIC_FUNC_DEF ||
        n->type == NODE_ASYNC_FUNC_DEF || n->type == NODE_AWAIT_FUNC_DEF) return;

    
    if (n == call_node) { *past_call = 1; return; }

    if (*past_call) {
        
        if ((n->type == NODE_VAR || n->type == NODE_IDENT) &&
            n->str && strcmp(n->str, arg_name) == 0) {
            fprintf(stderr,
                "pregen ERROR: pointer '%s' passed to '%s' (param %d) "
                "with lifetime '%c' but used after call.\n",
                arg_name, callee, param_idx, lt);
            
        }
    }

    if (n->childs)
        for (unsigned long long ci = 0; ci < n->childs->size; ci++)
            check_use_r(&n->childs->data[ci], call_node, arg_name,
                         callee, param_idx, lt, past_call);
}

static void check_use_after_escaped_call(vector_node *nodes, int start, int end,
                                          VAddrSlot *slots, int slot_count) {
    (void)slots; (void)slot_count;
    g_escaped_call_count = 0;

    for (int j = start; j < end; j++)
        collect_escaped_calls_r(&nodes->data[j]);

    for (int ei = 0; ei < g_escaped_call_count; ei++) {
        EscapedCall *ec = &g_escaped_calls[ei];
        int past_call = 0;
        for (int j = start; j < end; j++)
            check_use_r(&nodes->data[j], ec->call_node, ec->arg_name,
                         ec->callee, ec->param_idx, ec->lt, &past_call);
    }
}


static void mark_escaped(vector_node *nodes, int start, int end,
                          VAddrSlot *slots, int slot_count) {
    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];
        
        if (n->type == NODE_FUNC_DEF    || n->type == NODE_STATIC_FUNC_DEF ||
            n->type == NODE_ASYNC_FUNC_DEF || n->type == NODE_AWAIT_FUNC_DEF) continue;
        mark_escaped_node(n, slots, slot_count);
    }
    check_use_after_escaped_call(nodes, start, end, slots, slot_count);
}

static void pregen_scope(vector_node *nodes, int start, int end,
                         const char *func_name,
                         int insert_deletes, int is_void) {

    for (int j = start; j < end; j++) {
        Node *n = &nodes->data[j];
        if (n->type != NODE_FUNC_DEF      && n->type != NODE_STATIC_FUNC_DEF &&
            n->type != NODE_ASYNC_FUNC_DEF && n->type != NODE_AWAIT_FUNC_DEF) continue;
        if (!n->childs || n->childs->size < 4) continue;
        Node *scope       = &n->childs->data[3];
        const char *fn    = n->childs->data[1].str ? n->childs->data[1].str : "";
        int nested_void   = (n->childs->data[0].str &&
                             strstr(n->childs->data[0].str, "void") != NULL);
        if (scope->type == NODE_SCOPE)
            pregen_scope(scope->childs, 0, (int)scope->childs->size,
                         fn, insert_deletes, nested_void);
    }

    
    VAddrSlot slots[MAX_VADDR_VARS];
    int slot_count = 0;
    collect_slots_in_scope(nodes, start, end, slots, &slot_count);
    if (slot_count == 0) return;

    
    mark_escaped(nodes, start, end, slots, slot_count);

    if (!insert_deletes) return;

    
    CFG cfg;
    cfg_build(&cfg, nodes);
    cfg_fill_use_def(&cfg, slots, slot_count);
    cfg_liveness(&cfg);
    check_return_paths(&cfg, is_void, func_name);
    sgc_apply_deletes(&cfg, slots, slot_count);

    for (int si = 0; si < slot_count; si++)
        vaddr_push_free(slots[si].vaddr);
}


#define MAX_IDX_VARS 256
typedef struct {
    char name[64];
    char idx_type[64];
} IdxVarRecord;

#define MAX_NOTDEL_VARS 128
static char g_notdel_vars[MAX_NOTDEL_VARS][64];
static int  g_notdel_count = 0;

static void notdel_reset(void) { g_notdel_count = 0; }

static void notdel_add(const char *name) {
    if (!name || !name[0] || g_notdel_count >= MAX_NOTDEL_VARS) return;
    for (int i = 0; i < g_notdel_count; i++)
        if (strcmp(g_notdel_vars[i], name) == 0) return;
    strncpy(g_notdel_vars[g_notdel_count++], name, 63);
}

static int notdel_check(const char *name) {
    if (!name) return 0;
    for (int i = 0; i < g_notdel_count; i++)
        if (strcmp(g_notdel_vars[i], name) == 0) return 1;
    return 0;
}

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
                notdel_reset();
                if (n->childs->size >= 3 && n->childs->data[2].type == NODE_PARAMS) {
                    Node *params = &n->childs->data[2];
                    for (unsigned long long pi = 0; pi < params->childs->size; pi++) {
                        Node *param = &params->childs->data[pi];
                        if (!param->childs || param->childs->size < 2) continue;
                        const char *ptype = param->childs->data[0].str ? param->childs->data[0].str : "";
                        const char *pname = param->childs->data[1].str ? param->childs->data[1].str : "";
                        if (strncmp(ptype, "notdel:", 7) == 0)
                            notdel_add(pname);
                    }
                }
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
                Node *arr_nd  = &cur->childs->data[0];
                Node *idx_nd  = &cur->childs->data[1];

                if ((idx_nd->type == NODE_VAR || idx_nd->type == NODE_IDENT) &&
                    idx_nd->str && idx_nd->str[0] &&
                    arr_nd->str && !notdel_check(arr_nd->str)) {

                    error("Error in %s:\n\tat [line: %i; col: %i] => dynamic indexation in autodel (%s)\n",
                           idx_nd->begin.file, idx_nd->begin.line, idx_nd->begin.col, arr_nd->str);
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

        
        if (n->type == NODE_ASYNC_FUNC_DEF || n->type == NODE_AWAIT_FUNC_DEF) {
            
            CoroutineContext saved_coro = current_coro;

            current_coro.is_active = 1;
            current_coro.count = 0;

            
            if (n->childs->size >= 3 && n->childs->data[2].type == NODE_PARAMS) {
                Node *params = &n->childs->data[2];
                for (size_t p = 0; p < params->childs->size; p++) {
                    Node *param = &params->childs->data[p];
                    if (param->childs && param->childs->size >= 2 && param->childs->data[1].str) {
                        coro_add_allowed_var(param->childs->data[1].str);
                    }
                }
            }

            
            if (n->childs->size >= 4 && n->childs->data[3].type == NODE_SCOPE) {
                validate_coroutine_memory(n->childs->data[3].childs);
            }

            
            current_coro = saved_coro;
            continue;
        }

        if (current_coro.is_active) {
            
            if (n->type == NODE_VAR_DEF || n->type == NODE_CHANNEL_VAR_DEF) {
                if (n->childs && n->childs->size >= 2 && n->childs->data[1].str) {
                    coro_add_allowed_var(n->childs->data[1].str);
                }
            }

            
            if (n->type == NODE_ASSIGN || n->type == NODE_MEMBER_ASSIGN ||
                n->type == NODE_INDEX_ASSIGN || n->type == NODE_PTR_ASSIGN ||
                n->type == NODE_MEMBER_INDEX_ASSIGN) {
                
                if (n->childs && n->childs->size > 0) {
                    Node *lhs = &n->childs->data[0];
                    const char* base_name = get_base_ident(lhs);
                    
                    if (base_name && !coro_is_var_allowed(base_name)) {
                        error("\n[Compile Error]: Memory safety violation in coroutine.\n"
                                        "Attempt to write to '%s'. Coroutines can only write to their own arguments, "
                                        "local variables, or variables explicitly marked as 'channel'.\n", base_name);
                        
                    }
                }
            }
        }

        
        if (n->childs) {
            validate_coroutine_memory(n->childs);
        }
    }
}

void pregen(vector_node *nodes) {
    autodel_counter      = 0;
    ownership_func_count = 0;
    ownership_funcs = malloc(max_ownership_funcs * 64);
    vaddr_pool_init();
    idx_var_reset();
    arr_size_reset();
    notdel_reset();

    validate_coroutine_memory(nodes);
    collect_array_sizes(nodes);

    {
        int s = 0, e = (int)nodes->size;
        inject_idx_vars(nodes, &s, &e, nodes, nodes, "");
    }

    emit_idx_global_defs(nodes);
    pregen_inline_new(nodes, 0, (int)nodes->size);

    
    size_t prev = (size_t)-1;
    while (prev != ownership_func_count) {
        prev = ownership_func_count;
        collect_ownership(nodes, 0, (int)nodes->size);
        patch_ownership_vardefs(nodes, 0, (int)nodes->size);
    }

    
    for (int j = 0; j < (int)nodes->size; j++) {
        Node *n = &nodes->data[j];
        if ((n->type == NODE_FUNC_DEF || n->type == NODE_STATIC_FUNC_DEF ||
             n->type == NODE_ASYNC_FUNC_DEF || n->type == NODE_AWAIT_FUNC_DEF) &&
            n->childs->size >= 4) {
            Node *scope       = &n->childs->data[3];
            const char *fname = n->childs->data[1].str ? n->childs->data[1].str : "";
            if (scope->type == NODE_SCOPE) {
                int is_void = (n->childs->data[0].str &&
                               strstr(n->childs->data[0].str, "void") != NULL);
                pregen_scope(scope->childs, 0, (int)scope->childs->size,
                             fname, 1, is_void);
            }
        }
        
    }
}