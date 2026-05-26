#include "fl_Transform.h"
#include "utils/vector_node.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define T_MAX_BLOCKS   2048
#define T_MAX_SUCCS      4
#define T_MAX_PREDS     16
#define T_MAX_VARS     512
#define T_MAX_STMTS   8192
#define T_MAX_VER      256
#define T_STACK_DEPTH  128

typedef struct {
    vector_node *vec;
    int          start;
    int          end;
    int succ[T_MAX_SUCCS];
    int succ_cnt;
    int pred[T_MAX_PREDS];
    int pred_cnt;
    int idom;
    int phi_vars[T_MAX_VARS];
    int phi_cnt;
} TBlock;

typedef struct {
    TBlock blocks[T_MAX_BLOCKS];
    int    cnt;
    int    entry;
    int    exit_b;
} TCFG;

typedef struct {
    char name[64];
    char type_str[80];
    
    int  def_blocks[T_MAX_BLOCKS];
    int  def_cnt;
    
    int  stack[T_STACK_DEPTH];
    int  stack_top;
    int  counter;
} TVarEntry;

static TVarEntry t_vars[T_MAX_VARS];
static int       t_var_cnt;

static void var_reset(void) {
    t_var_cnt = 0;
}


static int var_intern(const char *name) {
    
    char base[64];
    const char *hash = strchr(name, '#');
    if (hash) {
        int len = (int)(hash - name);
        if (len > 63) len = 63;
        memcpy(base, name, len);
        base[len] = '\0';
    } else {
        strncpy(base, name, 63);
        base[63] = '\0';
    }

    for (int i = 0; i < t_var_cnt; i++)
        if (strcmp(t_vars[i].name, base) == 0)
            return i;

    if (t_var_cnt >= T_MAX_VARS) {
        error("fl_Transform: too many variables in function\n");
        
    }
    int idx = t_var_cnt++;
    strncpy(t_vars[idx].name, base, 63);
    t_vars[idx].name[63]    = '\0';
    t_vars[idx].type_str[0] = '\0';
    t_vars[idx].def_cnt     = 0;
    t_vars[idx].stack_top   = 0;
    t_vars[idx].counter     = 0;
    return idx;
}

static void var_add_def(int vi, int block_id) {
    TVarEntry *e = &t_vars[vi];
    for (int i = 0; i < e->def_cnt; i++)
        if (e->def_blocks[i] == block_id) return;
    if (e->def_cnt < T_MAX_BLOCKS)
        e->def_blocks[e->def_cnt++] = block_id;
}

static TCFG g_cfg;

static int cfg_new_block(void) {
    if (g_cfg.cnt >= T_MAX_BLOCKS) {
        error("fl_Transform: CFG block limit exceeded\n");
        
    }
    int id = g_cfg.cnt++;
    TBlock *b  = &g_cfg.blocks[id];
    b->vec      = NULL;
    b->start    = 0;
    b->end      = -1;
    b->succ_cnt = 0;
    b->pred_cnt = 0;
    b->idom     = -1;
    b->phi_cnt  = 0;
    return id;
}

static void cfg_edge(int from, int to) {
    if (from < 0 || to < 0) return;
    TBlock *f = &g_cfg.blocks[from];
    for (int i = 0; i < f->succ_cnt; i++) if (f->succ[i] == to) return;
    if (f->succ_cnt < T_MAX_SUCCS) f->succ[f->succ_cnt++] = to;

    TBlock *t = &g_cfg.blocks[to];
    for (int i = 0; i < t->pred_cnt; i++) if (t->pred[i] == from) return;
    if (t->pred_cnt < T_MAX_PREDS) t->pred[t->pred_cnt++] = from;
}

static int cfg_build_vec(vector_node *vec, int start, int end, int exit_id);

