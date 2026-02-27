#include "fl_Parser.h"
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_SYMS 256
#define MAX_TYPES 64

static int get_field_index(LLVMTypeRef struct_type, const char *field_name);

typedef struct {
    char        name[64];
    LLVMTypeRef type;
} TypeEntry;

typedef struct {
    char        name[64];
    LLVMTypeRef type;
    char        field_names[64][64];
    LLVMTypeRef field_types[64];
    int         field_count;
} StructInfo;

static StructInfo struct_info[MAX_TYPES];
static int struct_info_count = 0;

static StructInfo* find_struct_info_by_type(LLVMTypeRef type) {
    for (int i = 0; i < struct_info_count; i++) {
        if (struct_info[i].type == type)
            return &struct_info[i];
    }
    return NULL;
}

static TypeEntry type_table[MAX_TYPES];
static int       type_count = 0;

static void type_push(const char *name, LLVMTypeRef type) {
    if (type_count >= MAX_TYPES) {
        fprintf(stderr, "codegen error: type table overflow\n");
        return;
    }
    strncpy(type_table[type_count].name, name, 63);
    type_table[type_count].type = type;
    type_count++;
}

static LLVMTypeRef type_lookup(const char *name) {
    for (int j = type_count - 1; j >= 0; j--)
        if (strcmp(type_table[j].name, name) == 0)
            return type_table[j].type;
    return NULL;
}

typedef struct {
    char         name[64];
    LLVMValueRef value;
    LLVMTypeRef  type;
    LLVMTypeRef  elem_type;
    int          is_func;
} Symbol;

static Symbol sym_table[MAX_SYMS];
static int    sym_count = 0;

static void sym_push(const char *name, LLVMValueRef val, LLVMTypeRef type, int is_func) {
    if (sym_count >= MAX_SYMS) {
        fprintf(stderr, "codegen error: symbol table overflow\n");
        return;
    }
    strncpy(sym_table[sym_count].name, name, 63);
    sym_table[sym_count].value   = val;
    sym_table[sym_count].type    = type;
    sym_table[sym_count].is_func = is_func;
    sym_count++;
}

static Symbol *sym_lookup(const char *name) {
    for (int j = sym_count - 1; j >= 0; j--)
        if (strcmp(sym_table[j].name, name) == 0)
            return &sym_table[j];
    return NULL;
}

static int sym_checkpoint(void) { return sym_count; }
static void sym_restore(int cp) { sym_count = cp; }

static LLVMContextRef ctx;
static LLVMModuleRef  mod;
static LLVMBuilderRef builder;

static LLVMTypeRef llvm_type_from_str(const char *s) {
    int len = strlen(s);
    if (len > 1 && s[len - 1] == '*') {
        char base[64];
        strncpy(base, s, len - 1);
        base[len - 1] = '\0';
        return LLVMPointerTypeInContext(ctx, 0);
    }

    if (strcmp(s, "int")    == 0) return LLVMInt32TypeInContext(ctx);
    if (strcmp(s, "short")  == 0) return LLVMInt16TypeInContext(ctx);
    if (strcmp(s, "long")   == 0) return LLVMInt64TypeInContext(ctx);
    if (strcmp(s, "char")   == 0) return LLVMInt8TypeInContext(ctx);
    if (strcmp(s, "float")  == 0) return LLVMFloatTypeInContext(ctx);
    if (strcmp(s, "double") == 0) return LLVMDoubleTypeInContext(ctx);
    if (strcmp(s, "void")   == 0) return LLVMVoidTypeInContext(ctx);

    LLVMTypeRef custom = type_lookup(s);
    if (custom) return custom;

    fprintf(stderr, "codegen error: unknown type '%s'\n", s);
    return LLVMInt32TypeInContext(ctx);
}

static int type_is_float(LLVMTypeRef t) {
    return t == LLVMFloatTypeInContext(ctx) ||
           t == LLVMDoubleTypeInContext(ctx);
}

static LLVMValueRef codegen_node(Node *n);

static LLVMValueRef codegen_var(Node *n) {
    Symbol *s = sym_lookup(n->str);
    if (!s) {
        fprintf(stderr, "codegen error: undefined variable '%s'\n", n->str);
        return NULL;
    }
    return LLVMBuildLoad2(builder, s->type, s->value, n->str);
}

