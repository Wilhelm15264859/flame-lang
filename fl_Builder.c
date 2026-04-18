#include "fl_Parser.h"
#include "fl_Preproc.h"
#include <llvm-c/Analysis.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/Error.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SYMS  64
#define MAX_TYPES 64

int g_is_import = 0;

static LLVMTypeRef g_T_struct_type = NULL;
static LLVMTargetDataRef g_data_layout = NULL;
static int get_field_index(LLVMTypeRef struct_type, const char *field_name);

extern OverloadEntry* global_modules_overloads[];
extern int global_modules_counts[];
extern int num_imported_modules;

// Функция для поиска перегрузки по искаженному имени (mangled name) в импортированных модулях
static OverloadEntry* find_extern_overload(const char* mangled_name) {
    for (int m = 0; m < num_imported_modules; m++) {
        OverloadEntry *mod_overloads = global_modules_overloads[m];
        int mod_count = global_modules_counts[m];
        
        for (int i = 0; i < mod_count; i++) {
            // Ищем совпадение по mangled-имени, так как в codegen приходит именно оно
            if (strcmp(mod_overloads[i].mangled, mangled_name) == 0) {
                return &mod_overloads[i]; // Нашли!
            }
        }
    }
    return NULL; // В импортированных модулях такой функции нет
}

typedef struct {
  char name[64];
  LLVMTypeRef type;
} TypeEntry;

typedef struct {
  char name[64];
  LLVMTypeRef type;
  char field_names[64][64];
  LLVMTypeRef field_types[64];
  char field_type_names[64][64];
  LLVMTypeRef field_fn_sigs[64];
  int field_count;
  char parent_name[64];
} StructInfo;

static StructInfo *struct_info = NULL;
static int max_struct_infos = MAX_TYPES;
static int struct_info_count = 0;

static StructInfo *find_struct_info_by_type(LLVMTypeRef type) {
  for (int i = 0; i < struct_info_count; i++) {
    if (struct_info[i].type == type)
      return &struct_info[i];
  }
  return NULL;
}

static TypeEntry *type_table = NULL;
static int max_types = MAX_TYPES;
static int type_count = 0;

/* ── typedef alias table ── */
#define MAX_TYPEDEFS 64
typedef struct { char alias[64]; char original[64]; } TypedefEntry;

static TypedefEntry *typedef_table = NULL;
static int max_typedefs = MAX_TYPEDEFS;
static int typedef_count = 0;

static int max_syms = MAX_SYMS;

static void typedef_push(const char *alias, const char *original) {
    for (int i = 0; i < typedef_count; i++)
        if (strcmp(typedef_table[i].alias, alias) == 0) return;
    if (typedef_count >= max_typedefs) {
        max_typedefs *= 2;
        typedef_table = realloc(typedef_table, sizeof(TypedefEntry) * max_typedefs);
        if (!typedef_table) {
            fprintf(stderr, "codegen error: out of memory (typedef_table)\n");
            exit(1);
        }
    }
    strncpy(typedef_table[typedef_count].alias,    alias,    63);
    strncpy(typedef_table[typedef_count].original, original, 63);
    typedef_count++;
}

static const char *typedef_resolve(const char *name) {
    for (int i = 0; i < typedef_count; i++)
        if (strcmp(typedef_table[i].alias, name) == 0)
            return typedef_table[i].original;
    return name;
}

static void type_push(const char *name, LLVMTypeRef type) {
  if (type_count >= max_types) {
    max_types *= 2;
    type_table = realloc(type_table, sizeof(TypeEntry) * max_types);
    if (!type_table) {
        fprintf(stderr, "codegen error: out of memory (type_table)\n");
        exit(1);
    }
  }
  strncpy(type_table[type_count].name, name, 63);
  type_table[type_count].type = type;
  type_count++;
}

static StructInfo *find_struct_info_by_name(const char *name) {
  for (int i = 0; i < struct_info_count; i++) {
    if (strcmp(struct_info[i].name, name) == 0)
      return &struct_info[i];
  }
  return NULL;
}

static LLVMTypeRef type_lookup(const char *name) {
  for (int j = type_count - 1; j >= 0; j--)
    if (strcmp(type_table[j].name, name) == 0)
      return type_table[j].type;
  return NULL;
}

static LLVMContextRef ctx;
static LLVMModuleRef mod;
static uint64_t djb2(const char *s) {
  uint64_t h = 5381;
  for (; *s; s++)
    h = ((h << 5) + h) + (unsigned char)*s;
  return h;
}

static LLVMValueRef codegen_type_literal(Node *n) {
  /* n->str = "int" / "float" / имя struct */
  char gname[128];
  snprintf(gname, sizeof(gname), "_T_%s", n->str);

  LLVMValueRef gvar = LLVMGetNamedGlobal(mod, gname);
  if (gvar)
    return gvar; /* уже создана (примитив) */

  LLVMTypeRef elem_t = type_lookup(n->str);
  LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx, 0);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx);

  /* Массив хешей типов полей */
  StructInfo *info = find_struct_info_by_name(n->str);
  LLVMValueRef hashes_global = LLVMConstNull(ptr);
  if (info && info->field_count > 0) {
    LLVMValueRef hash_vals[64];
    for (int fi = 0; fi < info->field_count; fi++)
      hash_vals[fi] = LLVMConstInt(i64, djb2(info->field_type_names[fi]), 0);
    LLVMTypeRef arr_t = LLVMArrayType(i64, info->field_count);
    char hname[128];
    snprintf(hname, sizeof(hname), "_T_%s_hashes", n->str);
    LLVMValueRef harr = LLVMAddGlobal(mod, arr_t, hname);
    LLVMSetInitializer(harr, LLVMConstArray(i64, hash_vals, info->field_count));
    LLVMSetGlobalConstant(harr, 1);
    hashes_global = harr; /* decay to pointer при передаче */
  }

  gvar = LLVMAddGlobal(mod, g_T_struct_type, gname);
  LLVMValueRef size_val =
      elem_t ? LLVMConstInt(i64, LLVMABISizeOfType(g_data_layout, elem_t), 0)
             : LLVMConstInt(i64, 0, 0);

  /* Ищем деструктор: _Foo_delete_* */
  LLVMValueRef destruct_fn = LLVMConstNull(ptr);
  {
    char prefix[128];
    snprintf(prefix, sizeof(prefix), "_%s_delete_", n->str);
    LLVMValueRef fn = LLVMGetFirstFunction(mod);
    while (fn) {
      if (strncmp(LLVMGetValueName(fn), prefix, strlen(prefix)) == 0) {
        destruct_fn = fn;
        break;
      }
      fn = LLVMGetNextFunction(fn);
    }
  }

  /* Ищем конструктор: _Foo_new_* */
  LLVMValueRef construct_fn = LLVMConstNull(ptr);
  {
    char prefix[128];
    snprintf(prefix, sizeof(prefix), "_%s_new_", n->str);
    LLVMValueRef fn = LLVMGetFirstFunction(mod);
    while (fn) {
      if (strncmp(LLVMGetValueName(fn), prefix, strlen(prefix)) == 0) {
        construct_fn = fn;
        break;
      }
      fn = LLVMGetNextFunction(fn);
    }
  }

  LLVMValueRef elems[] = {size_val, LLVMConstInt(i64, djb2(n->str), 0),
                          destruct_fn, construct_fn, hashes_global};
  LLVMSetInitializer(gvar, LLVMConstNamedStruct(g_T_struct_type, elems, 5));
  LLVMSetGlobalConstant(gvar, 1);
  return gvar;
}

typedef struct {
  char name[64];
  LLVMValueRef value;
  LLVMTypeRef type;
  LLVMTypeRef elem_type;
  LLVMTypeRef fn_type;
  int is_func;
  int is_vla;
} Symbol;

static Symbol *sym_table;
static int *sym_count;
static Symbol *external_sym_table = NULL;
static int external_sym_count = 0;

static void sym_push(const char *name, LLVMValueRef val, LLVMTypeRef type,
                     int is_func) {
  if (*sym_count >= max_syms) {
    int old_max = max_syms;
    max_syms *= 2;
    sym_table = realloc(sym_table, sizeof(Symbol) * max_syms);
    if (!sym_table) {
      fprintf(stderr, "codegen error: out of memory (sym_table)\n");
      exit(1);
    }
    memset(sym_table + old_max, 0, sizeof(Symbol) * old_max);
  }
  strncpy(sym_table[*sym_count].name, name, 63);
  sym_table[*sym_count].value = val;
  sym_table[*sym_count].type = type;
  sym_table[*sym_count].is_func = is_func;
  (*sym_count)++;
}

static Symbol *sym_lookup(const char *name) {
  for (int j = *sym_count - 1; j >= 0; j--)
    if (strcmp(sym_table[j].name, name) == 0)
      return &sym_table[j];

  return NULL;
}

static int sym_checkpoint(void) { return *sym_count; }
static void sym_restore(int cp) { *sym_count = cp; }

static LLVMBuilderRef builder;

static LLVMTypeRef llvm_type_from_str(const char *s) {
  s = typedef_resolve(s);
  if (strchr(s, '*')) {
    return LLVMPointerTypeInContext(ctx, 0);
  }
  if (strcmp(s, "int") == 0)
    return LLVMInt32TypeInContext(ctx);
  if (strcmp(s, "short") == 0)
    return LLVMInt16TypeInContext(ctx);
  if (strcmp(s, "long") == 0)
    return LLVMInt64TypeInContext(ctx);
  if (strcmp(s, "char") == 0)
    return LLVMInt8TypeInContext(ctx);
  if (strcmp(s, "float") == 0)
    return LLVMFloatTypeInContext(ctx);
  if (strcmp(s, "double") == 0)
    return LLVMDoubleTypeInContext(ctx);
  if (strcmp(s, "void") == 0)
    return LLVMVoidTypeInContext(ctx);
  if (strcmp(s, "T") == 0)
    return LLVMPointerTypeInContext(ctx, 0);
  LLVMTypeRef custom = type_lookup(s);
  if (custom)
    return custom;
  fprintf(stderr, "codegen error: unknown type '%s'\n", s);
  exit(1);
  return LLVMInt32TypeInContext(ctx);
}

static int ptr_depth(const char *s) {
  int len = (int)strlen(s);
  int d = 0;
  while (len - 1 - d >= 0 && s[len - 1 - d] == '*')
    d++;
  return d;
}

static void strip_stars(const char *s, int n, char *out, int out_size) {
  int len = (int)strlen(s);
  int new_len = len - n;
  if (new_len < 0)
    new_len = 0;
  if (new_len >= out_size)
    new_len = out_size - 1;
  strncpy(out, s, new_len);
  out[new_len] = '\0';
}

static LLVMTypeRef elem_type_for(const char *type_str) {
  int d = ptr_depth(type_str);
  if (d == 0)
    return llvm_type_from_str(type_str);

  char inner[80];
  strip_stars(type_str, 1, inner, sizeof(inner));

  if (ptr_depth(inner) > 0)
    return LLVMPointerTypeInContext(ctx, 0);

  return llvm_type_from_str(inner);
}

static int type_is_float(LLVMTypeRef t) {
  return t == LLVMFloatTypeInContext(ctx) || t == LLVMDoubleTypeInContext(ctx);
}

/* Returns 1 if name is a primitive (non-struct) type */
static int is_primitive_type(const char *name) {
  return strcmp(name, "int") == 0 || strcmp(name, "short") == 0 ||
         strcmp(name, "long") == 0 || strcmp(name, "char") == 0 ||
         strcmp(name, "float") == 0 || strcmp(name, "double") == 0;
}

static LLVMValueRef coerce_to(LLVMValueRef val, LLVMTypeRef target) {
  if (!val || !target)
    return val;
  LLVMTypeRef src = LLVMTypeOf(val);
  if (src == target)
    return val;

  LLVMTypeKind src_kind = LLVMGetTypeKind(src);
  LLVMTypeKind dst_kind = LLVMGetTypeKind(target);

  if (src_kind == LLVMIntegerTypeKind && dst_kind == LLVMIntegerTypeKind) {
    unsigned src_bits = LLVMGetIntTypeWidth(src);
    unsigned dst_bits = LLVMGetIntTypeWidth(target);
    if (src_bits < dst_bits)
      return LLVMBuildZExt(builder, val, target, "zext");
    if (src_bits > dst_bits)
      return LLVMBuildTrunc(builder, val, target, "trunc");
    return val;
  }

  if (type_is_float(src) && dst_kind == LLVMIntegerTypeKind)
    return LLVMBuildFPToSI(builder, val, target, "fp2si");

  if (src_kind == LLVMIntegerTypeKind && type_is_float(target))
    return LLVMBuildSIToFP(builder, val, target, "si2fp");

  if (type_is_float(src) && type_is_float(target)) {
    if (src == LLVMFloatTypeInContext(ctx) &&
        target == LLVMDoubleTypeInContext(ctx))
      return LLVMBuildFPExt(builder, val, target, "fpext");
    if (src == LLVMDoubleTypeInContext(ctx) &&
        target == LLVMFloatTypeInContext(ctx))
      return LLVMBuildFPTrunc(builder, val, target, "fptrunc");
  }

  if (src_kind == LLVMIntegerTypeKind && dst_kind == LLVMPointerTypeKind)
    return LLVMBuildIntToPtr(builder, val, target, "i2p");

  return val;
}

static LLVMValueRef codegen_node(Node *n);

/* If new(val) is used with a primitive type, store val after malloc */
static void codegen_new_primitive_init(LLVMValueRef ptr, LLVMTypeRef elem_t,
                                       Node *args_node) {
  if (!args_node || args_node->childs->size == 0)
    return;
  LLVMValueRef val = codegen_node(&args_node->childs->data[0]);
  if (!val)
    return;
  val = coerce_to(val, elem_t);
  LLVMBuildStore(builder, val, ptr);
}

static LLVMValueRef codegen_var(Node *n) {
  Symbol *s = sym_lookup(n->str);
  if (!s) {
    fprintf(stderr, "codegen error: undefined variable '%s'\n", n->str);
    exit(1);
    return NULL;
  }
  if (LLVMGetTypeKind(s->type) == LLVMArrayTypeKind) {
    LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0);
    return LLVMBuildGEP2(builder, s->type, s->value, &zero, 1, "arrptr");
  }
  if (s->is_vla) {
    return s->value;
  }
  return LLVMBuildLoad2(builder, s->type, s->value, n->str);
}

