#define _CRT_SECURE_NO_WARNINGS
#include "codegen.h"
#include "builtin_table.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// StolasScript code generator — x86-64, GNU as Intel syntax.
//
// Values are either raw machine words (TY_INT / TY_BOOL / TY_STR) or
// boxed StolaValue* pointers (TY_DYN). The semantic pass decided the
// type of every variable and expression; this file emits:
//   * native integer arithmetic / comparisons / branches for typed code,
//   * runtime calls for dynamic values,
//   * callee-saved register allocation (rbx, r12-r15) for hot locals,
//   * a statically tracked, always 16-byte aligned stack at call sites.
// ABIs: System V AMD64 (Linux) or Windows x64 (hosted on Windows).
// ============================================================

static Program *P;

// ------------------------------------------------------------
// Output buffers
// ------------------------------------------------------------

typedef struct {
  char *d;
  size_t n, cap;
} Buf;

static void buf_vprintf(Buf *b, const char *fmt, va_list ap) {
  va_list ap2;
  va_copy(ap2, ap);
  int need = vsnprintf(NULL, 0, fmt, ap2);
  va_end(ap2);
  if (b->n + need + 1 > b->cap) {
    b->cap = (b->n + need + 1) * 2;
    b->d = realloc(b->d, b->cap);
  }
  vsnprintf(b->d + b->n, need + 1, fmt, ap);
  b->n += need;
}

static void buf_printf(Buf *b, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  buf_vprintf(b, fmt, ap);
  va_end(ap);
}

static void buf_append(Buf *dst, Buf *src) {
  if (src->n)
    buf_printf(dst, "%.*s", (int)src->n, src->d);
}

static Buf text_out;  // finished functions
static Buf data_out;  // .data / .bss / .rodata
static Buf init_out;  // hosted: constant initialisation code for main
static Buf *cur;      // where E() writes

static void E(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  buf_printf(cur, "    ");
  buf_vprintf(cur, fmt, ap);
  buf_printf(cur, "\n");
  va_end(ap);
}

static void LABEL(int id) { buf_printf(cur, ".L%d:\n", id); }

static int label_id = 0;
static int new_label(void) { return label_id++; }

static int cg_errors = 0;
static void cg_error(ASTNode *n, const char *fmt, ...) {
  char msg[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  fprintf(stderr, "%s:%d: error: %s\n", n && n->file ? n->file : P->filename,
          n ? n->line : 0, msg);
  cg_errors++;
}

// ------------------------------------------------------------
// ABI helpers and per-function state
// ------------------------------------------------------------

static const char *sysv_args[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
static const char *win_args[] = {"rcx", "rdx", "r8", "r9"};

static const char *ARG(int i) { return P->win64 ? win_args[i] : sysv_args[i]; }
static int NREGARGS(void) { return P->win64 ? 4 : 6; }

static FuncInfo *CF;     // current function
static int depth;        // 8-byte words pushed since the frame was set up
static int nslots;       // frame slots allocated in current function
static int nsaved;       // callee-saved registers pushed in prologue
static int epilogue;     // label of current function's epilogue
static int try_depth;    // active try blocks in current function

#define MAX_LOOPS 128
static LoopInfo loops[MAX_LOOPS];
static int nloops;

static void PUSH(const char *r) {
  E("push %s", r);
  depth++;
}
static void POP(const char *r) {
  E("pop %s", r);
  depth--;
}

// Call with a correctly aligned stack (and Win64 shadow space).
static void call_aligned(const char *target) {
  int adj = (depth & 1) ? 8 : 0;
  if (P->win64)
    adj += 32;
  if (adj)
    E("sub rsp, %d", adj);
  E("call %s", target);
  if (adj)
    E("add rsp, %d", adj);
}

static int alloc_slot(void) { return nslots++; }
static int slot_offset(int slot) { return 8 * nsaved + 8 * (slot + 1); }

static int dflt_ty(int t) {
  if (t == TY_NONE)
    return P->native ? TY_INT : TY_DYN;
  return t;
}

static int is_raw(int t) { return t == TY_INT || t == TY_BOOL || t == TY_STR; }

// ------------------------------------------------------------
// Symbol names
// ------------------------------------------------------------

static char *mangle(const char *prefix, const char *name) {
  size_t n = strlen(name);
  char *out = malloc(strlen(prefix) + n * 3 + 1);
  char *p = out + sprintf(out, "%s", prefix);
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)name[i];
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') || c == '_')
      *p++ = (char)c;
    else
      p += sprintf(p, "_%02X", c);
  }
  *p = '\0';
  return out;
}

static const char *global_sym(Var *v) {
  static char bufs[4][300];
  static int k = 0;
  char *b = bufs[k++ & 3];
  char *m = mangle(v->kind == SYM_STATIC ? "ST_" : "G_", v->name);
  snprintf(b, 300, "%s", m);
  free(m);
  return b;
}

// Operand text for a variable's storage
static const char *var_loc(Var *v) {
  static char bufs[8][320];
  static int k = 0;
  char *b = bufs[k++ & 7];
  if (v->kind == SYM_LOCAL) {
    if (v->reg)
      snprintf(b, 320, "%s", v->reg);
    else
      snprintf(b, 320, "qword ptr [rbp - %d]", slot_offset(v->slot));
  } else {
    snprintf(b, 320, "qword ptr [rip + %s]", global_sym(v));
  }
  return b;
}

static int var_in_memory(Var *v) {
  return !(v->kind == SYM_LOCAL && v->reg);
}

// ------------------------------------------------------------
// Constant pools
// ------------------------------------------------------------

typedef struct {
  char *data;
  int len;
  int need_value; // hosted: boxed StolaValue* slot required
} StrConst;
static StrConst *strs;
static int nstrs, capstrs;

static int str_const(const char *data, int len, int need_value) {
  for (int i = 0; i < nstrs; i++)
    if (strs[i].len == len && memcmp(strs[i].data, data, len) == 0) {
      if (need_value)
        strs[i].need_value = 1;
      return i;
    }
  if (nstrs == capstrs) {
    capstrs = capstrs ? capstrs * 2 : 64;
    strs = realloc(strs, sizeof(StrConst) * capstrs);
  }
  strs[nstrs].data = malloc(len + 1);
  memcpy(strs[nstrs].data, data, len);
  strs[nstrs].data[len] = 0;
  strs[nstrs].len = len;
  strs[nstrs].need_value = need_value;
  return nstrs++;
}

static int cstr(const char *s) { return str_const(s, (int)strlen(s), 0); }

typedef struct {
  long long v;
  int is_float;
} NumConst;
static NumConst *nums;
static int nnums, capnums;

static int num_const(long long v, int is_float) {
  for (int i = 0; i < nnums; i++)
    if (nums[i].v == v && nums[i].is_float == is_float)
      return i;
  if (nnums == capnums) {
    capnums = capnums ? capnums * 2 : 64;
    nums = realloc(nums, sizeof(NumConst) * capnums);
  }
  nums[nnums].v = v;
  nums[nnums].is_float = is_float;
  return nnums++;
}

// ------------------------------------------------------------
// Forward declarations
// ------------------------------------------------------------

static void gen(ASTNode *e);
static void gen_as(ASTNode *e, int want);
static void gen_branch(ASTNode *e, int label, int jump_if);
static void gen_stmt(ASTNode *s);
static void gen_block(ASTNode *b);

// ------------------------------------------------------------
// Conversions between representations (value in rax)
// ------------------------------------------------------------

static void convert(int from, int to, ASTNode *where) {
  from = dflt_ty(from);
  to = dflt_ty(to);
  if (from == to)
    return;
  if (P->native) {
    if (to == TY_BOOL) {
      E("test rax, rax");
      E("setne al");
      E("movzx eax, al");
    }
    return; // INT / STR / BOOL share the machine representation
  }
  (void)where;
  if (to == TY_DYN) {
    if (from == TY_INT) {
      E("mov %s, rax", ARG(0));
      call_aligned("stola_new_int");
    } else if (from == TY_BOOL) {
      E("mov %s, rax", ARG(0));
      call_aligned("stola_bool_value");
    }
    return;
  }
  if (from == TY_DYN) {
    E("mov %s, rax", ARG(0));
    if (to == TY_BOOL)
      call_aligned("stola_is_truthy");
    else
      call_aligned("stola_to_int64");
    if (to == TY_BOOL)
      E("movzx eax, al");
    return;
  }
  if (to == TY_BOOL) { // INT -> BOOL
    E("test rax, rax");
    E("setne al");
    E("movzx eax, al");
  }
  // BOOL -> INT: identical
}

// Can `e` be used directly as an instruction operand of type `want`?
static int operand_of(ASTNode *e, int want, char *out, size_t outsz) {
  if (!e)
    return 0;
  if (e->type == AST_NUMBER_LITERAL && (want == TY_INT || want == TY_BOOL)) {
    long long v = strtoll(e->as.number_literal.value, NULL, 10);
    if (v >= -2147483647LL - 1 && v <= 2147483647LL) {
      snprintf(out, outsz, "%lld", v);
      return 1;
    }
    return 0;
  }
  if (e->type == AST_BOOLEAN_LITERAL && (want == TY_INT || want == TY_BOOL)) {
    snprintf(out, outsz, "%d", e->as.boolean_literal.value);
    return 1;
  }
  if (e->type == AST_IDENTIFIER && e->sym) {
    Var *v = e->sym;
    if (v->kind == SYM_CONST && !v->const_is_str &&
        (want == TY_INT || want == TY_BOOL) && v->const_int >= -2147483647LL - 1 &&
        v->const_int <= 2147483647LL) {
      snprintf(out, outsz, "%lld", (long long)v->const_int);
      return 1;
    }
    if ((v->kind == SYM_LOCAL || v->kind == SYM_GLOBAL) &&
        dflt_ty(v->ty) == dflt_ty(want)) {
      snprintf(out, outsz, "%s", var_loc(v));
      return 1;
    }
    if ((v->kind == SYM_LOCAL || v->kind == SYM_GLOBAL) && P->native &&
        is_raw(dflt_ty(v->ty)) && is_raw(dflt_ty(want))) {
      snprintf(out, outsz, "%s", var_loc(v));
      return 1;
    }
  }
  if (e->type == AST_STRING_LITERAL && want == TY_DYN && !P->native) {
    int id = str_const(e->as.string_literal.value, e->as.string_literal.length, 1);
    snprintf(out, outsz, "qword ptr [rip + .LSV%d]", id);
    return 1;
  }
  return 0;
}

// Load `e` (converted to `want`) into register `reg`.
static void load_into(const char *reg, ASTNode *e, int want) {
  char op[320];
  if (operand_of(e, want, op, sizeof(op))) {
    E("mov %s, %s", reg, op);
    return;
  }
  gen_as(e, want);
  if (strcmp(reg, "rax") != 0)
    E("mov %s, rax", reg);
}

// ------------------------------------------------------------
// Calls
// ------------------------------------------------------------