static int cfg_build_vec(vector_node *vec, int start, int end, int exit_id) {
    int cur = cfg_new_block();
    g_cfg.blocks[cur].vec   = vec;
    g_cfg.blocks[cur].start = start;
    g_cfg.blocks[cur].end   = start - 1;

    for (int j = start; j < end; j++) {
        Node *n = &vec->data[j];

        
        if (n->type == NODE_IF && n->childs) {
            g_cfg.blocks[cur].end = j - 1;

            
            int cond = cfg_new_block();
            g_cfg.blocks[cond].vec   = vec;
            g_cfg.blocks[cond].start = j;
            g_cfg.blocks[cond].end   = j;
            cfg_edge(cur, cond);

            
            int merge = cfg_new_block();
            g_cfg.blocks[merge].vec   = vec;
            g_cfg.blocks[merge].start = j + 1;
            g_cfg.blocks[merge].end   = j;

            int has_else = 0;
            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *ch = &n->childs->data[k];
                if (ch->type == NODE_SCOPE && ch->childs) {
                    int branch_exit = cfg_build_vec(
                        ch->childs, 0, (int)ch->childs->size, merge);
                    cfg_edge(cond, (g_cfg.cnt > cond + 1) ? cond + 1 : merge);
                    (void)branch_exit;
                } else if (ch->type == NODE_ELSE && ch->childs) {
                    has_else = 1;
                    for (unsigned long long m = 0; m < ch->childs->size; m++) {
                        Node *ec = &ch->childs->data[m];
                        if (ec->type == NODE_SCOPE && ec->childs)
                            cfg_build_vec(ec->childs, 0,
                                          (int)ec->childs->size, merge);
                    }
                }
            }
            if (!has_else) cfg_edge(cond, merge);

            cur = merge;
            g_cfg.blocks[cur].start = j + 1;
            continue;
        }

        
        if ((n->type == NODE_WHILE || n->type == NODE_FOR ||
             n->type == NODE_DO_WHILE) && n->childs) {
            g_cfg.blocks[cur].end = j - 1;

            int hdr = cfg_new_block();
            g_cfg.blocks[hdr].vec   = vec;
            g_cfg.blocks[hdr].start = j;
            g_cfg.blocks[hdr].end   = j;

            int after = cfg_new_block();
            g_cfg.blocks[after].vec   = vec;
            g_cfg.blocks[after].start = j + 1;
            g_cfg.blocks[after].end   = j;

            cfg_edge(cur, hdr);

            for (unsigned long long k = 0; k < n->childs->size; k++) {
                Node *ch = &n->childs->data[k];
                if (ch->type == NODE_SCOPE && ch->childs)
                    cfg_build_vec(ch->childs, 0,
                                  (int)ch->childs->size, hdr);
            }
            cfg_edge(hdr, after);

            cur = after;
            g_cfg.blocks[cur].start = j + 1;
            continue;
        }

        
        g_cfg.blocks[cur].end = j;
        if (n->type == NODE_RETURN) {
            cfg_edge(cur, exit_id);
            
            int dead = cfg_new_block();
            g_cfg.blocks[dead].vec   = vec;
            g_cfg.blocks[dead].start = j + 1;
            g_cfg.blocks[dead].end   = j;
            cur = dead;
        }
    }

    cfg_edge(cur, exit_id);
    return cur;
}

static void cfg_build(vector_node *scope_vec) {
    g_cfg.cnt   = 0;
    g_cfg.entry = cfg_new_block();
    g_cfg.exit_b= cfg_new_block();

    int first = cfg_build_vec(scope_vec, 0, (int)scope_vec->size, g_cfg.exit_b);
    cfg_edge(g_cfg.entry, first);
}

static int rpo[T_MAX_BLOCKS];
static int rpo_idx[T_MAX_BLOCKS];
static int rpo_cnt;

static void rpo_dfs(int id, uint8_t *visited, int *post_cnt) {
    if (visited[id]) return;
    visited[id] = 1;
    TBlock *b = &g_cfg.blocks[id];
    for (int i = 0; i < b->succ_cnt; i++)
        rpo_dfs(b->succ[i], visited, post_cnt);
    rpo[*post_cnt] = id;
    (*post_cnt)++;
}

static void compute_rpo(void) {
    uint8_t visited[T_MAX_BLOCKS] = {0};
    int post_cnt = 0;
    rpo_dfs(g_cfg.entry, visited, &post_cnt);
    
    for (int i = 0; i < post_cnt / 2; i++) {
        int tmp = rpo[i];
        rpo[i] = rpo[post_cnt - 1 - i];
        rpo[post_cnt - 1 - i] = tmp;
    }
    rpo_cnt = post_cnt;
    for (int i = 0; i < rpo_cnt; i++)
        rpo_idx[rpo[i]] = i;
}