static LLVMValueRef codegen_binop(Node *n) {
  if (n->childs->size < 2)
    return NULL;
  LLVMValueRef left = codegen_node(&n->childs->data[0]);
  LLVMValueRef right = codegen_node(&n->childs->data[1]);
  if (!left || !right)
    return NULL;
  const char *op = n->str;
  int left_is_ptr = LLVMGetTypeKind(LLVMTypeOf(left)) == LLVMPointerTypeKind;
  int right_is_ptr = LLVMGetTypeKind(LLVMTypeOf(right)) == LLVMPointerTypeKind;
  if (left_is_ptr && !right_is_ptr) {
    if (strcmp(op, "==") == 0 || strcmp(op, "!=") == 0 ||
        strcmp(op, "<") == 0 || strcmp(op, ">") == 0) {
      LLVMTypeKind rk = LLVMGetTypeKind(LLVMTypeOf(right));
      if (rk == LLVMIntegerTypeKind) {
        right = LLVMBuildIntToPtr(builder, right,
                                  LLVMPointerTypeInContext(ctx, 0), "i2p");
      }
      LLVMValueRef cmp = NULL;
      if (strcmp(op, "==") == 0)
        cmp = LLVMBuildICmp(builder, LLVMIntEQ, left, right, "eq");
      if (strcmp(op, "!=") == 0)
        cmp = LLVMBuildICmp(builder, LLVMIntNE, left, right, "ne");
      if (strcmp(op, "<") == 0)
        cmp = LLVMBuildICmp(builder, LLVMIntULT, left, right, "lt");
      if (strcmp(op, ">") == 0)
        cmp = LLVMBuildICmp(builder, LLVMIntUGT, left, right, "gt");
      if (cmp)
        return LLVMBuildZExt(builder, cmp, LLVMInt32TypeInContext(ctx), "bool");
    }
    LLVMTypeRef pointee = LLVMInt8TypeInContext(ctx);
    if (n->childs->data[0].type == NODE_VAR) {
      Symbol *ps = sym_lookup(n->childs->data[0].str);
      if (ps && ps->elem_type)
        pointee = ps->elem_type;
    }
    if (strcmp(op, "+") == 0)
      return LLVMBuildGEP2(builder, pointee, left, &right, 1, "ptradd");
    if (strcmp(op, "-") == 0) {
      LLVMValueRef neg = LLVMBuildNeg(builder, right, "neg");
      return LLVMBuildGEP2(builder, pointee, left, &neg, 1, "ptrsub");
    }
  }
  if (left_is_ptr && right_is_ptr && strcmp(op, "-") == 0)
    return LLVMBuildPtrDiff2(builder, LLVMInt8TypeInContext(ctx), left, right,
                             "ptrdiff");

  if (!left_is_ptr && !right_is_ptr) {
    LLVMTypeRef lt = LLVMTypeOf(left);
    LLVMTypeRef rt = LLVMTypeOf(right);
    if (lt != rt) {
      LLVMTypeKind lk = LLVMGetTypeKind(lt);
      LLVMTypeKind rk = LLVMGetTypeKind(rt);
      if (lk == LLVMIntegerTypeKind && rk == LLVMIntegerTypeKind) {
        unsigned lb = LLVMGetIntTypeWidth(lt);
        unsigned rb = LLVMGetIntTypeWidth(rt);
        if (lb < rb)
          left = LLVMBuildZExt(builder, left, rt, "zext");
        else if (rb < lb)
          right = LLVMBuildZExt(builder, right, lt, "zext");
      } else if (type_is_float(lt) && rk == LLVMIntegerTypeKind) {
        right = LLVMBuildSIToFP(builder, right, lt, "si2fp");
      } else if (lk == LLVMIntegerTypeKind && type_is_float(rt)) {
        left = LLVMBuildSIToFP(builder, left, rt, "si2fp");
      } else if (type_is_float(lt) && type_is_float(rt)) {
        if (lt == LLVMFloatTypeInContext(ctx))
          left = LLVMBuildFPExt(builder, left, rt, "fpext");
        else
          right = LLVMBuildFPExt(builder, right, lt, "fpext");
      }
    }
  }

  int is_fp =
      type_is_float(LLVMTypeOf(left)) || type_is_float(LLVMTypeOf(right));
  if (strcmp(op, "+") == 0)
    return is_fp ? LLVMBuildFAdd(builder, left, right, "fadd")
                 : LLVMBuildAdd(builder, left, right, "add");
  if (strcmp(op, "-") == 0)
    return is_fp ? LLVMBuildFSub(builder, left, right, "fsub")
                 : LLVMBuildSub(builder, left, right, "sub");
  if (strcmp(op, "*") == 0)
    return is_fp ? LLVMBuildFMul(builder, left, right, "fmul")
                 : LLVMBuildMul(builder, left, right, "mul");
  if (strcmp(op, "/") == 0)
    return is_fp ? LLVMBuildFDiv(builder, left, right, "fdiv")
                 : LLVMBuildSDiv(builder, left, right, "div");
  if (strcmp(op, "%") == 0)
    return LLVMBuildSRem(builder, left, right, "rem");
  if (left_is_ptr && right_is_ptr) {
    LLVMValueRef cmp = NULL;
    if (strcmp(op, "==") == 0)
      cmp = LLVMBuildICmp(builder, LLVMIntEQ, left, right, "eq");
    else if (strcmp(op, "!=") == 0)
      cmp = LLVMBuildICmp(builder, LLVMIntNE, left, right, "ne");
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
                : LLVMBuildICmp(builder, LLVMIntEQ, left, right, "eq");
  else if (strcmp(op, "!=") == 0)
    cmp = is_fp ? LLVMBuildFCmp(builder, LLVMRealONE, left, right, "ne")
                : LLVMBuildICmp(builder, LLVMIntNE, left, right, "ne");
  else if (strcmp(op, "<") == 0)
    cmp = is_fp ? LLVMBuildFCmp(builder, LLVMRealOLT, left, right, "lt")
                : LLVMBuildICmp(builder, LLVMIntSLT, left, right, "lt");
  else if (strcmp(op, ">") == 0)
    cmp = is_fp ? LLVMBuildFCmp(builder, LLVMRealOGT, left, right, "gt")
                : LLVMBuildICmp(builder, LLVMIntSGT, left, right, "gt");
  else if (strcmp(op, "<=") == 0)
    cmp = is_fp ? LLVMBuildFCmp(builder, LLVMRealOLE, left, right, "le")
                : LLVMBuildICmp(builder, LLVMIntSLE, left, right, "le");
  else if (strcmp(op, ">=") == 0)
    cmp = is_fp ? LLVMBuildFCmp(builder, LLVMRealOGE, left, right, "ge")
                : LLVMBuildICmp(builder, LLVMIntSGE, left, right, "ge");
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
  exit(1);
  return NULL;
}

static LLVMValueRef codegen_unop(Node *n) {
  if (n->childs->size < 1)
    return NULL;
  LLVMValueRef val = codegen_node(&n->childs->data[0]);
  if (!val)
    return NULL;
  if (strcmp(n->str, "-") == 0)
    return type_is_float(LLVMTypeOf(val)) ? LLVMBuildFNeg(builder, val, "fneg")
                                          : LLVMBuildNeg(builder, val, "neg");
  if (strcmp(n->str, "!") == 0) {
    LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(val), 0, 0);
    LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntEQ, val, zero, "not");
    return LLVMBuildZExt(builder, cmp, LLVMInt32TypeInContext(ctx), "bool");
  }
  if (strcmp(n->str, "~") == 0)
    return LLVMBuildNot(builder, val, "bnot");
  fprintf(stderr, "codegen error: unknown unary op '%s'\n", n->str);
  exit(1);
  return NULL;
}

static LLVMValueRef codegen_index(Node *n) {
  if (n->childs->size < 2)
    return NULL;
  Node *arr_node = &n->childs->data[0];
  Node *idx_node = &n->childs->data[1];
  LLVMValueRef idx = codegen_node(idx_node);
  idx = coerce_to(idx, LLVMInt32TypeInContext(ctx));

  if (arr_node->type == NODE_VAR) {
    Symbol *s = sym_lookup(arr_node->str);
    if (!s) {
      fprintf(stderr, "codegen error: undefined variable '%s'\n",
              arr_node->str);
      exit(1);
      return NULL;
    }
    LLVMTypeRef elem_type =
        s->elem_type ? s->elem_type : LLVMInt8TypeInContext(ctx);
    LLVMValueRef ptr;
    if (LLVMGetTypeKind(s->type) == LLVMPointerTypeKind) {
      LLVMValueRef base;
      if (s->is_vla) {
        base = s->value;
      } else {
        base = LLVMBuildLoad2(builder, s->type, s->value, "ptr");
      }
      ptr = LLVMBuildGEP2(builder, elem_type, base, &idx, 1, "gep");
    } else {
      LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0);
      LLVMValueRef indices[2] = {zero, idx};
      ptr = LLVMBuildGEP2(builder, s->type, s->value, indices, 2, "gep");
    }
    return LLVMBuildLoad2(builder, elem_type, ptr, "elem");
  }

  if (arr_node->type == NODE_MEMBER_ARROW ||
      arr_node->type == NODE_MEMBER_DOT) {
    Node *var_node = &arr_node->childs->data[0];
    Symbol *s = sym_lookup(var_node->str);
    if (!s) {
      fprintf(stderr, "codegen error: undefined variable '%s'\n",
              var_node->str);
      exit(1);
      return NULL;
    }

    LLVMTypeRef struct_type;
    LLVMValueRef struct_ptr;

    if (arr_node->type == NODE_MEMBER_ARROW) {
      struct_type = s->elem_type;
      struct_ptr = LLVMBuildLoad2(builder, LLVMPointerTypeInContext(ctx, 0),
                                  s->value, "ptr");
    } else {
      struct_type = s->type;
      struct_ptr = s->value;
    }

    if (!struct_type || LLVMGetTypeKind(struct_type) != LLVMStructTypeKind) {
      fprintf(stderr, "codegen error: not a struct in index\n");
      exit(1);
      return NULL;
    }

    int field_index = get_field_index(struct_type, arr_node->str);
    if (field_index < 0)
      return NULL;

    LLVMValueRef field_indices[] = {
        LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
        LLVMConstInt(LLVMInt32TypeInContext(ctx), field_index, 0)};
    LLVMValueRef field_ptr = LLVMBuildGEP2(builder, struct_type, struct_ptr,
                                           field_indices, 2, "field_gep");
    LLVMTypeRef field_type = LLVMStructGetTypeAtIndex(struct_type, field_index);

    LLVMValueRef elem_ptr;
    LLVMTypeRef elem_type;

    if (LLVMGetTypeKind(field_type) == LLVMArrayTypeKind) {
      elem_type = LLVMGetElementType(field_type);
      LLVMValueRef indices[] = {LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
                                idx};
      elem_ptr =
          LLVMBuildGEP2(builder, field_type, field_ptr, indices, 2, "elem_gep");
    } else if (LLVMGetTypeKind(field_type) == LLVMPointerTypeKind) {
      LLVMValueRef base =
          LLVMBuildLoad2(builder, field_type, field_ptr, "fld_ptr");
      elem_type = LLVMInt8TypeInContext(ctx);
      StructInfo *info = find_struct_info_by_type(struct_type);
      if (info && field_index < info->field_count) {
        const char *ftn = info->field_type_names[field_index];
        char inner[64];
        strncpy(inner, ftn, 63);
        inner[63] = '\0';
        char *star = strchr(inner, '*');
        if (star)
          *star = '\0';
        LLVMTypeRef et = type_lookup(inner);
        if (et)
          elem_type = et;
      }
      elem_ptr = LLVMBuildGEP2(builder, elem_type, base, &idx, 1, "elem_gep");
    } else {
      fprintf(stderr,
              "codegen error: field is not array or pointer in index\n");
      exit(1);
      return NULL;
    }

    return LLVMBuildLoad2(builder, elem_type, elem_ptr, "elem");
  }

  fprintf(stderr, "codegen error: unsupported lhs in index expression\n");
  exit(1);
  return NULL;
}

static LLVMValueRef codegen_ptr_assign(Node *n) {
  if (n->childs->size < 2)
    return NULL;
  const char *name = n->childs->data[0].str;
  Symbol *s = sym_lookup(name);
  if (!s) {
    fprintf(stderr, "codegen error: undefined pointer '%s'\n", name);
    exit(1);
    return NULL;
  }
  LLVMValueRef ptr = LLVMBuildLoad2(builder, LLVMPointerTypeInContext(ctx, 0),
                                    s->value, "ptr");
  LLVMValueRef val = codegen_node(&n->childs->data[1]);
  if (!val)
    return NULL;
  if (s->elem_type)
    val = coerce_to(val, s->elem_type);
  LLVMBuildStore(builder, val, ptr);
  return val;
}

static LLVMTypeRef parse_ftype_str(const char *s) {
  if (!s || strncmp(s, "F<", 2) != 0)
    return NULL;

  char ret_str[64] = "void";
  char arg_strs[8][64];
  int fargc = 0;

  const char *lt = strchr(s, '<');
  const char *gt = strchr(s, '>');
  if (lt && gt && gt > lt + 1) {
    int len = (int)(gt - lt - 1);
    if (len > 63)
      len = 63;
    strncpy(ret_str, lt + 1, len);
    ret_str[len] = '\0';
  }

  const char *lp = strchr(s, '(');
  const char *rp = strrchr(s, ')');
  if (lp && rp && rp > lp + 1) {
    char args_buf[256];
    int len = (int)(rp - lp - 1);
    if (len > 255)
      len = 255;
    strncpy(args_buf, lp + 1, len);
    args_buf[len] = '\0';
    char *tok = strtok(args_buf, ",");
    while (tok && fargc < 8) {
      while (*tok == ' ')
        tok++;
      strncpy(arg_strs[fargc++], tok, 63);
      tok = strtok(NULL, ",");
    }
  }

  LLVMTypeRef f_ret = llvm_type_from_str(ret_str);
  LLVMTypeRef f_args[8];
  for (int fi = 0; fi < fargc; fi++)
    f_args[fi] = llvm_type_from_str(arg_strs[fi]);

  return LLVMFunctionType(f_ret, f_args, (unsigned)fargc, 0);
}

static LLVMValueRef codegen_array_def(Node *n) {
  if (n->childs->size < 3)
    return NULL;

  const char *type_str_raw = n->childs->data[0].str;
  const char *type_str = (strncmp(type_str_raw, "autodel:", 8) == 0)
                             ? type_str_raw + 8
                             : type_str_raw;
  const char *name = n->childs->data[1].str;
  Node *size_node = &n->childs->data[2];

  LLVMTypeRef fn_sig = parse_ftype_str(type_str);
  if (fn_sig) {
    LLVMTypeRef fn_ptr_type = LLVMPointerType(fn_sig, 0);

    if (size_node->type == NODE_I32 || size_node->type == NODE_I64 ||
        size_node->type == NODE_I16 || size_node->type == NODE_I8) {
      int sz = atoi(size_node->str);
      LLVMTypeRef arr_type = LLVMArrayType(fn_ptr_type, (unsigned)sz);
      LLVMValueRef ptr = LLVMBuildAlloca(builder, arr_type, name);
      LLVMBuildStore(builder, LLVMConstNull(arr_type), ptr);
      sym_push(name, ptr, arr_type, 0);
      sym_table[*sym_count - 1].elem_type = fn_ptr_type;
      sym_table[*sym_count - 1].fn_type = fn_sig;
      sym_table[*sym_count - 1].is_vla = 0;
      return ptr;
    }

    LLVMValueRef size_val = codegen_node(size_node);
    if (!size_val)
      return NULL;
    size_val = coerce_to(size_val, LLVMInt64TypeInContext(ctx));
    LLVMValueRef ptr =
        LLVMBuildArrayAlloca(builder, fn_ptr_type, size_val, name);
    LLVMTypeRef ptr_type = LLVMPointerTypeInContext(ctx, 0);
    sym_push(name, ptr, ptr_type, 0);
    sym_table[*sym_count - 1].elem_type = fn_ptr_type;
    sym_table[*sym_count - 1].fn_type = fn_sig;
    sym_table[*sym_count - 1].is_vla = 1;
    return ptr;
  }

  LLVMTypeRef elem_type = llvm_type_from_str(type_str);

  if (size_node->type == NODE_I32 || size_node->type == NODE_I64 ||
      size_node->type == NODE_I16 || size_node->type == NODE_I8) {
    int size = atoi(size_node->str);
    LLVMTypeRef arr_type = LLVMArrayType(elem_type, (unsigned)size);
    LLVMValueRef ptr = LLVMBuildAlloca(builder, arr_type, name);
    LLVMBuildStore(builder, LLVMConstNull(arr_type), ptr);
    sym_push(name, ptr, arr_type, 0);
    sym_table[*sym_count - 1].elem_type = elem_type;
    sym_table[*sym_count - 1].is_vla = 0;
    return ptr;
  }

  LLVMValueRef size_val = codegen_node(size_node);
  if (!size_val)
    return NULL;
  size_val = coerce_to(size_val, LLVMInt64TypeInContext(ctx));

  LLVMValueRef ptr = LLVMBuildArrayAlloca(builder, elem_type, size_val, name);

  LLVMTypeRef ptr_type = LLVMPointerTypeInContext(ctx, 0);
  sym_push(name, ptr, ptr_type, 0);
  sym_table[*sym_count - 1].elem_type = elem_type;
  sym_table[*sym_count - 1].is_vla = 1;
  return ptr;
}

static LLVMTypeRef last_loaded_fn_sig = NULL;

