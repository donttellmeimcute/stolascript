#include "semantic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Note: To avoid deprecation warnings on Windows
#ifdef _WIN32
#pragma warning(disable : 4996)
#define strdup _strdup
#endif

// Forward declarations
static void analyze_node(SemanticAnalyzer *analyzer, ASTNode *node);

static SymbolTable *create_symbol_table(SymbolTable *outer, int is_function) {
  SymbolTable *table = malloc(sizeof(SymbolTable));
  table->symbols = NULL;
  table->is_function_scope = is_function;

  if (is_function || !outer) {
    table->local_count = 0;
  } else {
    table->local_count = outer->local_count;
  }
  table->outer = outer;
  return table;
}

static void free_symbol_table(SymbolTable *table) {
  Symbol *sym = table->symbols;
  while (sym) {
    Symbol *next = sym->next;
    free(sym->name);
    free(sym);
    sym = next;
  }
  free(table);
}

static void analyzer_add_error(SemanticAnalyzer *analyzer, const char *msg) {
  analyzer->error_count++;
  analyzer->errors =
      realloc(analyzer->errors, sizeof(char *) * analyzer->error_count);
  analyzer->errors[analyzer->error_count - 1] = strdup(msg);
}

static Symbol *define_symbol(SemanticAnalyzer *analyzer, const char *name,
                             SymbolType type, int arity, const char *val_type) {
  Symbol *sym = malloc(sizeof(Symbol));
  sym->name = strdup(name);
  sym->type = type;
  sym->arity = arity;
  sym->value_type = val_type ? strdup(val_type) : strdup("any");
  sym->return_type = strdup("any");
  sym->param_types = NULL;

  if (type == SYMBOL_LOCAL) {
    sym->index = analyzer->current_scope->local_count++;
  } else {
    sym->index = 0; // Globals might need a different allocation strategy later
  }

  sym->next = analyzer->current_scope->symbols;
  analyzer->current_scope->symbols = sym;
  return sym;
}

Symbol *resolve_symbol(SemanticAnalyzer *analyzer, const char *name) {
  SymbolTable *current = analyzer->current_scope;
  while (current) {
    Symbol *sym = current->symbols;
    while (sym) {
      if (strcmp(sym->name, name) == 0) {
        return sym;
      }
      sym = sym->next;
    }
    current = current->outer;
  }
  return NULL; // Not found
}

static void enter_scope(SemanticAnalyzer *analyzer, int is_function) {
  SymbolTable *new_scope =
      create_symbol_table(analyzer->current_scope, is_function);
  analyzer->current_scope = new_scope;
}

static void leave_scope(SemanticAnalyzer *analyzer) {
  SymbolTable *old_scope = analyzer->current_scope;
  // We should copy the updated local count back to the outer scope if it wasn't
  // a function scope
  if (!old_scope->is_function_scope && old_scope->outer) {
    old_scope->outer->local_count = old_scope->local_count;
  }

  analyzer->current_scope = old_scope->outer;
  free_symbol_table(old_scope);
}