static int dom_intersect(int b1, int b2) {
    
    while (b1 != b2) {
        while (rpo_idx[b1] > rpo_idx[b2]) b1 = g_cfg.blocks[b1].idom;
        while (rpo_idx[b2] > rpo_idx[b1]) b2 = g_cfg.blocks[b2].idom;
    }
    return b1;
}

static void compute_dominators(void) {
    int n = g_cfg.cnt;
    for (int i = 0; i < n; i++)
        g_cfg.blocks[i].idom = -1;

    g_cfg.blocks[g_cfg.entry].idom = g_cfg.entry;

    int changed = 1;
    while (changed) {
        changed = 0;
        
        for (int ri = 1; ri < rpo_cnt; ri++) {
            int b = rpo[ri];
            TBlock *blk = &g_cfg.blocks[b];

            int new_idom = -1;
            for (int pi = 0; pi < blk->pred_cnt; pi++) {
                int p = blk->pred[pi];
                if (g_cfg.blocks[p].idom == -1) continue;
                if (new_idom == -1)
                    new_idom = p;
                else
                    new_idom = dom_intersect(p, new_idom);
            }
            if (new_idom == -1) new_idom = g_cfg.entry;

            if (blk->idom != new_idom) {
                blk->idom = new_idom;
                changed   = 1;
            }
        }
    }
}

#define DF_WORDS  ((T_MAX_BLOCKS + 63) / 64)
static uint64_t df[T_MAX_BLOCKS][DF_WORDS];

static void df_set(int b, int frontier) {
    df[b][frontier >> 6] |= (uint64_t)1 << (frontier & 63);
}
static int df_test(int b, int frontier) {
    return (df[b][frontier >> 6] >> (frontier & 63)) & 1;
}

static void compute_df(void) {
    memset(df, 0, sizeof(df));
    int n = g_cfg.cnt;
    for (int b = 0; b < n; b++) {
        TBlock *blk = &g_cfg.blocks[b];
        if (blk->pred_cnt < 2) continue;
        for (int pi = 0; pi < blk->pred_cnt; pi++) {
            int runner = blk->pred[pi];
            while (runner != blk->idom && runner != -1) {
                df_set(runner, b);
                runner = g_cfg.blocks[runner].idom;
            }
        }
    }
}

static void collect_defs_in_node(Node *n, int block_id) {
    if (!n) return;

    if (n->type == NODE_VAR_DEF && n->childs && n->childs->size >= 2) {
        const char *nm = n->childs->data[1].str;
        if (nm && nm[0]) {
            int vi = var_intern(nm);
            var_add_def(vi, block_id);
            
            if (n->childs->data[0].str && t_vars[vi].type_str[0] == '\0') {
                const char *ts = n->childs->data[0].str;
                
                if (strncmp(ts, "autodel:", 8) == 0) ts += 8;
                strncpy(t_vars[vi].type_str, ts, 79);
                t_vars[vi].type_str[79] = '\0';
            }
        }
    } else if (n->type == NODE_ASSIGN && n->childs && n->childs->size >= 1) {
        Node *lhs = &n->childs->data[0];
        if ((lhs->type == NODE_VAR || lhs->type == NODE_IDENT) && lhs->str) {
            int vi = var_intern(lhs->str);
            var_add_def(vi, block_id);
        }
    }
}

static void collect_defs_in_block(int bid) {
    TBlock *b = &g_cfg.blocks[bid];
    if (!b->vec || b->start > b->end) return;
    for (int j = b->start; j <= b->end && j < (int)b->vec->size; j++)
        collect_defs_in_node(&b->vec->data[j], bid);
}

static uint8_t phi_placed[T_MAX_VARS][T_MAX_BLOCKS];
static uint8_t in_worklist[T_MAX_BLOCKS];