static LLVMValueRef codegen_func_call(Node *n) {
  const char *fname = n->str;
  Symbol *s = sym_lookup(fname);

  last_loaded_fn_sig = NULL; /* сбрасываем перед вычислением аргументов */

  if (s && s->fn_type) {
    LLVMValueRef fn_ptr = LLVMBuildLoad2(
        builder, LLVMPointerType(s->fn_type, 0), s->value, "fptr");

    LLVMTypeRef param_types_arr[64];
    unsigned param_count = LLVMCountParamTypes(s->fn_type);
    if (param_count > 0 && param_count <= 64)
      LLVMGetParamTypes(s->fn_type, param_types_arr);

    LLVMValueRef args[64];
    unsigned argc = 0;
    if (n->childs->size >= 2) {
      Node *args_node = &n->childs->data[1];
      for (unsigned long long j = 0; j < args_node->childs->size && argc < 64;
           j++) {
        LLVMValueRef arg = codegen_node(&args_node->childs->data[j]);
        if (arg) {
          if (argc < param_count)
            arg = coerce_to(arg, param_types_arr[argc]);
          args[argc++] = arg;
        }
      }
    }
    int returns_void =
        LLVMGetTypeKind(LLVMGetReturnType(s->fn_type)) == LLVMVoidTypeKind;
    return LLVMBuildCall2(builder, s->fn_type, fn_ptr, args, argc,
                          returns_void ? "" : "fcall");
  }

  if (s && !s->is_func && s->fn_type == NULL && s->elem_type) {
  }

  LLVMValueRef func;
  LLVMTypeRef ftype;

  if (s && s->is_func) {
      func = s->value;
      ftype = s->type;
  } else {
      func = LLVMGetNamedFunction(mod, fname); // Пытаемся найти локально
      
      // ЕСЛИ ЛОКАЛЬНО НЕТ — ИЩЕМ В ИМПОРТАХ И ОБЪЯВЛЯЕМ
      if (!func) {
          // Здесь мы ищем fname (например, _success_) в global_modules_overloads
          // (напишите небольшую вспомогательную функцию для поиска)
          OverloadEntry* ext_func = find_extern_overload(fname); 
          
          if (ext_func) {
              // Нашли в другом модуле! Строим сигнатуру LLVM и объявляем.
              LLVMTypeRef ret_t = llvm_type_from_str(ext_func->ret_type);
              LLVMTypeRef param_t[64];
              for (int i = 0; i < ext_func->param_count; i++) {
                  param_t[i] = llvm_type_from_str(ext_func->param_types[i]);
              }
              ftype = LLVMFunctionType(ret_t, param_t, ext_func->param_count, 0);
              
              // Создаем "заглушку" в текущем модуле, которая ссылается на внешний объектный файл
              func = LLVMAddFunction(mod, fname, ftype);
              LLVMSetLinkage(func, LLVMExternalLinkage); 
          }
      }

      // Если все еще нет...
      if (!func) {
          // (Ваш существующий блок last_loaded_fn_sig или ошибка)
          fprintf(stderr, "codegen error: undefined function '%s'\n", fname);
          exit(1);
      }
      
      ftype = LLVMGlobalGetValueType(func);
  }

  LLVMTypeRef param_types_arr[64];
  unsigned param_types_count = LLVMCountParamTypes(ftype);
  if (param_types_count > 0 && param_types_count <= 64)
    LLVMGetParamTypes(ftype, param_types_arr);

  LLVMValueRef args[64];
  unsigned argc = 0;
  if (n->childs->size >= 2) {
    Node *args_node = &n->childs->data[1];
    for (unsigned long long j = 0; j < args_node->childs->size && argc < 64;
         j++) {
      LLVMValueRef arg = codegen_node(&args_node->childs->data[j]);
      if (arg) {
        if (argc < param_types_count)
          arg = coerce_to(arg, param_types_arr[argc]);
        args[argc++] = arg;
      }
    }
  }
  int returns_void =
      LLVMGetTypeKind(LLVMGetReturnType(ftype)) == LLVMVoidTypeKind;
  return LLVMBuildCall2(builder, ftype, func, args, argc,
                        returns_void ? "" : "call");
}

static LLVMValueRef codegen_if(Node *n, LLVMValueRef func) {
  if (n->childs->size < 2)
    return NULL;
  LLVMValueRef cond = codegen_node(&n->childs->data[0]);
  if (!cond)
    return NULL;
  LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(cond), 0, 0);
  LLVMValueRef cond_i1 =
      LLVMBuildICmp(builder, LLVMIntNE, cond, zero, "ifcond");
  LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(ctx, func, "then");
  LLVMBasicBlockRef else_bb = LLVMAppendBasicBlockInContext(ctx, func, "else");
  LLVMBasicBlockRef merge_bb =
      LLVMAppendBasicBlockInContext(ctx, func, "ifend");
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
  if (n->childs->size < 2)
    return NULL;
  LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx, func, "wcond");
  LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx, func, "wbody");
  LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx, func, "wend");
  LLVMBuildBr(builder, cond_bb);
  LLVMPositionBuilderAtEnd(builder, cond_bb);
  LLVMValueRef cond = codegen_node(&n->childs->data[0]);
  LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(cond), 0, 0);
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
    exit(1);
    LLVMBuildRetVoid(builder);
    return NULL;
  }
  LLVMBasicBlockRef cur = LLVMGetInsertBlock(builder);
  LLVMValueRef func = LLVMGetBasicBlockParent(cur);
  LLVMTypeRef ftype = LLVMGlobalGetValueType(func);
  LLVMTypeRef ret_type = LLVMGetReturnType(ftype);
  val = coerce_to(val, ret_type);
  LLVMBuildRet(builder, val);
  return NULL;
}

static LLVMValueRef codegen_addr(Node *n) {
  Symbol *s = sym_lookup(n->str);
  if (!s) {
    fprintf(stderr, "codegen error: undefined variable '%s'\n", n->str);
    exit(1);
    return NULL;
  }
  return s->value;
}

static LLVMValueRef codegen_deref(Node *n) {
  if (n->childs->size < 1)
    return NULL;
  Node *inner = &n->childs->data[0];

  if (inner->type == NODE_VAR) {
    Symbol *s = sym_lookup(inner->str);
    if (!s) {
      fprintf(stderr, "codegen error: undefined variable '%s' in deref\n",
              inner->str);
      exit(1);
      return NULL;
    }
    LLVMValueRef ptr = LLVMBuildLoad2(builder, s->type, s->value, "deref_ptr");
    LLVMTypeRef load_t =
        s->elem_type ? s->elem_type : LLVMInt32TypeInContext(ctx);
    return LLVMBuildLoad2(builder, load_t, ptr, "deref");
  }

  if (inner->type == NODE_DEREF) {
    Node *deepest = inner;
    int depth = 1;
    while (deepest->type == NODE_DEREF && deepest->childs->size > 0) {
      deepest = &deepest->childs->data[0];
      depth++;
    }

    if (deepest->type != NODE_VAR) {
      fprintf(stderr, "codegen error: complex deref chain not supported\n");
      exit(1);
      return NULL;
    }

    Symbol *s = sym_lookup(deepest->str);
    if (!s) {
      fprintf(stderr, "codegen error: undefined variable '%s' in deref chain\n",
              deepest->str);
      exit(1);
      return NULL;
    }

    LLVMValueRef cur_ptr = LLVMBuildLoad2(builder, s->type, s->value, "dp0");
    LLVMTypeRef cur_elem =
        s->elem_type ? s->elem_type : LLVMInt32TypeInContext(ctx);

    for (int i = 1; i < depth; i++) {
      if (LLVMGetTypeKind(cur_elem) != LLVMPointerTypeKind) {
        fprintf(stderr, "codegen error: deref of non-pointer (depth %d)\n", i);
        exit(1);
        return NULL;
      }
      char tmp_name[16];
      snprintf(tmp_name, sizeof(tmp_name), "dp%d", i);
      LLVMValueRef next_ptr =
          LLVMBuildLoad2(builder, cur_elem, cur_ptr, tmp_name);
      cur_ptr = next_ptr;
      cur_elem = (i + 1 < depth) ? LLVMPointerTypeInContext(ctx, 0)
                                 : LLVMInt32TypeInContext(ctx);
    }

    return LLVMBuildLoad2(builder, cur_elem, cur_ptr, "deref_final");
  }

  LLVMValueRef ptr = codegen_node(inner);
  if (!ptr)
    return NULL;
  return LLVMBuildLoad2(builder, LLVMInt32TypeInContext(ctx), ptr, "deref_gen");
}

static LLVMValueRef codegen_func_def(Node *n) {
  if (n->childs->size < 4)
    return NULL;

  const char *ret_str = n->childs->data[0].str;
  const char *fname = n->childs->data[1].str;
  Node *params = &n->childs->data[2];
  Node *scope = &n->childs->data[3];
  LLVMTypeRef ret_type = llvm_type_from_str(ret_str);

  int is_extern_c = (strncmp(fname, "__extern_c__", 12) == 0);
  char real_name[128];
  if (is_extern_c)
    strncpy(real_name, fname + 12, sizeof(real_name) - 1);
  else
    strncpy(real_name, fname, sizeof(real_name) - 1);
  real_name[sizeof(real_name) - 1] = '\0';
  fname = real_name;

  LLVMTypeRef param_types[64];
  unsigned param_count = 0;

  if (params->type == NODE_PARAMS) {
    for (unsigned long long j = 0; j < params->childs->size && param_count < 64;
         j++) {
      Node *param = &params->childs->data[j];
      if (param->childs->size < 1)
        continue;
      const char *ptype_str = param->childs->data[0].str;

      LLVMTypeRef fn_sig = parse_ftype_str(ptype_str);
      if (fn_sig) {
        param_types[param_count++] = LLVMPointerType(fn_sig, 0);
      } else {
        char pt[80];
        strncpy(pt, ptype_str, 79);
        pt[79] = '\0';
        if (strncmp(pt, "autodel:", 8) == 0)
          memmove(pt, pt + 8, strlen(pt));
        param_types[param_count++] = llvm_type_from_str(pt);
      }
    }
  }

  LLVMTypeRef func_type =
      LLVMFunctionType(ret_type, param_types, param_count, 0);
  LLVMValueRef func = LLVMAddFunction(mod, fname, func_type);
  if (is_extern_c)
    LLVMSetLinkage(func, LLVMExternalLinkage);
  else if (strcmp(fname, "main") != 0) {
    LLVMSetLinkage(func, g_is_import ? LLVMExternalLinkage : LLVMInternalLinkage);
  }

  sym_push(fname, func, func_type, 1);

  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx, func, "entry");
  LLVMPositionBuilderAtEnd(builder, entry);

  int cp = sym_checkpoint();

  if (params->type == NODE_PARAMS) {
    unsigned pi = 0;
    for (unsigned long long j = 0; j < params->childs->size && pi < param_count;
         j++, pi++) {
      Node *param = &params->childs->data[j];
      if (param->childs->size < 1)
        continue;
      const char *ptype_str = param->childs->data[0].str;
      const char *pname =
          (param->childs->size >= 2 && param->childs->data[1].str)
              ? param->childs->data[1].str
              : "_p";

      LLVMValueRef arg_val = LLVMGetParam(func, pi);
      LLVMTypeRef arg_type = param_types[pi];
      LLVMValueRef slot = LLVMBuildAlloca(builder, arg_type, pname);
      LLVMBuildStore(builder, arg_val, slot);

      if (*sym_count < MAX_SYMS) {
        strncpy(sym_table[*sym_count].name, pname, 63);
        sym_table[*sym_count].value = slot;
        sym_table[*sym_count].type = arg_type;
        sym_table[*sym_count].elem_type = NULL;
        if (LLVMGetTypeKind(arg_type) == LLVMPointerTypeKind) {
          char base[80];
          strncpy(base, ptype_str, 79);
          int blen = strlen(base);
          while (blen > 0 && base[blen - 1] == '*')
            base[--blen] = '\0';
          const char *lookup = base;
          if (strncmp(lookup, "autodel:", 8) == 0)
            lookup += 8;
          LLVMTypeRef et = type_lookup(lookup);
          if (et)
            sym_table[*sym_count].elem_type = et;
        }
        sym_table[*sym_count].is_func = 0;
        sym_table[*sym_count].is_vla = 0;

        LLVMTypeRef fnsig = parse_ftype_str(ptype_str);
        sym_table[*sym_count].fn_type = fnsig;

        (*sym_count)++;
      }
    }
  }

  for (unsigned long long j = 0; j < scope->childs->size; j++) {
    fprintf(stderr, "DEBUG scope j=%llu type=%d str='%s'\n", j,
            scope->childs->data[j].type,
            scope->childs->data[j].str ? scope->childs->data[j].str : "(null)");
    codegen_node(&scope->childs->data[j]);
  }

  LLVMBasicBlockRef cur_block = LLVMGetInsertBlock(builder);
  if (!LLVMGetBasicBlockTerminator(cur_block)) {
    if (LLVMGetTypeKind(ret_type) == LLVMVoidTypeKind)
      LLVMBuildRetVoid(builder);
    else
      LLVMBuildRet(builder, LLVMConstInt(ret_type, 0, 0));
  }

  LLVMBasicBlockRef last_bb = LLVMGetLastBasicBlock(func);
  if (last_bb && !LLVMGetBasicBlockTerminator(last_bb)) {
    LLVMPositionBuilderAtEnd(builder, last_bb);
    if (LLVMGetTypeKind(LLVMGetReturnType(LLVMGlobalGetValueType(func))) ==
        LLVMVoidTypeKind)
      LLVMBuildRetVoid(builder);
    else
      LLVMBuildUnreachable(builder);
  }

  sym_restore(cp);
  LLVMClearInsertionPosition(builder);
  return func;
}

/* ──────────────────────────────────────────────────────────────────────────
 * Helpers: check if a NODE_DELETE inside a scope deletes a given field
 * via self->field  (NODE_MEMBER_ARROW with obj "self").
 * ────────────────────────────────────────────────────────────────────────── */
static int scope_has_delete_of_field(Node *scope, const char *field_name) {
  if (!scope) return 0;
  for (unsigned long long i = 0; i < scope->childs->size; i++) {
    Node *ch = &scope->childs->data[i];
    if (ch->type == NODE_DELETE && ch->childs->size >= 2) {
      Node *var_node = &ch->childs->data[1];
      /* delete self->field */
      if (var_node->type == NODE_MEMBER_ARROW &&
          var_node->childs->size >= 1 &&
          strcmp(var_node->str, field_name) == 0) {
        return 1;
      }
    }
    /* recurse into scopes / if / while / for / do-while bodies */
    if (scope_has_delete_of_field(ch, field_name))
      return 1;
  }
  return 0;
}

/* Collect the AST scope-node of the no-arg destructor method of class_name.
 * "No-arg" means the mangled function has exactly 1 param (self pointer). */
static Node *find_noarg_dtor_scope(Node *struct_ast_node,
                                   const char *class_name) {
  char prefix[128];
  snprintf(prefix, sizeof(prefix), "_%s_delete_", class_name);
  for (unsigned long long j = 0; j < struct_ast_node->childs->size; j++) {
    Node *ch = &struct_ast_node->childs->data[j];
    if (ch->type != NODE_FUNC_DEF && ch->type != NODE_STATIC_FUNC_DEF)
      continue;
    if (ch->childs->size < 4)
      continue;
    const char *fname = ch->childs->data[1].str;
    if (!fname || strncmp(fname, prefix, strlen(prefix)) != 0)
      continue;
    /* Check params: only self (1 param) means no-arg destructor */
    Node *params = &ch->childs->data[2];
    if (params->type == NODE_PARAMS && params->childs->size == 1)
      return &ch->childs->data[3]; /* return the scope node */
  }
  return NULL;
}

/* Emit a guarded free+zero for a pointer field of `self`.
 * `self_val`  — loaded ptr to the struct object (i8* / opaque ptr)
 * `struct_t`  — the LLVM struct type
 * `field_idx` — field index inside the struct
 * `field_name`— name of the field (for diagnostics)
 * `cls`       — element class name (for destructor lookup)
 */
static void emit_field_delete(LLVMValueRef self_val, LLVMTypeRef struct_t,
                              int field_idx, const char *field_name,
                              const char *cls, LLVMValueRef cur_fn) {
  (void)field_name; // Fix unused parameter warning
  LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx, 0);
  LLVMValueRef indices[] = {
      LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
      LLVMConstInt(LLVMInt32TypeInContext(ctx), field_idx, 0)};
  LLVMValueRef field_slot =
      LLVMBuildGEP2(builder, struct_t, self_val, indices, 2, "adel_slot");
  LLVMValueRef field_val =
      LLVMBuildLoad2(builder, ptr_t, field_slot, "adel_ptr");

  /* Guard: if (field != NULL) */
  LLVMValueRef null_ptr = LLVMConstPointerNull(ptr_t);
  LLVMValueRef is_nonnull =
      LLVMBuildICmp(builder, LLVMIntNE, field_val, null_ptr, "adel_nn");

  LLVMBasicBlockRef then_bb =
      LLVMAppendBasicBlockInContext(ctx, cur_fn, "adel_then");
  LLVMBasicBlockRef merge_bb =
      LLVMAppendBasicBlockInContext(ctx, cur_fn, "adel_merge");
  LLVMBuildCondBr(builder, is_nonnull, then_bb, merge_bb);

  LLVMPositionBuilderAtEnd(builder, then_bb);

  /* Try to find a no-arg destructor for the element type */
  LLVMValueRef dtor = NULL;
  if (cls && cls[0] != '\0') {
    char dtor_prefix[128];
    snprintf(dtor_prefix, sizeof(dtor_prefix), "_%s_delete_", cls);
    LLVMValueRef fn = LLVMGetFirstFunction(mod);
    while (fn) {
      const char *fn_name = LLVMGetValueName(fn);
      if (strncmp(fn_name, dtor_prefix, strlen(dtor_prefix)) == 0) {
        /* Only use it if it's the no-arg variant (1 param = self) */
        LLVMTypeRef ft = LLVMGlobalGetValueType(fn);
        if (LLVMCountParamTypes(ft) == 1) {
          dtor = fn;
          break;
        }
      }
      fn = LLVMGetNextFunction(fn);
    }
  }

  if (dtor) {
    LLVMTypeRef dtor_type = LLVMGlobalGetValueType(dtor);
    LLVMBuildCall2(builder, dtor_type, dtor, &field_val, 1, "");
  }

  LLVMValueRef free_fn = LLVMGetNamedFunction(mod, "free");
  LLVMTypeRef free_type = LLVMGlobalGetValueType(free_fn);
  LLVMBuildCall2(builder, free_type, free_fn, &field_val, 1, "");
  LLVMBuildStore(builder, LLVMConstNull(ptr_t), field_slot);

  LLVMBuildBr(builder, merge_bb);
  LLVMPositionBuilderAtEnd(builder, merge_bb);
}