void semantic_init(SemanticAnalyzer *analyzer, int is_freestanding) {
  analyzer->current_scope = create_symbol_table(NULL, 1);
  analyzer->errors = NULL;
  analyzer->error_count = 0;
  analyzer->in_class = 0;
  analyzer->is_freestanding = is_freestanding;

  if (is_freestanding)
    return;

  // Define built-in functions
  define_symbol(analyzer, "print", SYMBOL_FUNCTION, 1, "any");
  define_symbol(analyzer, "len", SYMBOL_FUNCTION, 1, "number");
  define_symbol(analyzer, "length", SYMBOL_FUNCTION, 1, "number");
  define_symbol(analyzer, "range", SYMBOL_FUNCTION, 2, "array");
  define_symbol(analyzer, "push", SYMBOL_FUNCTION, 2, "any");
  define_symbol(analyzer, "pop", SYMBOL_FUNCTION, 1, "any");
  define_symbol(analyzer, "shift", SYMBOL_FUNCTION, 1, "any");
  define_symbol(analyzer, "unshift", SYMBOL_FUNCTION, 2, "any");
  define_symbol(analyzer, "to_string", SYMBOL_FUNCTION, 1, "string");
  define_symbol(analyzer, "to_number", SYMBOL_FUNCTION, 1, "number");
  define_symbol(analyzer, "string_split", SYMBOL_FUNCTION, 2, "array");
  define_symbol(analyzer, "string_starts_with", SYMBOL_FUNCTION, 2, "bool");
  define_symbol(analyzer, "string_ends_with", SYMBOL_FUNCTION, 2, "bool");
  define_symbol(analyzer, "string_contains", SYMBOL_FUNCTION, 2, "bool");
  define_symbol(analyzer, "string_substring", SYMBOL_FUNCTION, 3, "string");
  define_symbol(analyzer, "string_index_of", SYMBOL_FUNCTION, 2, "number");
  define_symbol(analyzer, "string_replace", SYMBOL_FUNCTION, 3, "string");
  define_symbol(analyzer, "string_trim", SYMBOL_FUNCTION, 1, "string");
  define_symbol(analyzer, "uppercase", SYMBOL_FUNCTION, 1, "string");
  define_symbol(analyzer, "lowercase", SYMBOL_FUNCTION, 1, "string");
  define_symbol(analyzer, "socket_connect", SYMBOL_FUNCTION, 2, "number");
  define_symbol(analyzer, "socket_send", SYMBOL_FUNCTION, 2, "number");
  define_symbol(analyzer, "socket_receive", SYMBOL_FUNCTION, 1, "string");
  define_symbol(analyzer, "socket_close", SYMBOL_FUNCTION, 1, "any");
  define_symbol(analyzer, "ws_connect", SYMBOL_FUNCTION, 1, "number");
  define_symbol(analyzer, "ws_send", SYMBOL_FUNCTION, 2, "number");
  define_symbol(analyzer, "ws_receive", SYMBOL_FUNCTION, 1, "string");
  define_symbol(analyzer, "ws_close", SYMBOL_FUNCTION, 1, "any");
  define_symbol(analyzer, "ws_server_create", SYMBOL_FUNCTION, 1, "number");
  define_symbol(analyzer, "ws_server_accept", SYMBOL_FUNCTION, 1, "number");
  define_symbol(analyzer, "ws_server_close", SYMBOL_FUNCTION, 1, "any");
  define_symbol(analyzer, "ws_select", SYMBOL_FUNCTION, 2, "any");
  define_symbol(analyzer, "json_encode", SYMBOL_FUNCTION, 1, "string");
  define_symbol(analyzer, "json_decode", SYMBOL_FUNCTION, 1, "any");
  define_symbol(analyzer, "current_time", SYMBOL_FUNCTION, 0, "number");
  define_symbol(analyzer, "sleep", SYMBOL_FUNCTION, 1, "any");
  define_symbol(analyzer, "random", SYMBOL_FUNCTION, 0, "number");
  define_symbol(analyzer, "floor", SYMBOL_FUNCTION, 1, "number");
  define_symbol(analyzer, "ceil", SYMBOL_FUNCTION, 1, "number");
  define_symbol(analyzer, "round", SYMBOL_FUNCTION, 1, "number");
  define_symbol(analyzer, "read_file", SYMBOL_FUNCTION, 1, "string");
  define_symbol(analyzer, "write_file", SYMBOL_FUNCTION, 2, "bool");
  define_symbol(analyzer, "append_file", SYMBOL_FUNCTION, 2, "bool");
  define_symbol(analyzer, "file_exists", SYMBOL_FUNCTION, 1, "bool");
  define_symbol(analyzer, "http_fetch", SYMBOL_FUNCTION, 1, "any");
  define_symbol(analyzer, "thread_spawn", SYMBOL_FUNCTION, 2, "number");
  define_symbol(analyzer, "thread_join", SYMBOL_FUNCTION, 1, "any");
  define_symbol(analyzer, "mutex_create", SYMBOL_FUNCTION, 0, "number");
  define_symbol(analyzer, "mutex_lock", SYMBOL_FUNCTION, 1, "any");
  define_symbol(analyzer, "mutex_unlock", SYMBOL_FUNCTION, 1, "any");
}

void semantic_free(SemanticAnalyzer *analyzer) {
  while (analyzer->current_scope) {
    leave_scope(analyzer);
  }
  for (int i = 0; i < analyzer->error_count; i++) {
    free(analyzer->errors[i]);
  }
  if (analyzer->errors) {
    free(analyzer->errors);
  }
}