static Node *make_phi_node(const char *var_name, int pred_cnt, Token begin) {
    Node *phi     = malloc(sizeof(Node));
    phi->type     = NODE_PHI;
    phi->str      = malloc(strlen(var_name) + 1);
    strcpy(phi->str, var_name);
    phi->result_type = NULL;
    phi->begin    = begin;
    phi->childs   = malloc(sizeof(vector_node));
    vn_init(phi->childs, pred_cnt > 0 ? pred_cnt : 2);

    for (int i = 0; i < pred_cnt; i++) {
        Node op;
        op.type   = NODE_VAR;
        op.str    = malloc(strlen(var_name) + 1);
        strcpy(op.str, var_name);
        op.childs = NULL;
        op.result_type = NULL;
        op.begin  = begin;
        vn_push_back(phi->childs, op);
    }
    return phi;
}

static void insert_phi_for_var(int vi) {
    TVarEntry *e = &t_vars[vi];

    
    int worklist[T_MAX_BLOCKS];
    int wl_top = 0;
    memset(in_worklist, 0, sizeof(in_worklist));

    for (int i = 0; i < e->def_cnt; i++) {
        int b = e->def_blocks[i];
        if (!in_worklist[b]) {
            in_worklist[b] = 1;
            worklist[wl_top++] = b;
        }
    }

    while (wl_top > 0) {
        int b = worklist[--wl_top];
        for (int y = 0; y < g_cfg.cnt; y++) {
            if (!df_test(b, y)) continue;
            if (phi_placed[vi][y]) continue;

            phi_placed[vi][y] = 1;
            
            TBlock *yb = &g_cfg.blocks[y];
            if (yb->phi_cnt < T_MAX_VARS)
                yb->phi_vars[yb->phi_cnt++] = vi;

            if (yb->vec) {
                Token phi_begin;
                if (yb->start < (int)yb->vec->size)
                    phi_begin = yb->vec->data[yb->start].begin;
                else
                    memset(&phi_begin, 0, sizeof(phi_begin));
                Node *phi_n = make_phi_node(e->name, yb->pred_cnt, phi_begin);
                vn_push_back(yb->vec, *phi_n);
                free(phi_n);
                int ins = (int)yb->vec->size - 1;
                while (ins > yb->start) {
                    Node tmp = yb->vec->data[ins];
                    yb->vec->data[ins]     = yb->vec->data[ins - 1];
                    yb->vec->data[ins - 1] = tmp;
                    ins--;
                }
                for (int bi = 0; bi < g_cfg.cnt; bi++) {
                    TBlock *ob = &g_cfg.blocks[bi];
                    if (ob->vec != yb->vec) continue;
                    if (bi == y) {
                        ob->end++;
                        continue;
                    }
                    if (ob->start >= yb->start) {
                        ob->start++;
                        ob->end++;
                    }
                }
                
                var_add_def(vi, y);
            }

            if (!in_worklist[y]) {
                in_worklist[y] = 1;
                if (wl_top < T_MAX_BLOCKS)
                    worklist[wl_top++] = y;
            }
        }
    }
}

static void insert_all_phis(void) {
    memset(phi_placed, 0, sizeof(phi_placed));
    for (int vi = 0; vi < t_var_cnt; vi++)
        if (t_vars[vi].def_cnt > 0)
            insert_phi_for_var(vi);
}

static char *make_ver_name(int vi, int ver) {
    char buf[128];
    snprintf(buf, sizeof(buf), "%s#%d", t_vars[vi].name, ver);
    char *s = malloc(strlen(buf) + 1);
    strcpy(s, buf);
    return s;
}

static int stack_push(int vi) {
    TVarEntry *e = &t_vars[vi];
    int ver = e->counter++;
    if (e->stack_top >= T_STACK_DEPTH) {
        error("fl_Transform: rename stack overflow for '%s'\n", e->name);
        
    }
    e->stack[e->stack_top++] = ver;
    return ver;
}

static void stack_pop(int vi) {
    if (t_vars[vi].stack_top > 0)
        t_vars[vi].stack_top--;
}

static int stack_top(int vi) {
    TVarEntry *e = &t_vars[vi];
    if (e->stack_top == 0) return 0;
    return e->stack[e->stack_top - 1];
}