// Generic call: args[i] converted to tys[i]; callee is a label.
// If `fnreg_expr` is set, the callee address is evaluated (native call_ptr).
static void emit_call(const char *label, ASTNode **args, int n, const int *tys,
                      ASTNode *fnreg_expr) {
  int R = n < NREGARGS() ? n : NREGARGS();
  int S = n - R;
  int simple = !fnreg_expr && S == 0;
  char op[320];
  for (int i = 0; i < n && simple; i++)
    if (!operand_of(args[i], tys[i], op, sizeof(op)))
      simple = 0;
  if (simple) {
    for (int i = 0; i < n; i++)
      load_into(ARG(i), args[i], tys[i]);
    call_aligned(label);
    return;
  }
  if (!fnreg_expr && S == 0) {
    // Register-only call: evaluate complex args first (pushing all but the
    // last one), then load the side-effect-free simple ones directly.
    int last = -1;
    for (int i = 0; i < n; i++)
      if (!operand_of(args[i], tys[i], op, sizeof(op)))
        last = i;
    for (int i = 0; i < n; i++) {
      if (operand_of(args[i], tys[i], op, sizeof(op)))
        continue;
      gen_as(args[i], tys[i]);
      if (i != last)
        PUSH("rax");
    }
    E("mov %s, rax", ARG(last));
    for (int i = last - 1; i >= 0; i--)
      if (!operand_of(args[i], tys[i], op, sizeof(op)))
        POP(ARG(i));
    for (int i = 0; i < n; i++)
      if (operand_of(args[i], tys[i], op, sizeof(op)))
        E("mov %s, %s", ARG(i), op);
    call_aligned(label);
    return;
  }

  for (int i = 0; i < n; i++) {
    gen_as(args[i], tys[i]);
    PUSH("rax");
  }
  if (fnreg_expr) {
    gen_as(fnreg_expr, TY_INT);
    PUSH("rax");
  }
  int extra = fnreg_expr ? 1 : 0; // callee address on top of the args
  int shadow = P->win64 ? 32 : 0;
  int pad = ((depth + S) & 1) ? 1 : 0;
  int area = shadow + 8 * (S + pad);
  if (area)
    E("sub rsp, %d", area);
  for (int j = 0; j < S; j++) {
    int k = R + j;
    E("mov rax, [rsp + %d]", area + 8 * extra + 8 * (n - 1 - k));
    E("mov [rsp + %d], rax", shadow + 8 * j);
  }
  if (fnreg_expr)
    E("mov r11, [rsp + %d]", area);
  for (int i = 0; i < R; i++)
    E("mov %s, [rsp + %d]", ARG(i), area + 8 * extra + 8 * (n - 1 - i));
  if (fnreg_expr)
    E("call r11");
  else
    E("call %s", label);
  E("add rsp, %d", area + 8 * (n + extra));
  depth -= n + extra;
}

// Calls a runtime function with all-DYN arguments.
static void call_rt_args(const char *fn, ASTNode **args, int n) {
  int tys[16];
  for (int i = 0; i < n && i < 16; i++)
    tys[i] = TY_DYN;
  emit_call(fn, args, n, tys, NULL);
}

// Push all args (as `want`) then call fn(prefix..., argc, argv_reversed).
// `npre` registers ARG(0..npre-1) must be loaded by `pre` callback values
// which are already pushed below the args (pre_on_stack).
static void push_args(ASTNode **args, int n, int want) {
  for (int i = 0; i < n; i++) {
    gen_as(args[i], want);
    PUSH("rax");
  }
}

// ------------------------------------------------------------
// Expressions
// ------------------------------------------------------------

static void load_null(void) {
  if (P->native)
    E("xor eax, eax");
  else
    E("lea rax, [rip + stola_null_value]");
}

static void gen_string_literal(ASTNode *e) {
  if (P->native) {
    int id = str_const(e->as.string_literal.value, e->as.string_literal.length, 0);
    E("lea rax, [rip + .LS%d]", id);
  } else {
    int id = str_const(e->as.string_literal.value, e->as.string_literal.length, 1);
    E("mov rax, qword ptr [rip + .LSV%d]", id);
  }
}

static void gen_int_literal(ASTNode *e, int want) {
  const char *s = e->as.number_literal.value;
  long long v = s[0] == '-' ? strtoll(s, NULL, 10) : (long long)strtoull(s, NULL, 10);
  if (want == TY_DYN && !P->native) {
    int id = num_const(v, 0);
    E("mov rax, qword ptr [rip + .LIV%d]", id);
    return;
  }
  if (v == 0)
    E("xor eax, eax");
  else
    E("mov rax, %lld", v);
}

static void gen_function_value(FuncInfo *f) {
  // Functions used as values: boxed function object (wrapper if typed)
  int user_params = f->nparams - (f->is_method ? 1 : 0) - (f->is_lambda ? 1 : 0);
  char lbl[300];
  snprintf(lbl, sizeof(lbl), "%s%s", f->needs_wrapper ? "W" : "", f->label);
  E("lea %s, [rip + %s]", ARG(0), lbl);
  E("mov %s, %d", ARG(1), user_params);
  E("lea %s, [rip + .LS%d]", ARG(2), cstr(f->name));
  call_aligned("stola_new_function");
}

static void gen_identifier(ASTNode *e) {
  Var *v = e->sym;
  if (!v) {
    cg_error(e, "unresolved name '%s'", e->as.identifier.value);
    return;
  }
  switch (v->kind) {
  case SYM_LOCAL:
  case SYM_GLOBAL:
    E("mov rax, %s", var_loc(v));
    break;
  case SYM_CONST:
    if (v->const_is_str)
      gen_string_literal(v->const_str);
    else if (v->const_int == 0)
      E("xor eax, eax");
    else
      E("mov rax, %lld", (long long)v->const_int);
    break;
  case SYM_STATIC:
    E("lea rax, [rip + %s]", global_sym(v));
    break;
  case SYM_FUNC:
    if (P->native)
      E("lea rax, [rip + %s]", v->func->label);
    else
      gen_function_value(v->func);
    break;
  default:
    cg_error(e, "'%s' cannot be used as a value", v->name);
    break;
  }
}

static const char *dyn_op_func(TokenType op) {
  switch (op) {
  case TOKEN_PLUS: return "stola_add";
  case TOKEN_MINUS: return "stola_sub";
  case TOKEN_TIMES: return "stola_mul";
  case TOKEN_DIVIDED_BY: return "stola_div";
  case TOKEN_MODULO: return "stola_mod";
  case TOKEN_POWER: return "stola_pow";
  case TOKEN_BIT_AND: return "stola_band";
  case TOKEN_BIT_OR: return "stola_bor";
  case TOKEN_BIT_XOR: return "stola_bxor";
  case TOKEN_SHL: return "stola_shl";
  case TOKEN_SHR: return "stola_shr";
  default: return "stola_add";
  }
}

static int cmp_code(TokenType op) {
  switch (op) {
  case TOKEN_EQUALS: return 0;
  case TOKEN_NOT_EQUALS: return 1;
  case TOKEN_LESS_THAN: return 2;
  case TOKEN_GREATER_THAN: return 3;
  case TOKEN_LESS_OR_EQUALS: return 4;
  case TOKEN_GREATER_OR_EQUALS: return 5;
  default: return 0;
  }
}

static int is_compare(TokenType op) {
  return op == TOKEN_EQUALS || op == TOKEN_NOT_EQUALS ||
         op == TOKEN_LESS_THAN || op == TOKEN_GREATER_THAN ||
         op == TOKEN_LESS_OR_EQUALS || op == TOKEN_GREATER_OR_EQUALS;
}

// condition codes (signed) for a comparison
static const char *cc_for(TokenType op, int negate) {
  switch (op) {
  case TOKEN_EQUALS: return negate ? "ne" : "e";
  case TOKEN_NOT_EQUALS: return negate ? "e" : "ne";
  case TOKEN_LESS_THAN: return negate ? "ge" : "l";
  case TOKEN_GREATER_THAN: return negate ? "le" : "g";
  case TOKEN_LESS_OR_EQUALS: return negate ? "g" : "le";
  case TOKEN_GREATER_OR_EQUALS: return negate ? "l" : "ge";
  default: return "e";
  }
}

static int raw_operands(ASTNode *a, ASTNode *b) {
  int ta = dflt_ty(a->ty), tb = dflt_ty(b->ty);
  return is_raw(ta) && is_raw(tb);
}

// Emits `cmp rax, <right>` with left/right converted to INT.
static void gen_int_compare(ASTNode *l, ASTNode *r) {
  char op[320], lop[320];
  if (operand_of(l, TY_INT, lop, sizeof(lop)) && operand_of(r, TY_INT, op, sizeof(op))) {
    int l_imm = (lop[0] >= '0' && lop[0] <= '9') || lop[0] == '-';
    int l_mem = strstr(lop, "ptr") != NULL, r_mem = strstr(op, "ptr") != NULL;
    if (!l_imm && !(l_mem && r_mem)) {
      E("cmp %s, %s", lop, op);
      return;
    }
  }
  gen_as(l, TY_INT);
  if (operand_of(r, TY_INT, op, sizeof(op))) {
    E("cmp rax, %s", op);
  } else {
    PUSH("rax");
    gen_as(r, TY_INT);
    E("mov rcx, rax");
    POP("rax");
    E("cmp rax, rcx");
  }
}

// dynamic comparison → eax = 0/1
static void gen_dyn_compare(ASTNode *l, ASTNode *r, int code) {
  gen_as(l, TY_DYN);
  PUSH("rax");
  gen_as(r, TY_DYN);
  E("mov %s, rax", ARG(1));
  POP(ARG(0));
  E("mov %s, %d", ARG(2), code);
  call_aligned("stola_compare");
  E("movzx eax, al");
}

// log2 of a positive power-of-two integer constant, or -1
static int pow2_const(ASTNode *e) {
  long long v;
  if (e && e->type == AST_NUMBER_LITERAL)
    v = strtoll(e->as.number_literal.value, NULL, 10);
  else if (e && e->type == AST_IDENTIFIER && e->sym &&
           ((Var *)e->sym)->kind == SYM_CONST && !((Var *)e->sym)->const_is_str)
    v = ((Var *)e->sym)->const_int;
  else
    return -1;
  if (v <= 0 || (v & (v - 1)) != 0)
    return -1;
  int k = 0;
  while ((1LL << k) < v)
    k++;
  return k;
}

static void gen_int_binop(TokenType op, ASTNode *l, ASTNode *r) {
  char opnd[320];
  int simple = operand_of(r, TY_INT, opnd, sizeof(opnd));
  int k = pow2_const(r);
  if (k >= 0 && k < 63 &&
      (op == TOKEN_DIVIDED_BY || op == TOKEN_MODULO || op == TOKEN_TIMES)) {
    // strength reduction for powers of two (signed, truncating semantics)
    gen_as(l, TY_INT);
    if (op == TOKEN_TIMES) {
      if (k)
        E("shl rax, %d", k);
      return;
    }
    if (k == 0) {
      if (op == TOKEN_MODULO)
        E("xor eax, eax");
      return;
    }
    E("mov rcx, rax");
    E("sar rcx, 63");
    E("shr rcx, %d", 64 - k);   // bias = 2^k - 1 for negative values
    if (op == TOKEN_DIVIDED_BY) {
      E("add rax, rcx");
      E("sar rax, %d", k);
    } else {
      E("lea rdx, [rax + rcx]");
      E("and rdx, %lld", -(1LL << k));
      E("sub rax, rdx");
    }
    return;
  }
  gen_as(l, TY_INT);
  switch (op) {
  case TOKEN_PLUS:
  case TOKEN_MINUS:
  case TOKEN_BIT_AND:
  case TOKEN_BIT_OR:
  case TOKEN_BIT_XOR: {
    const char *ins = op == TOKEN_PLUS    ? "add"
                      : op == TOKEN_MINUS ? "sub"
                      : op == TOKEN_BIT_AND ? "and"
                      : op == TOKEN_BIT_OR  ? "or"
                                            : "xor";
    if (simple) {
      E("%s rax, %s", ins, opnd);
    } else {
      PUSH("rax");
      gen_as(r, TY_INT);
      E("mov rcx, rax");
      POP("rax");
      E("%s rax, rcx", ins);
    }
    return;
  }
  case TOKEN_TIMES:
    if (simple && ((opnd[0] >= '0' && opnd[0] <= '9') || opnd[0] == '-')) {
      E("imul rax, rax, %s", opnd); // immediate
    } else if (simple) {
      E("imul rax, %s", opnd); // register or memory
    } else {
      PUSH("rax");
      gen_as(r, TY_INT);
      E("mov rcx, rax");
      POP("rax");
      E("imul rax, rcx");
    }
    return;
  default:
    break;
  }
  // ops needing the right operand in rcx
  if (simple) {
    E("mov rcx, %s", opnd);
  } else {
    PUSH("rax");
    gen_as(r, TY_INT);
    E("mov rcx, rax");
    POP("rax");
  }
  switch (op) {
  case TOKEN_DIVIDED_BY:
  case TOKEN_MODULO:
    if (!P->native) {
      int ok = new_label();
      E("test rcx, rcx");
      E("jnz .L%d", ok);
      call_aligned("stola_raise_zero_division");
      LABEL(ok);
    }
    E("cqo");
    E("idiv rcx");
    if (op == TOKEN_MODULO)
      E("mov rax, rdx");
    break;
  case TOKEN_SHL:
    E("shl rax, cl");
    break;
  case TOKEN_SHR:
    E("sar rax, cl");
    break;
  case TOKEN_POWER: {
    int top = new_label(), done = new_label();
    E("mov rdx, rax");
    E("mov eax, 1");
    LABEL(top);
    E("test rcx, rcx");
    E("jle .L%d", done);
    E("imul rax, rdx");
    E("dec rcx");
    E("jmp .L%d", top);
    LABEL(done);
    break;
  }
  default:
    break;
  }
}