static LLVMValueRef codegen_binop(Node *n) {
    if (n->childs->size < 2) return NULL;

    LLVMValueRef left  = codegen_node(&n->childs->data[0]);
    LLVMValueRef right = codegen_node(&n->childs->data[1]);
    if (!left || !right) return NULL;

    const char *op = n->str;

    int left_is_ptr  = LLVMGetTypeKind(LLVMTypeOf(left))  == LLVMPointerTypeKind;
    int right_is_ptr = LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMPointerTypeKind;

    if (left_is_ptr && !right_is_ptr) {
        if (strcmp(op, "+") == 0) {
            return LLVMBuildGEP2(builder,
                LLVMInt8TypeInContext(ctx),
                left, &right, 1, "ptраdd");
        }
        if (strcmp(op, "-") == 0) {
            LLVMValueRef neg = LLVMBuildNeg(builder, right, "neg");
            return LLVMBuildGEP2(builder,
                LLVMInt8TypeInContext(ctx),
                left, &neg, 1, "ptrsub");
        }
    }

    if (left_is_ptr && right_is_ptr && strcmp(op, "-") == 0) {
        return LLVMBuildPtrDiff2(builder,
            LLVMInt8TypeInContext(ctx),
            left, right, "ptrdiff");
    }

    int is_fp = type_is_float(LLVMTypeOf(left)) ||
                type_is_float(LLVMTypeOf(right));;

    if (strcmp(op, "+") == 0)
        return is_fp ? LLVMBuildFAdd(builder, left, right, "fadd")
                     : LLVMBuildAdd (builder, left, right, "add");
    if (strcmp(op, "-") == 0)
        return is_fp ? LLVMBuildFSub(builder, left, right, "fsub")
                     : LLVMBuildSub (builder, left, right, "sub");
    if (strcmp(op, "*") == 0)
        return is_fp ? LLVMBuildFMul(builder, left, right, "fmul")
                     : LLVMBuildMul (builder, left, right, "mul");
    if (strcmp(op, "/") == 0)
        return is_fp ? LLVMBuildFDiv(builder, left, right, "fdiv")
                     : LLVMBuildSDiv(builder, left, right, "div");
    if (strcmp(op, "%") == 0)
        return LLVMBuildSRem(builder, left, right, "rem");

    if (left_is_ptr && right_is_ptr) {
        LLVMValueRef cmp = NULL;
        if (strcmp(op, "==") == 0)
            cmp = LLVMBuildICmp(builder, LLVMIntEQ,  left, right, "eq");
        else if (strcmp(op, "!=") == 0)
            cmp = LLVMBuildICmp(builder, LLVMIntNE,  left, right, "ne");
        else if (strcmp(op, "<") == 0)
            cmp = LLVMBuildICmp(builder, LLVMIntULT, left, right, "lt");
        else if (strcmp(op, ">") == 0)
            cmp = LLVMBuildICmp(builder, LLVMIntUGT, left, right, "gt");
        if (cmp)
            return LLVMBuildZExt(builder, cmp, LLVMInt32TypeInContext(ctx), "bool");
    }

    LLVMValueRef cmp = NULL;
    if (strcmp(op, "==") == 0)
        cmp = is_fp ? LLVMBuildFCmp(builder, LLVMRealOEQ, left, right, "eq")
                    : LLVMBuildICmp(builder, LLVMIntEQ,   left, right, "eq");
    else if (strcmp(op, "!=") == 0)
        cmp = is_fp ? LLVMBuildFCmp(builder, LLVMRealONE, left, right, "ne")
                    : LLVMBuildICmp(builder, LLVMIntNE,   left, right, "ne");
    else if (strcmp(op, "<") == 0)
        cmp = is_fp ? LLVMBuildFCmp(builder, LLVMRealOLT, left, right, "lt")
                    : LLVMBuildICmp(builder, LLVMIntSLT,  left, right, "lt");
    else if (strcmp(op, ">") == 0)
        cmp = is_fp ? LLVMBuildFCmp(builder, LLVMRealOGT, left, right, "gt")
                    : LLVMBuildICmp(builder, LLVMIntSGT,  left, right, "gt");
    else if (strcmp(op, "<=") == 0)
        cmp = is_fp ? LLVMBuildFCmp(builder, LLVMRealOLE, left, right, "le")
                    : LLVMBuildICmp(builder, LLVMIntSLE,  left, right, "le");
    else if (strcmp(op, ">=") == 0)
        cmp = is_fp ? LLVMBuildFCmp(builder, LLVMRealOGE, left, right, "ge")
                    : LLVMBuildICmp(builder, LLVMIntSGE,  left, right, "ge");
    else if (strcmp(op, "&&") == 0)
        cmp = LLVMBuildAnd(builder, left, right, "and");
    else if (strcmp(op, "||") == 0)
        cmp = LLVMBuildOr(builder, left, right, "or");

    if (cmp)
        return LLVMBuildZExt(builder, cmp, LLVMInt32TypeInContext(ctx), "bool");

    if (strcmp(op, "&") == 0)
        return LLVMBuildAnd(builder, left, right, "band");
    if (strcmp(op, "|") == 0)
        return LLVMBuildOr(builder, left, right, "bor");
    if (strcmp(op, "^") == 0)
        return LLVMBuildXor(builder, left, right, "bxor");
    if (strcmp(op, "<<") == 0)
        return LLVMBuildShl(builder, left, right, "shl");
    if (strcmp(op, ">>") == 0)
        return LLVMBuildAShr(builder, left, right, "shr");

    fprintf(stderr, "codegen error: unknown operator '%s'\n", op);
    return NULL;
}