static void rename_str(char **str_ptr) {
    if (!str_ptr || !*str_ptr) return;
    const char *s = *str_ptr;
    if (!s[0]) return;

    
    char base[64];
    const char *hash = strchr(s, '#');
    if (hash) {
        int len = (int)(hash - s);
        if (len > 63) len = 63;
        memcpy(base, s, len);
        base[len] = '\0';
    } else {
        strncpy(base, s, 63);
        base[63] = '\0';
    }

    
    int vi = -1;
    for (int i = 0; i < t_var_cnt; i++)
        if (strcmp(t_vars[i].name, base) == 0) { vi = i; break; }
    if (vi < 0) return;

    
    char *new_s = make_ver_name(vi, stack_top(vi));
    free(*str_ptr);
    *str_ptr = new_s;
}


static void rename_uses_in_node(Node *n) {
    if (!n) return;
    if (n->type == NODE_VAR || n->type == NODE_IDENT ||
        n->type == NODE_ADDR) {
        rename_str(&n->str);
    }
    if (n->childs)
        for (unsigned long long i = 0; i < n->childs->size; i++)
            rename_uses_in_node(&n->childs->data[i]);
}


static int dom_ch[T_MAX_BLOCKS][T_MAX_BLOCKS];
static int dom_ch_cnt[T_MAX_BLOCKS];

static void build_dom_children(void) {
    memset(dom_ch_cnt, 0, sizeof(dom_ch_cnt));
    for (int b = 0; b < g_cfg.cnt; b++) {
        if (b == g_cfg.entry) continue;
        int idom = g_cfg.blocks[b].idom;
        if (idom >= 0 && idom != b)
            dom_ch[idom][dom_ch_cnt[idom]++] = b;
    }
}


static int pred_slot(int b, int pred) {
    TBlock *blk = &g_cfg.blocks[b];
    for (int i = 0; i < blk->pred_cnt; i++)
        if (blk->pred[i] == pred) return i;
    return 0;
}

