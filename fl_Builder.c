#include "fl_Parser.h"
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define MAX_SYMS 256

typedef struct {
    char         name[64];
    LLVMValueRef value;
    LLVMTypeRef  type;
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
    if (strcmp(s, "int")    == 0) return LLVMInt32TypeInContext(ctx);
    if (strcmp(s, "short")  == 0) return LLVMInt16TypeInContext(ctx);
    if (strcmp(s, "long")   == 0) return LLVMInt64TypeInContext(ctx);
    if (strcmp(s, "char")   == 0) return LLVMInt8TypeInContext(ctx);
    if (strcmp(s, "float")  == 0) return LLVMFloatTypeInContext(ctx);
    if (strcmp(s, "double") == 0) return LLVMDoubleTypeInContext(ctx);
    if (strcmp(s, "void")   == 0) return LLVMVoidTypeInContext(ctx);
    fprintf(stderr, "codegen error: unknown type '%s', defaulting to i32\n", s);
    return LLVMInt32TypeInContext(ctx);
}

static int type_is_float(LLVMTypeRef t) {
    return t == LLVMFloatTypeInContext(ctx) ||
           t == LLVMDoubleTypeInContext(ctx);
}

static LLVMValueRef codegen_node(Node *n);

static LLVMValueRef codegen_number(Node *n) {
    return LLVMConstInt(LLVMInt32TypeInContext(ctx), atoi(n->str), 1);
}

static LLVMValueRef codegen_float(Node *n) {
    return LLVMConstReal(LLVMDoubleTypeInContext(ctx), atof(n->str));
}

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

    int is_fp = type_is_float(LLVMTypeOf(left)) ||
                type_is_float(LLVMTypeOf(right));

    const char *op = n->str;

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

static LLVMValueRef codegen_var_def(Node *n) {
    if (n->childs->size < 2) return NULL;

    const char  *type_str = n->childs->data[0].str;
    const char  *name     = n->childs->data[1].str;
    LLVMTypeRef  lltype   = llvm_type_from_str(type_str);

    LLVMBasicBlockRef cur_block = LLVMGetInsertBlock(builder);

    if (cur_block) {
        LLVMValueRef ptr = LLVMBuildAlloca(builder, lltype, name);
        sym_push(name, ptr, lltype, 0);
        
        if (n->childs->size >= 3 && n->childs->data[2].type != NODE_UNDEF) {
            LLVMValueRef init = codegen_node(&n->childs->data[2]);
            if (init) LLVMBuildStore(builder, init, ptr);
        }
        return ptr;
    } else {
        LLVMValueRef global_var = LLVMAddGlobal(mod, lltype, name);
        LLVMSetInitializer(global_var, LLVMConstNull(lltype)); 
        sym_push(name, global_var, lltype, 0);
        return global_var;
    }
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

    LLVMValueRef cond = codegen_node(&n->childs->data[0]);
    if (!cond) return NULL;

    LLVMValueRef zero     = LLVMConstInt(LLVMTypeOf(cond), 0, 0);
    LLVMValueRef cond_i1  = LLVMBuildICmp(builder, LLVMIntNE, cond, zero, "ifcond");

    LLVMBasicBlockRef then_bb  = LLVMAppendBasicBlockInContext(ctx, func, "then");
    LLVMBasicBlockRef else_bb  = LLVMAppendBasicBlockInContext(ctx, func, "else");
    LLVMBasicBlockRef merge_bb = LLVMAppendBasicBlockInContext(ctx, func, "ifend");

    LLVMBuildCondBr(builder, cond_i1, then_bb, else_bb);

    LLVMPositionBuilderAtEnd(builder, then_bb);
    int cp = sym_checkpoint();
    codegen_node(&n->childs->data[1]);
    sym_restore(cp);
    LLVMBuildBr(builder, merge_bb);

    LLVMPositionBuilderAtEnd(builder, else_bb);
    if (n->childs->size >= 3)
        codegen_node(&n->childs->data[2]);
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

static LLVMValueRef codegen_node(Node *n) {
    if (!n) return NULL;

    LLVMBasicBlockRef cur   = LLVMGetInsertBlock(builder);
    LLVMValueRef      func  = cur ? LLVMGetBasicBlockParent(cur) : NULL;

    switch (n->type) {
        case NODE_NUMBER:       return codegen_number(n);
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
        case NODE_UNDEF:        return NULL;
        default:                return NULL;
    }
}


void codegen(vector_node *nodes, const char *out_file) {
    ctx     = LLVMContextCreate();
    mod     = LLVMModuleCreateWithNameInContext("flame", ctx);
    builder = LLVMCreateBuilderInContext(ctx);

    for (unsigned long long j = 0; j < nodes->size; j++)
        codegen_node(&nodes->data[j]);

    char *err_msg = NULL;
    if (LLVMVerifyModule(mod, LLVMPrintMessageAction, &err_msg)) {
        fprintf(stderr, "codegen: module verification failed\n");
        LLVMDisposeMessage(err_msg);
    }

    if (LLVMWriteBitcodeToFile(mod, out_file) != 0)
        fprintf(stderr, "codegen: failed to write '%s'\n", out_file);
    else
        fprintf(stderr, "codegen: wrote '%s'\n", out_file);

    LLVMDisposeBuilder(builder);
    LLVMDisposeModule(mod);
    LLVMContextDispose(ctx);
}