static LLVMValueRef codegen_unop(Node *n) {
    if (n->childs->size < 1) return NULL;
    LLVMValueRef val = codegen_node(&n->childs->data[0]);
    if (!val) return NULL;

    if (strcmp(n->str, "-") == 0)
        return type_is_float(LLVMTypeOf(val))
               ? LLVMBuildFNeg(builder, val, "fneg")
               : LLVMBuildNeg (builder, val, "neg");

    if (strcmp(n->str, "!") == 0) {
        LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(val), 0, 0);
        LLVMValueRef cmp  = LLVMBuildICmp(builder, LLVMIntEQ, val, zero, "not");
        return LLVMBuildZExt(builder, cmp, LLVMInt32TypeInContext(ctx), "bool");
    }
    if (strcmp(n->str, "~") == 0)
        return LLVMBuildNot(builder, val, "bnot");

    fprintf(stderr, "codegen error: unknown unary op '%s'\n", n->str);
    return NULL;
}

static LLVMValueRef codegen_index(Node *n) {
    if (n->childs->size < 2) return NULL;
    Node *arr_node = &n->childs->data[0];
    Node *idx_node = &n->childs->data[1];

    Symbol *s = sym_lookup(arr_node->str);
    if (!s) {
        fprintf(stderr, "codegen error: undefined array '%s'\n", arr_node->str);
        return NULL;
    }

    LLVMValueRef idx      = codegen_node(idx_node);
    LLVMValueRef indices[] = { idx };
    LLVMValueRef ptr = LLVMBuildGEP2(builder, s->type, s->value, indices, 1, "gep");
    return LLVMBuildLoad2(builder, LLVMInt32TypeInContext(ctx), ptr, "elem");
}

static LLVMValueRef codegen_ptr_assign(Node *n) {
    if (n->childs->size < 2) return NULL;

    const char *name = n->childs->data[0].str;
    Symbol     *s    = sym_lookup(name);
    if (!s) {
        fprintf(stderr, "codegen error: undefined pointer '%s'\n", name);
        return NULL;
    }

    LLVMValueRef ptr = LLVMBuildLoad2(builder,
        LLVMPointerTypeInContext(ctx, 0), s->value, "ptr");

    LLVMValueRef val = codegen_node(&n->childs->data[1]);
    if (!val) return NULL;

    LLVMBuildStore(builder, val, ptr);
    return val;
}

static LLVMValueRef codegen_array_def(Node *n) {
    if (n->childs->size < 3) return NULL;

    const char  *type_str  = n->childs->data[0].str;
    const char  *name      = n->childs->data[1].str;
    int          size      = atoi(n->childs->data[2].str);
    LLVMTypeRef  elem_type = llvm_type_from_str(type_str);
    LLVMTypeRef  arr_type  = LLVMArrayType(elem_type, (unsigned)size);

    LLVMValueRef ptr = LLVMBuildAlloca(builder, arr_type, name);

    LLVMBuildStore(builder,
        LLVMConstNull(arr_type),
        ptr);

    sym_push(name, ptr, elem_type, 0);

    return ptr;
}

static LLVMValueRef codegen_func_call(Node *n) {
    const char *fname = n->str;
    Symbol     *s     = sym_lookup(fname);

    LLVMValueRef func;
    LLVMTypeRef  ftype;

    if (s && s->is_func) {
        func  = s->value;
        ftype = s->type;
    } else {
        func = LLVMGetNamedFunction(mod, fname);
        if (!func) {
            fprintf(stderr, "codegen error: undefined function '%s'\n", fname);
            return NULL;
        }
        ftype = LLVMGlobalGetValueType(func);
    }

    LLVMValueRef args[64];
    unsigned     argc = 0;

    if (n->childs->size >= 2) {
        Node *args_node = &n->childs->data[1];
        for (unsigned long long j = 0; j < args_node->childs->size && argc < 64; j++) {
            LLVMValueRef arg = codegen_node(&args_node->childs->data[j]);
            if (arg) args[argc++] = arg;
        }
    }

    int returns_void = LLVMGetTypeKind(LLVMGetReturnType(ftype)) == LLVMVoidTypeKind;
    return LLVMBuildCall2(builder, ftype, func, args, argc,
                          returns_void ? "" : "call");
}

static LLVMValueRef codegen_if(Node *n, LLVMValueRef func) {
    if (n->childs->size < 2) return NULL;

    LLVMValueRef cond    = codegen_node(&n->childs->data[0]);
    if (!cond) return NULL;

    LLVMValueRef zero    = LLVMConstInt(LLVMTypeOf(cond), 0, 0);
    LLVMValueRef cond_i1 = LLVMBuildICmp(builder, LLVMIntNE, cond, zero, "ifcond");

    LLVMBasicBlockRef then_bb  = LLVMAppendBasicBlockInContext(ctx, func, "then");
    LLVMBasicBlockRef else_bb  = LLVMAppendBasicBlockInContext(ctx, func, "else");
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx, func, "ifend");

    LLVMBuildCondBr(builder, cond_i1, then_bb, else_bb);

    LLVMPositionBuilderAtEnd(builder, then_bb);
    int cp = sym_checkpoint();
    codegen_node(&n->childs->data[1]);
    sym_restore(cp);
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
        LLVMBuildBr(builder, merge_bb);

    LLVMPositionBuilderAtEnd(builder, else_bb);
    if (n->childs->size >= 3) {
        cp = sym_checkpoint();
        codegen_node(&n->childs->data[2]);
        sym_restore(cp);
    }
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
        LLVMBuildBr(builder, merge_bb);

    LLVMPositionBuilderAtEnd(builder, merge_bb);
    return NULL;
}