static void rename_block(int bid) {
    TBlock *b     = &g_cfg.blocks[bid];
    int     pushed = 0;

    
    if (b->vec && b->start <= b->end) {
        
        for (int j = b->start; j <= b->end && j < (int)b->vec->size; j++) {
            Node *n = &b->vec->data[j];
            if (n->type != NODE_PHI) break;
            
            int vi = -1;
            for (int i = 0; i < t_var_cnt; i++)
                if (strcmp(t_vars[i].name, n->str) == 0) { vi = i; break; }
            if (vi < 0) continue;
            int ver = stack_push(vi);
            pushed++;
            char *ver_name = make_ver_name(vi, ver);

            n->type = NODE_VAR_DEF;

            
            Node type_node;
            type_node.type   = NODE_TYPE;
            type_node.childs = malloc(sizeof(vector_node));
            vn_init(type_node.childs, 1);
            type_node.result_type = NULL;
            const char *ts = (vi >= 0 && t_vars[vi].type_str[0])
                             ? t_vars[vi].type_str : "int";
            type_node.str = malloc(strlen(ts) + 1);
            strcpy(type_node.str, ts);

            
            Node ident_node;
            ident_node.type   = NODE_IDENT;
            ident_node.childs = malloc(sizeof(vector_node));
            vn_init(ident_node.childs, 1);
            ident_node.result_type = NULL;
            ident_node.str = ver_name;

            Node *phi_expr = malloc(sizeof(Node));
            phi_expr->type        = NODE_PHI;
            phi_expr->str         = malloc(strlen(t_vars[vi].name) + 1);
            strcpy(phi_expr->str, t_vars[vi].name);
            phi_expr->childs      = n->childs;
            phi_expr->result_type = NULL;

            
            n->childs = malloc(sizeof(vector_node));
            vn_init(n->childs, 3);
            vn_push_back(n->childs, type_node);
            vn_push_back(n->childs, ident_node);
            vn_push_back(n->childs, *phi_expr);
            free(phi_expr);

            
            free(n->str);
            n->str = malloc(1); n->str[0] = '\0';
        }
    }

    
    if (b->vec && b->start <= b->end) {
        for (int j = b->start; j <= b->end && j < (int)b->vec->size; j++) {
            Node *n = &b->vec->data[j];
            if (n->type == NODE_PHI) continue;

            if (n->type == NODE_VAR_DEF && n->childs && n->childs->size >= 2) {
                
                if (n->childs->size >= 3)
                    rename_uses_in_node(&n->childs->data[2]);
                
                const char *nm = n->childs->data[1].str;
                if (nm && nm[0]) {
                    int vi = -1;
                    for (int i = 0; i < t_var_cnt; i++)
                        if (strcmp(t_vars[i].name, nm) == 0) { vi = i; break; }
                    if (vi >= 0) {
                        int ver = stack_push(vi);
                        pushed++;
                        free(n->childs->data[1].str);
                        n->childs->data[1].str = make_ver_name(vi, ver);
                    }
                }
            } else if (n->type == NODE_ASSIGN && n->childs && n->childs->size >= 3) {
                
                rename_uses_in_node(&n->childs->data[2]);
                Node *lhs = &n->childs->data[0];

                if ((lhs->type == NODE_VAR || lhs->type == NODE_IDENT) && lhs->str) {
                    int vi = -1;
                    for (int i = 0; i < t_var_cnt; i++)
                        if (strcmp(t_vars[i].name, lhs->str) == 0) { vi = i; break; }

                    if (vi >= 0) {
                        int ver = stack_push(vi);
                        pushed++;

                        
                        char *ver_name = make_ver_name(vi, ver);

                        
                        Node rhs = n->childs->data[2];

                        
                        Node type_node;
                        type_node.type   = NODE_TYPE;
                        type_node.childs = malloc(sizeof(vector_node));
                        vn_init(type_node.childs, 1);
                        type_node.result_type = NULL;
                        const char *ts = t_vars[vi].type_str[0]
                                         ? t_vars[vi].type_str : "int";
                        type_node.str = malloc(strlen(ts) + 1);
                        strcpy(type_node.str, ts);

                        
                        Node ident_node;
                        ident_node.type   = NODE_IDENT;
                        ident_node.childs = malloc(sizeof(vector_node));
                        vn_init(ident_node.childs, 1);
                        ident_node.result_type = NULL;
                        ident_node.str = ver_name;

                        free(n->childs->data[0].str);
                        free(n->childs->data[1].str);
                        
                        vn_free(n->childs);
                        free(n->childs);

                        n->childs = malloc(sizeof(vector_node));
                        vn_init(n->childs, 3);
                        vn_push_back(n->childs, type_node);
                        vn_push_back(n->childs, ident_node);
                        vn_push_back(n->childs, rhs);

                        
                        free(n->str);
                        n->str = malloc(1); n->str[0] = '\0';

                        
                        n->type = NODE_VAR_DEF;
                    } else {
                        
                        rename_str(&lhs->str);
                    }
                } else {
                    rename_uses_in_node(lhs);
                }
            } else {
                
                rename_uses_in_node(n);
            }
        }
    }

    
    for (int si = 0; si < b->succ_cnt; si++) {
        int s = b->succ[si];
        int slot = pred_slot(s, bid);
        TBlock *sb = &g_cfg.blocks[s];
        if (!sb->vec) continue;

        
        for (int j = sb->start; j <= sb->end && j < (int)sb->vec->size; j++) {
            Node *phi_n = &sb->vec->data[j];
            if (phi_n->type != NODE_PHI) break;
            
            const char *phi_base = phi_n->str;
            
            char base[64];
            const char *hash = strchr(phi_base, '#');
            int blen = hash ? (int)(hash - phi_base) : (int)strlen(phi_base);
            if (blen > 63) blen = 63;
            memcpy(base, phi_base, blen);
            base[blen] = '\0';

            int vi = -1;
            for (int i = 0; i < t_var_cnt; i++)
                if (strcmp(t_vars[i].name, base) == 0) { vi = i; break; }
            if (vi < 0) continue;

            
            if (phi_n->childs && slot < (int)phi_n->childs->size) {
                free(phi_n->childs->data[slot].str);
                phi_n->childs->data[slot].str = make_ver_name(vi, stack_top(vi));
            }
        }
    }

    
    for (int ci = 0; ci < dom_ch_cnt[bid]; ci++)
        rename_block(dom_ch[bid][ci]);

    
    
    if (b->vec && b->start <= b->end) {
        for (int j = b->start; j <= b->end && j < (int)b->vec->size && pushed > 0; j++) {
            Node *n = &b->vec->data[j];
            
            if (n->type != NODE_VAR_DEF) break;
            if (!n->childs || n->childs->size < 3) break;
            if (n->childs->data[2].type != NODE_PHI) break;
            const char *vname = n->childs->data[1].str;
            if (!vname) break;
            char base[64];
            const char *hash = strchr(vname, '#');
            int blen = hash ? (int)(hash - vname) : (int)strlen(vname);
            if (blen > 63) blen = 63;
            memcpy(base, vname, blen);
            base[blen] = '\0';
            for (int i = 0; i < t_var_cnt; i++)
                if (strcmp(t_vars[i].name, base) == 0) { stack_pop(i); pushed--; break; }
        }
        for (int j = b->start; j <= b->end && j < (int)b->vec->size && pushed > 0; j++) {
            Node *n = &b->vec->data[j];
            if (n->type != NODE_VAR_DEF) continue;
            if (!n->childs || n->childs->size < 2) continue;
            
            if (n->childs->size >= 3 && n->childs->data[2].type == NODE_PHI) continue;
            const char *nm = n->childs->data[1].str;
            if (!nm) continue;
            char base[64];
            const char *hash = strchr(nm, '#');
            int blen = hash ? (int)(hash - nm) : (int)strlen(nm);
            if (blen > 63) blen = 63;
            memcpy(base, nm, blen);
            base[blen] = '\0';
            for (int i = 0; i < t_var_cnt; i++)
                if (strcmp(t_vars[i].name, base) == 0) { stack_pop(i); pushed--; break; }
        }
    }
    (void)pushed;
}

