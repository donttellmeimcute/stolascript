#include "semantic.h"
#include "builtin_table.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// Semantic analysis
//
//  1. Declarations: functions, classes, structs, consts, statics.
//  2. Scopes (Python rules): names assigned inside a function are
//     locals unless declared `global`; top-level names are globals.
//     Lambdas capture enclosing locals by value.
//  3. Escape analysis: functions used as values / methods / lambdas
//     keep a dynamic ABI.
//  4. Whole-program type inference (monotone fixpoint):
//     every variable, parameter and return value gets the most
//     precise of BOOL / INT / DYN (STR in native targets).
//     INT and BOOL values live unboxed in machine registers.
// ============================================================

static Program *P;
static int changed;

static int tyjoin(int a, int b) {
  if (a == TY_NONE)
    return b;
  if (b == TY_NONE)
    return a;
  if (a == b)
    return a;
  if (P->native)
    return TY_INT;
  return TY_DYN;
}

static void diag(int is_error, ASTNode *node, const char *fmt, ...) {
  char msg[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  char full[800];
  snprintf(full, sizeof(full), "%s:%d: %s: %s",
           node && node->file ? node->file : P->filename,
           node ? node->line : 0, is_error ? "error" : "warning", msg);
  if (is_error) {
    for (int i = 0; i < P->error_count; i++)
      if (strcmp(P->errors[i], full) == 0)
        return;
    AST_PUSH(P->errors, P->error_count, ast_strdup(full));
  } else {
    for (int i = 0; i < P->warning_count; i++)
      if (strcmp(P->warnings[i], full) == 0)
        return;
    AST_PUSH(P->warnings, P->warning_count, ast_strdup(full));
  }
}

#define error(node, ...) diag(1, node, __VA_ARGS__)
#define warning(node, ...) diag(0, node, __VA_ARGS__)

// Mangle a source name into a valid assembler symbol
static char *mangle(const char *prefix, const char *name) {
  size_t n = strlen(name);
  char *out = malloc(strlen(prefix) + n * 3 + 1);
  char *p = out;
  p += sprintf(p, "%s", prefix);
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

static Var *new_var(const char *name, SymKind kind) {
  Var *v = calloc(1, sizeof(Var));
  v->name = ast_strdup(name);
  v->kind = kind;
  v->capture_index = -1;
  return v;
}

static Var *find_in(Var **list, int n, const char *name) {
  for (int i = 0; i < n; i++)
    if (strcmp(list[i]->name, name) == 0)
      return list[i];
  return NULL;
}

static Var *find_global(const char *name) {
  return find_in(P->globals, P->nglobals, name);
}

static Var *add_global(const char *name, SymKind kind, ASTNode *decl) {
  Var *v = find_global(name);
  if (v) {
    if (kind != SYM_GLOBAL || v->kind != SYM_GLOBAL) {
      if (v->kind == SYM_GLOBAL && kind != SYM_GLOBAL) {
        error(decl, "'%s' is already used as a global variable", name);
      } else if (kind != SYM_GLOBAL) {
        error(decl, "'%s' is already defined", name);
      }
    }
    return v;
  }
  v = new_var(name, kind);
  v->decl = decl;
  AST_PUSH(P->globals, P->nglobals, v);
  return v;
}

static Var *find_local(FuncInfo *fn, const char *name) {
  return find_in(fn->locals, fn->nlocals, name);
}

static int is_declared_global(FuncInfo *fn, const char *name) {
  for (int i = 0; i < fn->nglobal_names; i++)
    if (strcmp(fn->global_names[i], name) == 0)
      return 1;
  return 0;
}

static Var *add_local(FuncInfo *fn, const char *name) {
  Var *v = find_local(fn, name);
  if (v)
    return v;
  v = new_var(name, SYM_LOCAL);
  AST_PUSH(fn->locals, fn->nlocals, v);
  return v;
}

static int map_type(const char *t, ASTNode *where) {
  if (!t)
    return TY_NONE;
  static const char *ints[] = {"int", "i64", "i32", "i16", "i8", "u64",
                               "u32", "u16", "u8", "uint", "usize", "isize",
                               "ptr", "addr", "byte", "char", "word", "qword",
                               NULL};
  for (int i = 0; ints[i]; i++)
    if (strcmp(t, ints[i]) == 0)
      return TY_INT;
  if (strcmp(t, "bool") == 0)
    return TY_BOOL;
  if (strcmp(t, "number") == 0)
    return P->native ? TY_INT : TY_DYN;
  if (strcmp(t, "str") == 0 || strcmp(t, "string") == 0 ||
      strcmp(t, "cstr") == 0)
    return P->native ? TY_STR : TY_DYN;
  if (strcmp(t, "any") == 0)
    return P->native ? TY_NONE : TY_NONE; // "any" = let inference decide
  if (P->native) {
    if (strcmp(t, "void") != 0)
      warning(where, "type '%s' is treated as 'int' in native targets", t);
    return TY_INT;
  }
  return TY_DYN;
}

// ------------------------------------------------------------
// Constant evaluation (const declarations, static sizes)
// ------------------------------------------------------------

static int const_eval(ASTNode *e, int64_t *out) {
  if (!e)
    return 0;
  switch (e->type) {
  case AST_NUMBER_LITERAL:
    *out = strtoll(e->as.number_literal.value, NULL, 10);
    if (e->as.number_literal.value[0] != '-') {
      // allow full 64-bit unsigned literals (e.g. 0xFFFFFFFFFFFFFFFF)
      *out = (int64_t)strtoull(e->as.number_literal.value, NULL, 10);
    }
    return 1;
  case AST_BOOLEAN_LITERAL:
    *out = e->as.boolean_literal.value;
    return 1;
  case AST_IDENTIFIER: {
    Var *v = find_global(e->as.identifier.value);
    if (v && v->kind == SYM_CONST && !v->const_is_str) {
      *out = v->const_int;
      return 1;
    }
    return 0;
  }
  case AST_UNARY_OP: {
    int64_t a;
    if (!const_eval(e->as.unary_op.right, &a))
      return 0;
    if (e->as.unary_op.op == TOKEN_MINUS)
      *out = -a;
    else if (e->as.unary_op.op == TOKEN_BIT_NOT)
      *out = ~a;
    else if (e->as.unary_op.op == TOKEN_NOT)
      *out = !a;
    else
      return 0;
    return 1;
  }
  case AST_BINARY_OP: {
    int64_t a, b;
    if (!const_eval(e->as.binary_op.left, &a) ||
        !const_eval(e->as.binary_op.right, &b))
      return 0;
    switch (e->as.binary_op.op) {
    case TOKEN_PLUS: *out = (int64_t)((uint64_t)a + (uint64_t)b); return 1;
    case TOKEN_MINUS: *out = (int64_t)((uint64_t)a - (uint64_t)b); return 1;
    case TOKEN_TIMES: *out = (int64_t)((uint64_t)a * (uint64_t)b); return 1;
    case TOKEN_DIVIDED_BY:
      if (b == 0) return 0;
      *out = a / b; return 1;
    case TOKEN_MODULO:
      if (b == 0) return 0;
      *out = a % b; return 1;
    case TOKEN_BIT_AND: *out = a & b; return 1;
    case TOKEN_BIT_OR: *out = a | b; return 1;
    case TOKEN_BIT_XOR: *out = a ^ b; return 1;
    case TOKEN_SHL: *out = (int64_t)((uint64_t)a << (b & 63)); return 1;
    case TOKEN_SHR: *out = a >> (b & 63); return 1;
    default: return 0;
    }
  }
  default:
    return 0;
  }
}

// ------------------------------------------------------------
// Phase 1: declarations
// ------------------------------------------------------------

static FuncInfo *new_func(ASTNode *decl, const char *name, const char *label) {
  FuncInfo *fn = calloc(1, sizeof(FuncInfo));
  fn->name = ast_strdup(name);
  fn->label = ast_strdup(label);
  fn->decl = decl;
  AST_PUSH(P->funcs, P->nfuncs, fn);
  return fn;
}

static void setup_params(FuncInfo *fn) {
  FunctionDeclNode *fd = &fn->decl->as.function_decl;
  if (fn->is_method) {
    Var *self = add_local(fn, "this");
    self->is_param = 1;
    self->declared = 1;
    self->ty = TY_DYN;
    AST_PUSH(fn->params, fn->nparams, self);
  }
  if (fn->is_lambda) {
    // hidden closure environment parameter
    Var *env = add_local(fn, "__env");
    env->is_param = 1;
    env->declared = 1;
    env->ty = TY_DYN;
    AST_PUSH(fn->params, fn->nparams, env);
  }
  for (int i = 0; i < fd->param_count; i++) {
    if (find_local(fn, fd->parameters[i]))
      error(fn->decl, "duplicate parameter '%s'", fd->parameters[i]);
    Var *v = add_local(fn, fd->parameters[i]);
    v->is_param = 1;
    int t = map_type(fd->param_types ? fd->param_types[i] : NULL, fn->decl);
    if (t != TY_NONE) {
      v->ty = t;
      v->declared = 1;
    }
    AST_PUSH(fn->params, fn->nparams, v);
  }
  if (fd->return_type) {
    if (strcmp(fd->return_type, "void") == 0) {
      fn->ret_ty = P->native ? TY_INT : TY_DYN;
      fn->ret_declared = 1;
    } else {
      int t = map_type(fd->return_type, fn->decl);
      if (t != TY_NONE) {
        fn->ret_ty = t;
        fn->ret_declared = 1;
      }
    }
  }
  if (fd->is_interrupt) {
    fn->is_interrupt = 1;
    if (fd->param_count > 1)
      error(fn->decl,
            "interrupt function '%s' takes at most one parameter (the CPU "
            "error code)",
            fd->name);
    fn->has_error_code = fd->param_count == 1;
    for (int i = 0; i < fn->nparams; i++) {
      fn->params[i]->ty = TY_INT;
      fn->params[i]->declared = 1;
    }
    fn->ret_ty = P->native ? TY_INT : TY_DYN;
    fn->ret_declared = 1;
  }
}

static void declare_toplevel(ASTNode *stmt) {
  switch (stmt->type) {
  case AST_FUNCTION_DECL: {
    const char *name = stmt->as.function_decl.name;
    Var *v = add_global(name, SYM_FUNC, stmt);
    char *label = mangle("S_", name);
    FuncInfo *fn = new_func(stmt, name, label);
    free(label);
    v->func = fn;
    break;
  }
  case AST_CLASS_DECL:
  case AST_STRUCT_DECL: {
    int is_struct = stmt->type == AST_STRUCT_DECL;
    const char *name =
        is_struct ? stmt->as.struct_decl.name : stmt->as.class_decl.name;
    if (P->native) {
      error(stmt, "%s '%s' is not available in native targets (use static "
                  "memory and load/store)",
            is_struct ? "struct" : "class", name);
      return;
    }
    Var *v = add_global(name, is_struct ? SYM_STRUCT : SYM_CLASS, stmt);
    ClassInfo *cls = calloc(1, sizeof(ClassInfo));
    cls->name = ast_strdup(name);
    cls->is_struct = is_struct;
    cls->decl = stmt;
    v->cls = cls;
    AST_PUSH(P->classes, P->nclasses, cls);
    if (is_struct) {
      cls->fields = stmt->as.struct_decl.fields;
      cls->nfields = stmt->as.struct_decl.field_count;
    } else {
      if (stmt->as.class_decl.parent)
        cls->parent_name = ast_strdup(stmt->as.class_decl.parent);
      for (int i = 0; i < stmt->as.class_decl.method_count; i++) {
        ASTNode *m = stmt->as.class_decl.methods[i];
        char full[512];
        snprintf(full, sizeof(full), "%s.%s", name, m->as.function_decl.name);
        char raw[512];
        snprintf(raw, sizeof(raw), "%s__%s", name, m->as.function_decl.name);
        char *label = mangle("S_", raw);
        FuncInfo *fn = new_func(m, full, label);
        free(label);
        fn->is_method = 1;
        fn->cls = cls;
        fn->escapes = 1;
        for (int j = 0; j < cls->nmethods; j++)
          if (strcmp(cls->methods[j]->decl->as.function_decl.name,
                     m->as.function_decl.name) == 0)
            error(m, "method '%s' defined twice in class '%s'",
                  m->as.function_decl.name, name);
        AST_PUSH(cls->methods, cls->nmethods, fn);
      }
    }
    break;
  }
  case AST_CONST_DECL: {
    Var *v = add_global(stmt->as.const_decl.name, SYM_CONST, stmt);
    ASTNode *val = stmt->as.const_decl.value;
    if (val && val->type == AST_STRING_LITERAL) {
      v->const_is_str = 1;
      v->const_str = val;
      v->ty = P->native ? TY_STR : TY_DYN;
    } else if (val && val->type == AST_IDENTIFIER && find_global(val->as.identifier.value) &&
               find_global(val->as.identifier.value)->kind == SYM_CONST &&
               find_global(val->as.identifier.value)->const_is_str) {
      Var *o = find_global(val->as.identifier.value);
      v->const_is_str = 1;
      v->const_str = o->const_str;
      v->ty = o->ty;
    } else if (const_eval(val, &v->const_int)) {
      v->ty = TY_INT;
    } else {
      error(stmt, "const '%s' must be a constant integer or string expression",
            stmt->as.const_decl.name);
      v->ty = TY_INT;
    }
    v->declared = 1;
    break;
  }
  case AST_STATIC_DECL: {
    Var *v = add_global(stmt->as.static_decl.name, SYM_STATIC, stmt);
    if (!const_eval(stmt->as.static_decl.size, &v->static_size) ||
        v->static_size <= 0) {
      error(stmt, "static '%s' needs a positive constant size",
            stmt->as.static_decl.name);
      v->static_size = 8;
    }
    v->static_align = 16;
    if (stmt->as.static_decl.align &&
        !const_eval(stmt->as.static_decl.align, &v->static_align))
      error(stmt, "static alignment must be constant");
    v->ty = TY_INT;
    v->declared = 1;
    break;
  }
  case AST_C_FUNCTION_DECL: {
    if (P->native) {
      error(stmt, "c_function is only available in the hosted target");
      return;
    }
    Var *v = add_global(stmt->as.c_function_decl.name, SYM_CFUNC, stmt);
    v->ty = TY_DYN;
    v->declared = 1;
    break;
  }
  case AST_IMPORT_NATIVE:
    if (P->native)
      error(stmt, "import_native is only available in the hosted target");
    else
      AST_PUSH(P->native_libs, P->nnative_libs,
               ast_strdup(stmt->as.import_native.dll_name));
    break;
  default:
    break;
  }
}

// ------------------------------------------------------------
// Phase 2: local collection (and lambda discovery)
// ------------------------------------------------------------

static void collect_expr(FuncInfo *fn, ASTNode *e);
static void collect_stmt(FuncInfo *fn, ASTNode *s);

static void declare_name(FuncInfo *fn, const char *name, ASTNode *where,
                         const char *annotation) {
  Var *v;
  if (fn->is_main || is_declared_global(fn, name)) {
    v = find_global(name);
    if (v && v->kind != SYM_GLOBAL) {
      if (v->kind == SYM_CONST)
        error(where, "cannot assign to constant '%s'", name);
      else if (v->kind == SYM_FUNC || v->kind == SYM_CLASS ||
               v->kind == SYM_STRUCT)
        error(where, "cannot assign to '%s': it is a function or class",
              name);
      else
        error(where, "cannot assign to '%s'", name);
      return;
    }
    v = add_global(name, SYM_GLOBAL, where);
  } else {
    Var *g = find_global(name);
    if (g && g->kind == SYM_CONST) {
      error(where, "cannot assign to constant '%s'", name);
      return;
    }
    v = add_local(fn, name);
  }
  if (annotation) {
    int t = map_type(annotation, where);
    if (t != TY_NONE) {
      if (v->declared && v->ty != t)
        error(where, "conflicting type annotations for '%s'", name);
      v->ty = t;
      v->declared = 1;
    }
  }
}

static void register_lambda(FuncInfo *fn, ASTNode *lam) {
  ASTNode *decl = lam->as.lambda.func;
  if (P->native) {
    error(lam, "lambdas are not available in native targets");
    return;
  }
  char *label = mangle("S_", decl->as.function_decl.name);
  FuncInfo *lf = new_func(decl, "<lambda>", label);
  free(label);
  lf->is_lambda = 1;
  lf->outer = fn;
  lf->escapes = 1;
  lam->sym = lf;
}

static void collect_expr(FuncInfo *fn, ASTNode *e) {
  if (!e)
    return;
  switch (e->type) {
  case AST_BINARY_OP:
    collect_expr(fn, e->as.binary_op.left);
    collect_expr(fn, e->as.binary_op.right);
    break;
  case AST_UNARY_OP:
    collect_expr(fn, e->as.unary_op.right);
    break;
  case AST_CALL_EXPR:
    collect_expr(fn, e->as.call_expr.function);
    for (int i = 0; i < e->as.call_expr.arg_count; i++)
      collect_expr(fn, e->as.call_expr.args[i]);
    break;
  case AST_ARRAY_LITERAL:
    for (int i = 0; i < e->as.array_literal.element_count; i++)
      collect_expr(fn, e->as.array_literal.elements[i]);
    break;
  case AST_DICT_LITERAL:
    for (int i = 0; i < e->as.dict_literal.pair_count; i++) {
      collect_expr(fn, e->as.dict_literal.keys[i]);
      collect_expr(fn, e->as.dict_literal.values[i]);
    }
    break;
  case AST_MEMBER_ACCESS:
    collect_expr(fn, e->as.member_access.object);
    if (e->as.member_access.is_computed)
      collect_expr(fn, e->as.member_access.property);
    break;
  case AST_SLICE:
    collect_expr(fn, e->as.slice.object);
    collect_expr(fn, e->as.slice.start);
    collect_expr(fn, e->as.slice.end);
    break;
  case AST_NEW_EXPR:
    for (int i = 0; i < e->as.new_expr.arg_count; i++)
      collect_expr(fn, e->as.new_expr.args[i]);
    break;
  case AST_TERNARY:
    collect_expr(fn, e->as.ternary.cond);
    collect_expr(fn, e->as.ternary.then_expr);
    collect_expr(fn, e->as.ternary.else_expr);
    break;
  case AST_LAMBDA:
    register_lambda(fn, e);
    break;
  default:
    break;
  }
}

static void collect_block(FuncInfo *fn, ASTNode *b) {
  if (!b)
    return;
  if (b->type != AST_BLOCK) {
    collect_stmt(fn, b);
    return;
  }
  for (int i = 0; i < b->as.block.statement_count; i++)
    collect_stmt(fn, b->as.block.statements[i]);
}

// First pass over a function body: record `global` declarations so that
// later assignments know whether a name is local.
static void collect_globals_decl(FuncInfo *fn, ASTNode *s) {
  if (!s)
    return;
  switch (s->type) {
  case AST_GLOBAL_DECL:
    for (int i = 0; i < s->as.global_decl.count; i++) {
      AST_PUSH(fn->global_names, fn->nglobal_names,
               ast_strdup(s->as.global_decl.names[i]));
      if (!fn->is_main)
        add_global(s->as.global_decl.names[i], SYM_GLOBAL, s);
    }
    break;
  case AST_BLOCK:
    for (int i = 0; i < s->as.block.statement_count; i++)
      collect_globals_decl(fn, s->as.block.statements[i]);
    break;
  case AST_IF_STMT:
    collect_globals_decl(fn, s->as.if_stmt.consequence);
    for (int i = 0; i < s->as.if_stmt.elif_count; i++)
      collect_globals_decl(fn, s->as.if_stmt.elif_consequences[i]);
    collect_globals_decl(fn, s->as.if_stmt.alternative);
    break;
  case AST_WHILE_STMT:
    collect_globals_decl(fn, s->as.while_stmt.body);
    break;
  case AST_LOOP_STMT:
    collect_globals_decl(fn, s->as.loop_stmt.body);
    break;
  case AST_FOR_STMT:
    collect_globals_decl(fn, s->as.for_stmt.body);
    break;
  case AST_TRY_CATCH:
    collect_globals_decl(fn, s->as.try_catch_stmt.try_block);
    collect_globals_decl(fn, s->as.try_catch_stmt.catch_block);
    break;
  case AST_MATCH_STMT:
    for (int i = 0; i < s->as.match_stmt.case_count; i++)
      collect_globals_decl(fn, s->as.match_stmt.consequences[i]);
    collect_globals_decl(fn, s->as.match_stmt.default_consequence);
    break;
  default:
    break;
  }
}

static void collect_stmt(FuncInfo *fn, ASTNode *s) {
  if (!s)
    return;
  switch (s->type) {
  case AST_BLOCK:
    collect_block(fn, s);
    break;
  case AST_EXPRESSION_STMT:
    collect_expr(fn, s->as.expression_stmt.expression);
    break;
  case AST_ASSIGNMENT:
    if (s->as.assignment.target->type == AST_IDENTIFIER)
      declare_name(fn, s->as.assignment.target->as.identifier.value, s,
                   s->as.assignment.type_annotation);
    else
      collect_expr(fn, s->as.assignment.target);
    collect_expr(fn, s->as.assignment.value);
    break;
  case AST_IF_STMT:
    collect_expr(fn, s->as.if_stmt.condition);
    collect_block(fn, s->as.if_stmt.consequence);
    for (int i = 0; i < s->as.if_stmt.elif_count; i++) {
      collect_expr(fn, s->as.if_stmt.elif_conditions[i]);
      collect_block(fn, s->as.if_stmt.elif_consequences[i]);
    }
    collect_block(fn, s->as.if_stmt.alternative);
    break;
  case AST_WHILE_STMT:
    collect_expr(fn, s->as.while_stmt.condition);
    collect_block(fn, s->as.while_stmt.body);
    break;
  case AST_LOOP_STMT:
    declare_name(fn, s->as.loop_stmt.iterator_name, s, NULL);
    collect_expr(fn, s->as.loop_stmt.start_expr);
    collect_expr(fn, s->as.loop_stmt.end_expr);
    collect_expr(fn, s->as.loop_stmt.step_expr);
    collect_block(fn, s->as.loop_stmt.body);
    break;
  case AST_FOR_STMT:
    declare_name(fn, s->as.for_stmt.iterator_name, s, NULL);
    if (s->as.for_stmt.value_name)
      declare_name(fn, s->as.for_stmt.value_name, s, NULL);
    collect_expr(fn, s->as.for_stmt.iterable);
    collect_block(fn, s->as.for_stmt.body);
    break;
  case AST_MATCH_STMT:
    collect_expr(fn, s->as.match_stmt.condition);
    for (int i = 0; i < s->as.match_stmt.case_count; i++) {
      collect_expr(fn, s->as.match_stmt.cases[i]);
      collect_block(fn, s->as.match_stmt.consequences[i]);
    }
    collect_block(fn, s->as.match_stmt.default_consequence);
    break;
  case AST_RETURN_STMT:
    collect_expr(fn, s->as.return_stmt.return_value);
    break;
  case AST_TRY_CATCH:
    if (P->native) {
      error(s, "try/catch is not available in native targets");
      break;
    }
    fn->has_try = 1;
    declare_name(fn, s->as.try_catch_stmt.catch_var, s, NULL);
    collect_block(fn, s->as.try_catch_stmt.try_block);
    collect_block(fn, s->as.try_catch_stmt.catch_block);
    break;
  case AST_THROW:
    if (P->native)
      error(s, "throw is not available in native targets");
    collect_expr(fn, s->as.throw_stmt.exception_value);
    break;
  case AST_ASM_BLOCK:
    fn->has_asm = 1;
    break;
  case AST_FUNCTION_DECL:
  case AST_CLASS_DECL:
  case AST_STRUCT_DECL:
    if (!fn->is_main)
      error(s, "nested declarations are not supported; use a lambda "
               "(function(...) ... end) instead");
    break;
  case AST_CONST_DECL:
  case AST_STATIC_DECL:
    if (!fn->is_main)
      error(s, "const/static must be declared at the top level");
    break;
  default:
    break;
  }
}

// ------------------------------------------------------------
// Phase 3: name resolution, captures, escapes
// ------------------------------------------------------------

// Finds `name` as a local of fn, creating captures through lambda chains.
static Var *resolve_local(FuncInfo *fn, const char *name) {
  Var *v = find_local(fn, name);
  if (v)
    return v;
  if (fn->is_lambda && fn->outer && !is_declared_global(fn, name)) {
    Var *ov = resolve_local(fn->outer, name);
    if (ov && !fn->outer->is_main) {
      v = add_local(fn, name);
      v->ty = TY_DYN;
      v->declared = 1;
      v->capture_index = fn->ncaptures;
      AST_PUSH(fn->captures, fn->ncaptures, v);
      return v;
    }
  }
  return NULL;
}

Var *sem_lookup(Program *prog, FuncInfo *fn, const char *name) {
  (void)prog;
  if (fn && !fn->is_main && !is_declared_global(fn, name)) {
    Var *v = resolve_local(fn, name);
    if (v)
      return v;
  }
  return find_global(name);
}

int sem_is_builtin(Program *prog, const char *name) {
  return builtin_lookup(name, prog->native) != NULL;
}

FuncInfo *sem_find_method(ClassInfo *cls, const char *name) {
  for (ClassInfo *c = cls; c; c = c->parent)
    for (int i = 0; i < c->nmethods; i++)
      if (strcmp(c->methods[i]->decl->as.function_decl.name, name) == 0)
        return c->methods[i];
  return NULL;
}

static void resolve_expr(FuncInfo *fn, ASTNode *e, int loop_depth);
static void resolve_stmt(FuncInfo *fn, ASTNode *s, int loop_depth);

static double depth_weight(int d) {
  double w = 1;
  for (int i = 0; i < d && i < 4; i++)
    w *= 8;
  return w;
}

static void touch(Var *v, int loop_depth) {
  if (v && v->kind == SYM_LOCAL)
    v->weight += depth_weight(loop_depth);
}

static void resolve_identifier_value(FuncInfo *fn, ASTNode *e, int ld) {
  const char *name = e->as.identifier.value;
  Var *v = sem_lookup(P, fn, name);
  e->sym = v;
  if (!v) {
    if (builtin_lookup(name, P->native))
      error(e, "builtin function '%s' cannot be used as a value", name);
    else
      error(e, "undefined variable '%s'", name);
    return;
  }
  touch(v, ld);
  if (v->kind == SYM_FUNC && !P->native) {
    v->func->escapes = 1;
  }
  if ((v->kind == SYM_CLASS || v->kind == SYM_STRUCT))
    error(e, "class '%s' cannot be used as a value", name);
}

static void resolve_expr(FuncInfo *fn, ASTNode *e, int ld) {
  if (!e)
    return;
  switch (e->type) {
  case AST_IDENTIFIER:
    resolve_identifier_value(fn, e, ld);
    break;
  case AST_FLOAT_LITERAL:
    if (P->native)
      error(e, "floating point literals are not available in native targets");
    break;
  case AST_BINARY_OP:
    resolve_expr(fn, e->as.binary_op.left, ld);
    resolve_expr(fn, e->as.binary_op.right, ld);
    if (P->native && (e->as.binary_op.op == TOKEN_IN ||
                      e->as.binary_op.op == TOKEN_NOT_IN))
      error(e, "'in' is not available in native targets");
    break;
  case AST_UNARY_OP:
    resolve_expr(fn, e->as.unary_op.right, ld);
    break;
  case AST_TERNARY:
    resolve_expr(fn, e->as.ternary.cond, ld);
    resolve_expr(fn, e->as.ternary.then_expr, ld);
    resolve_expr(fn, e->as.ternary.else_expr, ld);
    break;
  case AST_CALL_EXPR: {
    ASTNode *callee = e->as.call_expr.function;
    for (int i = 0; i < e->as.call_expr.arg_count; i++)
      resolve_expr(fn, e->as.call_expr.args[i], ld);
    if (callee->type == AST_IDENTIFIER) {
      const char *name = callee->as.identifier.value;
      Var *v = sem_lookup(P, fn, name);
      callee->sym = v;
      if (v) {
        touch(v, ld);
        if (v->kind == SYM_FUNC) {
          FunctionDeclNode *fd = &v->func->decl->as.function_decl;
          int required = 0;
          for (int i = 0; i < fd->param_count; i++)
            if (!fd->param_defaults || !fd->param_defaults[i])
              required = i + 1;
          if (e->as.call_expr.arg_count < required ||
              e->as.call_expr.arg_count > fd->param_count)
            error(e, "function '%s' expects %s%d argument%s, got %d", name,
                  required != fd->param_count ? "between " : "",
                  required != fd->param_count ? required : fd->param_count,
                  fd->param_count == 1 ? "" : "s", e->as.call_expr.arg_count);
          if (fd->is_interrupt)
            error(e, "interrupt function '%s' cannot be called directly",
                  name);
        } else if (v->kind == SYM_STRUCT) {
          if (e->as.call_expr.arg_count > v->cls->nfields)
            error(e, "struct '%s' has %d fields, got %d arguments", name,
                  v->cls->nfields, e->as.call_expr.arg_count);
        } else if (v->kind == SYM_CONST || v->kind == SYM_STATIC) {
          if (!P->native)
            error(e, "'%s' is not callable", name);
        } else if (v->kind == SYM_CFUNC) {
          if (e->as.call_expr.arg_count > 4)
            error(e, "c_function calls support at most 4 arguments");
        } else if (P->native && (v->kind == SYM_LOCAL || v->kind == SYM_GLOBAL)) {
          error(e, "'%s' is a variable; use call_ptr(%s, ...) to call a "
                   "function address in native targets",
                name, name);
        }
      } else {
        const BuiltinInfo *b = builtin_lookup(name, P->native);
        if (!b) {
          if (builtin_lookup(name, !P->native))
            error(e, "builtin '%s' is not available in the %s target", name,
                  P->native ? "native" : "hosted");
          else
            error(e, "undefined function '%s'", name);
        } else {
          int n = e->as.call_expr.arg_count;
          if (n < b->min_args || (b->max_args >= 0 && n > b->max_args))
            error(e, "builtin '%s' expects %d%s argument(s), got %d", name,
                  b->min_args,
                  b->max_args < 0 ? "+" : (b->max_args != b->min_args ? "-N" : ""),
                  n);
          if (P->native && strcmp(name, "addr_of") == 0 && n == 1) {
            ASTNode *a = e->as.call_expr.args[0];
            if (a->type != AST_IDENTIFIER)
              error(e, "addr_of expects a function, static or variable name");
            else if (a->sym && ((Var *)a->sym)->kind == SYM_LOCAL)
              ((Var *)a->sym)->addr_taken = 1; // keep it in memory
          }
          if (P->native && P->target != TARGET_LINUX &&
              (strcmp(name, "argc") == 0 || strcmp(name, "argv") == 0))
            error(e, "argc/argv are only available in the linux target");
          if (P->native && P->target != TARGET_KERNEL &&
              (strcmp(name, "multiboot_info") == 0 ||
               strcmp(name, "multiboot_magic") == 0))
            error(e, "multiboot_info() is only available in the kernel target");
        }
      }
    } else if (callee->type == AST_MEMBER_ACCESS &&
               !callee->as.member_access.is_computed) {
      if (P->native)
        error(e, "method calls are not available in native targets");
      ASTNode *obj = callee->as.member_access.object;
      if (obj->type == AST_SUPER) {
        if (!fn->is_method || !fn->cls)
          error(e, "'super' used outside of a method");
        else if (!fn->cls->parent && !fn->cls->parent_name)
          error(e, "class '%s' has no parent class", fn->cls->name);
      } else {
        resolve_expr(fn, obj, ld);
      }
    } else {
      if (P->native)
        error(e, "calling computed values is not available in native targets");
      resolve_expr(fn, callee, ld);
    }
    break;
  }
  case AST_ARRAY_LITERAL:
    if (P->native)
      error(e, "arrays are not available in native targets (use static)");
    for (int i = 0; i < e->as.array_literal.element_count; i++)
      resolve_expr(fn, e->as.array_literal.elements[i], ld);
    break;
  case AST_DICT_LITERAL:
    if (P->native)
      error(e, "dictionaries are not available in native targets");
    for (int i = 0; i < e->as.dict_literal.pair_count; i++) {
      resolve_expr(fn, e->as.dict_literal.keys[i], ld);
      resolve_expr(fn, e->as.dict_literal.values[i], ld);
    }
    break;
  case AST_MEMBER_ACCESS:
    if (P->native)
      error(e, "member access / indexing is not available in native targets "
               "(use load8/load64...)");
    if (e->as.member_access.object->type == AST_SUPER)
      error(e, "'super' can only be used to call a method");
    else
      resolve_expr(fn, e->as.member_access.object, ld);
    if (e->as.member_access.is_computed)
      resolve_expr(fn, e->as.member_access.property, ld);
    break;
  case AST_SLICE:
    if (P->native)
      error(e, "slices are not available in native targets");
    resolve_expr(fn, e->as.slice.object, ld);
    resolve_expr(fn, e->as.slice.start, ld);
    resolve_expr(fn, e->as.slice.end, ld);
    break;
  case AST_NEW_EXPR: {
    if (P->native)
      error(e, "'new' is not available in native targets");
    const char *cname = e->as.new_expr.class_name->as.identifier.value;
    Var *v = find_global(cname);
    if (!v || (v->kind != SYM_CLASS && v->kind != SYM_STRUCT))
      error(e, "'%s' is not a class", cname);
    else
      e->sym = v;
    for (int i = 0; i < e->as.new_expr.arg_count; i++)
      resolve_expr(fn, e->as.new_expr.args[i], ld);
    break;
  }
  case AST_THIS:
    if (!fn->is_method && !(fn->is_lambda && resolve_local(fn, "this")))
      error(e, "'this'/'self' used outside of a method");
    else {
      Var *v = resolve_local(fn, "this");
      e->sym = v;
      touch(v, ld);
    }
    break;
  case AST_SUPER:
    error(e, "'super' can only be used to call a parent method");
    break;
  case AST_LAMBDA: {
    // Resolution of the lambda body happens when its FuncInfo is processed;
    // here we only make sure captures exist (processed lazily).
    break;
  }
  default:
    break;
  }
}

static void resolve_block(FuncInfo *fn, ASTNode *b, int ld) {
  if (!b)
    return;
  if (b->type != AST_BLOCK) {
    resolve_stmt(fn, b, ld);
    return;
  }
  for (int i = 0; i < b->as.block.statement_count; i++)
    resolve_stmt(fn, b->as.block.statements[i], ld);
}

static int loop_nesting = 0;

static void resolve_stmt(FuncInfo *fn, ASTNode *s, int ld) {
  if (!s)
    return;
  switch (s->type) {
  case AST_BLOCK:
    resolve_block(fn, s, ld);
    break;
  case AST_EXPRESSION_STMT:
    resolve_expr(fn, s->as.expression_stmt.expression, ld);
    break;
  case AST_ASSIGNMENT: {
    ASTNode *t = s->as.assignment.target;
    resolve_expr(fn, s->as.assignment.value, ld);
    if (t->type == AST_IDENTIFIER) {
      Var *v = sem_lookup(P, fn, t->as.identifier.value);
      t->sym = v;
      touch(v, ld);
      if (v && fn->is_lambda && v->capture_index >= 0)
        warning(s, "assigning to captured variable '%s' only changes the "
                   "lambda's copy",
                v->name);
    } else {
      resolve_expr(fn, t, ld);
    }
    break;
  }
  case AST_IF_STMT:
    resolve_expr(fn, s->as.if_stmt.condition, ld);
    resolve_block(fn, s->as.if_stmt.consequence, ld);
    for (int i = 0; i < s->as.if_stmt.elif_count; i++) {
      resolve_expr(fn, s->as.if_stmt.elif_conditions[i], ld);
      resolve_block(fn, s->as.if_stmt.elif_consequences[i], ld);
    }
    resolve_block(fn, s->as.if_stmt.alternative, ld);
    break;
  case AST_WHILE_STMT:
    resolve_expr(fn, s->as.while_stmt.condition, ld + 1);
    loop_nesting++;
    resolve_block(fn, s->as.while_stmt.body, ld + 1);
    loop_nesting--;
    break;
  case AST_LOOP_STMT: {
    Var *v = sem_lookup(P, fn, s->as.loop_stmt.iterator_name);
    s->sym = v;
    touch(v, ld + 1);
    resolve_expr(fn, s->as.loop_stmt.start_expr, ld);
    resolve_expr(fn, s->as.loop_stmt.end_expr, ld + 1);
    resolve_expr(fn, s->as.loop_stmt.step_expr, ld + 1);
    loop_nesting++;
    resolve_block(fn, s->as.loop_stmt.body, ld + 1);
    loop_nesting--;
    break;
  }
  case AST_FOR_STMT: {
    Var *v = sem_lookup(P, fn, s->as.for_stmt.iterator_name);
    s->sym = v;
    touch(v, ld + 1);
    if (s->as.for_stmt.value_name)
      touch(sem_lookup(P, fn, s->as.for_stmt.value_name), ld + 1);
    ASTNode *it = s->as.for_stmt.iterable;
    int is_range = it->type == AST_CALL_EXPR &&
                   it->as.call_expr.function->type == AST_IDENTIFIER &&
                   !sem_lookup(P, fn, it->as.call_expr.function->as.identifier.value) &&
                   strcmp(it->as.call_expr.function->as.identifier.value,
                          "range") == 0;
    if (is_range && P->native) {
      // `for i in range(...)` compiles to a counting loop (no builtin call)
      int n = it->as.call_expr.arg_count;
      if (n < 1 || n > 3)
        error(it, "range() expects 1 to 3 arguments, got %d", n);
      for (int i = 0; i < n; i++)
        resolve_expr(fn, it->as.call_expr.args[i], ld);
    } else {
      resolve_expr(fn, it, ld);
    }
    if (P->native && !is_range)
      error(s, "native targets only support `for x in range(a, b)` loops");
    loop_nesting++;
    resolve_block(fn, s->as.for_stmt.body, ld + 1);
    loop_nesting--;
    break;
  }
  case AST_MATCH_STMT:
    resolve_expr(fn, s->as.match_stmt.condition, ld);
    for (int i = 0; i < s->as.match_stmt.case_count; i++) {
      resolve_expr(fn, s->as.match_stmt.cases[i], ld);
      resolve_block(fn, s->as.match_stmt.consequences[i], ld);
    }
    resolve_block(fn, s->as.match_stmt.default_consequence, ld);
    break;
  case AST_RETURN_STMT:
    if (fn->is_interrupt && s->as.return_stmt.return_value)
      error(s, "interrupt functions cannot return a value");
    resolve_expr(fn, s->as.return_stmt.return_value, ld);
    break;
  case AST_TRY_CATCH:
    resolve_block(fn, s->as.try_catch_stmt.try_block, ld);
    touch(sem_lookup(P, fn, s->as.try_catch_stmt.catch_var), ld);
    resolve_block(fn, s->as.try_catch_stmt.catch_block, ld);
    break;
  case AST_THROW:
    resolve_expr(fn, s->as.throw_stmt.exception_value, ld);
    break;
  case AST_BREAK_STMT:
  case AST_CONTINUE_STMT:
    if (loop_nesting == 0)
      error(s, "'%s' outside of a loop",
            s->type == AST_BREAK_STMT ? "break" : "continue");
    break;
  case AST_ASM_BLOCK:
    if (!P->native) {
      const char *code = s->as.asm_block.code;
      static const char *priv[] = {"hlt", "lgdt", "lidt", "cli", "sti",
                                   " in ", "out ", NULL};
      for (int i = 0; priv[i]; i++)
        if (strstr(code, priv[i])) {
          warning(s, "privileged instruction in asm block outside of a "
                     "native/kernel target");
          break;
        }
    }
    break;
  default:
    break;
  }
}

// ------------------------------------------------------------
// Phase 4: type inference
// ------------------------------------------------------------

static int infer_expr(FuncInfo *fn, ASTNode *e);

static void join_into_var(Var *v, int t) {
  if (!v || v->declared)
    return;
  if (v->kind != SYM_LOCAL && v->kind != SYM_GLOBAL)
    return;
  int nt = tyjoin(v->ty, t);
  if (nt != v->ty) {
    v->ty = nt;
    changed = 1;
  }
}

static int arith_type(TokenType op, int a, int b) {
  if (a == TY_NONE || b == TY_NONE) {
    if (a == TY_DYN || b == TY_DYN)
      return P->native ? TY_INT : TY_DYN;
    return TY_NONE;
  }
  (void)op;
  if (P->native)
    return TY_INT;
  int ai = a == TY_INT || a == TY_BOOL;
  int bi = b == TY_INT || b == TY_BOOL;
  if (ai && bi)
    return TY_INT;
  return TY_DYN;
}

static int is_range_call(ASTNode *it) {
  return it && it->type == AST_CALL_EXPR &&
         it->as.call_expr.function->type == AST_IDENTIFIER &&
         !it->as.call_expr.function->sym &&
         strcmp(it->as.call_expr.function->as.identifier.value, "range") == 0;
}

static int infer_call(FuncInfo *fn, ASTNode *e) {
  ASTNode *callee = e->as.call_expr.function;
  int nargs = e->as.call_expr.arg_count;
  int arg_ty[64];
  for (int i = 0; i < nargs; i++) {
    int t = infer_expr(fn, e->as.call_expr.args[i]);
    if (i < 64)
      arg_ty[i] = t;
  }
  if (callee->type == AST_IDENTIFIER) {
    Var *v = callee->sym;
    if (v && v->kind == SYM_FUNC) {
      FuncInfo *f = v->func;
      if (!f->escapes || P->native) {
        int base = 0; // plain functions have no hidden params
        FunctionDeclNode *fd = &f->decl->as.function_decl;
        for (int i = 0; i < fd->param_count && base + i < f->nparams; i++) {
          Var *pv = f->params[base + i];
          if (i < nargs && i < 64)
            join_into_var(pv, arg_ty[i]);
          else if (fd->param_defaults && fd->param_defaults[i])
            join_into_var(pv, infer_expr(f, fd->param_defaults[i]));
        }
      }
      return f->ret_ty;
    }
    if (v && v->kind == SYM_STRUCT)
      return TY_DYN;
    if (v && v->kind == SYM_CLASS)
      return TY_DYN;
    if (v && (v->kind == SYM_CFUNC))
      return TY_DYN;
    if (v && (v->kind == SYM_LOCAL || v->kind == SYM_GLOBAL))
      return P->native ? TY_INT : TY_DYN;
    if (!v) {
      const BuiltinInfo *b =
          builtin_lookup(callee->as.identifier.value, P->native);
      if (!b)
        return P->native ? TY_INT : TY_DYN;
      if (P->native && (strcmp(b->name, "as_str") == 0 ||
                        strcmp(b->name, "argv") == 0))
        return TY_STR;
      switch (b->ret) {
      case RK_INT:
        return TY_INT;
      case RK_BOOL:
        return TY_BOOL;
      default:
        return P->native ? TY_INT : TY_DYN;
      }
    }
    return P->native ? TY_INT : TY_DYN;
  }
  if (callee->type == AST_MEMBER_ACCESS) {
    if (callee->as.member_access.object->type != AST_SUPER)
      infer_expr(fn, callee->as.member_access.object);
    return TY_DYN;
  }
  infer_expr(fn, callee);
  return TY_DYN;
}

static int infer_expr(FuncInfo *fn, ASTNode *e) {
  if (!e)
    return TY_NONE;
  int t = TY_DYN;
  switch (e->type) {
  case AST_NUMBER_LITERAL:
    t = TY_INT;
    break;
  case AST_FLOAT_LITERAL:
    t = TY_DYN;
    break;
  case AST_STRING_LITERAL:
    t = P->native ? TY_STR : TY_DYN;
    break;
  case AST_BOOLEAN_LITERAL:
    t = TY_BOOL;
    break;
  case AST_NULL_LITERAL:
    t = P->native ? TY_INT : TY_DYN;
    break;
  case AST_IDENTIFIER: {
    Var *v = e->sym;
    if (!v)
      t = TY_DYN;
    else if (v->kind == SYM_FUNC)
      t = P->native ? TY_INT : TY_DYN;
    else if (v->kind == SYM_STATIC)
      t = TY_INT;
    else if (v->kind == SYM_CFUNC || v->kind == SYM_CLASS ||
             v->kind == SYM_STRUCT)
      t = TY_DYN;
    else
      t = v->ty;
    break;
  }
  case AST_BINARY_OP: {
    int a = infer_expr(fn, e->as.binary_op.left);
    int b = infer_expr(fn, e->as.binary_op.right);
    switch (e->as.binary_op.op) {
    case TOKEN_EQUALS:
    case TOKEN_NOT_EQUALS:
    case TOKEN_LESS_THAN:
    case TOKEN_GREATER_THAN:
    case TOKEN_LESS_OR_EQUALS:
    case TOKEN_GREATER_OR_EQUALS:
    case TOKEN_IN:
    case TOKEN_NOT_IN:
      t = TY_BOOL;
      break;
    case TOKEN_AND:
    case TOKEN_OR:
      if (a == TY_NONE || b == TY_NONE)
        t = (a == TY_DYN || b == TY_DYN) ? TY_DYN : TY_NONE;
      else if (a == TY_BOOL && b == TY_BOOL)
        t = TY_BOOL;
      else
        t = tyjoin(a, b);
      if (P->native && t != TY_NONE && t != TY_BOOL)
        t = TY_INT;
      break;
    default:
      t = arith_type(e->as.binary_op.op, a, b);
      break;
    }
    break;
  }
  case AST_UNARY_OP: {
    int a = infer_expr(fn, e->as.unary_op.right);
    if (e->as.unary_op.op == TOKEN_NOT)
      t = TY_BOOL;
    else if (a == TY_NONE)
      t = TY_NONE;
    else if (a == TY_INT || a == TY_BOOL || P->native)
      t = TY_INT;
    else
      t = TY_DYN;
    break;
  }
  case AST_TERNARY: {
    infer_expr(fn, e->as.ternary.cond);
    int a = infer_expr(fn, e->as.ternary.then_expr);
    int b = infer_expr(fn, e->as.ternary.else_expr);
    t = tyjoin(a, b);
    break;
  }
  case AST_CALL_EXPR:
    t = infer_call(fn, e);
    break;
  case AST_ARRAY_LITERAL:
    for (int i = 0; i < e->as.array_literal.element_count; i++)
      infer_expr(fn, e->as.array_literal.elements[i]);
    break;
  case AST_DICT_LITERAL:
    for (int i = 0; i < e->as.dict_literal.pair_count; i++) {
      infer_expr(fn, e->as.dict_literal.keys[i]);
      infer_expr(fn, e->as.dict_literal.values[i]);
    }
    break;
  case AST_MEMBER_ACCESS:
    infer_expr(fn, e->as.member_access.object);
    if (e->as.member_access.is_computed)
      infer_expr(fn, e->as.member_access.property);
    break;
  case AST_SLICE:
    infer_expr(fn, e->as.slice.object);
    infer_expr(fn, e->as.slice.start);
    infer_expr(fn, e->as.slice.end);
    break;
  case AST_NEW_EXPR:
    for (int i = 0; i < e->as.new_expr.arg_count; i++)
      infer_expr(fn, e->as.new_expr.args[i]);
    break;
  default:
    break;
  }
  e->ty = t;
  return t;
}

static void infer_stmt(FuncInfo *fn, ASTNode *s);

static void infer_block(FuncInfo *fn, ASTNode *b) {
  if (!b)
    return;
  if (b->type != AST_BLOCK) {
    infer_stmt(fn, b);
    return;
  }
  for (int i = 0; i < b->as.block.statement_count; i++)
    infer_stmt(fn, b->as.block.statements[i]);
}

static void join_ret(FuncInfo *fn, int t) {
  if (fn->ret_declared || fn->is_main)
    return;
  int nt = tyjoin(fn->ret_ty, t);
  if (nt != fn->ret_ty) {
    fn->ret_ty = nt;
    changed = 1;
  }
}

static void infer_stmt(FuncInfo *fn, ASTNode *s) {
  if (!s)
    return;
  switch (s->type) {
  case AST_BLOCK:
    infer_block(fn, s);
    break;
  case AST_EXPRESSION_STMT:
    infer_expr(fn, s->as.expression_stmt.expression);
    break;
  case AST_ASSIGNMENT: {
    ASTNode *tgt = s->as.assignment.target;
    int vt = infer_expr(fn, s->as.assignment.value);
    if (tgt->type == AST_IDENTIFIER) {
      Var *v = tgt->sym;
      if (!s->as.assignment.value) {
        // declaration only
      } else if (s->as.assignment.op != TOKEN_ASSIGN) {
        int cur = v ? v->ty : TY_DYN;
        join_into_var(v, arith_type(s->as.assignment.op, cur, vt));
      } else {
        join_into_var(v, vt);
      }
      tgt->ty = v ? v->ty : TY_DYN;
    } else {
      infer_expr(fn, tgt);
    }
    break;
  }
  case AST_IF_STMT:
    infer_expr(fn, s->as.if_stmt.condition);
    infer_block(fn, s->as.if_stmt.consequence);
    for (int i = 0; i < s->as.if_stmt.elif_count; i++) {
      infer_expr(fn, s->as.if_stmt.elif_conditions[i]);
      infer_block(fn, s->as.if_stmt.elif_consequences[i]);
    }
    infer_block(fn, s->as.if_stmt.alternative);
    break;
  case AST_WHILE_STMT:
    infer_expr(fn, s->as.while_stmt.condition);
    infer_block(fn, s->as.while_stmt.body);
    break;
  case AST_LOOP_STMT: {
    int a = infer_expr(fn, s->as.loop_stmt.start_expr);
    int b = infer_expr(fn, s->as.loop_stmt.end_expr);
    int c = s->as.loop_stmt.step_expr ? infer_expr(fn, s->as.loop_stmt.step_expr)
                                      : TY_INT;
    int t = TY_INT;
    if (a == TY_NONE || b == TY_NONE || c == TY_NONE)
      t = TY_NONE;
    else if (!P->native &&
             ((a != TY_INT && a != TY_BOOL) || (b != TY_INT && b != TY_BOOL) ||
              (c != TY_INT && c != TY_BOOL)))
      t = TY_DYN;
    join_into_var(s->sym, t);
    infer_block(fn, s->as.loop_stmt.body);
    break;
  }
  case AST_FOR_STMT: {
    ASTNode *it = s->as.for_stmt.iterable;
    int t = TY_DYN;
    if (is_range_call(it)) {
      int all_int = 1, none = 0;
      for (int i = 0; i < it->as.call_expr.arg_count; i++) {
        int at = infer_expr(fn, it->as.call_expr.args[i]);
        if (at == TY_NONE)
          none = 1;
        else if (at != TY_INT && at != TY_BOOL)
          all_int = 0;
      }
      it->ty = TY_DYN;
      t = none ? TY_NONE : (all_int || P->native ? TY_INT : TY_DYN);
    } else {
      infer_expr(fn, it);
    }
    join_into_var(s->sym, t);
    if (s->as.for_stmt.value_name)
      join_into_var(sem_lookup(P, fn, s->as.for_stmt.value_name), TY_DYN);
    infer_block(fn, s->as.for_stmt.body);
    break;
  }
  case AST_MATCH_STMT:
    infer_expr(fn, s->as.match_stmt.condition);
    for (int i = 0; i < s->as.match_stmt.case_count; i++) {
      infer_expr(fn, s->as.match_stmt.cases[i]);
      infer_block(fn, s->as.match_stmt.consequences[i]);
    }
    infer_block(fn, s->as.match_stmt.default_consequence);
    break;
  case AST_RETURN_STMT:
    if (s->as.return_stmt.return_value)
      join_ret(fn, infer_expr(fn, s->as.return_stmt.return_value));
    else
      join_ret(fn, P->native ? TY_INT : TY_DYN);
    break;
  case AST_TRY_CATCH:
    infer_block(fn, s->as.try_catch_stmt.try_block);
    join_into_var(sem_lookup(P, fn, s->as.try_catch_stmt.catch_var), TY_DYN);
    infer_block(fn, s->as.try_catch_stmt.catch_block);
    break;
  case AST_THROW:
    infer_expr(fn, s->as.throw_stmt.exception_value);
    break;
  default:
    break;
  }
}

// Conservative: can execution fall off the end of this statement list?
static int stmt_falls_through(ASTNode *s);

static int block_falls_through(ASTNode *b) {
  if (!b)
    return 1;
  if (b->type != AST_BLOCK)
    return stmt_falls_through(b);
  for (int i = 0; i < b->as.block.statement_count; i++)
    if (!stmt_falls_through(b->as.block.statements[i]))
      return 0;
  return 1;
}

static int contains_break(ASTNode *s) {
  if (!s)
    return 0;
  switch (s->type) {
  case AST_BREAK_STMT:
    return 1;
  case AST_BLOCK:
    for (int i = 0; i < s->as.block.statement_count; i++)
      if (contains_break(s->as.block.statements[i]))
        return 1;
    return 0;
  case AST_IF_STMT:
    if (contains_break(s->as.if_stmt.consequence) ||
        contains_break(s->as.if_stmt.alternative))
      return 1;
    for (int i = 0; i < s->as.if_stmt.elif_count; i++)
      if (contains_break(s->as.if_stmt.elif_consequences[i]))
        return 1;
    return 0;
  case AST_MATCH_STMT:
    for (int i = 0; i < s->as.match_stmt.case_count; i++)
      if (contains_break(s->as.match_stmt.consequences[i]))
        return 1;
    return contains_break(s->as.match_stmt.default_consequence);
  case AST_TRY_CATCH:
    return contains_break(s->as.try_catch_stmt.try_block) ||
           contains_break(s->as.try_catch_stmt.catch_block);
  default:
    return 0; // nested loops own their breaks
  }
}

static int stmt_falls_through(ASTNode *s) {
  if (!s)
    return 1;
  switch (s->type) {
  case AST_RETURN_STMT:
  case AST_THROW:
    return 0;
  case AST_BLOCK:
    return block_falls_through(s);
  case AST_IF_STMT:
    if (!s->as.if_stmt.alternative)
      return 1;
    if (block_falls_through(s->as.if_stmt.consequence) ||
        block_falls_through(s->as.if_stmt.alternative))
      return 1;
    for (int i = 0; i < s->as.if_stmt.elif_count; i++)
      if (block_falls_through(s->as.if_stmt.elif_consequences[i]))
        return 1;
    return 0;
  case AST_WHILE_STMT: {
    ASTNode *c = s->as.while_stmt.condition;
    if (c->type == AST_BOOLEAN_LITERAL && c->as.boolean_literal.value &&
        !contains_break(s->as.while_stmt.body))
      return 0;
    return 1;
  }
  case AST_TRY_CATCH:
    return block_falls_through(s->as.try_catch_stmt.try_block) ||
           block_falls_through(s->as.try_catch_stmt.catch_block);
  default:
    return 1;
  }
}

// ------------------------------------------------------------
// Driver
// ------------------------------------------------------------

static ASTNode *func_body(FuncInfo *fn) { return fn->decl->as.function_decl.body; }

static void finalize_types(void) {
  int deflt = P->native ? TY_INT : TY_DYN;
  for (int i = 0; i < P->nglobals; i++)
    if (P->globals[i]->ty == TY_NONE)
      P->globals[i]->ty = deflt;
  for (int f = 0; f < P->nfuncs; f++) {
    FuncInfo *fn = P->funcs[f];
    for (int i = 0; i < fn->nlocals; i++)
      if (fn->locals[i]->ty == TY_NONE)
        fn->locals[i]->ty = deflt;
    if (fn->ret_ty == TY_NONE)
      fn->ret_ty = deflt;
  }
}

static ASTNode *main_body_block;

Program *semantic_analyze(ASTNode *program, Target target, int win64,
                          const char *filename) {
  P = calloc(1, sizeof(Program));
  P->target = target;
  P->native = target != TARGET_HOSTED;
  P->win64 = win64;
  P->filename = filename;

  // Main pseudo-function holds top-level statements
  FuncInfo *mainf = calloc(1, sizeof(FuncInfo));
  mainf->name = ast_strdup("<main>");
  mainf->label = ast_strdup("S___main");
  mainf->is_main = 1;
  P->main_func = mainf;

  main_body_block = ast_new(AST_BLOCK, 1);
  // Declarations first (so that forward references work)
  for (int i = 0; i < program->as.program.statement_count; i++) {
    ASTNode *s = program->as.program.statements[i];
    declare_toplevel(s);
  }
  for (int i = 0; i < program->as.program.statement_count; i++) {
    ASTNode *s = program->as.program.statements[i];
    switch (s->type) {
    case AST_FUNCTION_DECL:
    case AST_CLASS_DECL:
    case AST_STRUCT_DECL:
    case AST_CONST_DECL:
    case AST_STATIC_DECL:
    case AST_C_FUNCTION_DECL:
    case AST_IMPORT_NATIVE:
    case AST_IMPORT_STMT:
      break;
    default:
      AST_PUSH(main_body_block->as.block.statements,
               main_body_block->as.block.statement_count, s);
    }
  }
  // main is processed first (its globals must exist before functions)
  {
    FuncInfo **old = P->funcs;
    int n = P->nfuncs;
    P->funcs = NULL;
    P->nfuncs = 0;
    AST_PUSH(P->funcs, P->nfuncs, mainf);
    for (int i = 0; i < n; i++)
      AST_PUSH(P->funcs, P->nfuncs, old[i]);
    free(old);
  }

  // Link class parents
  for (int i = 0; i < P->nclasses; i++) {
    ClassInfo *c = P->classes[i];
    if (c->parent_name) {
      Var *pv = find_global(c->parent_name);
      if (!pv || pv->kind != SYM_CLASS)
        error(c->decl, "parent class '%s' of '%s' not found", c->parent_name,
              c->name);
      else
        c->parent = pv->cls;
    }
  }
  for (int i = 0; i < P->nclasses; i++) { // cycle check
    int steps = 0;
    for (ClassInfo *c = P->classes[i]; c && steps < 1000; c = c->parent)
      steps++;
    if (steps >= 1000) {
      error(P->classes[i]->decl, "inheritance cycle involving class '%s'",
            P->classes[i]->name);
      P->classes[i]->parent = NULL;
    }
  }

  // Phase 2: params + locals (lambdas discovered on the way are appended)
  collect_globals_decl(mainf, main_body_block);
  collect_block(mainf, main_body_block);
  for (int f = 1; f < P->nfuncs; f++) {
    FuncInfo *fn = P->funcs[f];
    setup_params(fn);
    collect_globals_decl(fn, func_body(fn));
    collect_block(fn, func_body(fn));
    fn->falls_through = block_falls_through(func_body(fn));
  }

  // Phase 3: resolution
  for (int f = 0; f < P->nfuncs; f++) {
    FuncInfo *fn = P->funcs[f];
    loop_nesting = 0;
    resolve_block(fn, fn->is_main ? main_body_block : func_body(fn), 0);
    if (!fn->is_main) {
      FunctionDeclNode *fd = &fn->decl->as.function_decl;
      for (int i = 0; i < fd->param_count; i++)
        if (fd->param_defaults && fd->param_defaults[i])
          resolve_expr(P->main_func, fd->param_defaults[i], 0);
    }
  }

  if (P->native) {
    for (int f = 1; f < P->nfuncs; f++)
      P->funcs[f]->escapes = 0;
  }

  // Escaping functions: untyped params become dynamic
  for (int f = 1; f < P->nfuncs; f++) {
    FuncInfo *fn = P->funcs[f];
    if (fn->escapes || fn->is_method || fn->is_lambda) {
      for (int i = 0; i < fn->nparams; i++)
        if (!fn->params[i]->declared) {
          fn->params[i]->ty = P->native ? TY_INT : TY_DYN;
          fn->params[i]->declared = 1;
        }
    }
    if (fn->falls_through)
      fn->ret_ty = fn->ret_declared ? fn->ret_ty
                                    : (P->native ? TY_INT : TY_DYN);
  }

  // Phase 4: fixpoint type inference
  if (P->error_count == 0) {
    int iter = 0;
    do {
      changed = 0;
      for (int f = 0; f < P->nfuncs; f++) {
        FuncInfo *fn = P->funcs[f];
        infer_block(fn, fn->is_main ? main_body_block : func_body(fn));
      }
    } while (changed && ++iter < 200);
    finalize_types();
    // final pass so that every node carries its final type
    for (int f = 0; f < P->nfuncs; f++) {
      FuncInfo *fn = P->funcs[f];
      infer_block(fn, fn->is_main ? main_body_block : func_body(fn));
      if (!fn->is_main) {
        FunctionDeclNode *fd = &fn->decl->as.function_decl;
        for (int i = 0; i < fd->param_count; i++)
          if (fd->param_defaults && fd->param_defaults[i])
            infer_expr(fn, fd->param_defaults[i]);
      }
    }
    finalize_types();
  }

  // Wrappers for typed functions used dynamically
  for (int f = 1; f < P->nfuncs; f++) {
    FuncInfo *fn = P->funcs[f];
    if (P->native)
      continue;
    if (fn->escapes || fn->is_method || fn->is_lambda) {
      int typed = fn->ret_ty != TY_DYN;
      for (int i = 0; i < fn->nparams; i++)
        if (fn->params[i]->ty != TY_DYN)
          typed = 1;
      fn->needs_wrapper = typed;
    }
  }

  mainf->decl = ast_new(AST_FUNCTION_DECL, 1);
  mainf->decl->as.function_decl.name = ast_strdup("<main>");
  mainf->decl->as.function_decl.body = main_body_block;
  return P;
}

void semantic_print_diagnostics(Program *prog) {
  for (int i = 0; i < prog->warning_count; i++)
    fprintf(stderr, "%s\n", prog->warnings[i]);
  for (int i = 0; i < prog->error_count; i++)
    fprintf(stderr, "%s\n", prog->errors[i]);
}