static LLVMValueRef codegen_while(Node *n, LLVMValueRef func) {
    if (n->childs->size < 2) return NULL;

    LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx, func, "wcond");
    LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx, func, "wbody");
    LLVMBasicBlockRef end_bb  = LLVMAppendBasicBlockInContext(ctx, func, "wend");

    LLVMBuildBr(builder, cond_bb);

    LLVMPositionBuilderAtEnd(builder, cond_bb);
    LLVMValueRef cond    = codegen_node(&n->childs->data[0]);
    LLVMValueRef zero    = LLVMConstInt(LLVMTypeOf(cond), 0, 0);
    LLVMValueRef cond_i1 = LLVMBuildICmp(builder, LLVMIntNE, cond, zero, "wcond");
    LLVMBuildCondBr(builder, cond_i1, body_bb, end_bb);

    LLVMPositionBuilderAtEnd(builder, body_bb);
    int cp = sym_checkpoint();
    codegen_node(&n->childs->data[1]);
    sym_restore(cp);

    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
        LLVMBuildBr(builder, cond_bb);

    LLVMPositionBuilderAtEnd(builder, end_bb);
    return NULL;
}

static LLVMValueRef codegen_scope(Node *n) {
    int cp = sym_checkpoint();
    for (unsigned long long j = 0; j < n->childs->size; j++)
        codegen_node(&n->childs->data[j]);
    sym_restore(cp);
    return NULL;
}

static LLVMValueRef codegen_return(Node *n) {
    if (n->childs->size == 0 || n->childs->data[0].type == NODE_UNDEF) {
        LLVMBuildRetVoid(builder);
        return NULL;
    }

    LLVMValueRef val = codegen_node(&n->childs->data[0]);
    if (!val) {
        fprintf(stderr, "codegen error: return expression is NULL\n");
        LLVMBuildRetVoid(builder);
        return NULL;
    }

    LLVMBuildRet(builder, val);
    return NULL;
}

static LLVMValueRef codegen_addr(Node *n) {
    Symbol *s = sym_lookup(n->str);
    if (!s) {
        fprintf(stderr, "codegen error: undefined variable '%s'\n", n->str);
        return NULL;
    }
    return s->value;
}

static LLVMValueRef codegen_deref(Node *n) {
    if (n->childs->size < 1) return NULL;

    Node *inner = &n->childs->data[0];
    Symbol *s = sym_lookup(inner->str);

    LLVMValueRef ptr = codegen_node(inner);
    if (!ptr) return NULL;

    LLVMTypeRef load_type = (s && s->elem_type)
        ? s->elem_type
        : LLVMInt32TypeInContext(ctx);

    return LLVMBuildLoad2(builder, load_type, ptr, "deref");
}

static LLVMValueRef codegen_func_def(Node *n) {
    if (n->childs->size < 4) return NULL;

    const char  *ret_str  = n->childs->data[0].str;
    const char  *fname    = n->childs->data[1].str;
    Node        *params   = &n->childs->data[2];
    Node        *scope    = &n->childs->data[3];
    LLVMTypeRef  ret_type = llvm_type_from_str(ret_str);

    LLVMTypeRef param_types[64];
    unsigned    param_count = 0;

    if (params->type == NODE_PARAMS) {
        for (unsigned long long j = 0; j < params->childs->size && param_count < 64; j++) {
            Node *param = &params->childs->data[j];
            if (param->childs->size >= 1)
                param_types[param_count++] = llvm_type_from_str(param->childs->data[0].str);
        }
    }

    LLVMTypeRef  func_type = LLVMFunctionType(ret_type, param_types, param_count, 0);
    LLVMValueRef func      = LLVMAddFunction(mod, fname, func_type);

    sym_push(fname, func, func_type, 1);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx, func, "entry");
    LLVMPositionBuilderAtEnd(builder, entry);

    int cp = sym_checkpoint();
    if (params->type == NODE_PARAMS) {
        for (unsigned long long j = 0; j < params->childs->size && j < param_count; j++) {
            Node        *param     = &params->childs->data[j];
            const char  *ptype_str = param->childs->data[0].str;
            const char  *pname     = param->childs->data[1].str;
            LLVMTypeRef  ptype     = llvm_type_from_str(ptype_str);
            LLVMValueRef ptr       = LLVMBuildAlloca(builder, ptype, pname);
            LLVMBuildStore(builder, LLVMGetParam(func, (unsigned)j), ptr);
            sym_push(pname, ptr, ptype, 0);
        }
    }

    for (unsigned long long j = 0; j < scope->childs->size; j++)
        codegen_node(&scope->childs->data[j]);

    LLVMBasicBlockRef cur_block = LLVMGetInsertBlock(builder);
    if (!LLVMGetBasicBlockTerminator(cur_block)) {
        if (LLVMGetTypeKind(ret_type) == LLVMVoidTypeKind)
            LLVMBuildRetVoid(builder);
        else
            LLVMBuildRet(builder, LLVMConstInt(ret_type, 0, 0));
    }

    sym_restore(cp);
    return func;
}