static void gen_binary(ASTNode *e) {
  TokenType op = e->as.binary_op.op;
  ASTNode *l = e->as.binary_op.left, *r = e->as.binary_op.right;
  int ty = dflt_ty(e->ty);

  if (op == TOKEN_AND || op == TOKEN_OR) {
    if (ty == TY_BOOL) {
      int f = new_label(), end = new_label();
      gen_branch(e, f, 0);
      E("mov eax, 1");
      E("jmp .L%d", end);
      LABEL(f);
      E("xor eax, eax");
      LABEL(end);
      return;
    }
    // value semantics: a or b → a if truthy(a) else b
    int keep = new_label(), end = new_label();
    int slot = alloc_slot();
    gen_as(l, ty);
    E("mov qword ptr [rbp - %d], rax", slot_offset(slot));
    if (ty == TY_DYN) {
      E("mov %s, rax", ARG(0));
      call_aligned("stola_is_truthy");
      E("test eax, eax");
    } else {
      E("test rax, rax");
    }
    E("%s .L%d", op == TOKEN_OR ? "jnz" : "jz", keep);
    gen_as(r, ty);
    E("jmp .L%d", end);
    LABEL(keep);
    E("mov rax, qword ptr [rbp - %d]", slot_offset(slot));
    LABEL(end);
    return;
  }

  if (is_compare(op)) {
    if (raw_operands(l, r)) {
      gen_int_compare(l, r);
      E("set%s al", cc_for(op, 0));
      E("movzx eax, al");
    } else {
      gen_dyn_compare(l, r, cmp_code(op));
    }
    return;
  }

  if (op == TOKEN_IN || op == TOKEN_NOT_IN) {
    // item in container  →  stola_contains(container, item)
    gen_as(r, TY_DYN);
    PUSH("rax");
    gen_as(l, TY_DYN);
    E("mov %s, rax", ARG(1));
    POP(ARG(0));
    call_aligned("stola_contains");
    E("movzx eax, al");
    if (op == TOKEN_NOT_IN)
      E("xor eax, 1");
    return;
  }

  if (ty == TY_INT || P->native) {
    gen_int_binop(op, l, r);
    return;
  }

  // dynamic arithmetic
  gen_as(l, TY_DYN);
  PUSH("rax");
  gen_as(r, TY_DYN);
  E("mov %s, rax", ARG(1));
  POP(ARG(0));
  call_aligned(dyn_op_func(op));
}

static void gen_unary(ASTNode *e) {
  ASTNode *r = e->as.unary_op.right;
  int ty = dflt_ty(e->ty);
  switch (e->as.unary_op.op) {
  case TOKEN_NOT:
    gen_as(r, TY_BOOL);
    E("xor eax, 1");
    break;
  case TOKEN_MINUS:
    if (ty == TY_INT || P->native) {
      gen_as(r, TY_INT);
      E("neg rax");
    } else {
      gen_as(r, TY_DYN);
      E("mov %s, rax", ARG(0));
      call_aligned("stola_neg");
    }
    break;
  case TOKEN_BIT_NOT:
    if (ty == TY_INT || P->native) {
      gen_as(r, TY_INT);
      E("not rax");
    } else {
      gen_as(r, TY_DYN);
      E("mov %s, rax", ARG(0));
      call_aligned("stola_bnot");
    }
    break;
  default:
    gen(r);
    break;
  }
}

// --- Native inline builtins ---------------------------------------------

static void gen_native_builtin(ASTNode *e, const char *name) {
  ASTNode **a = e->as.call_expr.args;
  int n = e->as.call_expr.arg_count;

#define ARG1_TO_RAX() gen_as(a[0], TY_INT)
#define TWO_ARGS()                                                             \
  do {                                                                         \
    gen_as(a[0], TY_INT);                                                      \
    PUSH("rax");                                                               \
    gen_as(a[1], TY_INT);                                                      \
    E("mov rcx, rax");                                                         \
    POP("rax");                                                                \
  } while (0)

  if (!strcmp(name, "load8")) { ARG1_TO_RAX(); E("movzx eax, byte ptr [rax]"); }
  else if (!strcmp(name, "load16")) { ARG1_TO_RAX(); E("movzx eax, word ptr [rax]"); }
  else if (!strcmp(name, "load32")) { ARG1_TO_RAX(); E("mov eax, dword ptr [rax]"); }
  else if (!strcmp(name, "load64") || !strcmp(name, "memory_read")) {
    ARG1_TO_RAX(); E("mov rax, qword ptr [rax]");
  } else if (!strcmp(name, "store8") || !strcmp(name, "memory_write_byte")) {
    TWO_ARGS(); E("mov byte ptr [rax], cl"); E("mov rax, rcx");
  } else if (!strcmp(name, "store16")) {
    TWO_ARGS(); E("mov word ptr [rax], cx"); E("mov rax, rcx");
  } else if (!strcmp(name, "store32")) {
    TWO_ARGS(); E("mov dword ptr [rax], ecx"); E("mov rax, rcx");
  } else if (!strcmp(name, "store64") || !strcmp(name, "memory_write")) {
    TWO_ARGS(); E("mov qword ptr [rax], rcx"); E("mov rax, rcx");
  } else if (!strcmp(name, "outb") || !strcmp(name, "outw") || !strcmp(name, "outl")) {
    TWO_ARGS();
    E("mov rdx, rax");
    E("mov rax, rcx");
    E(name[3] == 'b' ? "out dx, al" : name[3] == 'w' ? "out dx, ax" : "out dx, eax");
  } else if (!strcmp(name, "inb") || !strcmp(name, "inw") || !strcmp(name, "inl")) {
    ARG1_TO_RAX();
    E("mov rdx, rax");
    E("xor eax, eax");
    E(name[2] == 'b' ? "in al, dx" : name[2] == 'w' ? "in ax, dx" : "in eax, dx");
  } else if (!strcmp(name, "cli") || !strcmp(name, "sti") ||
             !strcmp(name, "hlt") || !strcmp(name, "pause")) {
    E("%s", name);
    E("xor eax, eax");
  } else if (!strcmp(name, "unreachable")) {
    E("ud2");
  } else if (!strcmp(name, "lidt") || !strcmp(name, "lgdt")) {
    ARG1_TO_RAX(); E("%s [rax]", name);
  } else if (!strcmp(name, "ltr")) {
    ARG1_TO_RAX(); E("ltr ax");
  } else if (!strcmp(name, "invlpg")) {
    ARG1_TO_RAX(); E("invlpg [rax]");
  } else if (!strcmp(name, "rdtsc")) {
    E("rdtsc"); E("shl rdx, 32"); E("or rax, rdx");
  } else if (!strncmp(name, "read_cr", 7)) {
    E("mov rax, cr%c", name[7]);
  } else if (!strncmp(name, "write_cr", 8)) {
    ARG1_TO_RAX(); E("mov cr%c, rax", name[8]);
  } else if (!strcmp(name, "rdmsr")) {
    ARG1_TO_RAX(); E("mov ecx, eax"); E("rdmsr"); E("shl rdx, 32"); E("or rax, rdx");
  } else if (!strcmp(name, "wrmsr")) {
    TWO_ARGS(); // rax = msr, rcx = value
    E("mov rdx, rcx"); E("xchg rax, rcx"); // ecx = msr, rax = value
    E("shr rdx, 32"); E("wrmsr");
  } else if (!strcmp(name, "read_rsp")) {
    E("mov rax, rsp");
  } else if (!strcmp(name, "read_rflags")) {
    E("pushfq"); E("pop rax");
  } else if (!strcmp(name, "mem_copy") || !strcmp(name, "mem_set")) {
    gen_as(a[0], TY_INT); PUSH("rax");
    gen_as(a[1], TY_INT); PUSH("rax");
    gen_as(a[2], TY_INT);
    E("mov rcx, rax");
    if (!strcmp(name, "mem_copy")) {
      POP("rsi"); POP("rdi");
      E("mov rax, rdi");
      E("cld"); E("rep movsb");
    } else {
      POP("rax"); POP("rdi");
      E("mov rdx, rdi");
      E("cld"); E("rep stosb");
      E("mov rax, rdx");
    }
  } else if (!strcmp(name, "addr_of")) {
    Var *v = a[0]->sym;
    if (!v) { cg_error(e, "addr_of: unknown name"); return; }
    if (v->kind == SYM_FUNC) E("lea rax, [rip + %s]", v->func->label);
    else if (v->kind == SYM_STATIC || v->kind == SYM_GLOBAL)
      E("lea rax, [rip + %s]", global_sym(v));
    else if (v->kind == SYM_LOCAL && !v->reg)
      E("lea rax, [rbp - %d]", slot_offset(v->slot));
    else cg_error(e, "addr_of: cannot take the address of '%s'", v->name);
  } else if (!strcmp(name, "syscall")) {
    static const char *sregs[] = {"rax", "rdi", "rsi", "rdx", "r10", "r8", "r9"};
    for (int i = 0; i < n; i++) { gen_as(a[i], TY_INT); PUSH("rax"); }
    for (int i = n - 1; i >= 0; i--) POP(sregs[i]);
    E("syscall");
  } else if (!strcmp(name, "call_ptr")) {
    int tys[8];
    for (int i = 0; i < n - 1; i++) tys[i] = TY_INT;
    emit_call(NULL, a + 1, n - 1, tys, a[0]);
  } else if (!strcmp(name, "multiboot_info")) {
    E("mov rax, qword ptr [rip + stola_mb_info]");
  } else if (!strcmp(name, "multiboot_magic")) {
    E("mov rax, qword ptr [rip + stola_mb_magic]");
  } else if (!strcmp(name, "as_str")) {
    ARG1_TO_RAX();
  } else if (!strcmp(name, "argc")) {
    E("mov rax, qword ptr [rip + stola_argc]");
  } else if (!strcmp(name, "argv")) {
    ARG1_TO_RAX();
    E("mov rcx, qword ptr [rip + stola_argv]");
    E("mov rax, qword ptr [rcx + rax*8]");
  } else if (!strcmp(name, "print")) {
    // native print → user-level print_str / print_int / print_char
    const char *need[] = {"print_str", "print_int", "print_char"};
    FuncInfo *fns[3] = {0};
    for (int k = 0; k < 3; k++) {
      Var *v = sem_lookup(P, NULL, need[k]);
      if (!v || v->kind != SYM_FUNC) {
        cg_error(e, "print() in native targets needs a function '%s' "
                    "(import a console module, e.g. `import linux`)", need[k]);
        return;
      }
      fns[k] = v->func;
    }
    ASTNode sp = {0};
    sp.type = AST_NUMBER_LITERAL;
    sp.ty = TY_INT;
    for (int i = 0; i < n; i++) {
      if (i > 0) {
        char lit[] = "32";
        sp.as.number_literal.value = lit;
        ASTNode *pa = &sp;
        int t = TY_INT;
        emit_call(fns[2]->label, &pa, 1, &t, NULL);
      }
      int at = dflt_ty(a[i]->ty);
      if (at == TY_BOOL) {
        gen_as(a[i], TY_BOOL);
        E("lea rcx, [rip + .LS%d]", cstr("false"));
        E("lea %s, [rip + .LS%d]", ARG(0), cstr("true"));
        E("test eax, eax");
        E("cmovz %s, rcx", ARG(0));
        call_aligned(fns[0]->label);
        continue;
      }
      int t = at == TY_STR ? TY_STR : TY_INT;
      emit_call(fns[t == TY_STR ? 0 : 1]->label, &a[i], 1, &t, NULL);
    }
    char nl[] = "10";
    sp.as.number_literal.value = nl;
    ASTNode *pa = &sp;
    int t = TY_INT;
    emit_call(fns[2]->label, &pa, 1, &t, NULL);
    E("xor eax, eax");
  } else {
    cg_error(e, "builtin '%s' is not supported in this target", name);
  }
#undef ARG1_TO_RAX
#undef TWO_ARGS
}