/* Main entry point called after all methods of a class have been codegen'd.
 *
 * Cases:
 * A) No no-arg destructor defined at all → generate one from scratch.
 * B) No-arg destructor defined → append delete-calls for pointer fields
 * that the user did NOT already delete manually, inserting them just
 * before the terminator of the last basic block.
 */
static void codegen_auto_delete_fields(const char *class_name,
                                       LLVMTypeRef struct_type,
                                       Node *struct_ast) {
  /* Collect pointer fields that need auto-delete */
  StructInfo *si = find_struct_info_by_name(class_name);
  if (!si || si->field_count == 0)
    return;

  /* Build list of pointer-type fields */
  int ptr_fields[64];
  char ptr_field_cls[64][64]; /* element class name (stripped of '*') */
  int ptr_field_count = 0;

  for (int i = 0; i < si->field_count && ptr_field_count < 64; i++) {
    if (LLVMGetTypeKind(si->field_types[i]) != LLVMPointerTypeKind)
      continue;
    ptr_fields[ptr_field_count] = i;
    /* Strip one '*' to get element class */
    const char *ftn = si->field_type_names[i];
    char cls[64];
    strncpy(cls, ftn, 63);
    cls[63] = '\0';
    char *star = strrchr(cls, '*');
    if (star) *star = '\0';
    /* Strip trailing spaces */
    int cl = (int)strlen(cls);
    while (cl > 0 && cls[cl-1] == ' ') cls[--cl] = '\0';
    strncpy(ptr_field_cls[ptr_field_count], cls, 63);
    ptr_field_count++;
  }

  if (ptr_field_count == 0)
    return;

  /* Find the LLVM function for the no-arg destructor (if exists) */
  char dtor_prefix[128];
  snprintf(dtor_prefix, sizeof(dtor_prefix), "_%s_delete_", class_name);

  LLVMValueRef existing_dtor = NULL;
  {
    LLVMValueRef fn = LLVMGetFirstFunction(mod);
    while (fn) {
      const char *fn_name = LLVMGetValueName(fn);
      if (strncmp(fn_name, dtor_prefix, strlen(dtor_prefix)) == 0) {
        LLVMTypeRef ft = LLVMGlobalGetValueType(fn);
        if (LLVMCountParamTypes(ft) == 1) { /* only self → no-arg */
          existing_dtor = fn;
          break;
        }
      }
      fn = LLVMGetNextFunction(fn);
    }
  }

  /* Collect the AST scope of the no-arg dtor so we can check
   * which fields the user already deletes manually. */
  Node *dtor_scope = find_noarg_dtor_scope(struct_ast, class_name);

  /* ── CASE A: no dtor defined → generate one ── */
  if (!existing_dtor) {
    /* Build: void _ClassName_delete_0(ClassName* self) */
    char dtor_name[128];
    snprintf(dtor_name, sizeof(dtor_name), "_%s_delete_0", class_name);

    LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx, 0);
    LLVMTypeRef param_types[1] = {ptr_t};
    LLVMTypeRef func_type =
        LLVMFunctionType(LLVMVoidTypeInContext(ctx), param_types, 1, 0);

    LLVMValueRef func = LLVMAddFunction(mod, dtor_name, func_type);
    sym_push(dtor_name, func, func_type, 1);

    LLVMBasicBlockRef entry =
        LLVMAppendBasicBlockInContext(ctx, func, "entry");
    LLVMPositionBuilderAtEnd(builder, entry);

    /* self = first param */
    LLVMValueRef self_slot = LLVMBuildAlloca(builder, ptr_t, "self");
    LLVMBuildStore(builder, LLVMGetParam(func, 0), self_slot);
    LLVMValueRef self_val =
        LLVMBuildLoad2(builder, ptr_t, self_slot, "self_val");

    for (int k = 0; k < ptr_field_count; k++) {
      int fi = ptr_fields[k];
      emit_field_delete(self_val, struct_type, fi,
                        si->field_names[fi], ptr_field_cls[k], func);
    }

    LLVMBuildRetVoid(builder);
    LLVMClearInsertionPosition(builder);
    return;
  }

  /* ── CASE B: dtor exists → append deletes for fields user missed ── */

  /* Find the last basic block that has a ret terminator (the real exit). */
  LLVMBasicBlockRef ret_bb = NULL;
  {
    LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(existing_dtor);
    while (bb) {
      LLVMValueRef term = LLVMGetBasicBlockTerminator(bb);
      if (term && LLVMGetInstructionOpcode(term) == LLVMRet)
        ret_bb = bb;
      bb = LLVMGetNextBasicBlock(bb);
    }
  }
  if (!ret_bb) {
    /* No ret block found — dtor body is probably empty; use last bb */
    ret_bb = LLVMGetLastBasicBlock(existing_dtor);
  }
  if (!ret_bb)
    return;

  /* The `self` pointer is param 0 of the existing dtor.
   * Find the alloca named "self" in the entry block. */
  LLVMValueRef self_alloca = NULL;
  {
    LLVMBasicBlockRef entry_bb = LLVMGetFirstBasicBlock(existing_dtor);
    if (entry_bb) {
      LLVMValueRef inst = LLVMGetFirstInstruction(entry_bb);
      while (inst) {
        if (LLVMGetInstructionOpcode(inst) == LLVMAlloca &&
            strcmp(LLVMGetValueName(inst), "self") == 0) {
          self_alloca = inst;
          break;
        }
        inst = LLVMGetNextInstruction(inst);
      }
    }
  }

  /* Count how many fields we will actually emit */
  int emit_count = 0;
  for (int k = 0; k < ptr_field_count; k++) {
    const char *field_name = si->field_names[ptr_fields[k]];
    if (!dtor_scope || !scope_has_delete_of_field(dtor_scope, field_name))
      emit_count++;
  }

  if (emit_count == 0) {
    LLVMClearInsertionPosition(builder);
    return;
  }

  /* Insert a new block "adel_start" just before ret_bb.
   * All predecessors of ret_bb are redirected to adel_start,
   * and adel_start falls through to ret_bb after the deletes. */
  LLVMBasicBlockRef adel_start =
      LLVMInsertBasicBlockInContext(ctx, ret_bb, "adel_start");

  /* Redirect predecessors of ret_bb → adel_start */
  {
    LLVMUseRef pred_use = LLVMGetFirstUse(LLVMBasicBlockAsValue(ret_bb));
    while (pred_use) {
      LLVMValueRef user = LLVMGetUser(pred_use);
      LLVMOpcode opc = LLVMGetInstructionOpcode(user);
      if (opc == LLVMBr || opc == LLVMSwitch) {
        unsigned num_ops = LLVMGetNumOperands(user);
        for (unsigned oi = 0; oi < num_ops; oi++) {
          if (LLVMGetOperand(user, oi) == LLVMBasicBlockAsValue(ret_bb))
            LLVMSetOperand(user, oi, LLVMBasicBlockAsValue(adel_start));
        }
      }
      pred_use = LLVMGetNextUse(pred_use);
    }
  }

  LLVMPositionBuilderAtEnd(builder, adel_start);

  /* Load self pointer in adel_start */
  LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx, 0);
  LLVMValueRef self_val = self_alloca
      ? LLVMBuildLoad2(builder, ptr_t, self_alloca, "self_adel")
      : LLVMGetParam(existing_dtor, 0);

  for (int k = 0; k < ptr_field_count; k++) {
    int fi = ptr_fields[k];
    const char *field_name = si->field_names[fi];

    /* Skip if user already wrote delete for this field */
    if (dtor_scope && scope_has_delete_of_field(dtor_scope, field_name))
      continue;

    emit_field_delete(self_val, struct_type, fi,
                      field_name, ptr_field_cls[k], existing_dtor);
  }

  /* After last adel_merge, jump to the original ret block */
  LLVMBuildBr(builder, ret_bb);

  LLVMClearInsertionPosition(builder);
}

static LLVMValueRef codegen_struct_def(Node *n) {
  const char *full_name = n->str;
  char class_name[64] = "";
  char parent_name[64] = "";
  const char *delim = strchr(full_name, '<');
  if (delim) {
    int len = delim - full_name;
    strncpy(class_name, full_name, len);
    class_name[len] = '\0';
    strncpy(parent_name, delim + 1, 63);
  } else {
    strncpy(class_name, full_name, 63);
  }

  unsigned field_count = 0;
  LLVMTypeRef field_types[64];
  char field_names[64][64];
  char tmp_field_type_names[64][64];

  if (parent_name[0] != '\0') {
    StructInfo *parent = find_struct_info_by_name(parent_name);
    if (parent) {
      for (int i = 0; i < parent->field_count && field_count < 64; i++) {
        field_types[field_count] = parent->field_types[i];
        strncpy(field_names[field_count], parent->field_names[i], 63);
        strncpy(tmp_field_type_names[field_count], parent->field_type_names[i],
                63);
        field_count++;
      }
    }
  }

  for (unsigned j = 0; j < (unsigned)n->childs->size && field_count < 64; j++) {
    Node *child = &n->childs->data[j];
    if (child->type == NODE_FUNC_DEF)
      continue;
    if (child->type == NODE_STATIC_FUNC_DEF)
      continue;
    if (child->type == NODE_STATIC_VAR_DEF)
      continue;
    if (child->childs->size < 1) {
      fprintf(stderr, "codegen error: struct field missing type\n");
      exit(1);
      return NULL;
    }

    const char *ftype_raw = child->childs->data[0].str;
    const char *ftype_str =
        (strncmp(ftype_raw, "autodel:", 8) == 0) ? ftype_raw + 8 : ftype_raw;

    if (child->type == NODE_ARRAY_DEF && child->childs->size >= 3) {
      int arr_size = atoi(child->childs->data[2].str);
      LLVMTypeRef fn_sig_elem = parse_ftype_str(ftype_str);
      LLVMTypeRef elem = fn_sig_elem ? LLVMPointerType(fn_sig_elem, 0)
                                     : llvm_type_from_str(ftype_str);
      field_types[field_count] = LLVMArrayType(elem, (unsigned)arr_size);
    } else {
      LLVMTypeRef fn_sig_f = parse_ftype_str(ftype_str);
      field_types[field_count] = fn_sig_f ? LLVMPointerType(fn_sig_f, 0)
                                          : llvm_type_from_str(ftype_str);
    }

    if (child->childs->size >= 2 && child->childs->data[1].type == NODE_IDENT) {
      strncpy(field_names[field_count], child->childs->data[1].str, 63);
      field_names[field_count][63] = '\0';
    } else {
      field_names[field_count][0] = '\0';
    }

    strncpy(tmp_field_type_names[field_count], ftype_str, 63);
    tmp_field_type_names[field_count][63] = '\0';
    field_count++;
  }

  LLVMTypeRef struct_type = type_lookup(n->str);
  if (!struct_type) {
    struct_type = LLVMStructCreateNamed(ctx, n->str);
    type_push(n->str, struct_type);
  }
  LLVMStructSetBody(struct_type, field_types, field_count, 0);

  StructInfo *si = find_struct_info_by_name(n->str);
  if (!si) {
    if (struct_info_count >= max_struct_infos) {
      int old_max = max_struct_infos;
      max_struct_infos *= 2;
      struct_info = realloc(struct_info, sizeof(StructInfo) * max_struct_infos);
      if (!struct_info) {
        fprintf(stderr, "codegen error: out of memory (struct_info)\n");
        exit(1);
      }
      memset(struct_info + old_max, 0, sizeof(StructInfo) * old_max);
    }
    si = &struct_info[struct_info_count++];
  }
  strncpy(si->name, n->str, 63);
  strncpy(si->parent_name, parent_name, 63);
  si->type = struct_type;
  si->field_count = (int)field_count;
  for (unsigned i = 0; i < field_count; i++) {
    strncpy(si->field_names[i], field_names[i], 63);
    strncpy(si->field_type_names[i], tmp_field_type_names[i], 63);
    si->field_types[i] = field_types[i];
    si->field_fn_sigs[i] = parse_ftype_str(tmp_field_type_names[i]);
  }

  for (unsigned j = 0; j < (unsigned)n->childs->size; j++) {
    Node *child = &n->childs->data[j];
    if (child->type != NODE_STATIC_VAR_DEF)
      continue;
    if (child->childs->size < 2)
      continue;
    const char *field_type_str = child->childs->data[0].str;
    const char *field_name = child->childs->data[1].str;
    char global_name[128];
    snprintf(global_name, sizeof(global_name), "%s_%s", n->str, field_name);
    LLVMTypeRef fn_sig_g = parse_ftype_str(field_type_str);
    LLVMTypeRef gtype = fn_sig_g ? LLVMPointerType(fn_sig_g, 0)
                                 : llvm_type_from_str(field_type_str);
    LLVMValueRef gvar = LLVMAddGlobal(mod, gtype, global_name);
    LLVMSetInitializer(gvar, LLVMConstNull(gtype));
    sym_push(global_name, gvar, gtype, 0);
  }

  for (unsigned j = 0; j < (unsigned)n->childs->size; j++) {
    Node *child = &n->childs->data[j];
    if (child->type == NODE_FUNC_DEF)
      codegen_func_def(child);
  }

  for (unsigned j = 0; j < (unsigned)n->childs->size; j++) {
    Node *child = &n->childs->data[j];
    if (child->type == NODE_STATIC_FUNC_DEF)
      codegen_func_def(child);
  }

  /* ── Auto-delete pointer fields in the no-arg destructor ── */
  codegen_auto_delete_fields(class_name, struct_type, n);

  LLVMClearInsertionPosition(builder);
  return NULL;
}

static void codegen_new(Node *n, LLVMValueRef var_ptr, LLVMTypeRef elem_type) {
  if (!elem_type) {
    fprintf(stderr, "codegen error: unknown class type in new\n");
    exit(1);
    return;
  }

  LLVMValueRef malloc_fn = LLVMGetNamedFunction(mod, "malloc");
  LLVMTypeRef malloc_type = LLVMGlobalGetValueType(malloc_fn);
  LLVMTypeRef malloc_param[1];
  LLVMGetParamTypes(malloc_type, malloc_param);

  /* Если тип — переменная T (runtime type info), берём size из структуры _T */
  LLVMValueRef size_val;
  Symbol *ts = sym_lookup(n->str);
  if (ts && g_T_struct_type &&
      LLVMGetTypeKind(ts->type) == LLVMPointerTypeKind) {
    /* Проверяем что это реально T: смотрим элемент-тип через StructGEP */
    LLVMValueRef t_ptr = LLVMBuildLoad2(
        builder, LLVMPointerTypeInContext(ctx, 0), ts->value, "tptr");
    LLVMValueRef sz_gep =
        LLVMBuildStructGEP2(builder, g_T_struct_type, t_ptr, 0, "szptr");
    size_val =
        LLVMBuildLoad2(builder, LLVMInt64TypeInContext(ctx), sz_gep, "sz");
  } else {
    size_val = LLVMSizeOf(elem_type);
  }

  size_val = coerce_to(size_val, malloc_param[0]);
  LLVMValueRef ptr =
      LLVMBuildCall2(builder, malloc_type, malloc_fn, &size_val, 1, "newptr");
  LLVMBuildStore(builder, ptr, var_ptr);
  if (is_primitive_type(n->str)) {
    Node *args_node = &n->childs->data[1];
    codegen_new_primitive_init(ptr, elem_type, args_node);
    return;
  }

  char init_prefix[128];
  snprintf(init_prefix, sizeof(init_prefix), "_%s_new_", n->str);
  LLVMValueRef init_fn = NULL;
  LLVMValueRef fn_iter = LLVMGetFirstFunction(mod);
  while (fn_iter) {
    const char *fn_name = LLVMGetValueName(fn_iter);
    if (strncmp(fn_name, init_prefix, strlen(init_prefix)) == 0) {
      init_fn = fn_iter;
      break;
    }
    fn_iter = LLVMGetNextFunction(fn_iter);
  }
  if (init_fn) {
    LLVMTypeRef init_type = LLVMGlobalGetValueType(init_fn);
    LLVMTypeRef init_param_types[65];
    unsigned init_param_count = LLVMCountParamTypes(init_type);
    if (init_param_count > 0 && init_param_count <= 65)
      LLVMGetParamTypes(init_type, init_param_types);
    LLVMValueRef args[65];
    unsigned argc = 0;
    args[argc++] = ptr;
    Node *args_node = &n->childs->data[1];
    for (unsigned long long j = 0; j < args_node->childs->size && argc < 65;
         j++) {
      LLVMValueRef arg = codegen_node(&args_node->childs->data[j]);
      if (arg) {
        if (argc < init_param_count)
          arg = coerce_to(arg, init_param_types[argc]);
        args[argc++] = arg;
      }
    }
    LLVMBuildCall2(builder, init_type, init_fn, args, argc, "");
  }
}