static LLVMValueRef codegen_assign(Node *n) {
    if (n->childs->size < 3) return NULL;

    const char *name = n->childs->data[0].str;
    Symbol     *s    = sym_lookup(name);
    if (!s) {
        fprintf(stderr, "codegen error: undefined variable '%s'\n", name);
        return NULL;
    }

    LLVMValueRef val = codegen_node(&n->childs->data[2]);
    if (!val) return NULL;

    LLVMBuildStore(builder, val, s->value);
    return val;
}

static LLVMValueRef codegen_index_assign(Node *n) {
    if (n->childs->size < 3) return NULL;

    const char *name = n->childs->data[0].str;
    Symbol     *s    = sym_lookup(name);
    if (!s) {
        fprintf(stderr, "codegen error: undefined array '%s'\n", name);
        return NULL;
    }

    LLVMValueRef idx = codegen_node(&n->childs->data[1]);
    LLVMValueRef val = codegen_node(&n->childs->data[2]);
    if (!idx || !val) return NULL;

    LLVMValueRef indices[] = {
        LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
        idx
    };
    LLVMTypeRef  arr_type = LLVMArrayType(s->type, 0);
    LLVMValueRef ptr      = LLVMBuildGEP2(builder, arr_type, s->value, indices, 2, "gep");
    LLVMBuildStore(builder, val, ptr);
    return val;
}

static LLVMValueRef codegen_struct_def(Node *n) {
    unsigned field_count = (unsigned)n->childs->size;
    LLVMTypeRef field_types[64];
    char field_names[64][64];
    
    for (unsigned j = 0; j < field_count && j < 64; j++) {
        Node *field = &n->childs->data[j];
        if (field->childs->size < 1) {
            fprintf(stderr, "codegen error: struct field missing type\n");
            return NULL;
        }
        
        field_types[j] = llvm_type_from_str(field->childs->data[0].str);
        
        if (field->childs->size >= 2 && field->childs->data[1].type == NODE_IDENT) {
            strncpy(field_names[j], field->childs->data[1].str, 63);
            field_names[j][63] = '\0';
        } else {
            field_names[j][0] = '\0';
        }
    }
    
    LLVMTypeRef struct_type = LLVMStructCreateNamed(ctx, n->str);
    LLVMStructSetBody(struct_type, field_types, field_count, 0);
    
    type_push(n->str, struct_type);
    
    if (struct_info_count < MAX_TYPES) {
        strncpy(struct_info[struct_info_count].name, n->str, 63);
        struct_info[struct_info_count].type = struct_type;
        struct_info[struct_info_count].field_count = field_count;
        
        for (unsigned i = 0; i < field_count; i++) {
            strncpy(struct_info[struct_info_count].field_names[i], field_names[i], 63);
            struct_info[struct_info_count].field_types[i] = field_types[i];
        }
        
        struct_info_count++;
    } else {
        fprintf(stderr, "codegen error: struct info table overflow\n");
    }
    
    return NULL;
}

static LLVMValueRef codegen_var_def(Node *n) {
    if (n->childs->size < 2) return NULL;

    const char *type_str = n->childs->data[0].str;
    const char *name     = n->childs->data[1].str;
    LLVMTypeRef lltype   = llvm_type_from_str(type_str);

    LLVMTypeRef elem_type = lltype;
    int len = strlen(type_str);
    if (len > 1 && type_str[len - 1] == '*') {
        char base[64];
        strncpy(base, type_str, len - 1);
        base[len - 1] = '\0';
        elem_type = llvm_type_from_str(base);
    }

    LLVMBasicBlockRef cur_block = LLVMGetInsertBlock(builder);

    if (cur_block) {
        LLVMValueRef ptr = LLVMBuildAlloca(builder, lltype, name);

        if (sym_count < MAX_SYMS) {
            strncpy(sym_table[sym_count].name, name, 63);
            sym_table[sym_count].value     = ptr;
            sym_table[sym_count].type      = lltype;
            sym_table[sym_count].elem_type = elem_type;
            sym_table[sym_count].is_func   = 0;
            sym_count++;
        }

        if (n->childs->size >= 3 && n->childs->data[2].type != NODE_UNDEF) {
            LLVMValueRef init = codegen_node(&n->childs->data[2]);
            if (init) LLVMBuildStore(builder, init, ptr);
        }
        return ptr;
    } else {
        LLVMValueRef global_var = LLVMAddGlobal(mod, lltype, name);
        LLVMSetInitializer(global_var, LLVMConstNull(lltype));

        if (sym_count < MAX_SYMS) {
            strncpy(sym_table[sym_count].name, name, 63);
            sym_table[sym_count].value     = global_var;
            sym_table[sym_count].type      = lltype;
            sym_table[sym_count].elem_type = elem_type;
            sym_table[sym_count].is_func   = 0;
            sym_count++;
        }
        return global_var;
    }
}