// --- Hosted calls --------------------------------------------------------

static void gen_struct_ctor(ClassInfo *cls, ASTNode **args, int n) {
  E("lea %s, [rip + .LS%d]", ARG(0), cstr(cls->name));
  call_aligned("stola_new_object");
  PUSH("rax");
  for (int i = 0; i < cls->nfields; i++) {
    if (i < n)
      gen_as(args[i], TY_DYN);
    else
      load_null();
    E("mov %s, rax", ARG(2));
    E("mov %s, [rsp]", ARG(0));
    E("lea %s, [rip + .LS%d]", ARG(1), cstr(cls->fields[i]));
    call_aligned("stola_struct_set");
  }
  POP("rax");
}

// Direct call to a known function with parameter coercion and defaults.
static void gen_direct_call(FuncInfo *f, ASTNode *self_value_on_stack,
                            ASTNode **args, int n, ASTNode *where) {
  FunctionDeclNode *fd = &f->decl->as.function_decl;
  int hidden = f->is_method ? 1 : 0;
  int total = hidden + fd->param_count;
  ASTNode **all = calloc(total + 1, sizeof(ASTNode *));
  int *tys = calloc(total + 1, sizeof(int));
  ASTNode self_node = {0};
  if (hidden) {
    // `this` comes from a pre-evaluated value stored in a temp slot
    all[0] = self_value_on_stack;
    tys[0] = TY_DYN;
  }
  for (int i = 0; i < fd->param_count; i++) {
    ASTNode *arg = i < n ? args[i]
                         : (fd->param_defaults ? fd->param_defaults[i] : NULL);
    if (!arg) {
      cg_error(where, "missing argument %d for '%s'", i + 1, f->name);
      arg = &self_node;
      self_node.type = AST_NULL_LITERAL;
    }
    all[hidden + i] = arg;
    tys[hidden + i] = dflt_ty(f->params[hidden + i]->ty);
  }
  emit_call(f->label, all, total, tys, NULL);
  free(all);
  free(tys);
}

// Creates an AST identifier node bound to a temp slot holding a DYN value.
static ASTNode *temp_ident(Var *tmpvar) {
  ASTNode *id = ast_new(AST_IDENTIFIER, 0);
  id->as.identifier.value = tmpvar->name;
  id->sym = tmpvar;
  id->ty = tmpvar->ty;
  return id;
}

static Var *new_temp_var(int ty) {
  Var *v = calloc(1, sizeof(Var));
  v->name = "__tmp";
  v->kind = SYM_LOCAL;
  v->ty = ty;
  v->slot = alloc_slot();
  v->capture_index = -1;
  return v;
}

static void gen_new_object(ClassInfo *cls, ASTNode **args, int n, ASTNode *where) {
  if (cls->is_struct) {
    gen_struct_ctor(cls, args, n);
    return;
  }
  E("lea %s, [rip + .LS%d]", ARG(0), cstr(cls->name));
  call_aligned("stola_new_object");
  FuncInfo *init = sem_find_method(cls, "init");
  if (!init) {
    if (n > 0)
      cg_error(where, "class '%s' has no init() but arguments were given",
               cls->name);
    return;
  }
  Var *tmp = new_temp_var(TY_DYN);
  E("mov %s, rax", var_loc(tmp));
  ASTNode *self = temp_ident(tmp);
  FunctionDeclNode *fd = &init->decl->as.function_decl;
  int required = 0;
  for (int i = 0; i < fd->param_count; i++)
    if (!fd->param_defaults || !fd->param_defaults[i])
      required = i + 1;
  if (n < required || n > fd->param_count)
    cg_error(where, "%s.init expects %d argument(s), got %d", cls->name,
             fd->param_count, n);
  else
    gen_direct_call(init, self, args, n, where);
  E("mov rax, %s", var_loc(tmp));
}

static void gen_method_call(ASTNode *e) {
  ASTNode *callee = e->as.call_expr.function;
  ASTNode *obj = callee->as.member_access.object;
  const char *mname = callee->as.member_access.property->as.identifier.value;
  ASTNode **args = e->as.call_expr.args;
  int n = e->as.call_expr.arg_count;

  if (obj->type == AST_SUPER) {
    ClassInfo *parent = CF->cls ? CF->cls->parent : NULL;
    FuncInfo *m = parent ? sem_find_method(parent, mname) : NULL;
    if (!m) {
      cg_error(e, "parent class has no method '%s'", mname);
      return;
    }
    Var *self = sem_lookup(P, CF, "this");
    gen_direct_call(m, temp_ident(self), args, n, e);
    convert(m->ret_ty, TY_DYN, e);
    return;
  }

  gen_as(obj, TY_DYN);
  PUSH("rax");
  push_args(args, n, TY_DYN);
  E("mov %s, [rsp + %d]", ARG(0), 8 * n);
  E("lea %s, [rip + .LS%d]", ARG(1), cstr(mname));
  E("mov %s, %d", ARG(2), n);
  E("mov %s, rsp", ARG(3));
  call_aligned("stola_call_method");
  E("add rsp, %d", 8 * (n + 1));
  depth -= n + 1;
}

static void gen_value_call(ASTNode *callee, ASTNode **args, int n) {
  gen_as(callee, TY_DYN);
  PUSH("rax");
  push_args(args, n, TY_DYN);
  E("mov %s, [rsp + %d]", ARG(0), 8 * n);
  E("mov %s, %d", ARG(1), n);
  E("mov %s, rsp", ARG(2));
  call_aligned("stola_call_value");
  E("add rsp, %d", 8 * (n + 1));
  depth -= n + 1;
}

static void gen_call(ASTNode *e) {
  ASTNode *callee = e->as.call_expr.function;
  ASTNode **args = e->as.call_expr.args;
  int n = e->as.call_expr.arg_count;

  if (callee->type == AST_MEMBER_ACCESS && !callee->as.member_access.is_computed) {
    gen_method_call(e);
    return;
  }
  if (callee->type != AST_IDENTIFIER) {
    gen_value_call(callee, args, n);
    return;
  }
  const char *name = callee->as.identifier.value;
  Var *v = callee->sym;
  if (v) {
    switch (v->kind) {
    case SYM_FUNC:
      gen_direct_call(v->func, NULL, args, n, e);
      return;
    case SYM_STRUCT:
    case SYM_CLASS:
      gen_new_object(v->cls, args, n, e);
      return;
    case SYM_CFUNC:
      push_args(args, n, TY_DYN);
      E("lea %s, [rip + .LS%d]", ARG(0), cstr(name));
      E("mov %s, %d", ARG(1), n);
      E("mov %s, rsp", ARG(2));
      call_aligned("stola_invoke_c_function");
      if (n) {
        E("add rsp, %d", 8 * n);
        depth -= n;
      }
      return;
    default:
      gen_value_call(callee, args, n);
      return;
    }
  }
  const BuiltinInfo *b = builtin_lookup(name, P->native);
  if (!b) {
    cg_error(e, "undefined function '%s'", name);
    return;
  }
  if (P->native) {
    gen_native_builtin(e, name);
    return;
  }
  if (b->variadic) {
    push_args(args, n, TY_DYN);
    E("mov %s, %d", ARG(0), n);
    E("mov %s, rsp", ARG(1));
    call_aligned(b->cname);
    if (n) {
      E("add rsp, %d", 8 * n);
      depth -= n;
    }
  } else {
    call_rt_args(b->cname, args, n);
  }
  if (b->ret == RK_VOID)
    load_null();
  else if (b->ret == RK_BOOL)
    E("movzx eax, al");
}

static void gen_lambda(ASTNode *e) {
  FuncInfo *lf = e->sym;
  if (!lf) {
    cg_error(e, "internal: unregistered lambda");
    return;
  }
  int user_params = lf->nparams - 1;
  char lbl[300];
  snprintf(lbl, sizeof(lbl), "%s%s", lf->needs_wrapper ? "W" : "", lf->label);
  E("lea %s, [rip + %s]", ARG(0), lbl);
  E("mov %s, %d", ARG(1), user_params);
  E("mov %s, %d", ARG(2), lf->ncaptures);
  call_aligned("stola_new_closure");
  if (lf->ncaptures == 0)
    return;
  PUSH("rax");
  for (int i = 0; i < lf->ncaptures; i++) {
    Var *outer = sem_lookup(P, CF, lf->captures[i]->name);
    if (!outer) {
      cg_error(e, "internal: capture '%s' not found", lf->captures[i]->name);
      continue;
    }
    E("mov rax, %s", var_loc(outer));
    convert(outer->ty, TY_DYN, e);
    E("mov %s, rax", ARG(2));
    E("mov %s, [rsp]", ARG(0));
    E("mov %s, %d", ARG(1), i);
    call_aligned("stola_closure_set");
  }
  POP("rax");
}

static void gen(ASTNode *e) {
  if (!e) {
    load_null();
    return;
  }
  switch (e->type) {
  case AST_NUMBER_LITERAL:
    gen_int_literal(e, TY_INT);
    break;
  case AST_FLOAT_LITERAL: {
    double d = strtod(e->as.number_literal.value, NULL);
    long long bits;
    memcpy(&bits, &d, 8);
    int id = num_const(bits, 1);
    E("mov rax, qword ptr [rip + .LFV%d]", id);
    break;
  }
  case AST_STRING_LITERAL:
    gen_string_literal(e);
    break;
  case AST_BOOLEAN_LITERAL:
    E("mov eax, %d", e->as.boolean_literal.value);
    break;
  case AST_NULL_LITERAL:
    load_null();
    break;
  case AST_IDENTIFIER:
    gen_identifier(e);
    break;
  case AST_THIS:
    if (e->sym)
      E("mov rax, %s", var_loc((Var *)e->sym));
    break;
  case AST_BINARY_OP:
    gen_binary(e);
    break;
  case AST_UNARY_OP:
    gen_unary(e);
    break;
  case AST_TERNARY: {
    int other = new_label(), end = new_label();
    int ty = dflt_ty(e->ty);
    gen_branch(e->as.ternary.cond, other, 0);
    gen_as(e->as.ternary.then_expr, ty);
    E("jmp .L%d", end);
    LABEL(other);
    gen_as(e->as.ternary.else_expr, ty);
    LABEL(end);
    break;
  }
  case AST_CALL_EXPR:
    gen_call(e);
    break;
  case AST_ARRAY_LITERAL: {
    int n = e->as.array_literal.element_count;
    E("mov %s, %d", ARG(0), n);
    call_aligned("stola_new_array_cap");
    if (n == 0)
      break;
    PUSH("rax");
    for (int i = 0; i < n; i++) {
      gen_as(e->as.array_literal.elements[i], TY_DYN);
      E("mov %s, rax", ARG(1));
      E("mov %s, [rsp]", ARG(0));
      call_aligned("stola_push");
    }
    POP("rax");
    break;
  }
  case AST_DICT_LITERAL: {
    call_aligned("stola_new_dict");
    PUSH("rax");
    for (int i = 0; i < e->as.dict_literal.pair_count; i++) {
      gen_as(e->as.dict_literal.keys[i], TY_DYN);
      PUSH("rax");
      gen_as(e->as.dict_literal.values[i], TY_DYN);
      E("mov %s, rax", ARG(2));
      POP(ARG(1));
      E("mov %s, [rsp]", ARG(0));
      call_aligned("stola_dict_set");
    }
    POP("rax");
    break;
  }
  case AST_MEMBER_ACCESS:
    if (e->as.member_access.is_computed) {
      gen_as(e->as.member_access.object, TY_DYN);
      PUSH("rax");
      gen_as(e->as.member_access.property, TY_DYN);
      E("mov %s, rax", ARG(1));
      POP(ARG(0));
      call_aligned("stola_getitem");
    } else {
      gen_as(e->as.member_access.object, TY_DYN);
      E("mov %s, rax", ARG(0));
      E("lea %s, [rip + .LS%d]", ARG(1),
        cstr(e->as.member_access.property->as.identifier.value));
      call_aligned("stola_struct_get");
    }
    break;
  case AST_SLICE: {
    gen_as(e->as.slice.object, TY_DYN);
    PUSH("rax");
    gen(e->as.slice.start ? e->as.slice.start : NULL);
    if (e->as.slice.start)
      convert(e->as.slice.start->ty, TY_DYN, e);
    PUSH("rax");
    gen(e->as.slice.end ? e->as.slice.end : NULL);
    if (e->as.slice.end)
      convert(e->as.slice.end->ty, TY_DYN, e);
    E("mov %s, rax", ARG(2));
    POP(ARG(1));
    POP(ARG(0));
    call_aligned("stola_slice");
    break;
  }
  case AST_NEW_EXPR: {
    Var *v = e->sym;
    if (!v) {
      cg_error(e, "unknown class");
      break;
    }
    gen_new_object(v->cls, e->as.new_expr.args, e->as.new_expr.arg_count, e);
    break;
  }
  case AST_LAMBDA:
    gen_lambda(e);
    break;
  default:
    cg_error(e, "unsupported expression");
    break;
  }
}