int semantic_analyze(SemanticAnalyzer *analyzer, ASTNode *program) {
  if (!program || program->type != AST_PROGRAM)
    return 0;

  // ── Pre-pass: hoist all top-level function and class names ──────────────
  // This allows functions defined later in the file (or stdlib) to be called
  // by functions defined earlier, without needing forward declarations in the
  // source.  We register every AST_FUNCTION_DECL and AST_CLASS_DECL name
  // in the global scope before we fully analyze any body.
  for (int i = 0; i < program->as.program.statement_count; i++) {
    ASTNode *stmt = program->as.program.statements[i];
    if (stmt->type == AST_FUNCTION_DECL) {
      const char *fname = stmt->as.function_decl.name;
      if (!resolve_symbol(analyzer, fname)) {
        define_symbol(analyzer, fname, SYMBOL_FUNCTION,
                      stmt->as.function_decl.param_count, "any");
      }
    } else if (stmt->type == AST_CLASS_DECL) {
      const char *cname = stmt->as.class_decl.name;
      if (!resolve_symbol(analyzer, cname)) {
        define_symbol(analyzer, cname, SYMBOL_CLASS, 0, cname);
      }
    }
  }
  // ────────────────────────────────────────────────────────────────────────

  for (int i = 0; i < program->as.program.statement_count; i++) {
    ASTNode *stmt = program->as.program.statements[i];
    if (analyzer->is_freestanding) {
      if (stmt->type == AST_CLASS_DECL) {
        analyzer_add_error(analyzer,
                           "Classes are not supported in freestanding mode.");
      } else if (stmt->type == AST_TRY_CATCH || stmt->type == AST_THROW) {
        analyzer_add_error(
            analyzer,
            "Exception handling is not supported in freestanding mode.");
      }
    }
    analyze_node(analyzer, stmt);
  }

  return analyzer->error_count == 0;
}