static LLVMValueRef codegen_index_addr(Node *n) {
  if (n->childs->size < 2)
    return NULL;
  Node *arr_node = &n->childs->data[0];
  Node *idx_node = &n->childs->data[1];
  LLVMValueRef idx = codegen_node(idx_node);
  idx = coerce_to(idx, LLVMInt32TypeInContext(ctx));

  if (arr_node->type == NODE_VAR) {
    Symbol *s = sym_lookup(arr_node->str);
    if (!s)
      return NULL;
    LLVMTypeRef elem_type =
        s->elem_type ? s->elem_type : LLVMInt8TypeInContext(ctx);
    if (LLVMGetTypeKind(s->type) == LLVMPointerTypeKind) {
      LLVMValueRef base =
          s->is_vla ? s->value
                    : LLVMBuildLoad2(builder, s->type, s->value, "ptr");
      return LLVMBuildGEP2(builder, elem_type, base, &idx, 1, "elemptr");
    } else {
      LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0);
      LLVMValueRef indices[2] = {zero, idx};
      return LLVMBuildGEP2(builder, s->type, s->value, indices, 2, "elemptr");
    }
  }

  if (arr_node->type == NODE_MEMBER_ARROW ||
      arr_node->type == NODE_MEMBER_DOT) {
    Node *var_node = &arr_node->childs->data[0];
    Symbol *s = sym_lookup(var_node->str);
    if (!s)
      return NULL;

    LLVMTypeRef struct_type;
    LLVMValueRef struct_ptr;
    if (arr_node->type == NODE_MEMBER_ARROW) {
      struct_type = s->elem_type;
      struct_ptr = LLVMBuildLoad2(builder, LLVMPointerTypeInContext(ctx, 0),
                                  s->value, "ptr");
    } else {
      struct_type = s->type;
      struct_ptr = s->value;
    }
    if (!struct_type || LLVMGetTypeKind(struct_type) != LLVMStructTypeKind)
      return NULL;

    int field_index = get_field_index(struct_type, arr_node->str);
    if (field_index < 0)
      return NULL;

    LLVMValueRef field_indices[] = {
        LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
        LLVMConstInt(LLVMInt32TypeInContext(ctx), field_index, 0)};
    LLVMValueRef field_ptr = LLVMBuildGEP2(builder, struct_type, struct_ptr,
                                           field_indices, 2, "field_gep");
    LLVMTypeRef field_type = LLVMStructGetTypeAtIndex(struct_type, field_index);

    if (LLVMGetTypeKind(field_type) == LLVMArrayTypeKind) {
      LLVMValueRef indices[] = {LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
                                idx};
      return LLVMBuildGEP2(builder, field_type, field_ptr, indices, 2,
                           "elemptr");
    } else if (LLVMGetTypeKind(field_type) == LLVMPointerTypeKind) {
      LLVMValueRef base =
          LLVMBuildLoad2(builder, field_type, field_ptr, "fld_ptr");
      LLVMTypeRef elem_type = LLVMInt8TypeInContext(ctx);
      StructInfo *info = find_struct_info_by_type(struct_type);
      if (info && field_index < info->field_count) {
        const char *ftn = info->field_type_names[field_index];
        char inner[64];
        strncpy(inner, ftn, 63);
        inner[63] = '\0';
        char *star = strchr(inner, '*');
        if (star)
          *star = '\0';
        LLVMTypeRef et = type_lookup(inner);
        if (et)
          elem_type = et;
      }
      return LLVMBuildGEP2(builder, elem_type, base, &idx, 1, "elemptr");
    }
  }
  return NULL;
}

static LLVMValueRef codegen_var_def(Node *n) {
  if (n->childs->size < 2)
    return NULL;

  const char *type_str_raw = n->childs->data[0].str;
  const char *name = n->childs->data[1].str;

  int is_const = 0;
  const char *after_const = type_str_raw;
  if (strncmp(type_str_raw, "const:", 6) == 0) {
    is_const = 1;
    after_const = type_str_raw + 6;
  }

  char type_str[80];
  if (strncmp(after_const, "autodel:", 8) == 0)
    strncpy(type_str, after_const + 8, 79);
  else
    strncpy(type_str, after_const, 79);
  type_str[79] = '\0';

  LLVMTypeRef fn_sig = parse_ftype_str(type_str);
  if (fn_sig) {
    LLVMTypeRef ptr_type = LLVMPointerType(fn_sig, 0);
    LLVMValueRef ptr = NULL;

    LLVMBasicBlockRef cur_block = LLVMGetInsertBlock(builder);
    if (cur_block) {
      ptr = LLVMBuildAlloca(builder, ptr_type, name);

      if (n->childs->size >= 3 && n->childs->data[2].type != NODE_UNDEF) {
        Node *rhs = &n->childs->data[2];
        const char *rname = rhs->str;
        LLVMValueRef fn_val = NULL;

        if (rname && rname[0])
          fn_val = LLVMGetNamedFunction(mod, rname);
        if (!fn_val && rname && rname[0]) {
          Symbol *fs = sym_lookup(rname);
          if (fs)
            fn_val = fs->value;
        }
        if (!fn_val)
          fn_val = codegen_node(rhs);

        if (fn_val)
          LLVMBuildStore(builder, fn_val, ptr);
      }
    } else {
      ptr = LLVMAddGlobal(mod, ptr_type, name);
      LLVMSetInitializer(ptr, LLVMConstNull(ptr_type));
      if (is_const)
        LLVMSetGlobalConstant(ptr, 1);
    }

    if (*sym_count < MAX_SYMS) {
      strncpy(sym_table[*sym_count].name, name, 63);
      sym_table[*sym_count].value = ptr;
      sym_table[*sym_count].type = ptr_type;
      sym_table[*sym_count].elem_type = NULL;
      sym_table[*sym_count].fn_type = fn_sig;
      sym_table[*sym_count].is_func = 0;
      sym_table[*sym_count].is_vla = 0;
      (*sym_count)++;
    }
    return ptr;
  }

  LLVMTypeRef lltype = llvm_type_from_str(type_str);
  LLVMTypeRef elem_type = elem_type_for(type_str);

  LLVMBasicBlockRef cur_block = LLVMGetInsertBlock(builder);
  if (cur_block) {
    LLVMValueRef ptr = LLVMBuildAlloca(builder, lltype, name);
    if (*sym_count < MAX_SYMS) {
      strncpy(sym_table[*sym_count].name, name, 63);
      sym_table[*sym_count].value = ptr;
      sym_table[*sym_count].type = lltype;
      sym_table[*sym_count].elem_type = elem_type;
      sym_table[*sym_count].fn_type = NULL;
      sym_table[*sym_count].is_func = 0;
      (*sym_count)++;
    }
    if (n->childs->size >= 3 && n->childs->data[2].type != NODE_UNDEF) {
      if (n->childs->data[2].type == NODE_NEW) {
        codegen_new(&n->childs->data[2], ptr, elem_type);
      } else if (n->childs->data[2].type == NODE_INDEX &&
                 LLVMGetTypeKind(lltype) == LLVMPointerTypeKind) {
        LLVMValueRef addr = codegen_index_addr(&n->childs->data[2]);
        if (addr)
          LLVMBuildStore(builder, addr, ptr);
      } else {
        LLVMValueRef init = codegen_node(&n->childs->data[2]);
        if (init) {
          init = coerce_to(init, lltype);
          LLVMBuildStore(builder, init, ptr);
        }
      }
      return ptr;
    }
  } else {
    LLVMValueRef global_var = LLVMAddGlobal(mod, lltype, name);
    if (n->childs->size >= 3 && n->childs->data[2].type != NODE_UNDEF) {
      LLVMValueRef init = codegen_node(&n->childs->data[2]);
      if (init && LLVMIsConstant(init))
        LLVMSetInitializer(global_var, coerce_to(init, lltype));
      else
        LLVMSetInitializer(global_var, LLVMConstNull(lltype));
    } else {
      LLVMSetInitializer(global_var, LLVMConstNull(lltype));
    }
    if (is_const)
      LLVMSetGlobalConstant(global_var, 1);
    if (*sym_count < MAX_SYMS) {
      strncpy(sym_table[*sym_count].name, name, 63);
      sym_table[*sym_count].value = global_var;
      sym_table[*sym_count].type = lltype;
      sym_table[*sym_count].elem_type = elem_type;
      sym_table[*sym_count].fn_type = NULL;
      sym_table[*sym_count].is_func = 0;
      (*sym_count)++;
    }
    return global_var;
  }

  return NULL;
}

static LLVMTypeRef get_field_fn_sig(LLVMTypeRef struct_type, int field_index) {
  for (int si = 0; si < struct_info_count; si++) {
    if (struct_info[si].type == struct_type &&
        field_index < struct_info[si].field_count)
      return struct_info[si].field_fn_sigs[field_index];
  }
  return NULL;
}

static LLVMValueRef codegen_member_dot(Node *n) {
  if (n->childs->size < 1)
    return NULL;
  Node *left = &n->childs->data[0];
  Symbol *s = sym_lookup(left->str);
  if (!s) {
    char global_name[128];
    snprintf(global_name, sizeof(global_name), "%s_%s", left->str, n->str);
    Symbol *gs = sym_lookup(global_name);
    if (gs)
      return LLVMBuildLoad2(builder, gs->type, gs->value, global_name);
    fprintf(stderr, "codegen error: undefined variable '%s'\n", left->str);
    exit(1);
    return NULL;
  }
  LLVMTypeRef struct_type = s->type;
  if (LLVMGetTypeKind(struct_type) != LLVMStructTypeKind) {
    fprintf(stderr, "codegen error: '%s' is not a struct\n", left->str);
    exit(1);
    return NULL;
  }
  int field_index = get_field_index(struct_type, n->str);
  if (field_index < 0)
    return NULL;
  LLVMValueRef indices[] = {
      LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
      LLVMConstInt(LLVMInt32TypeInContext(ctx), field_index, 0)};
  LLVMValueRef field_ptr =
      LLVMBuildGEP2(builder, struct_type, s->value, indices, 2, "gep");
  LLVMTypeRef field_type = LLVMStructGetTypeAtIndex(struct_type, field_index);
  last_loaded_fn_sig = get_field_fn_sig(struct_type, field_index);
  return LLVMBuildLoad2(builder, field_type, field_ptr, n->str);
}

static LLVMValueRef codegen_member_arrow(Node *n) {
  if (n->childs->size < 1)
    return NULL;
  Node *left = &n->childs->data[0];
  Symbol *s = sym_lookup(left->str);
  if (!s)
    return NULL;
  LLVMValueRef ptr = LLVMBuildLoad2(builder, LLVMPointerTypeInContext(ctx, 0),
                                    s->value, "ptr");
  LLVMTypeRef struct_type = s->elem_type;
  if (!struct_type || LLVMGetTypeKind(struct_type) != LLVMStructTypeKind) {
    fprintf(stderr, "codegen error: '%s' is not a pointer to struct\n",
            left->str);
    exit(1);
    return NULL;
  }
  int field_index = get_field_index(struct_type, n->str);
  if (field_index < 0)
    return NULL;
  LLVMValueRef indices[] = {
      LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
      LLVMConstInt(LLVMInt32TypeInContext(ctx), field_index, 0)};
  LLVMValueRef field_ptr =
      LLVMBuildGEP2(builder, struct_type, ptr, indices, 2, "gep");
  LLVMTypeRef field_type = LLVMStructGetTypeAtIndex(struct_type, field_index);
  last_loaded_fn_sig = get_field_fn_sig(struct_type, field_index);
  return LLVMBuildLoad2(builder, field_type, field_ptr, n->str);
}

static LLVMValueRef codegen_member_assign(Node *n) {
  if (n->childs->size < 3)
    return NULL;
  Node *member_expr = &n->childs->data[0];
  Node *value_expr = &n->childs->data[2];
  LLVMValueRef field_ptr = NULL;
  LLVMTypeRef field_type = NULL;
  if (member_expr->type == NODE_MEMBER_DOT) {
    if (member_expr->childs->size < 1)
      return NULL;
    Node *var_node = &member_expr->childs->data[0];
    Symbol *s = sym_lookup(var_node->str);
    if (!s) {
      char global_name[128];
      snprintf(global_name, sizeof(global_name), "%s_%s", var_node->str,
               member_expr->str);
      Symbol *gs = sym_lookup(global_name);
      if (gs) {
        LLVMValueRef val = codegen_node(value_expr);
        if (!val)
          return NULL;
        val = coerce_to(val, gs->type);
        LLVMBuildStore(builder, val, gs->value);
        return val;
      }
      fprintf(stderr, "codegen error: undefined variable '%s'\n",
              var_node->str);
      exit(1);
      return NULL;
    }
    LLVMTypeRef struct_type = s->type;
    if (LLVMGetTypeKind(struct_type) != LLVMStructTypeKind) {
      fprintf(stderr, "codegen error: '%s' is not a struct\n", var_node->str);
      exit(1);
      return NULL;
    }
    int field_index = get_field_index(struct_type, member_expr->str);
    if (field_index < 0)
      return NULL;
    LLVMValueRef indices[] = {
        LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
        LLVMConstInt(LLVMInt32TypeInContext(ctx), field_index, 0)};
    field_ptr =
        LLVMBuildGEP2(builder, struct_type, s->value, indices, 2, "gep");
    field_type = LLVMStructGetTypeAtIndex(struct_type, field_index);
  } else if (member_expr->type == NODE_MEMBER_ARROW) {
    if (member_expr->childs->size < 1)
      return NULL;
    Node *var_node = &member_expr->childs->data[0];
    Symbol *s = sym_lookup(var_node->str);
    if (!s) {
      fprintf(stderr, "codegen error: undefined variable '%s'\n",
              var_node->str);
      exit(1);
      return NULL;
    }
    LLVMTypeRef struct_type = s->elem_type;
    if (!struct_type || LLVMGetTypeKind(struct_type) != LLVMStructTypeKind) {
      fprintf(stderr, "codegen error: '%s' is not a pointer to struct\n",
              var_node->str);
      exit(1);
      return NULL;
    }
    LLVMValueRef ptr_val = LLVMBuildLoad2(
        builder, LLVMPointerTypeInContext(ctx, 0), s->value, "ptr");
    int field_index = get_field_index(struct_type, member_expr->str);
    if (field_index < 0)
      return NULL;
    LLVMValueRef indices[] = {
        LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
        LLVMConstInt(LLVMInt32TypeInContext(ctx), field_index, 0)};
    field_ptr = LLVMBuildGEP2(builder, struct_type, ptr_val, indices, 2, "gep");
    field_type = LLVMStructGetTypeAtIndex(struct_type, field_index);
  } else {
    fprintf(stderr,
            "codegen error: expected member expression in assignment\n");
    exit(1);
    return NULL;
  }
  if (!field_ptr)
    return NULL;
  LLVMValueRef val = codegen_node(value_expr);
  if (!val)
    return NULL;
  if (field_type)
    val = coerce_to(val, field_type);
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
    exit(1);
    return -1;
  }
  for (int i = 0; i < info->field_count; i++) {
    if (strcmp(info->field_names[i], field_name) == 0)
      return i;
  }
  fprintf(stderr, "codegen error: field '%s' not found in struct\n",
          field_name);
  exit(1);
  return -1;
}

static LLVMValueRef codegen_sizeof(Node *n) {
  LLVMTypeRef t = llvm_type_from_str(n->str);
  if (!t) {
    fprintf(stderr, "codegen error: unknown type '%s' in sizeof\n", n->str);
    exit(1);
    return NULL;
  }
  return LLVMSizeOf(t);
}