static void gen_as(ASTNode *e, int want) {
  want = dflt_ty(want);
  if (e && e->type == AST_NUMBER_LITERAL) {
    gen_int_literal(e, want);
    if (want == TY_BOOL)
      convert(TY_INT, TY_BOOL, e);
    return;
  }
  if (e && e->type == AST_IDENTIFIER && e->sym &&
      ((Var *)e->sym)->kind == SYM_CONST && !((Var *)e->sym)->const_is_str &&
      want == TY_DYN && !P->native) {
    int id = num_const(((Var *)e->sym)->const_int, 0);
    E("mov rax, qword ptr [rip + .LIV%d]", id);
    return;
  }
  gen(e);
  convert(e ? e->ty : TY_DYN, want, e);
}

// Conditional jump: jump to `label` iff truthiness(e) == jump_if.
static void gen_branch(ASTNode *e, int label, int jump_if) {
  if (!e)
    return;
  switch (e->type) {
  case AST_BOOLEAN_LITERAL:
    if (e->as.boolean_literal.value == jump_if)
      E("jmp .L%d", label);
    return;
  case AST_UNARY_OP:
    if (e->as.unary_op.op == TOKEN_NOT) {
      gen_branch(e->as.unary_op.right, label, !jump_if);
      return;
    }
    break;
  case AST_BINARY_OP: {
    TokenType op = e->as.binary_op.op;
    ASTNode *l = e->as.binary_op.left, *r = e->as.binary_op.right;
    if (op == TOKEN_AND) {
      if (!jump_if) {
        gen_branch(l, label, 0);
        gen_branch(r, label, 0);
      } else {
        int skip = new_label();
        gen_branch(l, skip, 0);
        gen_branch(r, label, 1);
        LABEL(skip);
      }
      return;
    }
    if (op == TOKEN_OR) {
      if (jump_if) {
        gen_branch(l, label, 1);
        gen_branch(r, label, 1);
      } else {
        int skip = new_label();
        gen_branch(l, skip, 1);
        gen_branch(r, label, 0);
        LABEL(skip);
      }
      return;
    }
    if ((op == TOKEN_EQUALS || op == TOKEN_NOT_EQUALS) && raw_operands(l, r) &&
        l->type == AST_BINARY_OP && l->as.binary_op.op == TOKEN_MODULO &&
        is_raw(dflt_ty(l->as.binary_op.left->ty)) &&
        pow2_const(l->as.binary_op.right) > 0 &&
        r->type == AST_NUMBER_LITERAL && strtoll(r->as.number_literal.value, NULL, 10) == 0) {
      // x % 2^k == 0  ⇔  (x & (2^k - 1)) == 0
      int k = pow2_const(l->as.binary_op.right);
      char opnd[320];
      if (operand_of(l->as.binary_op.left, TY_INT, opnd, sizeof(opnd)) &&
          !strstr(opnd, "ptr") && opnd[0] != '-' && (opnd[0] < '0' || opnd[0] > '9')) {
        E("test %s, %lld", opnd, (1LL << k) - 1);
      } else {
        gen_as(l->as.binary_op.left, TY_INT);
        E("test rax, %lld", (1LL << k) - 1);
      }
      int is_eq = op == TOKEN_EQUALS;
      E("%s .L%d", (is_eq == jump_if) ? "jz" : "jnz", label);
      return;
    }
    if (is_compare(op) && raw_operands(l, r)) {
      gen_int_compare(l, r);
      E("j%s .L%d", cc_for(op, !jump_if), label);
      return;
    }
    break;
  }
  default:
    break;
  }
  int ty = dflt_ty(e->ty);
  gen(e);
  if (ty == TY_DYN) {
    E("mov %s, rax", ARG(0));
    call_aligned("stola_is_truthy");
    E("test eax, eax");
  } else if (ty == TY_BOOL) {
    E("test eax, eax");
  } else {
    E("test rax, rax");
  }
  E("%s .L%d", jump_if ? "jnz" : "jz", label);
}

// ------------------------------------------------------------
// Statements
// ------------------------------------------------------------

static void store_var(Var *v) { E("mov %s, rax", var_loc(v)); }

static void gen_assign_ident(ASTNode *s) {
  ASTNode *t = s->as.assignment.target;
  Var *v = t->sym;
  if (!v) {
    cg_error(s, "cannot assign to '%s'", t->as.identifier.value);
    return;
  }
  int vty = dflt_ty(v->ty);
  ASTNode *val = s->as.assignment.value;
  if (!val) { // declaration only
    if (vty == TY_DYN)
      load_null();
    else
      E("xor eax, eax");
    store_var(v);
    return;
  }
  TokenType op = s->as.assignment.op;
  if (op == TOKEN_ASSIGN) {
    gen_as(val, vty);
    store_var(v);
    return;
  }
  // compound assignment
  if (is_raw(vty) && (P->native || vty == TY_INT)) {
    char opnd[320];
    const char *ins = op == TOKEN_PLUS      ? "add"
                      : op == TOKEN_MINUS   ? "sub"
                      : op == TOKEN_BIT_AND ? "and"
                      : op == TOKEN_BIT_OR  ? "or"
                      : op == TOKEN_BIT_XOR ? "xor"
                                            : NULL;
    if (ins && operand_of(val, TY_INT, opnd, sizeof(opnd)) &&
        !(var_in_memory(v) && strstr(opnd, "ptr"))) {
      if (!strcmp(opnd, "1") && (op == TOKEN_PLUS || op == TOKEN_MINUS))
        E("%s %s", op == TOKEN_PLUS ? "inc" : "dec", var_loc(v));
      else
        E("%s %s, %s", ins, var_loc(v), opnd);
      return;
    }
  }
  ASTNode bin = {0};
  bin.type = AST_BINARY_OP;
  bin.line = s->line;
  bin.file = s->file;
  bin.as.binary_op.op = op;
  bin.as.binary_op.left = t;
  bin.as.binary_op.right = val;
  int vt = dflt_ty(val->ty);
  int lt = vty;
  if (P->native)
    bin.ty = TY_INT;
  else if ((lt == TY_INT || lt == TY_BOOL) && (vt == TY_INT || vt == TY_BOOL))
    bin.ty = TY_INT;
  else
    bin.ty = TY_DYN;
  gen_as(&bin, vty);
  store_var(v);
}

static void gen_assign_member(ASTNode *s) {
  ASTNode *t = s->as.assignment.target;
  ASTNode *val = s->as.assignment.value;
  TokenType op = s->as.assignment.op;
  int computed = t->as.member_access.is_computed;
  ASTNode *obj = t->as.member_access.object;

  gen_as(obj, TY_DYN);
  PUSH("rax");
  if (computed) {
    gen_as(t->as.member_access.property, TY_DYN);
    PUSH("rax");
  }
  if (op != TOKEN_ASSIGN) {
    // current value
    if (computed) {
      E("mov %s, [rsp + 8]", ARG(0));
      E("mov %s, [rsp]", ARG(1));
      call_aligned("stola_getitem");
    } else {
      E("mov %s, [rsp]", ARG(0));
      E("lea %s, [rip + .LS%d]", ARG(1),
        cstr(t->as.member_access.property->as.identifier.value));
      call_aligned("stola_struct_get");
    }
    PUSH("rax");
    gen_as(val, TY_DYN);
    E("mov %s, rax", ARG(1));
    POP(ARG(0));
    call_aligned(dyn_op_func(op));
  } else {
    gen_as(val, TY_DYN);
  }
  E("mov %s, rax", ARG(2));
  if (computed) {
    POP(ARG(1));
    POP(ARG(0));
    call_aligned("stola_setitem");
  } else {
    POP(ARG(0));
    E("lea %s, [rip + .LS%d]", ARG(1),
      cstr(t->as.member_access.property->as.identifier.value));
    call_aligned("stola_struct_set");
  }
}

static void unwind_try(int to_depth) {
  for (int i = try_depth; i > to_depth; i--)
    call_aligned("stola_pop_try");
}

static void push_loop(int brk, int cont) {
  if (nloops >= MAX_LOOPS) {
    cg_error(NULL, "loops nested too deeply");
    return;
  }
  loops[nloops].break_label = brk;
  loops[nloops].continue_label = cont;
  loops[nloops].try_depth = try_depth;
  nloops++;
}

// Integer counting loop shared by `loop` and `for x in range(...)`.
// var: loop variable; start/end/step expressions (step may be NULL).
static void gen_counted_loop(Var *var, ASTNode *start, ASTNode *end,
                             ASTNode *step, ASTNode *body) {
  int vty = dflt_ty(var->ty);
  int int_var = vty == TY_INT || P->native;
  Var *ctr = int_var ? var : new_temp_var(TY_INT);
  Var *endv = new_temp_var(TY_INT);
  long long step_const = 1;
  int step_is_const = 1;
  Var *stepv = NULL;
  char tmp[320];
  if (step) {
    if (operand_of(step, TY_INT, tmp, sizeof(tmp)) &&
        (step->type == AST_NUMBER_LITERAL ||
         (step->type == AST_IDENTIFIER && ((Var *)step->sym)->kind == SYM_CONST))) {
      step_const = strtoll(tmp, NULL, 10);
    } else {
      step_is_const = 0;
      stepv = new_temp_var(TY_INT);
    }
  }
  gen_as(start, TY_INT);
  E("mov %s, rax", var_loc(ctr));
  gen_as(end, TY_INT);
  E("mov %s, rax", var_loc(endv));
  if (!step_is_const) {
    gen_as(step, TY_INT);
    E("mov %s, rax", var_loc(stepv));
  }
  int lbody = new_label(), lcont = new_label(), lcond = new_label(),
      lend = new_label();
  E("jmp .L%d", lcond);
  LABEL(lbody);
  if (!int_var) {
    E("mov rax, %s", var_loc(ctr));
    convert(TY_INT, vty, NULL);
    store_var(var);
  }
  push_loop(lend, lcont);
  gen_block(body);
  nloops--;
  LABEL(lcont);
  if (step_is_const) {
    if (step_const == 1)
      E("inc %s", var_loc(ctr));
    else if (step_const == -1)
      E("dec %s", var_loc(ctr));
    else
      E("add %s, %lld", var_loc(ctr), step_const);
  } else {
    E("mov rax, %s", var_loc(stepv));
    E("add %s, rax", var_loc(ctr));
  }
  LABEL(lcond);
  E("mov rax, %s", var_loc(ctr));
  E("cmp rax, %s", var_loc(endv));
  if (step_is_const) {
    E("%s .L%d", step_const >= 0 ? "jl" : "jg", lbody);
  } else {
    int neg = new_label();
    E("cmp %s, 0", var_loc(stepv));
    E("jl .L%d", neg);
    E("mov rax, %s", var_loc(ctr));
    E("cmp rax, %s", var_loc(endv));
    E("jl .L%d", lbody);
    E("jmp .L%d", lend);
    LABEL(neg);
    E("mov rax, %s", var_loc(ctr));
    E("cmp rax, %s", var_loc(endv));
    E("jg .L%d", lbody);
  }
  LABEL(lend);
}