static LLVMValueRef codegen_member_dot(Node *n) {
    if (n->childs->size < 1) return NULL;

    Node *left = &n->childs->data[0];
    Symbol *s  = sym_lookup(left->str);
    if (!s) {
        fprintf(stderr, "codegen error: undefined variable '%s'\n", left->str);
        return NULL;
    }

    LLVMTypeRef struct_type = s->type;
    if (LLVMGetTypeKind(struct_type) != LLVMStructTypeKind) {
        fprintf(stderr, "codegen error: '%s' is not a struct\n", left->str);
        return NULL;
    }

    int field_index = get_field_index(struct_type, n->str);
    if (field_index < 0) return NULL;

    LLVMValueRef indices[] = {
        LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
        LLVMConstInt(LLVMInt32TypeInContext(ctx), field_index, 0)
    };

    LLVMValueRef field_ptr = LLVMBuildGEP2(builder, struct_type, s->value, indices, 2, "gep");
    LLVMTypeRef  field_type = LLVMStructGetTypeAtIndex(struct_type, field_index);
    return LLVMBuildLoad2(builder, field_type, field_ptr, n->str);
}

static LLVMValueRef codegen_member_arrow(Node *n) {
    if (n->childs->size < 1) return NULL;

    Node *left = &n->childs->data[0];
    Symbol *s  = sym_lookup(left->str);
    if (!s) return NULL;

    LLVMValueRef ptr = LLVMBuildLoad2(builder,
        LLVMPointerTypeInContext(ctx, 0), s->value, "ptr");

    LLVMTypeRef struct_type = s->elem_type;
    if (!struct_type || LLVMGetTypeKind(struct_type) != LLVMStructTypeKind) {
        fprintf(stderr, "codegen error: '%s' is not a pointer to struct\n", left->str);
        return NULL;
    }

    int field_index = get_field_index(struct_type, n->str);
    if (field_index < 0) return NULL;

    LLVMValueRef indices[] = {
        LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
        LLVMConstInt(LLVMInt32TypeInContext(ctx), field_index, 0)
    };

    LLVMValueRef field_ptr  = LLVMBuildGEP2(builder, struct_type, ptr, indices, 2, "gep");
    LLVMTypeRef  field_type = LLVMStructGetTypeAtIndex(struct_type, field_index);
    return LLVMBuildLoad2(builder, field_type, field_ptr, n->str);
}

static LLVMValueRef codegen_member_assign(Node *n) {
    if (n->childs->size < 3) return NULL;

    Node *member_expr = &n->childs->data[0];
    Node *value_expr  = &n->childs->data[2];

    LLVMValueRef field_ptr = NULL;

    if (member_expr->type == NODE_MEMBER_DOT) {
        if (member_expr->childs->size < 1) return NULL;

        Node *var_node = &member_expr->childs->data[0];
        Symbol *s = sym_lookup(var_node->str);
        if (!s) {
            fprintf(stderr, "codegen error: undefined variable '%s'\n", var_node->str);
            return NULL;
        }

        LLVMTypeRef struct_type = s->type;
        if (LLVMGetTypeKind(struct_type) != LLVMStructTypeKind) {
            fprintf(stderr, "codegen error: '%s' is not a struct\n", var_node->str);
            return NULL;
        }

        int field_index = get_field_index(struct_type, member_expr->str);
        if (field_index < 0) return NULL;

        LLVMValueRef indices[] = {
            LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
            LLVMConstInt(LLVMInt32TypeInContext(ctx), field_index, 0)
        };
        field_ptr = LLVMBuildGEP2(builder, struct_type, s->value, indices, 2, "gep");
    }
    else if (member_expr->type == NODE_MEMBER_ARROW) {
        if (member_expr->childs->size < 1) return NULL;

        Node *var_node = &member_expr->childs->data[0];
        Symbol *s = sym_lookup(var_node->str);
        if (!s) {
            fprintf(stderr, "codegen error: undefined variable '%s'\n", var_node->str);
            return NULL;
        }

        LLVMTypeRef struct_type = s->elem_type;
        if (!struct_type || LLVMGetTypeKind(struct_type) != LLVMStructTypeKind) {
            fprintf(stderr, "codegen error: '%s' is not a pointer to struct\n", var_node->str);
            return NULL;
        }

        LLVMValueRef ptr_val = LLVMBuildLoad2(builder,
            LLVMPointerTypeInContext(ctx, 0), s->value, "ptr");

        int field_index = get_field_index(struct_type, member_expr->str);
        if (field_index < 0) return NULL;

        LLVMValueRef indices[] = {
            LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
            LLVMConstInt(LLVMInt32TypeInContext(ctx), field_index, 0)
        };
        field_ptr = LLVMBuildGEP2(builder, struct_type, ptr_val, indices, 2, "gep");
    }
    else {
        fprintf(stderr, "codegen error: expected member expression in assignment\n");
        return NULL;
    }

    if (!field_ptr) return NULL;

    LLVMValueRef val = codegen_node(value_expr);
    if (!val) return NULL;

    LLVMBuildStore(builder, val, field_ptr);
    return val;
}