static LLVMValueRef codegen_asm(Node *n) {
  if (!n || !n->str)
    return NULL;

  // Создаем копию строки, чтобы разделить ее символом '\x01'
  char *str_copy = strdup(n->str);
  char *instr_str = str_copy;
  char *out_csv = strchr(instr_str, '\x01');
  char *clobber_csv = NULL;
  char *in_csv = NULL;

  // Парсинг секций по разделителю \x01
  if (out_csv) {
    *out_csv++ = '\0';
    clobber_csv = strchr(out_csv, '\x01');
    if (clobber_csv) {
      *clobber_csv++ = '\0';
      in_csv = strchr(clobber_csv, '\x01');
      if (in_csv) {
        *in_csv++ = '\0';
      }
    }
  }

  LLVMTypeRef param_types[64];
  LLVMValueRef args[64];
  char constraints[1024] = "";
  unsigned argc = 0;

  LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx);
  LLVMTypeRef ret_type = LLVMVoidTypeInContext(ctx);
  char out_var_name[64] = "";

  // 1. Обработка выходной переменной (outputs_csv: "reg:varName")
  if (out_csv && out_csv[0] != '\0') {
    char *colon = strchr(out_csv, ':');
    if (colon) {
      *colon = '\0';
      char *reg = out_csv;
      char *var = colon + 1;
      strncpy(out_var_name, var, sizeof(out_var_name) - 1);

      Symbol *s = sym_lookup(var);
      if (s) {
        ret_type = i64;
        snprintf(constraints, sizeof(constraints), "={%s}", reg);
      } else {
        fprintf(stderr, "codegen error: undefined output variable '%s' in asm\n", var);
        exit(1);
      }
    }
  }

  // 2. Обработка позиционных переменных-аргументов из дочерних узлов (напр. $0, $1)
  unsigned total_childs = (unsigned)n->childs->size;
  // Первый узел — архитектура (NODE_IDENT), последний — для обратной совместимости (out_node)
  unsigned input_count = (total_childs >= 2) ? total_childs - 2 : 0;

  for (unsigned j = 0; j < input_count && argc < 64; j++) {
    Node *op = &n->childs->data[j + 1];
    if (!op->str || op->str[0] == '\0')
      continue;

    Symbol *s = sym_lookup(op->str);
    if (!s) {
      fprintf(stderr, "codegen error: undefined variable '%s' in asm\n", op->str);
      exit(1);
    }

    LLVMValueRef val = LLVMBuildLoad2(builder, s->type, s->value, op->str);
    LLVMTypeKind kind = LLVMGetTypeKind(LLVMTypeOf(val));
    if (kind == LLVMIntegerTypeKind) {
      unsigned bits = LLVMGetIntTypeWidth(LLVMTypeOf(val));
      if (bits < 64)
        val = LLVMBuildZExt(builder, val, i64, "zext64");
    } else if (kind == LLVMPointerTypeKind) {
      val = LLVMBuildPtrToInt(builder, val, i64, "ptr2int");
    }

    param_types[argc] = LLVMTypeOf(val);
    args[argc] = val;

    if (constraints[0] != '\0')
      strncat(constraints, ",", sizeof(constraints) - strlen(constraints) - 1);
    strncat(constraints, "r", sizeof(constraints) - strlen(constraints) - 1);
    argc++;
  }

  // 3. Обработка явных входных регистров (inputs_csv: "reg:varName,reg2:varName2")
  if (in_csv && in_csv[0] != '\0') {
    char *in_copy = strdup(in_csv);
    char *token = strtok(in_copy, ",");
    while (token && argc < 64) {
      char *colon = strchr(token, ':');
      if (colon) {
        *colon = '\0';
        char *reg = token;
        char *var = colon + 1;

        Symbol *s = sym_lookup(var);
        if (!s) {
          fprintf(stderr, "codegen error: undefined input variable '%s' in asm\n", var);
          exit(1);
        }

        LLVMValueRef val = LLVMBuildLoad2(builder, s->type, s->value, var);
        LLVMTypeKind kind = LLVMGetTypeKind(LLVMTypeOf(val));
        if (kind == LLVMIntegerTypeKind) {
          unsigned bits = LLVMGetIntTypeWidth(LLVMTypeOf(val));
          if (bits < 64)
            val = LLVMBuildZExt(builder, val, i64, "zext64");
        } else if (kind == LLVMPointerTypeKind) {
          val = LLVMBuildPtrToInt(builder, val, i64, "ptr2int");
        }

        param_types[argc] = LLVMTypeOf(val);
        args[argc] = val;

        if (constraints[0] != '\0')
          strncat(constraints, ",", sizeof(constraints) - strlen(constraints) - 1);
        
        size_t curr_len = strlen(constraints);
        snprintf(constraints + curr_len, sizeof(constraints) - curr_len, "{%s}", reg);
        argc++;
      }
      token = strtok(NULL, ",");
    }
    free(in_copy);
  }

  // 4. Обработка Clobbers-регистров (clobbers_csv: "rax,rdi,memory")
  if (clobber_csv && clobber_csv[0] != '\0') {
    char *clob_copy = strdup(clobber_csv);
    char *token = strtok(clob_copy, ",");
    while (token) {
      if (constraints[0] != '\0')
        strncat(constraints, ",", sizeof(constraints) - strlen(constraints) - 1);
      
      size_t curr_len = strlen(constraints);
      snprintf(constraints + curr_len, sizeof(constraints) - curr_len, "~{%s}", token);
      token = strtok(NULL, ",");
    }
    free(clob_copy);
  }

  // Создаем сигнатуру функции и вызываем inline asm
  LLVMTypeRef func_type = LLVMFunctionType(ret_type, param_types, argc, 0);
  LLVMValueRef asm_func =
      LLVMGetInlineAsm(func_type, instr_str, strlen(instr_str), constraints,
                       strlen(constraints), 1, 1, LLVMInlineAsmDialectIntel, 0);

  LLVMValueRef result = LLVMBuildCall2(builder, func_type, asm_func, args, argc,
                                       out_var_name[0] != '\0' ? "asmret" : "");

  // Если была выходная переменная - сохраняем результат
  if (out_var_name[0] != '\0') {
    Symbol *s = sym_lookup(out_var_name);
    if (s) {
      LLVMValueRef store_val = coerce_to(result, s->type);
      LLVMBuildStore(builder, store_val, s->value);
    }
  }

  free(str_copy);
  return result;
}

static LLVMValueRef codegen_i32(Node *n) {
  return LLVMConstInt(LLVMInt32TypeInContext(ctx), strtoll(n->str, NULL, 0), 1);
}
static LLVMValueRef codegen_i64(Node *n) {
  return LLVMConstInt(LLVMInt64TypeInContext(ctx), strtoll(n->str, NULL, 0), 1);
}
static LLVMValueRef codegen_i8(Node *n) {
  return LLVMConstInt(LLVMInt8TypeInContext(ctx), strtoll(n->str, NULL, 0), 1);
}
static LLVMValueRef codegen_i16(Node *n) {
  return LLVMConstInt(LLVMInt16TypeInContext(ctx), strtoll(n->str, NULL, 0), 1);
}
static LLVMValueRef codegen_float(Node *n) {
  return LLVMConstReal(LLVMFloatTypeInContext(ctx), atof(n->str));
}
static LLVMValueRef codegen_double(Node *n) {
  return LLVMConstReal(LLVMDoubleTypeInContext(ctx), atof(n->str));
}

static LLVMValueRef codegen_for(Node *n, LLVMValueRef func) {
  if (n->childs->size < 4)
    return NULL;
  int cp = sym_checkpoint();
  if (n->childs->data[0].type != NODE_UNDEF)
    codegen_node(&n->childs->data[0]);
  LLVMBasicBlockRef cond_bb = LLVMAppendBasicBlockInContext(ctx, func, "fcond");
  LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(ctx, func, "fbody");
  LLVMBasicBlockRef step_bb = LLVMAppendBasicBlockInContext(ctx, func, "fstep");
  LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx, func, "fend");
  LLVMBuildBr(builder, cond_bb);
  LLVMPositionBuilderAtEnd(builder, cond_bb);
  LLVMValueRef cond = codegen_node(&n->childs->data[1]);
  LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(cond), 0, 0);
  LLVMValueRef cond_i1 = LLVMBuildICmp(builder, LLVMIntNE, cond, zero, "fcond");
  LLVMBuildCondBr(builder, cond_i1, body_bb, end_bb);
  LLVMPositionBuilderAtEnd(builder, body_bb);
  codegen_node(&n->childs->data[3]);
  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
    LLVMBuildBr(builder, step_bb);
  LLVMPositionBuilderAtEnd(builder, step_bb);
  if (n->childs->data[2].type != NODE_UNDEF)
    codegen_node(&n->childs->data[2]);
  LLVMBuildBr(builder, cond_bb);
  LLVMPositionBuilderAtEnd(builder, end_bb);
  sym_restore(cp);
  return NULL;
}

static LLVMValueRef codegen_do_while(Node *n, LLVMValueRef func) {
  if (n->childs->size < 2)
    return NULL;
  LLVMBasicBlockRef body_bb =
      LLVMAppendBasicBlockInContext(ctx, func, "dobody");
  LLVMBasicBlockRef cond_bb =
      LLVMAppendBasicBlockInContext(ctx, func, "docond");
  LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(ctx, func, "doend");
  LLVMBuildBr(builder, body_bb);
  LLVMPositionBuilderAtEnd(builder, body_bb);
  int cp = sym_checkpoint();
  codegen_node(&n->childs->data[1]);
  sym_restore(cp);
  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(builder)))
    LLVMBuildBr(builder, cond_bb);
  LLVMPositionBuilderAtEnd(builder, cond_bb);
  LLVMValueRef cond = codegen_node(&n->childs->data[0]);
  LLVMValueRef zero = LLVMConstInt(LLVMTypeOf(cond), 0, 0);
  LLVMValueRef cond_i1 =
      LLVMBuildICmp(builder, LLVMIntNE, cond, zero, "docond");
  LLVMBuildCondBr(builder, cond_i1, body_bb, end_bb);
  LLVMPositionBuilderAtEnd(builder, end_bb);
  return NULL;
}

static LLVMValueRef codegen_delete(Node *n) {
  if (n->childs->size < 2)
    return NULL;

  fprintf(stderr, "codegen_delete: childs=%p size=%llu\n", (void *)n->childs,
          n->childs ? n->childs->size : 0);
  if (!n->childs || n->childs->size < 2) {
    fprintf(stderr, "codegen_delete: not enough children!\n");
    exit(1);
    return NULL;
  }
  Node *args_node = &n->childs->data[0];
  Node *var_node = &n->childs->data[1];
  fprintf(stderr, "codegen_delete: var_node type=%d str='%s'\n", var_node->type,
          var_node->str ? var_node->str : "(null)");

  LLVMValueRef ptr = NULL;
  LLVMValueRef ptr_slot = NULL;
  LLVMTypeRef elem_t = NULL;

  if (var_node->type == NODE_VAR) {
    Symbol *s = sym_lookup(var_node->str);
    if (!s) {
      fprintf(stderr, "codegen error: undefined variable '%s' in delete\n",
              var_node->str);
      exit(1);
      return NULL;
    }
    if (LLVMGetTypeKind(s->type) != LLVMPointerTypeKind)
      return NULL;
    ptr = LLVMBuildLoad2(builder, LLVMPointerTypeInContext(ctx, 0), s->value,
                         "delptr");
    ptr_slot = s->value;
    elem_t = s->elem_type;

  } else if (var_node->type == NODE_MEMBER_ARROW) {
    Node *obj_node = &var_node->childs->data[0];
    Symbol *s = sym_lookup(obj_node->str);
    if (!s) {
      fprintf(stderr, "codegen error: undefined '%s' in delete member\n",
              obj_node->str);
      exit(1);
      return NULL;
    }
    LLVMTypeRef struct_type = s->elem_type;
    if (!struct_type || LLVMGetTypeKind(struct_type) != LLVMStructTypeKind) {
      fprintf(stderr, "codegen error: elem_type is NULL for '%s'\n",
              obj_node->str);
      exit(1);
      return NULL;
    }

    LLVMValueRef self_ptr = LLVMBuildLoad2(
        builder, LLVMPointerTypeInContext(ctx, 0), s->value, "selfptr");

    int field_index = get_field_index(struct_type, var_node->str);
    if (field_index < 0)
      return NULL;

    LLVMValueRef indices[] = {
        LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
        LLVMConstInt(LLVMInt32TypeInContext(ctx), field_index, 0)};
    ptr_slot =
        LLVMBuildGEP2(builder, struct_type, self_ptr, indices, 2, "field_slot");
    LLVMTypeRef field_type = LLVMStructGetTypeAtIndex(struct_type, field_index);
    ptr = LLVMBuildLoad2(builder, field_type, ptr_slot, "delptr");

    StructInfo *info = find_struct_info_by_type(struct_type);
    if (info && field_index < info->field_count) {
      const char *ftype_name = info->field_type_names[field_index];
      char cls[64];
      strncpy(cls, ftype_name, 63);
      char *star = strchr(cls, '*');
      if (star)
        *star = '\0';
      elem_t = type_lookup(cls);
      if (!elem_t) {
        char mangled_prefix[128];
        snprintf(mangled_prefix, sizeof(mangled_prefix), "_%s_delete_", cls);
        LLVMValueRef fn = LLVMGetFirstFunction(mod);
        while (fn) {
          const char *fn_name = LLVMGetValueName(fn);
          if (strncmp(fn_name, mangled_prefix, strlen(mangled_prefix)) == 0) {
            LLVMTypeRef ftype = LLVMGlobalGetValueType(fn);
            if (LLVMCountParamTypes(ftype) >= 1) {
              LLVMTypeRef pt[1];
              LLVMGetParamTypes(ftype, pt);

              elem_t = type_lookup(cls);
            }
            break;
          }
          fn = LLVMGetNextFunction(fn);
        }
      }
    }
  } else {
    fprintf(stderr, "codegen error: unsupported var_node type in delete\n");
    exit(1);
    return NULL;
  }

  if (!ptr)
    return NULL;

  LLVMValueRef dtor = NULL;

  if (elem_t && LLVMGetTypeKind(elem_t) == LLVMStructTypeKind) {
    const char *struct_name = NULL;
    for (int j = 0; j < type_count; j++) {
      if (type_table[j].type == elem_t) {
        struct_name = type_table[j].name;
        break;
      }
    }
    if (struct_name) {
      char mangled_prefix[128];
      snprintf(mangled_prefix, sizeof(mangled_prefix), "_%s_delete_",
               struct_name);
      LLVMValueRef fn = LLVMGetFirstFunction(mod);
      while (fn) {
        const char *fn_name = LLVMGetValueName(fn);
        if (strncmp(fn_name, mangled_prefix, strlen(mangled_prefix)) == 0) {
          dtor = fn;
          break;
        }
        fn = LLVMGetNextFunction(fn);
      }
    }
  }

  if (!dtor && var_node->type == NODE_MEMBER_ARROW) {
    Node *obj_node = &var_node->childs->data[0];
    Symbol *s = sym_lookup(obj_node->str);
    StructInfo *info = s ? find_struct_info_by_type(s->elem_type) : NULL;
    int field_index = info ? get_field_index(s->elem_type, var_node->str) : -1;
    if (info && field_index >= 0 && field_index < info->field_count) {
      const char *ftype_name = info->field_type_names[field_index];
      char cls[64];
      strncpy(cls, ftype_name, 63);
      char *star = strchr(cls, '*');
      if (star)
        *star = '\0';
      char mangled_prefix[128];
      snprintf(mangled_prefix, sizeof(mangled_prefix), "_%s_delete_", cls);
      LLVMValueRef fn = LLVMGetFirstFunction(mod);
      while (fn) {
        const char *fn_name = LLVMGetValueName(fn);
        if (strncmp(fn_name, mangled_prefix, strlen(mangled_prefix)) == 0) {
          dtor = fn;
          break;
        }
        fn = LLVMGetNextFunction(fn);
      }
    }
  }

  /* Guard: if (ptr != null) — используем LLVMConstPointerNull для сравнения */
  LLVMTypeRef ptr_t = LLVMPointerTypeInContext(ctx, 0);
  LLVMValueRef null_ptr = LLVMConstPointerNull(ptr_t);
  LLVMValueRef is_nonzero =
      LLVMBuildICmp(builder, LLVMIntNE, ptr, null_ptr, "del_nonzero");

  LLVMBasicBlockRef cur_bb = LLVMGetInsertBlock(builder);
  LLVMValueRef cur_fn = LLVMGetBasicBlockParent(cur_bb);
  LLVMBasicBlockRef then_bb =
      LLVMAppendBasicBlockInContext(ctx, cur_fn, "del_then");
  LLVMBasicBlockRef merge_bb =
      LLVMAppendBasicBlockInContext(ctx, cur_fn, "del_merge");

  LLVMBuildCondBr(builder, is_nonzero, then_bb, merge_bb);

  /* --- then: ptr != 0, вызываем деструктор + free + обнуляем --- */
  LLVMPositionBuilderAtEnd(builder, then_bb);

  if (dtor) {
    LLVMTypeRef dtor_type = LLVMGlobalGetValueType(dtor);
    LLVMTypeRef dtor_param_types[65];
    unsigned dtor_param_count = LLVMCountParamTypes(dtor_type);
    if (dtor_param_count > 0 && dtor_param_count <= 65)
      LLVMGetParamTypes(dtor_type, dtor_param_types);
    LLVMValueRef args[65];
    unsigned argc = 0;
    args[argc++] = ptr;
    for (unsigned long long j = 0; j < args_node->childs->size && argc < 65;
         j++) {
      LLVMValueRef arg = codegen_node(&args_node->childs->data[j]);
      if (arg) {
        if (argc < dtor_param_count)
          arg = coerce_to(arg, dtor_param_types[argc]);
        args[argc++] = arg;
      }
    }
    LLVMBuildCall2(builder, dtor_type, dtor, args, argc, "");
  }

  LLVMValueRef free_fn = LLVMGetNamedFunction(mod, "free");
  LLVMTypeRef free_type = LLVMGlobalGetValueType(free_fn);
  LLVMBuildCall2(builder, free_type, free_fn, &ptr, 1, "");

  if (ptr_slot)
    LLVMBuildStore(builder, LLVMConstNull(LLVMPointerTypeInContext(ctx, 0)),
                   ptr_slot);

  LLVMBuildBr(builder, merge_bb);

  /* --- merge: продолжаем после проверки --- */
  LLVMPositionBuilderAtEnd(builder, merge_bb);

  return NULL;
}