static int is_range_call(ASTNode *it) {
  return it && it->type == AST_CALL_EXPR &&
         it->as.call_expr.function->type == AST_IDENTIFIER &&
         !it->as.call_expr.function->sym &&
         strcmp(it->as.call_expr.function->as.identifier.value, "range") == 0;
}

static void gen_for(ASTNode *s) {
  Var *var = s->sym;
  ASTNode *it = s->as.for_stmt.iterable;
  if (is_range_call(it) && !s->as.for_stmt.value_name) {
    int n = it->as.call_expr.arg_count;
    ASTNode **a = it->as.call_expr.args;
    int all_raw = 1;
    for (int i = 0; i < n; i++)
      if (!is_raw(dflt_ty(a[i]->ty)))
        all_raw = 0;
    if (all_raw || P->native) {
      ASTNode zero = {0};
      char z[] = "0";
      zero.type = AST_NUMBER_LITERAL;
      zero.ty = TY_INT;
      zero.as.number_literal.value = z;
      if (n == 1)
        gen_counted_loop(var, &zero, a[0], NULL, s->as.for_stmt.body);
      else
        gen_counted_loop(var, a[0], a[1], n > 2 ? a[2] : NULL,
                         s->as.for_stmt.body);
      return;
    }
  }
  // Generic iteration over arrays, strings and dict keys
  Var *itv = new_temp_var(TY_DYN);
  Var *idx = new_temp_var(TY_INT);
  gen_as(it, TY_DYN);
  E("mov %s, rax", var_loc(itv));
  E("mov %s, 0", var_loc(idx));
  int lcond = new_label(), lcont = new_label(), lend = new_label();
  LABEL(lcond);
  E("mov %s, %s", ARG(0), var_loc(itv));
  call_aligned("stola_len_i");
  E("cmp %s, rax", var_loc(idx));
  E("jge .L%d", lend);
  E("mov %s, %s", ARG(0), var_loc(itv));
  E("mov %s, %s", ARG(1), var_loc(idx));
  call_aligned("stola_iter_get");
  convert(TY_DYN, var->ty, s);
  store_var(var);
  if (s->as.for_stmt.value_name) {
    Var *vv = sem_lookup(P, CF, s->as.for_stmt.value_name);
    E("mov %s, %s", ARG(0), var_loc(itv));
    E("mov %s, %s", ARG(1), var_loc(idx));
    call_aligned("stola_iter_value");
    convert(TY_DYN, vv->ty, s);
    store_var(vv);
  }
  push_loop(lend, lcont);
  gen_block(s->as.for_stmt.body);
  nloops--;
  LABEL(lcont);
  E("inc %s", var_loc(idx));
  E("jmp .L%d", lcond);
  LABEL(lend);
}

static int is_asm_register(const char *w) {
  static const char *regs[] = {
      "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp", "r8", "r9",
      "r10", "r11", "r12", "r13", "r14", "r15", "eax", "ebx", "ecx", "edx",
      "esi", "edi", "ebp", "esp", "ax", "bx", "cx", "dx", "si", "di", "bp",
      "sp", "al", "bl", "cl", "dl", "ah", "bh", "ch", "dh", "sil", "dil",
      "cr0", "cr2", "cr3", "cr4", "cs", "ds", "es", "fs", "gs", "ss",
      "byte", "word", "dword", "qword", "ptr", "offset", "rip", NULL};
  for (int i = 0; regs[i]; i++)
    if (strcmp(regs[i], w) == 0)
      return 1;
  if (w[0] == 'r' && w[1] >= '0' && w[1] <= '9')
    return 1;
  return 0;
}

// Emits an asm { } block, replacing variable names with their storage.
static void gen_asm(ASTNode *s) {
  const char *p = s->as.asm_block.code;
  while (*p) {
    // one line at a time
    const char *eol = strchr(p, '\n');
    size_t len = eol ? (size_t)(eol - p) : strlen(p);
    char *line = malloc(len + 1);
    memcpy(line, p, len);
    line[len] = 0;
    // strip comments ('; ...', '// ...', '# ...')
    for (char *c = line; *c; c++) {
      if (*c == ';' || *c == '#' || (c[0] == '/' && c[1] == '/')) {
        *c = 0;
        break;
      }
    }
    // trim
    char *start = line;
    while (*start == ' ' || *start == '\t' || *start == '\r')
      start++;
    char *endp = start + strlen(start);
    while (endp > start && (endp[-1] == ' ' || endp[-1] == '\t' || endp[-1] == '\r'))
      *--endp = 0;
    if (*start) {
      Buf outl = {0};
      const char *q = start;
      int first_word = 1;
      while (*q) {
        if ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') || *q == '_') {
          const char *ws = q;
          while ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z') ||
                 (*q >= '0' && *q <= '9') || *q == '_')
            q++;
          char word[128];
          size_t wl = (size_t)(q - ws) < 127 ? (size_t)(q - ws) : 127;
          memcpy(word, ws, wl);
          word[wl] = 0;
          Var *v = NULL;
          int prev_dot = ws > start && ws[-1] == '.';
          if (!first_word && !prev_dot && *q != ':' && !is_asm_register(word))
            v = sem_lookup(P, CF, word);
          if (v && (v->kind == SYM_LOCAL || v->kind == SYM_GLOBAL)) {
            buf_printf(&outl, "%s", var_loc(v));
          } else if (v && v->kind == SYM_CONST && !v->const_is_str) {
            buf_printf(&outl, "%lld", (long long)v->const_int);
          } else if (v && v->kind == SYM_STATIC) {
            buf_printf(&outl, "%s", global_sym(v));
          } else if (v && v->kind == SYM_FUNC) {
            buf_printf(&outl, "%s", v->func->label);
          } else {
            buf_printf(&outl, "%s", word);
          }
          first_word = 0;
        } else if (*q >= '0' && *q <= '9') {
          // numbers (incl. 0x...) copied verbatim
          while ((*q >= '0' && *q <= '9') || (*q >= 'a' && *q <= 'z') ||
                 (*q >= 'A' && *q <= 'Z') || *q == '_')
            buf_printf(&outl, "%c", *q++);
          first_word = 0;
        } else {
          if (*q != ' ' && *q != '\t')
            first_word = 0;
          buf_printf(&outl, "%c", *q++);
        }
      }
      E("%s", outl.d ? outl.d : "");
      free(outl.d);
    }
    free(line);
    p += len;
    if (*p == '\n')
      p++;
  }
}

static void gen_match(ASTNode *s) {
  ASTNode *subj = s->as.match_stmt.condition;
  int sty = dflt_ty(subj->ty);
  Var *sv = new_temp_var(sty);
  gen_as(subj, sty);
  store_var(sv);
  ASTNode *sid = temp_ident(sv);
  int end = new_label();
  for (int i = 0; i < s->as.match_stmt.case_count; i++) {
    ASTNode *c = s->as.match_stmt.cases[i];
    int next = new_label();
    if (is_raw(sty) && is_raw(dflt_ty(c->ty))) {
      gen_int_compare(sid, c);
      E("jne .L%d", next);
    } else {
      gen_dyn_compare(sid, c, 0);
      E("test eax, eax");
      E("jz .L%d", next);
    }
    gen_block(s->as.match_stmt.consequences[i]);
    E("jmp .L%d", end);
    LABEL(next);
  }
  gen_block(s->as.match_stmt.default_consequence);
  LABEL(end);
}

static void gen_stmt(ASTNode *s) {
  if (!s)
    return;
  switch (s->type) {
  case AST_BLOCK:
    gen_block(s);
    break;
  case AST_EXPRESSION_STMT:
    gen(s->as.expression_stmt.expression);
    break;
  case AST_ASSIGNMENT:
    if (s->as.assignment.target->type == AST_IDENTIFIER)
      gen_assign_ident(s);
    else
      gen_assign_member(s);
    break;
  case AST_IF_STMT: {
    int end = new_label();
    int next = new_label();
    gen_branch(s->as.if_stmt.condition, next, 0);
    gen_block(s->as.if_stmt.consequence);
    E("jmp .L%d", end);
    for (int i = 0; i < s->as.if_stmt.elif_count; i++) {
      LABEL(next);
      next = new_label();
      gen_branch(s->as.if_stmt.elif_conditions[i], next, 0);
      gen_block(s->as.if_stmt.elif_consequences[i]);
      E("jmp .L%d", end);
    }
    LABEL(next);
    gen_block(s->as.if_stmt.alternative);
    LABEL(end);
    break;
  }
  case AST_WHILE_STMT: {
    int body = new_label(), cond = new_label(), end = new_label();
    E("jmp .L%d", cond);
    LABEL(body);
    push_loop(end, cond);
    gen_block(s->as.while_stmt.body);
    nloops--;
    LABEL(cond);
    gen_branch(s->as.while_stmt.condition, body, 1);
    LABEL(end);
    break;
  }
  case AST_LOOP_STMT: {
    Var *var = s->sym;
    gen_counted_loop(var, s->as.loop_stmt.start_expr, s->as.loop_stmt.end_expr,
                     s->as.loop_stmt.step_expr, s->as.loop_stmt.body);
    break;
  }
  case AST_FOR_STMT:
    gen_for(s);
    break;
  case AST_MATCH_STMT:
    gen_match(s);
    break;
  case AST_RETURN_STMT:
    if (CF->is_main) {
      unwind_try(0);
      E("jmp .L%d", epilogue);
      break;
    }
    if (s->as.return_stmt.return_value)
      gen_as(s->as.return_stmt.return_value, CF->ret_ty);
    else if (dflt_ty(CF->ret_ty) == TY_DYN)
      load_null();
    else
      E("xor eax, eax");
    if (try_depth > 0) {
      PUSH("rax");
      unwind_try(0);
      POP("rax");
    }
    E("jmp .L%d", epilogue);
    break;
  case AST_BREAK_STMT:
  case AST_CONTINUE_STMT:
    if (nloops == 0) {
      cg_error(s, "break/continue outside of loop");
      break;
    }
    unwind_try(loops[nloops - 1].try_depth);
    E("jmp .L%d", s->type == AST_BREAK_STMT ? loops[nloops - 1].break_label
                                            : loops[nloops - 1].continue_label);
    break;
  case AST_TRY_CATCH: {
    int lcatch = new_label(), lend = new_label();
    call_aligned("stola_push_try");
    E("mov %s, rax", ARG(0));
    E("call stola_setjmp");
    E("test rax, rax");
    E("jnz .L%d", lcatch);
    try_depth++;
    gen_block(s->as.try_catch_stmt.try_block);
    try_depth--;
    call_aligned("stola_pop_try");
    E("jmp .L%d", lend);
    LABEL(lcatch);
    // stola_throw already removed the handler
    call_aligned("stola_get_error");
    Var *cv = sem_lookup(P, CF, s->as.try_catch_stmt.catch_var);
    if (cv) {
      convert(TY_DYN, cv->ty, s);
      store_var(cv);
    }
    gen_block(s->as.try_catch_stmt.catch_block);
    LABEL(lend);
    break;
  }
  case AST_THROW:
    gen_as(s->as.throw_stmt.exception_value, TY_DYN);
    E("mov %s, rax", ARG(0));
    call_aligned("stola_throw");
    break;
  case AST_ASM_BLOCK:
    gen_asm(s);
    break;
  case AST_GLOBAL_DECL:
  case AST_CONST_DECL:
  case AST_STATIC_DECL:
  case AST_PASS:
  case AST_FUNCTION_DECL:
  case AST_CLASS_DECL:
  case AST_STRUCT_DECL:
  case AST_IMPORT_STMT:
  case AST_IMPORT_NATIVE:
  case AST_C_FUNCTION_DECL:
    break;
  default:
    cg_error(s, "unsupported statement");
    break;
  }
  if (depth != 0) {
    cg_error(s, "internal: unbalanced stack (%d) after statement", depth);
    depth = 0;
  }
}