static void analyze_node(SemanticAnalyzer *analyzer, ASTNode *node) {
  if (!node)
    return;

  switch (node->type) {
  case AST_ASM_BLOCK: {
    // Inline assembly: no semantic check required.
    // Warn if used outside freestanding mode with privileged instructions.
    if (!analyzer->is_freestanding && node->as.asm_block.code) {
      const char *code = node->as.asm_block.code;
      if (strstr(code, "hlt") || strstr(code, "lgdt") ||
          strstr(code, "lidt") || strstr(code, "in ") ||
          strstr(code, "out ")) {
        printf("[StolasScript Warning] Privileged instruction(s) in 'asm {}' "
               "block outside --freestanding mode.\n");
      }
    }
    break;
  }

  case AST_FUNCTION_DECL: {
    // Warn if interrupt function is used outside freestanding mode
    if (node->as.function_decl.is_interrupt && !analyzer->is_freestanding) {
      printf("[StolasScript Warning] 'interrupt function %s' should be used "
             "with --freestanding (kernel/bare-metal context).\n",
             node->as.function_decl.name);
    }

    // Register function first for recursion
    Symbol *func_sym = define_symbol(
        analyzer, node->as.function_decl.name, SYMBOL_FUNCTION,
        node->as.function_decl.param_count, node->as.function_decl.return_type);

    if (func_sym) {
      func_sym->param_types = malloc(sizeof(char *) * func_sym->arity);
      for (int i = 0; i < func_sym->arity; i++) {
        func_sym->param_types[i] =
            strdup(node->as.function_decl.param_types[i]);
      }
    }

    enter_scope(analyzer, 1);
    // Define parameters as locals
    for (int i = 0; i < node->as.function_decl.param_count; i++) {
      define_symbol(analyzer, node->as.function_decl.parameters[i],
                    SYMBOL_LOCAL, 0, node->as.function_decl.param_types[i]);
    }

    analyze_node(analyzer, node->as.function_decl.body);
    leave_scope(analyzer);
    break;
  }

  case AST_STRUCT_DECL: {
    define_symbol(analyzer, node->as.struct_decl.name, SYMBOL_STRUCT,
                  node->as.struct_decl.field_count, "struct");
    break;
  }
  case AST_C_FUNCTION_DECL: {
    define_symbol(analyzer, node->as.c_function_decl.name, SYMBOL_C_FUNCTION,
                  node->as.c_function_decl.param_count, "any");
    break;
  }
  case AST_IMPORT_NATIVE: {
    // Just valid at top level, no scope check needed for now.
    break;
  }
  case AST_CLASS_DECL: {
    // Register class as a symbol
    define_symbol(analyzer, node->as.class_decl.name, SYMBOL_CLASS,
                  node->as.class_decl.method_count, "class");

    analyzer->in_class++;
    for (int i = 0; i < node->as.class_decl.method_count; i++) {
      analyze_node(analyzer, node->as.class_decl.methods[i]);
    }
    analyzer->in_class--;
    break;
  }

  case AST_BLOCK: {
    enter_scope(analyzer, 0);
    for (int i = 0; i < node->as.block.statement_count; i++) {
      analyze_node(analyzer, node->as.block.statements[i]);
    }
    leave_scope(analyzer);
    break;
  }

  case AST_ASSIGNMENT: {
    analyze_node(analyzer, node->as.assignment.value);

    // If target is an identifier, we might be defining a new variable or
    // assigning to an existing one
    if (node->as.assignment.target->type == AST_IDENTIFIER) {
      const char *name = node->as.assignment.target->as.identifier.value;
      Symbol *sym = resolve_symbol(analyzer, name);
      if (!sym) {
        // Implicit declaration (dynamic language semantics)
        SymbolType stype = (analyzer->current_scope->outer == NULL)
                               ? SYMBOL_GLOBAL
                               : SYMBOL_LOCAL;
        sym = define_symbol(analyzer, name, stype, 0,
                            node->as.assignment.type_annotation);
      } else {
        // Enforce strong typing for strict declarations
        if (strcmp(node->as.assignment.type_annotation, "any") != 0 &&
            strcmp(sym->value_type, "any") != 0 &&
            strcmp(sym->value_type, node->as.assignment.type_annotation) != 0) {
          printf("[StolasScript Warning] Relajación de tipo dinámica: Variable "
                 "'%s' estaba tipada como '%s', pero se asigna de tipo '%s'\n",
                 name, sym->value_type, node->as.assignment.type_annotation);
        }
      }
    } else {
      // E.g., arr[0] = 5 or p.age = 26
      analyze_node(analyzer, node->as.assignment.target);
    }
    break;
  }

  case AST_IDENTIFIER: {
    Symbol *sym = resolve_symbol(analyzer, node->as.identifier.value);
    if (!sym) {
      char msg[256];
      snprintf(msg, sizeof(msg), "Undefined variable or function '%s'",
               node->as.identifier.value);
      analyzer_add_error(analyzer, msg);
    }
    break;
  }

  case AST_CALL_EXPR: {
    analyze_node(analyzer, node->as.call_expr.function);

    // Note: because the language is dynamic, we allow calling identifiers.
    // But we can check arity if the function identifier resolves to an
    // ahead-of-time function or builtin
    if (node->as.call_expr.function->type == AST_IDENTIFIER) {
      Symbol *sym = resolve_symbol(
          analyzer, node->as.call_expr.function->as.identifier.value);
      if (sym && sym->type == SYMBOL_FUNCTION) {
        // Check arity
        // For now, struct initialization is parsed as Call Expr! So struct has
        // arity = field count
      } else if (sym && sym->type == SYMBOL_STRUCT) {
        // Struct constructor check
        if (sym->arity != node->as.call_expr.arg_count) {
          char msg[256];
          snprintf(msg, sizeof(msg),
                   "Struct '%s' constructor expects %d arguments, got %d",
                   sym->name, sym->arity, node->as.call_expr.arg_count);
          analyzer_add_error(analyzer, msg);
        }
      }
    }

    for (int i = 0; i < node->as.call_expr.arg_count; i++) {
      analyze_node(analyzer, node->as.call_expr.args[i]);
    }
    break;
  }

  case AST_NEW_EXPR: {
    if (node->as.new_expr.class_name->type == AST_IDENTIFIER) {
      Symbol *sym = resolve_symbol(
          analyzer, node->as.new_expr.class_name->as.identifier.value);
      if (!sym || sym->type != SYMBOL_CLASS) {
        char msg[256];
        snprintf(msg, sizeof(msg), "Cannot instantiate non-class '%s'",
                 node->as.new_expr.class_name->as.identifier.value);
        analyzer_add_error(analyzer, msg);
      }
    }
    for (int i = 0; i < node->as.new_expr.arg_count; i++) {
      analyze_node(analyzer, node->as.new_expr.args[i]);
    }
    break;
  }

  case AST_THIS: {
    if (analyzer->in_class == 0) {
      analyzer_add_error(analyzer,
                         "'this' can only be used inside a class method");
    }
    break;
  }

  case AST_BINARY_OP:
    analyze_node(analyzer, node->as.binary_op.left);
    analyze_node(analyzer, node->as.binary_op.right);
    break;

  case AST_UNARY_OP:
    analyze_node(analyzer, node->as.unary_op.right);
    break;

  case AST_IF_STMT:
    analyze_node(analyzer, node->as.if_stmt.condition);
    analyze_node(analyzer, node->as.if_stmt.consequence);
    for (int i = 0; i < node->as.if_stmt.elif_count; i++) {
      analyze_node(analyzer, node->as.if_stmt.elif_conditions[i]);
      analyze_node(analyzer, node->as.if_stmt.elif_consequences[i]);
    }
    if (node->as.if_stmt.alternative) {
      analyze_node(analyzer, node->as.if_stmt.alternative);
    }
    break;

  case AST_WHILE_STMT:
    analyze_node(analyzer, node->as.while_stmt.condition);
    analyze_node(analyzer, node->as.while_stmt.body);
    break;

  case AST_LOOP_STMT:
    analyze_node(analyzer, node->as.loop_stmt.start_expr);
    analyze_node(analyzer, node->as.loop_stmt.end_expr);
    if (node->as.loop_stmt.step_expr) {
      analyze_node(analyzer, node->as.loop_stmt.step_expr);
    }
    enter_scope(analyzer, 0);
    define_symbol(analyzer, node->as.loop_stmt.iterator_name, SYMBOL_LOCAL, 0,
                  "number");
    analyze_node(analyzer, node->as.loop_stmt.body);
    leave_scope(analyzer);
    break;

  case AST_FOR_STMT:
    analyze_node(analyzer, node->as.for_stmt.iterable);
    enter_scope(analyzer, 0);
    define_symbol(analyzer, node->as.for_stmt.iterator_name, SYMBOL_LOCAL, 0,
                  "any");
    analyze_node(analyzer, node->as.for_stmt.body);
    leave_scope(analyzer);
    break;

  case AST_MATCH_STMT:
    analyze_node(analyzer, node->as.match_stmt.condition);
    for (int i = 0; i < node->as.match_stmt.case_count; i++) {
      analyze_node(analyzer, node->as.match_stmt.cases[i]);
      analyze_node(analyzer, node->as.match_stmt.consequences[i]);
    }
    if (node->as.match_stmt.default_consequence) {
      analyze_node(analyzer, node->as.match_stmt.default_consequence);
    }
    break;

  case AST_TRY_CATCH: {
    analyze_node(analyzer, node->as.try_catch_stmt.try_block);
    enter_scope(analyzer, 0); // New scope for catch block
    define_symbol(analyzer, node->as.try_catch_stmt.catch_var, SYMBOL_LOCAL, 0,
                  "any");
    analyze_node(analyzer, node->as.try_catch_stmt.catch_block);
    leave_scope(analyzer);
    break;
  }

  case AST_THROW:
    analyze_node(analyzer, node->as.throw_stmt.exception_value);
    break;

  case AST_RETURN_STMT:
    if (node->as.return_stmt.return_value) {
      analyze_node(analyzer, node->as.return_stmt.return_value);
    }
    break;

  case AST_EXPRESSION_STMT:
    analyze_node(analyzer, node->as.expression_stmt.expression);
    break;

  case AST_MEMBER_ACCESS:
    analyze_node(analyzer, node->as.member_access.object);
    if (node->as.member_access.is_computed) {
      analyze_node(analyzer, node->as.member_access.property);
    }
    break;

  case AST_ARRAY_LITERAL:
    for (int i = 0; i < node->as.array_literal.element_count; i++) {
      analyze_node(analyzer, node->as.array_literal.elements[i]);
    }
    break;

  case AST_DICT_LITERAL:
    for (int i = 0; i < node->as.dict_literal.pair_count; i++) {
      // Keys are identifiers used as string labels, not variable references
      // So we only analyze values, not keys
      analyze_node(analyzer, node->as.dict_literal.values[i]);
    }
    break;

  default:
    break;
  }
}

void semantic_print_errors(SemanticAnalyzer *analyzer) {
  if (analyzer->error_count > 0) {
    printf("Semantic errors:\n");
    for (int i = 0; i < analyzer->error_count; i++) {
      printf("\t%s\n", analyzer->errors[i]);
    }
  }
}