static void transform_function(vector_node *scope_vec, Node *params_node) {
    
    var_reset();
    memset(&g_cfg, 0, sizeof(g_cfg));
    memset(dom_ch_cnt, 0, sizeof(dom_ch_cnt));

    
    if (params_node && params_node->type == NODE_PARAMS && params_node->childs) {
        for (unsigned long long pi = 0; pi < params_node->childs->size; pi++) {
            Node *p = &params_node->childs->data[pi];
            if (p->type != NODE_VAR_DEF || !p->childs || p->childs->size < 2) continue;
            const char *nm = p->childs->data[1].str;
            if (nm && nm[0]) {
                int vi = var_intern(nm);
                var_add_def(vi, 0);
            }
        }
    }

    
    cfg_build(scope_vec);

    
    for (int b = 0; b < g_cfg.cnt; b++)
        collect_defs_in_block(b);

    
    if (t_var_cnt == 0) return;

    
    compute_rpo();
    compute_dominators();
    compute_df();

    
    insert_all_phis();

    
    build_dom_children();

    
    for (int vi = 0; vi < t_var_cnt; vi++) {
        t_vars[vi].stack_top = 0;
        t_vars[vi].counter   = 0;
    }

    rename_block(g_cfg.entry);
}

void ssa_transform(vector_node *nodes) {
    if (!nodes) return;
    for (unsigned long long i = 0; i < nodes->size; i++) {
        Node *n = &nodes->data[i];

        int is_func = (n->type == NODE_FUNC_DEF        ||
                       n->type == NODE_STATIC_FUNC_DEF  ||
                       n->type == NODE_ASYNC_FUNC_DEF   ||
                       n->type == NODE_AWAIT_FUNC_DEF);
        if (is_func && n->childs && n->childs->size >= 4) {
            Node *scope  = &n->childs->data[3];
            Node *params = (n->childs->size >= 3 &&
                            n->childs->data[2].type == NODE_PARAMS)
                           ? &n->childs->data[2] : NULL;
            if (scope->type == NODE_SCOPE && scope->childs)
                transform_function(scope->childs, params);
            continue;
        }

        
        if (n->type == NODE_SCOPE && n->childs)
            ssa_transform(n->childs);
    }
}

void ssa_base_name(const char *versioned, char *buf, int buf_len) {
    if (!versioned || !buf || buf_len <= 0) return;
    const char *hash = strchr(versioned, '#');
    if (!hash) {
        strncpy(buf, versioned, buf_len - 1);
        buf[buf_len - 1] = '\0';
    } else {
        int len = (int)(hash - versioned);
        if (len >= buf_len) len = buf_len - 1;
        memcpy(buf, versioned, len);
        buf[len] = '\0';
    }
}