static LLVMValueRef codegen_string(Node *n) {
    return LLVMBuildGlobalStringPtr(builder, n->str, "str");
}

static int get_field_index(LLVMTypeRef struct_type, const char *field_name) {
    StructInfo *info = find_struct_info_by_type(struct_type);
    if (!info) {
        fprintf(stderr, "codegen error: unknown struct type\n");
        return -1;
    }
    
    for (int i = 0; i < info->field_count; i++) {
        if (strcmp(info->field_names[i], field_name) == 0) {
            return i;
        }
    }
    
    fprintf(stderr, "codegen error: field '%s' not found in struct\n", field_name);
    return -1;
}

static LLVMValueRef codegen_sizeof(Node *n) {
    LLVMTypeRef t = llvm_type_from_str(n->str);
    if (!t) {
        fprintf(stderr, "codegen error: unknown type '%s' in sizeof\n", n->str);
        return NULL;
    }
    return LLVMSizeOf(t);
}

static LLVMValueRef codegen_asm(Node *n) {
    const char *instr = n->str;
    unsigned operand_count = (unsigned)n->childs->size - 1;

    LLVMTypeRef  param_types[64];
    LLVMValueRef args[64];
    char         constraints[256] = "";

    for (unsigned j = 0; j < operand_count && j < 64; j++) {
        Node   *op = &n->childs->data[j + 1];
        Symbol *s  = sym_lookup(op->str);
        if (!s) {
            fprintf(stderr, "codegen error: undefined variable '%s' in asm\n", op->str);
            return NULL;
        }

        LLVMValueRef val = LLVMBuildLoad2(builder, s->type, s->value, op->str);
        param_types[j]   = s->type;
        args[j]          = val;

        if (j > 0)
            strncat(constraints, ",", sizeof(constraints) - strlen(constraints) - 1);
        strncat(constraints, "r", sizeof(constraints) - strlen(constraints) - 1);
    }

    if (operand_count > 0)
        strncat(constraints, ",", sizeof(constraints) - strlen(constraints) - 1);
    strncat(constraints,
        "~{rax},~{rbx},~{rcx},~{rdx},~{rsi},~{rdi},~{memory}",
        sizeof(constraints) - strlen(constraints) - 1);

    LLVMTypeRef func_type = LLVMFunctionType(
        LLVMVoidTypeInContext(ctx),
        param_types, operand_count, 0);

    LLVMValueRef asm_func = LLVMGetInlineAsm(
        func_type,
        (char *)instr, strlen(instr),
        constraints, strlen(constraints),
        1, 1,
        LLVMInlineAsmDialectIntel,
        0);

    return LLVMBuildCall2(builder, func_type, asm_func, args, operand_count, "");
}

static LLVMValueRef codegen_i32(Node *n) {
    return LLVMConstInt(LLVMInt32TypeInContext(ctx), atoll(n->str), 1);
}

static LLVMValueRef codegen_i64(Node *n) {
    return LLVMConstInt(LLVMInt64TypeInContext(ctx), atoll(n->str), 1);
}

static LLVMValueRef codegen_i8(Node *n) {
    return LLVMConstInt(LLVMInt8TypeInContext(ctx), atoll(n->str), 1);
}

static LLVMValueRef codegen_i16(Node *n) {
    return LLVMConstInt(LLVMInt16TypeInContext(ctx), atoll(n->str), 1);
}

static LLVMValueRef codegen_float(Node *n) {
    return LLVMConstReal(LLVMFloatTypeInContext(ctx), atof(n->str));
}

static LLVMValueRef codegen_double(Node *n) {
    return LLVMConstReal(LLVMDoubleTypeInContext(ctx), atof(n->str));
}