static LLVMValueRef codegen_extern_func_def(Node *n) {
  if (n->childs->size < 3)
    return NULL;
  const char *ret_str = n->childs->data[0].str;
  const char *fname = n->childs->data[1].str;
  Node *params = &n->childs->data[2];
  LLVMTypeRef ret_type = llvm_type_from_str(ret_str);

  const char *real_name = fname;
  if (strncmp(fname, "__extern_c__", 12) == 0)
    real_name = fname + 12;

  LLVMTypeRef param_types[64];
  unsigned param_count = 0;
  if (params->type == NODE_PARAMS) {
    for (unsigned long long j = 0; j < params->childs->size && param_count < 64;
         j++) {
      Node *param = &params->childs->data[j];
      if (param->childs->size >= 1)
        param_types[param_count++] =
            llvm_type_from_str(param->childs->data[0].str);
    }
  }
  LLVMTypeRef func_type =
      LLVMFunctionType(ret_type, param_types, param_count, 0);

  LLVMValueRef func = LLVMAddFunction(mod, real_name, func_type);
  LLVMSetLinkage(func, LLVMExternalLinkage);

  sym_push(fname, func, func_type, 1);
  sym_push(real_name, func, func_type, 1);
  return func;
}

static LLVMValueRef codegen_member_index_assign(Node *n) {
  if (n->childs->size < 3)
    return NULL;

  Node *lhs = &n->childs->data[0];
  Node *idx_node = &n->childs->data[1];
  Node *value_expr = &n->childs->data[2];

  LLVMValueRef inner_idx = codegen_node(idx_node);
  inner_idx = coerce_to(inner_idx, LLVMInt32TypeInContext(ctx));

  if (lhs->type != NODE_MEMBER_DOT && lhs->type != NODE_MEMBER_ARROW) {
    fprintf(stderr, "codegen error: unsupported lhs in member_index_assign\n");
    return NULL;
  }

  const char *field_name = lhs->str;
  Node *lhs_child = &lhs->childs->data[0];

  LLVMValueRef struct_ptr = NULL;
  LLVMTypeRef struct_type = NULL;

  if (lhs_child->type == NODE_INDEX) {
    Node *arr_expr = &lhs_child->childs->data[0];
    Node *outer_idx_node = &lhs_child->childs->data[1];

    LLVMValueRef outer_idx = codegen_node(outer_idx_node);
    outer_idx = coerce_to(outer_idx, LLVMInt32TypeInContext(ctx));

    if (arr_expr->type == NODE_MEMBER_ARROW ||
        arr_expr->type == NODE_MEMBER_DOT) {
      Node *base_var = &arr_expr->childs->data[0];
      Symbol *s = sym_lookup(base_var->str);
      if (!s) {
        fprintf(stderr, "codegen error: undefined '%s'\n", base_var->str);
        return NULL;
      }

      LLVMTypeRef base_struct_type;
      LLVMValueRef base_ptr;
      if (arr_expr->type == NODE_MEMBER_ARROW) {
        base_struct_type = s->elem_type;
        base_ptr = LLVMBuildLoad2(builder, LLVMPointerTypeInContext(ctx, 0),
                                  s->value, "ptr");
      } else {
        base_struct_type = s->type;
        base_ptr = s->value;
      }

      if (!base_struct_type ||
          LLVMGetTypeKind(base_struct_type) != LLVMStructTypeKind) {
        fprintf(stderr, "codegen error: not a struct in chain\n");
        return NULL;
      }

      int arr_field_idx = get_field_index(base_struct_type, arr_expr->str);
      if (arr_field_idx < 0)
        return NULL;

      LLVMValueRef arr_field_indices[] = {
          LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
          LLVMConstInt(LLVMInt32TypeInContext(ctx), arr_field_idx, 0)};
      LLVMValueRef arr_field_ptr =
          LLVMBuildGEP2(builder, base_struct_type, base_ptr, arr_field_indices,
                        2, "arr_field_gep");
      LLVMTypeRef arr_field_type =
          LLVMStructGetTypeAtIndex(base_struct_type, arr_field_idx);

      LLVMTypeRef elem_struct_type = NULL;

      StructInfo *info = find_struct_info_by_type(base_struct_type);
      if (info && arr_field_idx < info->field_count) {
        const char *ftn = info->field_type_names[arr_field_idx];
        char inner[64];
        strncpy(inner, ftn, 63);
        inner[63] = '\0';
        char *star = strchr(inner, '*');
        if (star)
          *star = '\0';
        LLVMTypeRef et = type_lookup(inner);
        if (et)
          elem_struct_type = et;
      }

      if (LLVMGetTypeKind(arr_field_type) == LLVMPointerTypeKind) {
        LLVMValueRef base_loaded =
            LLVMBuildLoad2(builder, arr_field_type, arr_field_ptr, "arr_base");
        if (!elem_struct_type) {
          fprintf(stderr, "codegen error: unknown element type in chain\n");
          return NULL;
        }
        struct_ptr = LLVMBuildGEP2(builder, elem_struct_type, base_loaded,
                                   &outer_idx, 1, "elem_ptr");
        struct_type = elem_struct_type;
      } else if (LLVMGetTypeKind(arr_field_type) == LLVMArrayTypeKind) {
        LLVMTypeRef arr_elem_type = LLVMGetElementType(arr_field_type);
        LLVMValueRef indices[] = {
            LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0), outer_idx};
        struct_ptr = LLVMBuildGEP2(builder, arr_field_type, arr_field_ptr,
                                   indices, 2, "elem_ptr");
        struct_type = arr_elem_type;
      } else {
        fprintf(stderr, "codegen error: field is not array or pointer\n");
        return NULL;
      }
    } else {
      fprintf(stderr, "codegen error: unsupported array expr in chain\n");
      return NULL;
    }
  } else if (lhs_child->type == NODE_VAR ||
             lhs_child->type == NODE_MEMBER_ARROW ||
             lhs_child->type == NODE_MEMBER_DOT) {
    Symbol *s = sym_lookup(lhs_child->str);
    if (!s) {
      fprintf(stderr, "codegen error: undefined '%s'\n", lhs_child->str);
      return NULL;
    }
    if (lhs->type == NODE_MEMBER_ARROW) {
      struct_type = s->elem_type;
      struct_ptr = LLVMBuildLoad2(builder, LLVMPointerTypeInContext(ctx, 0),
                                  s->value, "ptr");
    } else {
      struct_type = s->type;
      struct_ptr = s->value;
    }
  } else {
    fprintf(stderr,
            "codegen error: unsupported lhs child in member_index_assign\n");
    return NULL;
  }

  if (!struct_ptr || !struct_type ||
      LLVMGetTypeKind(struct_type) != LLVMStructTypeKind) {
    fprintf(stderr, "codegen error: could not resolve struct in chain\n");
    return NULL;
  }

  int field_idx = get_field_index(struct_type, field_name);
  if (field_idx < 0)
    return NULL;

  LLVMValueRef field_indices[] = {
      LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
      LLVMConstInt(LLVMInt32TypeInContext(ctx), field_idx, 0)};
  LLVMValueRef field_ptr = LLVMBuildGEP2(builder, struct_type, struct_ptr,
                                         field_indices, 2, "field_gep");
  LLVMTypeRef field_type = LLVMStructGetTypeAtIndex(struct_type, field_idx);

  LLVMValueRef elem_ptr = NULL;
  LLVMTypeRef elem_type = NULL;

  if (LLVMGetTypeKind(field_type) == LLVMArrayTypeKind) {
    elem_type = LLVMGetElementType(field_type);
    LLVMValueRef indices[] = {LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
                              inner_idx};
    elem_ptr =
        LLVMBuildGEP2(builder, field_type, field_ptr, indices, 2, "elem_gep");
  } else if (LLVMGetTypeKind(field_type) == LLVMPointerTypeKind) {
    LLVMValueRef base =
        LLVMBuildLoad2(builder, field_type, field_ptr, "fld_ptr");
    elem_type = LLVMInt8TypeInContext(ctx);
    StructInfo *info = find_struct_info_by_type(struct_type);
    if (info && field_idx < info->field_count) {
      const char *ftn = info->field_type_names[field_idx];
      char inner[64];
      strncpy(inner, ftn, 63);
      inner[63] = '\0';
      char *star = strchr(inner, '*');
      if (star)
        *star = '\0';
      LLVMTypeRef et = type_lookup(inner);
      if (et)
        elem_type = et;
    }
    elem_ptr =
        LLVMBuildGEP2(builder, elem_type, base, &inner_idx, 1, "elem_gep");
  } else {
    fprintf(stderr, "codegen error: field '%s' is not array or pointer\n",
            field_name);
    return NULL;
  }

  LLVMValueRef val = codegen_node(value_expr);
  if (!val)
    return NULL;
  val = coerce_to(val, elem_type);
  LLVMBuildStore(builder, val, elem_ptr);
  return val;
}

static LLVMValueRef codegen_new_expr(Node *n) {
  const char *class_name = n->str;
  LLVMTypeRef elem_type = type_lookup(class_name);
  if (!elem_type) {
    fprintf(stderr, "codegen error: unknown class '%s' in new expr\n",
            class_name);
    exit(1);
    return NULL;
  }

  LLVMValueRef size_val = LLVMSizeOf(elem_type);
  LLVMValueRef malloc_fn = LLVMGetNamedFunction(mod, "malloc");
  LLVMTypeRef malloc_type = LLVMGlobalGetValueType(malloc_fn);
  LLVMTypeRef malloc_param[1];
  LLVMGetParamTypes(malloc_type, malloc_param);
  size_val = coerce_to(size_val, malloc_param[0]);
  LLVMValueRef ptr =
      LLVMBuildCall2(builder, malloc_type, malloc_fn, &size_val, 1, "newptr");

  /* Primitive init: int *x = new(5)  =>  *x = 5 */
  if (is_primitive_type(class_name)) {
    Node *args_node = &n->childs->data[1];
    codegen_new_primitive_init(ptr, elem_type, args_node);
    return ptr;
  }

  char init_prefix[128];
  snprintf(init_prefix, sizeof(init_prefix), "_%s_new_", n->str);
  LLVMValueRef init_fn = NULL;
  LLVMValueRef fn_iter = LLVMGetFirstFunction(mod);
  while (fn_iter) {
    const char *fn_name = LLVMGetValueName(fn_iter);
    if (strncmp(fn_name, init_prefix, strlen(init_prefix)) == 0) {
      init_fn = fn_iter;
      break;
    }
    fn_iter = LLVMGetNextFunction(fn_iter);
  }
  if (init_fn) {
    LLVMTypeRef init_type = LLVMGlobalGetValueType(init_fn);
    LLVMTypeRef init_param_types[65];
    unsigned init_param_count = LLVMCountParamTypes(init_type);
    if (init_param_count > 0 && init_param_count <= 65)
      LLVMGetParamTypes(init_type, init_param_types);

    LLVMValueRef args[65];
    unsigned argc = 0;
    args[argc++] = ptr;

    Node *args_node = &n->childs->data[1];
    for (unsigned long long j = 0; j < args_node->childs->size && argc < 65;
         j++) {
      LLVMValueRef arg = codegen_node(&args_node->childs->data[j]);
      if (arg) {
        if (argc < init_param_count)
          arg = coerce_to(arg, init_param_types[argc]);
        args[argc++] = arg;
      }
    }
    LLVMBuildCall2(builder, init_type, init_fn, args, argc, "");
  }

  return ptr;
}

static LLVMValueRef codegen_assign(Node *n) {
  if (n->childs->size < 3)
    return NULL;
  const char *name = n->childs->data[0].str;
  Symbol *s = sym_lookup(name);
  if (!s) {
    fprintf(stderr, "codegen error: undefined variable '%s'\n", name);
    exit(1);
    return NULL;
  }

  Node *rhs = &n->childs->data[2];

  if (rhs->type == NODE_NEW) {
    LLVMValueRef new_ptr = codegen_new_expr(rhs);
    if (!new_ptr)
      return NULL;
    if (LLVMGetTypeKind(s->type) == LLVMPointerTypeKind) {
      LLVMBuildStore(builder, new_ptr, s->value);
      return new_ptr;
    } else if (LLVMGetTypeKind(s->type) == LLVMStructTypeKind) {
      LLVMValueRef loaded = LLVMBuildLoad2(builder, s->type, new_ptr, "newval");
      LLVMBuildStore(builder, loaded, s->value);
      LLVMValueRef free_fn = LLVMGetNamedFunction(mod, "free");
      LLVMTypeRef free_type = LLVMGlobalGetValueType(free_fn);
      LLVMBuildCall2(builder, free_type, free_fn, &new_ptr, 1, "");
      return loaded;
    }
  }

  LLVMValueRef val = codegen_node(rhs);
  if (!val)
    return NULL;

  if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind &&
      LLVMGetTypeKind(s->type) == LLVMStructTypeKind) {
    val = LLVMBuildLoad2(builder, s->type, val, "deref_assign");
  } else {
    val = coerce_to(val, s->type);
  }
  LLVMBuildStore(builder, val, s->value);
  return val;
}

static LLVMValueRef codegen_index_assign(Node *n) {
  if (n->childs->size < 3)
    return NULL;
  const char *name = n->childs->data[0].str;
  Symbol *s = sym_lookup(name);
  if (!s) {
    fprintf(stderr, "codegen error: undefined variable '%s'\n", name);
    exit(1);
    return NULL;
  }
  LLVMValueRef idx = codegen_node(&n->childs->data[1]);
  idx = coerce_to(idx, LLVMInt32TypeInContext(ctx));
  if (!idx)
    return NULL;

  int is_ptr = LLVMGetTypeKind(s->type) == LLVMPointerTypeKind;
  LLVMTypeRef elem_type =
      s->elem_type ? s->elem_type : LLVMInt8TypeInContext(ctx);
  LLVMValueRef ptr;

  if (is_ptr) {
    LLVMValueRef base;
    if (s->is_vla) {
      base = s->value;
    } else {
      base = LLVMBuildLoad2(builder, s->type, s->value, "ptr");
    }
    LLVMValueRef indices[] = {idx};
    ptr = LLVMBuildGEP2(builder, elem_type, base, indices, 1, "gep");
  } else {
    LLVMValueRef indices[] = {LLVMConstInt(LLVMInt32TypeInContext(ctx), 0, 0),
                              idx};
    ptr = LLVMBuildGEP2(builder, s->type, s->value, indices, 2, "gep");
  }

  Node *rhs = &n->childs->data[2];

  if (rhs->type == NODE_NEW) {
    LLVMValueRef new_ptr = codegen_new_expr(rhs);
    if (!new_ptr)
      return NULL;
    LLVMTypeRef struct_type = elem_type;
    if (LLVMGetTypeKind(struct_type) == LLVMStructTypeKind) {
      LLVMValueRef loaded =
          LLVMBuildLoad2(builder, struct_type, new_ptr, "newval");
      LLVMBuildStore(builder, loaded, ptr);
    } else {
      LLVMValueRef loaded =
          LLVMBuildLoad2(builder, elem_type, new_ptr, "newval");
      LLVMBuildStore(builder, loaded, ptr);
    }
    LLVMValueRef free_fn = LLVMGetNamedFunction(mod, "free");
    LLVMTypeRef free_type = LLVMGlobalGetValueType(free_fn);
    LLVMBuildCall2(builder, free_type, free_fn, &new_ptr, 1, "");
    return ptr;
  }

  LLVMValueRef val = codegen_node(rhs);
  if (!val)
    return NULL;

  if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMPointerTypeKind &&
      LLVMGetTypeKind(elem_type) == LLVMStructTypeKind) {
    val = LLVMBuildLoad2(builder, elem_type, val, "deref_assign");
  } else if (LLVMGetTypeKind(LLVMTypeOf(val)) == LLVMStructTypeKind) {
    LLVMBuildStore(builder, val, ptr);
    return val;
  } else {
    val = coerce_to(val, elem_type);
  }
  LLVMBuildStore(builder, val, ptr);
  return val;
}