static void gen_block(ASTNode *b) {
  if (!b)
    return;
  if (b->type != AST_BLOCK) {
    gen_stmt(b);
    return;
  }
  for (int i = 0; i < b->as.block.statement_count; i++)
    gen_stmt(b->as.block.statements[i]);
}

// ------------------------------------------------------------
// Functions
// ------------------------------------------------------------

static const char *alloc_regs[] = {"rbx", "r12", "r13", "r14", "r15"};

static int cmp_weight(const void *a, const void *b) {
  const Var *x = *(Var *const *)a, *y = *(Var *const *)b;
  if (x->weight > y->weight)
    return -1;
  if (x->weight < y->weight)
    return 1;
  return 0;
}

static void assign_storage(FuncInfo *fn) {
  for (int i = 0; i < fn->nlocals; i++)
    fn->locals[i]->reg = NULL;
  nsaved = 0;
  int can_regalloc = !fn->is_main && !fn->has_try && !fn->has_asm &&
                     !fn->is_interrupt;
  if (can_regalloc) {
    Var **sorted = malloc(sizeof(Var *) * (fn->nlocals + 1));
    memcpy(sorted, fn->locals, sizeof(Var *) * fn->nlocals);
    qsort(sorted, fn->nlocals, sizeof(Var *), cmp_weight);
    for (int i = 0; i < fn->nlocals && nsaved < 5; i++) {
      if (sorted[i]->weight <= 0 || sorted[i]->addr_taken)
        continue;
      sorted[i]->reg = alloc_regs[nsaved++];
    }
    free(sorted);
  }
  nslots = 0;
  for (int i = 0; i < fn->nlocals; i++)
    if (!fn->locals[i]->reg)
      fn->locals[i]->slot = alloc_slot();
}

// Moves incoming parameters into their homes.
static void receive_params(FuncInfo *fn) {
  int R = NREGARGS();
  for (int i = 0; i < fn->nparams; i++) {
    Var *pv = fn->params[i];
    if (i < R) {
      E("mov %s, %s", var_loc(pv), ARG(i));
    } else {
      int off = P->win64 ? 16 + 32 + 8 * (i - 4) : 16 + 8 * (i - 6);
      E("mov rax, qword ptr [rbp + %d]", off);
      E("mov %s, rax", var_loc(pv));
    }
  }
}

static void init_locals(FuncInfo *fn) {
  for (int i = 0; i < fn->nlocals; i++) {
    Var *v = fn->locals[i];
    if (v->is_param || v->capture_index >= 0)
      continue;
    if (dflt_ty(v->ty) == TY_DYN) {
      E("lea rax, [rip + stola_null_value]");
      E("mov %s, rax", var_loc(v));
    } else if (v->reg) {
      E("xor %s, %s", v->reg, v->reg);
    } else {
      E("mov %s, 0", var_loc(v));
    }
  }
}

// Dynamic callers pass stola_missing_value for omitted arguments
static void fill_defaults(FuncInfo *fn) {
  if (fn->is_main || !fn->decl)
    return;
  FunctionDeclNode *fd = &fn->decl->as.function_decl;
  int hidden = fn->nparams - fd->param_count;
  for (int i = 0; i < fd->param_count; i++) {
    if (!fd->param_defaults || !fd->param_defaults[i])
      continue;
    Var *pv = fn->params[hidden + i];
    if (dflt_ty(pv->ty) != TY_DYN)
      continue; // typed params are filled by the caller / wrapper
    int skip = new_label();
    E("lea rax, [rip + stola_missing_value]");
    E("cmp %s, rax", var_loc(pv));
    E("jne .L%d", skip);
    gen_as(fd->param_defaults[i], TY_DYN);
    store_var(pv);
    LABEL(skip);
  }
}

static void load_captures(FuncInfo *fn) {
  if (!fn->is_lambda)
    return;
  Var *env = fn->params[0];
  for (int i = 0; i < fn->ncaptures; i++) {
    E("mov %s, %s", ARG(0), var_loc(env));
    E("mov %s, %d", ARG(1), i);
    call_aligned("stola_closure_get");
    store_var(fn->captures[i]);
  }
}

static void emit_hosted_main_init(void);

static void gen_function(FuncInfo *fn) {
  CF = fn;
  depth = 0;
  try_depth = 0;
  nloops = 0;
  assign_storage(fn);
  epilogue = new_label();

  Buf body = {0};
  cur = &body;

  if (fn->is_interrupt) {
    if (fn->has_error_code) {
      E("mov rax, qword ptr [rbp + 80]");
      store_var(fn->params[0]);
    }
  } else {
    receive_params(fn);
  }
  if (fn->is_main && !P->native)
    emit_hosted_main_init();
  init_locals(fn);
  load_captures(fn);
  fill_defaults(fn);

  gen_block(fn->decl->as.function_decl.body);

  // implicit return
  if (fn->is_main)
    E("xor eax, eax");
  else if (dflt_ty(fn->ret_ty) == TY_DYN)
    load_null();
  else
    E("xor eax, eax");

  int frame = 8 * nslots;
  if ((frame + 8 * nsaved) % 16)
    frame += 8;

  cur = &text_out;
  buf_printf(&text_out, "\n# %s\n", fn->name);
  int hosted_main = fn->is_main && !P->native;
  int fs_main = fn->is_main && P->target == TARGET_FREESTANDING;
  if (hosted_main || fs_main) {
    buf_printf(&text_out, ".global main\nmain:\n");
  } else {
    if (fn->is_interrupt || P->target == TARGET_FREESTANDING) {
      buf_printf(&text_out, ".global %s\n", fn->label);
      if (!fn->is_main && !fn->is_method && !fn->is_lambda)
        buf_printf(&text_out, ".global %s\n.set %s, %s\n",
                   fn->decl->as.function_decl.name,
                   fn->decl->as.function_decl.name, fn->label);
    }
    buf_printf(&text_out, "%s:\n", fn->label);
  }

  if (fn->is_interrupt) {
    static const char *saved[] = {"rax", "rcx", "rdx", "rsi", "rdi",
                                  "r8",  "r9",  "r10", "r11"};
    for (int i = 0; i < 9; i++)
      E("push %s", saved[i]);
    E("push rbp");
    E("mov rbp, rsp");
    if (frame)
      E("sub rsp, %d", frame);
    E("and rsp, -16");
    E("cld");
    buf_append(&text_out, &body);
    buf_printf(&text_out, ".L%d:\n", epilogue);
    E("mov rsp, rbp");
    E("pop rbp");
    for (int i = 8; i >= 0; i--)
      E("pop %s", saved[i]);
    if (fn->has_error_code)
      E("add rsp, 8");
    E("iretq");
  } else {
    E("push rbp");
    E("mov rbp, rsp");
    for (int i = 0; i < nsaved; i++)
      E("push %s", alloc_regs[i]);
    if (frame)
      E("sub rsp, %d", frame);
    buf_append(&text_out, &body);
    buf_printf(&text_out, ".L%d:\n", epilogue);
    if (hosted_main) {
      // flush output; returns the process exit code (0)
      E("and rsp, -16");
      if (P->win64)
        E("sub rsp, 32");
      E("call stola_runtime_exit");
    }
    if (nsaved)
      E("lea rsp, [rbp - %d]", 8 * nsaved);
    else
      E("mov rsp, rbp");
    for (int i = nsaved - 1; i >= 0; i--)
      E("pop %s", alloc_regs[i]);
    E("pop rbp");
    E("ret");
  }
  free(body.d);
}

// Wrapper with an all-dynamic ABI around a typed function
static void gen_wrapper(FuncInfo *fn) {
  CF = fn;
  depth = 0;
  nsaved = 0;
  nslots = 0;
  Buf body = {0};
  cur = &body;
  int n = fn->nparams;
  FunctionDeclNode *fd = &fn->decl->as.function_decl;
  int hidden = n - fd->param_count;
  Var **tmp = calloc(n + 1, sizeof(Var *));
  int R = NREGARGS();
  for (int i = 0; i < n; i++) {
    tmp[i] = new_temp_var(TY_DYN);
    if (i < R) {
      E("mov %s, %s", var_loc(tmp[i]), ARG(i));
    } else {
      int off = P->win64 ? 16 + 32 + 8 * (i - 4) : 16 + 8 * (i - 6);
      E("mov rax, qword ptr [rbp + %d]", off);
      E("mov %s, rax", var_loc(tmp[i]));
    }
  }
  // defaults for typed params
  for (int i = hidden; i < n; i++) {
    ASTNode *d = fd->param_defaults ? fd->param_defaults[i - hidden] : NULL;
    if (!d || dflt_ty(fn->params[i]->ty) == TY_DYN)
      continue;
    int skip = new_label();
    E("lea rax, [rip + stola_missing_value]");
    E("cmp %s, rax", var_loc(tmp[i]));
    E("jne .L%d", skip);
    gen_as(d, TY_DYN);
    E("mov %s, rax", var_loc(tmp[i]));
    LABEL(skip);
  }
  ASTNode **args = calloc(n + 1, sizeof(ASTNode *));
  int *tys = calloc(n + 1, sizeof(int));
  for (int i = 0; i < n; i++) {
    args[i] = temp_ident(tmp[i]);
    tys[i] = dflt_ty(fn->params[i]->ty);
  }
  emit_call(fn->label, args, n, tys, NULL);
  convert(fn->ret_ty, TY_DYN, NULL);

  int frame = 8 * nslots;
  if (frame % 16)
    frame += 8;
  cur = &text_out;
  buf_printf(&text_out, "\n# dynamic wrapper for %s\nW%s:\n", fn->name,
             fn->label);
  E("push rbp");
  E("mov rbp, rsp");
  if (frame)
    E("sub rsp, %d", frame);
  buf_append(&text_out, &body);
  E("mov rsp, rbp");
  E("pop rbp");
  E("ret");
  free(body.d);
  free(args);
  free(tys);
  free(tmp);
}

// Hosted main: runtime init, constants, classes, globals
static void emit_hosted_main_init(void) {
  // argc/argv are still in the first two argument registers
  E("lea %s, [rip + stola_longjmp]", ARG(2));
  call_aligned("stola_runtime_init");
  E("call .Lstola_init_consts");
  for (int i = 0; i < P->nclasses; i++) {
    ClassInfo *c = P->classes[i];
    if (c->is_struct)
      continue;
    E("lea %s, [rip + .LS%d]", ARG(0), cstr(c->name));
    if (c->parent)
      E("lea %s, [rip + .LS%d]", ARG(1), cstr(c->parent->name));
    else
      E("xor %s, %s", ARG(1), ARG(1));
    call_aligned("stola_register_class");
    for (int j = 0; j < c->nmethods; j++) {
      FuncInfo *m = c->methods[j];
      E("lea %s, [rip + .LS%d]", ARG(0), cstr(c->name));
      E("lea %s, [rip + .LS%d]", ARG(1), cstr(m->decl->as.function_decl.name));
      E("lea %s, [rip + %s%s]", ARG(2), m->needs_wrapper ? "W" : "", m->label);
      E("mov %s, %d", ARG(3), m->nparams - 1);
      call_aligned("stola_register_method");
    }
  }
  for (int i = 0; i < P->nglobals; i++) {
    Var *g = P->globals[i];
    if (g->kind == SYM_GLOBAL && dflt_ty(g->ty) == TY_DYN) {
      E("lea rax, [rip + stola_null_value]");
      E("mov %s, rax", var_loc(g));
    }
  }
  // load native libraries and bind c_function declarations
  for (int i = 0; i < P->nnative_libs; i++) {
    E("lea %s, [rip + .LS%d]", ARG(0), cstr(P->native_libs[i]));
    call_aligned("stola_load_dll");
  }
  for (int i = 0; i < P->nglobals; i++) {
    Var *g = P->globals[i];
    if (g->kind == SYM_CFUNC) {
      E("lea %s, [rip + .LS%d]", ARG(0), cstr(g->name));
      call_aligned("stola_bind_c_function");
    }
  }
}