static LLVMValueRef codegen_node(Node *n) {
    if (!n) return NULL;

    LLVMBasicBlockRef cur   = LLVMGetInsertBlock(builder);
    LLVMValueRef      func  = cur ? LLVMGetBasicBlockParent(cur) : NULL;

    switch (n->type) {
        case NODE_FLOAT:        return codegen_float(n);
        case NODE_VAR:          return codegen_var(n);
        case NODE_BINOP:        return codegen_binop(n);
        case NODE_UNOP:         return codegen_unop(n);
        case NODE_INDEX:        return codegen_index(n);
        case NODE_VAR_DEF:      return codegen_var_def(n);
        case NODE_FUNC_DEF:     return codegen_func_def(n);
        case NODE_FUNC_CALL:    return codegen_func_call(n);
        case NODE_SCOPE:        return codegen_scope(n);
        case NODE_IF:           return codegen_if(n, func);
        case NODE_WHILE:        return codegen_while(n, func);
        case NODE_ELSE:         return codegen_scope(n);
        case NODE_RETURN:       return codegen_return(n);
        case NODE_ARRAY_DEF:    return codegen_array_def(n);
        case NODE_ASSIGN:       return codegen_assign(n);
        case NODE_INDEX_ASSIGN: return codegen_index_assign(n); 
        case NODE_PTR_ASSIGN:   return codegen_ptr_assign(n);
        case NODE_ADDR:         return codegen_addr(n);
        case NODE_DEREF:        return codegen_deref(n);
        case NODE_STRING:       return codegen_string(n);
        case NODE_STRUCT_DEF:   return codegen_struct_def(n);
        case NODE_MEMBER_DOT:   return codegen_member_dot(n);
        case NODE_MEMBER_ARROW: return codegen_member_arrow(n);
        case NODE_MEMBER_ASSIGN:return codegen_member_assign(n);
        case NODE_SIZEOF:       return codegen_sizeof(n);
        case NODE_ASM:          return codegen_asm(n);
        case NODE_I32:          return codegen_i32(n);
        case NODE_I64:          return codegen_i64(n);
        case NODE_I8:           return codegen_i8(n);
        case NODE_I16:          return codegen_i16(n);
        case NODE_DOUBLE:       return codegen_double(n);
        case NODE_UNDEF:        return NULL;
        default:                return NULL;
    }
}


void codegen(vector_node *nodes, const char *out_file) {
    sym_count = 0;
    type_count = 0;
    struct_info_count = 0;
    memset(sym_table, 0, sizeof(sym_table));
    memset(type_table, 0, sizeof(type_table));
    memset(struct_info, 0, sizeof(struct_info));

    ctx     = LLVMContextCreate();
    mod     = LLVMModuleCreateWithNameInContext("flame", ctx);
    builder = LLVMCreateBuilderInContext(ctx);

    LLVMInitializeX86TargetInfo();
    LLVMInitializeX86Target();
    LLVMInitializeX86TargetMC();
    LLVMInitializeX86AsmPrinter();

    char *triple = LLVMGetDefaultTargetTriple();
    LLVMSetTarget(mod, triple);

    LLVMTargetRef target;
    char *err_target = NULL;
    LLVMGetTargetFromTriple(triple, &target, &err_target);
    if (err_target) {
        fprintf(stderr, "codegen: target error: %s\n", err_target);
        LLVMDisposeMessage(err_target);
    }

    LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
        target,
        triple,
        "generic",
        "",
        LLVMCodeGenLevelDefault,
        LLVMRelocPIC,
        LLVMCodeModelDefault
    );

    LLVMTargetDataRef data_layout = LLVMCreateTargetDataLayout(machine);
    LLVMSetModuleDataLayout(mod, data_layout);
    LLVMDisposeMessage(triple);

    {
        LLVMTypeRef malloc_params[] = { LLVMInt32TypeInContext(ctx) };
        LLVMTypeRef malloc_type = LLVMFunctionType(
            LLVMPointerTypeInContext(ctx, 0),
            malloc_params, 1, 0);
        LLVMAddFunction(mod, "malloc", malloc_type);

        LLVMTypeRef free_params[] = { LLVMPointerTypeInContext(ctx, 0) };
        LLVMTypeRef free_type = LLVMFunctionType(
            LLVMVoidTypeInContext(ctx),
            free_params, 1, 0);
        LLVMAddFunction(mod, "free", free_type);
    }

    for (unsigned long long j = 0; j < nodes->size; j++)
        codegen_node(&nodes->data[j]);

    char *err_msg = NULL;
    if (LLVMVerifyModule(mod, LLVMPrintMessageAction, &err_msg)) {
        fprintf(stderr, "codegen: module verification failed\n");
        LLVMDisposeMessage(err_msg);
    }

    char *ir = LLVMPrintModuleToString(mod);
    if (!ir) {
        fprintf(stderr, "codegen: failed to print module\n");
    } else {
        FILE *f = fopen(out_file, "w");
        if (f) {
            fputs(ir, f);
            fclose(f);
            fprintf(stderr, "codegen: wrote '%s'\n", out_file);
        } else {
            fprintf(stderr, "codegen: failed to open '%s'\n", out_file);
        }
        LLVMDisposeMessage(ir);
    }

    LLVMDisposeTargetData(data_layout);
    LLVMDisposeTargetMachine(machine);
    LLVMDisposeBuilder(builder);
    LLVMDisposeModule(mod);
    LLVMContextDispose(ctx);
}