static LLVMValueRef codegen_node(Node *n) {
  if (!n)
    return NULL;
  fprintf(stderr, "DEBUG codegen_node: type=%d str='%s'\n", n->type,
          n->str ? n->str : "(null)");
  LLVMBasicBlockRef cur = LLVMGetInsertBlock(builder);
  LLVMValueRef func = cur ? LLVMGetBasicBlockParent(cur) : NULL;
  switch (n->type) {
  case NODE_FLOAT:
    return codegen_float(n);
  case NODE_VAR:
    return codegen_var(n);
  case NODE_BINOP:
    return codegen_binop(n);
  case NODE_UNOP:
    return codegen_unop(n);
  case NODE_INDEX:
    return codegen_index(n);
  case NODE_VAR_DEF:
    return codegen_var_def(n);
  case NODE_FUNC_DEF:
    return codegen_func_def(n);
  case NODE_FUNC_CALL:
    return codegen_func_call(n);
  case NODE_SCOPE:
    return codegen_scope(n);
  case NODE_IF:
    return codegen_if(n, func);
  case NODE_WHILE:
    return codegen_while(n, func);
  case NODE_ELSE:
    return codegen_scope(n);
  case NODE_RETURN:
    return codegen_return(n);
  case NODE_ARRAY_DEF:
    return codegen_array_def(n);
  case NODE_ASSIGN:
    return codegen_assign(n);
  case NODE_INDEX_ASSIGN:
    return codegen_index_assign(n);
  case NODE_PTR_ASSIGN:
    return codegen_ptr_assign(n);
  case NODE_ADDR:
    return codegen_addr(n);
  case NODE_DEREF:
    return codegen_deref(n);
  case NODE_STRING:
    return codegen_string(n);
  case NODE_STRUCT_DEF:
    return codegen_struct_def(n);
  case NODE_MEMBER_DOT:
    return codegen_member_dot(n);
  case NODE_MEMBER_ARROW:
    return codegen_member_arrow(n);
  case NODE_MEMBER_ASSIGN:
    return codegen_member_assign(n);
  case NODE_SIZEOF:
    return codegen_sizeof(n);
  case NODE_ASM:
    return codegen_asm(n);
  case NODE_I32:
    return codegen_i32(n);
  case NODE_I64:
    return codegen_i64(n);
  case NODE_I8:
    return codegen_i8(n);
  case NODE_I16:
    return codegen_i16(n);
  case NODE_DOUBLE:
    return codegen_double(n);
  case NODE_STATIC_FUNC_DEF:
    return codegen_func_def(n);
  case NODE_STATIC_VAR_DEF:
    return codegen_var_def(n);
  case NODE_FOR:
    return codegen_for(n, func);
  case NODE_DO_WHILE:
    return codegen_do_while(n, func);
  case NODE_DELETE:
    return codegen_delete(n);
  case NODE_EXTERN_FUNC_DEF:
    return codegen_extern_func_def(n);
  case NODE_ADDR_INDEX:
    return codegen_index_addr(n);
  case NODE_MEMBER_INDEX_ASSIGN:
    return codegen_member_index_assign(n);
  case NODE_NEW:
    return codegen_new_expr(n);
  case NODE_TYPE_LITERAL:
    return codegen_type_literal(n);
  case NODE_TYPEDEF:
    /* typedef <original_type> <alias_name>
     * childs[0] = NODE_TYPE (original), childs[1] = NODE_IDENT (alias) */
    if (n->childs && n->childs->size >= 2) {
      const char *original = n->childs->data[0].str;
      const char *alias    = n->childs->data[1].str;
      if (original && alias) {
        typedef_push(alias, original);
        /* Если оригинал — известный LLVM-тип, регистрируем псевдоним */
        LLVMTypeRef orig_t = type_lookup(original);
        if (!orig_t) orig_t = llvm_type_from_str(original);
        if (orig_t) type_push(alias, orig_t);
      }
    }
    return NULL;
  case NODE_UNDEF:
    return NULL;
  default:
    return NULL;
  }
}

static void init_all_targets(void) {
  LLVMInitializeAllTargetInfos();
  LLVMInitializeAllTargets();
  LLVMInitializeAllTargetMCs();
  LLVMInitializeAllAsmPrinters();
  LLVMInitializeAllAsmParsers();
}

void **codegen(vector_node *nodes, const char *out_file, void *sym, int *sym_c,
             const char *extra_link_flags, const char *target_triple, const char *passes, char flags) {
  g_is_import = flags & 0b0001;
  
  max_syms = MAX_SYMS;
  sym_table = malloc(sizeof(Symbol) * max_syms);
  sym_count = malloc(sizeof(int)); *sym_count = 0;
  memset(sym_table, 0, sizeof(Symbol) * max_syms);

  if (sym && sym_c) {
    external_sym_table = (Symbol*)sym;
    external_sym_count = *sym_c;
  }

  // Создаем глобальный контекст и таблицы типов ТОЛЬКО ОДИН РАЗ
  if (!ctx) {
      ctx = LLVMContextCreate();
      
      type_count = 0;
      max_types = MAX_TYPES;
      type_table = calloc(max_types, sizeof(TypeEntry));

      struct_info_count = 0;
      max_struct_infos = MAX_TYPES;
      struct_info = calloc(max_struct_infos, sizeof(StructInfo));

      typedef_count = 0;
      max_typedefs = MAX_TYPEDEFS;
      typedef_table = calloc(max_typedefs, sizeof(TypedefEntry));
  }

  // Имя модуля берем из out_file, чтобы они не конфликтовали
  mod = LLVMModuleCreateWithNameInContext(out_file, ctx);
  builder = LLVMCreateBuilderInContext(ctx);

  if (sym && sym_c) {
      Symbol *ext_table = (Symbol *)sym;
      int ext_count = *(int *)sym_c;
      
      for (int i = 0; i < ext_count; i++) {
          Symbol *es = &ext_table[i];
          
          if (es->is_func) {
              // Создаем заглушку внешней функции
              LLVMValueRef f = LLVMGetNamedFunction(mod, es->name);
              if (!f) {
                  f = LLVMAddFunction(mod, es->name, es->type);
                  LLVMSetLinkage(f, LLVMExternalLinkage);
              }
              // Добавляем в текущую таблицу
              sym_push(es->name, f, es->type, 1);
              sym_table[*sym_count - 1].fn_type = es->fn_type;
              sym_table[*sym_count - 1].elem_type = es->elem_type;
          } else {
              // Создаем заглушку глобальной переменной
              LLVMValueRef g = LLVMGetNamedGlobal(mod, es->name);
              if (!g) {
                  g = LLVMAddGlobal(mod, es->type, es->name);
                  LLVMSetLinkage(g, LLVMExternalLinkage);
              }
              // Добавляем в текущую таблицу
              sym_push(es->name, g, es->type, 0);
              sym_table[*sym_count - 1].elem_type = es->elem_type;
              sym_table[*sym_count - 1].fn_type = es->fn_type;
          }
      }
  }

  init_all_targets();

  char *default_triple = LLVMGetDefaultTargetTriple();
  const char *triple = (target_triple && target_triple[0] != '\0')
                           ? target_triple
                           : default_triple;

  fprintf(stderr, "codegen: target triple = %s\n", triple);
  LLVMSetTarget(mod, triple);

  LLVMTargetRef target_ref = NULL;
  char *err_target = NULL;
  if (LLVMGetTargetFromTriple(triple, &target_ref, &err_target)) {
    fprintf(stderr, "codegen: target error: %s\n", err_target);
    LLVMDisposeMessage(err_target);
    LLVMDisposeMessage(default_triple);
    return NULL;
  }

  LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
      target_ref, triple, "generic", "", LLVMCodeGenLevelDefault, LLVMRelocPIC,
      LLVMCodeModelDefault);

  if (!machine) {
    fprintf(stderr, "codegen: failed to create target machine for '%s'\n",
            triple);
    LLVMDisposeMessage(default_triple);
    return NULL;
  }

  g_data_layout = LLVMCreateTargetDataLayout(machine);
  LLVMSetModuleDataLayout(mod, g_data_layout);
  LLVMDisposeMessage(default_triple);

  {
    /* Проверяем, нет ли уже malloc */
    if (!LLVMGetNamedFunction(mod, "malloc")) {
      LLVMTypeRef malloc_params[] = {LLVMInt64TypeInContext(ctx)};
      LLVMTypeRef malloc_type =
          LLVMFunctionType(LLVMPointerTypeInContext(ctx, 0), malloc_params, 1, 0);
      LLVMAddFunction(mod, "malloc", malloc_type);
    }

    /* Проверяем, нет ли уже free */
    if (!LLVMGetNamedFunction(mod, "free")) {
      LLVMTypeRef free_params[] = {LLVMPointerTypeInContext(ctx, 0)};
      LLVMTypeRef free_type =
          LLVMFunctionType(LLVMVoidTypeInContext(ctx), free_params, 1, 0);
      LLVMAddFunction(mod, "free", free_type);
    }
  }

  if (!g_T_struct_type) {
    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx, 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx);
    LLVMTypeRef fields[] = {i64, i64, ptr, ptr, ptr};
    g_T_struct_type = LLVMStructCreateNamed(ctx, "_T");
    LLVMStructSetBody(g_T_struct_type, fields, 5, 0);
    type_push("_T", g_T_struct_type);

    const char *prims[] = {"int",   "short",  "long", "char",
                           "float", "double", NULL};
    long prim_sizes[] = {4, 2, 8, 1, 4, 8};
    for (int pi = 0; prims[pi]; pi++) {
      char gname[32];
      snprintf(gname, sizeof(gname), "_T_%s", prims[pi]);
      
      LLVMValueRef gvar = LLVMGetNamedGlobal(mod, gname);
      if (!gvar) {
        gvar = LLVMAddGlobal(mod, g_T_struct_type, gname);
        LLVMValueRef elems[] = {LLVMConstInt(i64, prim_sizes[pi], 0),
                                LLVMConstInt(i64, djb2(prims[pi]), 0),
                                LLVMConstNull(ptr), LLVMConstNull(ptr),
                                LLVMConstNull(ptr)};
        LLVMSetInitializer(gvar, LLVMConstNamedStruct(g_T_struct_type, elems, 5));
        LLVMSetGlobalConstant(gvar, 1);
        LLVMSetLinkage(gvar, LLVMLinkOnceAnyLinkage);
      }
    }
  }

  for (unsigned long long j = 0; j < nodes->size; j++) {
    if (nodes->data[j].type == NODE_STRUCT_DEF) {
      Node *n = &nodes->data[j];
      if (n->childs->size == 0) {
        const char *sname = n->str;
        char base[64];
        const char *delim = strchr(sname, '<');
        if (delim) {
          int len = delim - sname;
          strncpy(base, sname, len);
          base[len] = '\0';
        } else {
          strncpy(base, sname, 63);
          base[63] = '\0';
        }
        LLVMTypeRef fwd = LLVMStructCreateNamed(ctx, base);
        type_push(base, fwd);
        if (strcmp(base, sname) != 0) {
          type_push(sname, fwd);
        }
      }
    }
  }

  for (unsigned long long j = 0; j < nodes->size; j++)
    if (nodes->data[j].type == NODE_EXTERN_FUNC_DEF)
      codegen_node(&nodes->data[j]);

  for (unsigned long long j = 0; j < nodes->size; j++)
    if (nodes->data[j].type == NODE_VAR_DEF)
      codegen_node(&nodes->data[j]);

  for (unsigned long long j = 0; j < nodes->size; j++)
    if (nodes->data[j].type == NODE_STRUCT_DEF)
      codegen_node(&nodes->data[j]);

  for (unsigned long long j = 0; j < nodes->size; j++)
    if (nodes->data[j].type != NODE_STRUCT_DEF &&
        nodes->data[j].type != NODE_EXTERN_FUNC_DEF)
      codegen_node(&nodes->data[j]);

  char *err_msg = NULL;
  if (LLVMVerifyModule(mod, LLVMPrintMessageAction, &err_msg)) {
    fprintf(stderr, "codegen: module verification failed\n");
    LLVMDisposeMessage(err_msg);
  }

  if (passes[0] == '\0') {
    LLVMPassBuilderOptionsRef options = LLVMCreatePassBuilderOptions();
    LLVMErrorRef err = LLVMRunPasses(
      mod, "default<O1>", machine, options
    );
    if (err != LLVMErrorSuccess) {
      char *msg = LLVMGetErrorMessage(err);
      printf("codegen: optimization error: %s\n", msg);
      LLVMDisposeErrorMessage(msg);
      exit(1);
    }
  } else {
    LLVMPassBuilderOptionsRef options = LLVMCreatePassBuilderOptions();
    LLVMErrorRef err = LLVMRunPasses(
      mod, passes, machine, options
    );
    if (err != LLVMErrorSuccess) {
      char *msg = LLVMGetErrorMessage(err);
      printf("codegen: optimization error: %s\n", msg);
      LLVMDisposeErrorMessage(msg);
      exit(1);
    }
  }

  if (flags & 0b0010) {
    char ir_file[256];
    snprintf(ir_file, sizeof(ir_file), "%s.ll", out_file);
    char *ir = LLVMPrintModuleToString(mod);
    if (ir) {
      FILE *f = fopen(ir_file, "w");
      if (f) {
        fputs(ir, f);
        fclose(f);
      }
      fprintf(stderr, "codegen: wrote IR '%s'\n", ir_file);
      LLVMDisposeMessage(ir);
    }
  }

  char obj_file[256];
  snprintf(obj_file, sizeof(obj_file), "%s.o", out_file);

  char *emit_err = NULL;
  if (LLVMTargetMachineEmitToFile(machine, mod, obj_file, LLVMObjectFile,
                                  &emit_err)) {
    fprintf(stderr, "codegen: emit error: %s\n", emit_err);
    LLVMDisposeMessage(emit_err);
  } else {
    fprintf(stderr, "codegen: wrote object '%s'\n", obj_file);

    int has_main = (LLVMGetNamedFunction(mod, "main") != NULL);

    if (g_is_import || !has_main) {
        fprintf(stderr, "codegen: no 'main' function found or module is imported, skipping linking (module built as .o).\n");
    } else {
        int import_count = 0;
        const char **import_objects = preprocess_get_imports(&import_count);
        char import_flags[2048] = "";
        for (int ii = 0; ii < import_count; ii++) {
          strncat(import_flags, " ", sizeof(import_flags) - strlen(import_flags) - 1);
          strncat(import_flags, import_objects[ii], sizeof(import_flags) - strlen(import_flags) - 1);
        }

        char cmd[4096];
        if (target_triple && target_triple[0] != '\0') {
          snprintf(cmd, sizeof(cmd), "clang --target=%s %s%s -o %s -lc -no-pie %s",
                   target_triple, obj_file, import_flags, out_file,
                   extra_link_flags ? extra_link_flags : "");
        } else {
          snprintf(cmd, sizeof(cmd), "clang %s%s -o %s -lc -no-pie %s", obj_file,
                   import_flags, out_file, extra_link_flags ? extra_link_flags : "");
        }
        int ret = system(cmd);
        if (ret != 0)
          fprintf(stderr, "codegen: linker failed (code %d)\n", ret);
        else
          fprintf(stderr, "codegen: linked '%s'\n", out_file);
    }

    int keep_objects = (flags & 0b0100);
    if (!g_is_import && !keep_objects) {
        if (has_main) {
            remove(obj_file);
        }
                int import_count = 0;
        const char **import_objects = preprocess_get_imports(&import_count);
        if (import_objects) {
            for (int ii = 0; ii < import_count; ii++) {
                remove(import_objects[ii]);
            }
        }
    }
  }

  LLVMDisposeTargetData(g_data_layout);
  LLVMDisposeTargetMachine(machine);
  LLVMDisposeBuilder(builder);
  LLVMDisposeModule(mod);
  //LLVMContextDispose(ctx);

  if (g_is_import) {
    void **ret = malloc(sizeof(void*) * 2);
    ret[0] = sym_table;
    ret[1] = sym_count;
    return ret;
  }
  else {
    return NULL;
  }
}