static void emit_string_bytes(Buf *b, const char *data, int len) {
  for (int i = 0; i < len; i += 16) {
    buf_printf(b, "    .byte ");
    for (int j = i; j < len && j < i + 16; j++)
      buf_printf(b, "%s%d", j > i ? "," : "", (unsigned char)data[j]);
    buf_printf(b, "\n");
  }
  buf_printf(b, "    .byte 0\n");
}

static void emit_kernel_boot(Buf *o) {
  buf_printf(o,
  "\n# ---------------------------------------------------------------\n"
  "# Multiboot (v1) header + 32-bit → 64-bit long mode trampoline.\n"
  "# Generated by the StolasScript compiler (--target kernel).\n"
  "# ---------------------------------------------------------------\n"
  ".section .multiboot, \"a\"\n"
  ".align 4\n"
  ".Lmb_header:\n"
  "    .long 0x1BADB002\n"
  "    .long 0x00010003\n"
  "    .long -(0x1BADB002 + 0x00010003)\n"
  "    .long .Lmb_header\n"
  "    .long __stola_load_start\n"
  "    .long __stola_load_end\n"
  "    .long __stola_bss_end\n"
  "    .long _start\n"
  "\n.section .boot, \"ax\"\n"
  ".code32\n"
  ".global _start\n"
  "_start:\n"
  "    cli\n"
  "    mov esp, offset .Lboot_stack_top\n"
  "    mov dword ptr [stola_mb_magic], eax\n"
  "    mov dword ptr [stola_mb_info], ebx\n"
  "    # zero .bss (page tables live there)\n"
  "    mov edi, offset __stola_bss_start\n"
  "    mov ecx, offset __stola_bss_end\n"
  "    sub ecx, edi\n"
  "    xor eax, eax\n"
  "    cld\n"
  "    rep stosb\n"
  "    # identity map the first 4 GiB with 2 MiB pages\n"
  "    mov eax, offset .Lpdpt\n"
  "    or eax, 3\n"
  "    mov dword ptr [.Lpml4], eax\n"
  "    mov edi, offset .Lpdpt\n"
  "    mov eax, offset .Lpd\n"
  "    or eax, 3\n"
  "    mov ecx, 4\n"
  ".Lfill_pdpt:\n"
  "    mov dword ptr [edi], eax\n"
  "    add eax, 4096\n"
  "    add edi, 8\n"
  "    loop .Lfill_pdpt\n"
  "    mov edi, offset .Lpd\n"
  "    mov eax, 0x83\n"
  "    xor edx, edx\n"
  "    mov ecx, 2048\n"
  ".Lfill_pd:\n"
  "    mov dword ptr [edi], eax\n"
  "    mov dword ptr [edi + 4], edx\n"
  "    add eax, 0x200000\n"
  "    adc edx, 0\n"
  "    add edi, 8\n"
  "    loop .Lfill_pd\n"
  "    mov eax, offset .Lpml4\n"
  "    mov cr3, eax\n"
  "    mov eax, cr4\n"
  "    or eax, 0x20\n"
  "    mov cr4, eax\n"
  "    mov ecx, 0xC0000080\n"
  "    rdmsr\n"
  "    or eax, 0x100\n"
  "    wrmsr\n"
  "    mov eax, cr0\n"
  "    or eax, 0x80000001\n"
  "    mov cr0, eax\n"
  "    lgdt [.Lgdt_ptr]\n"
  "    .byte 0xEA\n"
  "    .long .Llong_mode\n"
  "    .word 0x08\n"
  "\n.code64\n"
  ".Llong_mode:\n"
  "    mov ax, 0x10\n"
  "    mov ds, ax\n"
  "    mov es, ax\n"
  "    mov ss, ax\n"
  "    xor ax, ax\n"
  "    mov fs, ax\n"
  "    mov gs, ax\n"
  "    mov rsp, offset .Lkernel_stack_top\n"
  "    xor rbp, rbp\n"
  "    call S___main\n"
  ".Lhalt_forever:\n"
  "    cli\n"
  "    hlt\n"
  "    jmp .Lhalt_forever\n"
  "\n.section .rodata\n"
  ".align 16\n"
  ".global stola_gdt\n"
  "stola_gdt:\n"
  "    .quad 0\n"
  "    .quad 0x00AF9A000000FFFF\n"
  "    .quad 0x00CF92000000FFFF\n"
  ".Lgdt_end:\n"
  ".Lgdt_ptr:\n"
  "    .word .Lgdt_end - stola_gdt - 1\n"
  "    .quad stola_gdt\n"
  "\n.section .bss\n"
  ".align 4096\n"
  ".Lpml4: .zero 4096\n"
  ".Lpdpt: .zero 4096\n"
  ".Lpd: .zero 4096 * 4\n"
  ".align 16\n"
  ".Lboot_stack: .zero 4096\n"
  ".Lboot_stack_top:\n"
  ".align 16\n"
  ".Lkernel_stack: .zero 65536\n"
  ".Lkernel_stack_top:\n"
  "\n.section .data\n"
  ".align 8\n"
  ".global stola_mb_info\n"
  "stola_mb_info: .quad 0\n"
  ".global stola_mb_magic\n"
  "stola_mb_magic: .quad 0\n");
}

int codegen_generate(Program *prog, const char *output_file) {
  P = prog;
  text_out.n = data_out.n = init_out.n = 0;

  // Functions
  for (int f = 0; f < P->nfuncs; f++)
    gen_function(P->funcs[f]);
  for (int f = 0; f < P->nfuncs; f++)
    if (P->funcs[f]->needs_wrapper)
      gen_wrapper(P->funcs[f]);

  // Hosted constant initialiser (called from main)
  if (!P->native) {
    cur = &init_out;
    buf_printf(&init_out, "\n.Lstola_init_consts:\n");
    E("push rbx");
    if (P->win64)
      E("sub rsp, 32");
    for (int i = 0; i < nstrs; i++) {
      if (!strs[i].need_value)
        continue;
      E("lea %s, [rip + .LS%d]", ARG(0), i);
      E("mov %s, %d", ARG(1), strs[i].len);
      E("call stola_new_string_len");
      E("mov qword ptr [rip + .LSV%d], rax", i);
    }
    for (int i = 0; i < nnums; i++) {
      E("mov %s, %lld", ARG(0), nums[i].v);
      E("call %s", nums[i].is_float ? "stola_new_float_bits" : "stola_new_int");
      E("mov qword ptr [rip + .L%cV%d], rax", nums[i].is_float ? 'F' : 'I', i);
    }
    if (P->win64)
      E("add rsp, 32");
    E("pop rbx");
    E("ret");
  }

  FILE *out = fopen(output_file, "w");
  if (!out) {
    fprintf(stderr, "error: could not open output file %s\n", output_file);
    return 1;
  }
  Buf o = {0};
  buf_printf(&o, "# Generated by the StolasScript compiler\n");
  buf_printf(&o, ".intel_syntax noprefix\n");
  if (P->target == TARGET_KERNEL)
    emit_kernel_boot(&o);

  buf_printf(&o, "\n.text\n");
  if (P->target == TARGET_LINUX) {
    buf_printf(&o,
               ".global _start\n"
               "_start:\n"
               "    xor rbp, rbp\n"
               "    mov rdi, qword ptr [rsp]\n"
               "    lea rsi, [rsp + 8]\n"
               "    mov qword ptr [rip + stola_argc], rdi\n"
               "    mov qword ptr [rip + stola_argv], rsi\n"
               "    and rsp, -16\n"
               "    call S___main\n"
               "    xor edi, edi\n"
               "    mov eax, 60\n"
               "    syscall\n");
  }
  buf_append(&o, &text_out);
  buf_append(&o, &init_out);

  if (!P->native) {
    // setjmp / longjmp used by try/catch (saves callee-saved state)
    const char *a0 = ARG(0), *a1 = ARG(1);
    buf_printf(&o,
               "\n.global stola_setjmp\nstola_setjmp:\n"
               "    mov [%s], rbx\n    mov [%s+8], rbp\n    mov [%s+16], r12\n"
               "    mov [%s+24], r13\n    mov [%s+32], r14\n    mov [%s+40], r15\n"
               "    mov [%s+48], rsi\n    mov [%s+56], rdi\n"
               "    lea %s, [rsp+8]\n    mov [%s+64], %s\n"
               "    mov %s, [rsp]\n    mov [%s+72], %s\n"
               "    xor eax, eax\n    ret\n",
               a0, a0, a0, a0, a0, a0, a0, a0, a1, a0, a1, a1, a0, a1);
    buf_printf(&o,
               "\n.global stola_longjmp\nstola_longjmp:\n"
               "    mov rbx, [%s]\n    mov rbp, [%s+8]\n    mov r12, [%s+16]\n"
               "    mov r13, [%s+24]\n    mov r14, [%s+32]\n    mov r15, [%s+40]\n"
               "    mov rsi, [%s+48]\n    mov rax, [%s+72]\n    mov rsp, [%s+64]\n"
               "    mov rdi, [%s+56]\n    push rax\n    mov eax, 1\n    ret\n",
               a0, a0, a0, a0, a0, a0, a0, a0, a0, a0);
  }

  // Read-only strings
  buf_printf(&o, "\n.section .rodata\n");
  for (int i = 0; i < nstrs; i++) {
    buf_printf(&o, ".LS%d:\n", i);
    emit_string_bytes(&o, strs[i].data, strs[i].len);
  }

  // Globals, statics, constant slots
  buf_printf(&o, "\n.section .bss\n.align 16\n");
  for (int i = 0; i < P->nglobals; i++) {
    Var *g = P->globals[i];
    if (g->kind == SYM_GLOBAL)
      buf_printf(&o, "%s: .zero 8\n", global_sym(g));
  }
  for (int i = 0; i < P->nglobals; i++) {
    Var *g = P->globals[i];
    if (g->kind == SYM_STATIC) {
      long long al = g->static_align > 0 ? g->static_align : 16;
      int p2 = 0;
      while ((1LL << p2) < al)
        p2++;
      buf_printf(&o, ".balign %lld\n%s: .zero %lld\n", 1LL << p2, global_sym(g),
                 (long long)g->static_size);
    }
  }
  if (!P->native) {
    buf_printf(&o, ".balign 8\n");
    for (int i = 0; i < nstrs; i++)
      if (strs[i].need_value)
        buf_printf(&o, ".LSV%d: .zero 8\n", i);
    for (int i = 0; i < nnums; i++)
      buf_printf(&o, ".L%cV%d: .zero 8\n", nums[i].is_float ? 'F' : 'I', i);
  }
  if (P->target == TARGET_LINUX)
    buf_printf(&o, ".global stola_argc\nstola_argc: .zero 8\n"
                   ".global stola_argv\nstola_argv: .zero 8\n");
  if (P->target == TARGET_LINUX || P->target == TARGET_FREESTANDING ||
      P->target == TARGET_KERNEL)
    buf_printf(&o, "\n.section .note.GNU-stack,\"\",@progbits\n");
  else if (!P->win64)
    buf_printf(&o, "\n.section .note.GNU-stack,\"\",@progbits\n");

  fwrite(o.d, 1, o.n, out);
  fclose(out);
  free(o.d);
  return cg_errors ? 1 : 0;
}
