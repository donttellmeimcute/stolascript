#define _CRT_SECURE_NO_WARNINGS
#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// Code Generator for StolasScript — x64 Assembly, Intel syntax
// Windows: Windows x64 ABI (RCX/RDX/R8/R9, 32-byte shadow space)
// Linux:   System V AMD64 ABI (RDI/RSI/RDX/RCX, no shadow space)
// All values are StolaValue* pointers (8 bytes) on the stack.
// Literals call runtime constructors; ops dispatch through runtime.
// ============================================================

#ifdef _WIN32
  #define ARG0 "rcx"
  #define ARG1 "rdx"
  #define ARG2 "r8"
  #define ARG3 "r9"
  #define stola_strdup _strdup
#else
  #define ARG0 "rdi"
  #define ARG1 "rsi"
  #define ARG2 "rdx"
  #define ARG3 "rcx"
  #define stola_strdup strdup
#endif

static void generate_node(ASTNode *node, FILE *out, SemanticAnalyzer *analyzer,
                          int is_freestanding);

static int label_counter = 0;
static int string_counter = 0;

typedef struct {
  char *value;
  int label_id;
} StringEntry;
static StringEntry string_table[512];
static int string_table_count = 0;

static int get_label(void) { return label_counter++; }

static int add_string_literal(const char *value) {
  for (int i = 0; i < string_table_count; i++) {
    if (strcmp(string_table[i].value, value) == 0)
      return string_table[i].label_id;
  }
  int id = string_counter++;
  string_table[string_table_count].value = strdup(value);
  string_table[string_table_count].label_id = id;
  string_table_count++;
  return id;
}

// Variable offset via name hashing (64 slots * 8 bytes = 512 bytes of local
// space)
static int get_var_offset(const char *name) {
  int hash = 0;
  for (int i = 0; name[i]; i++) {
    hash = (hash * 31 + name[i]) % 64;
  }
  return (hash + 1) * 8;
}

// Emit a platform-aware ABI call: align stack, call, restore
static void emit_call(FILE *out, const char *func_name) {
  fprintf(out, "    mov r10, rsp\n");
  fprintf(out, "    and rsp, -16\n");
#ifdef _WIN32
  // Windows x64: 32-byte shadow space + 16 alignment = 48 bytes
  fprintf(out, "    sub rsp, 48\n");
  fprintf(out, "    mov [rsp + 32], r10\n");
  fprintf(out, "    call %s\n", func_name);
  fprintf(out, "    mov rsp, [rsp + 32]\n");
#else
  // System V AMD64: no shadow space, push/pop to restore
  fprintf(out, "    push r10\n");
  fprintf(out, "    call %s\n", func_name);
  fprintf(out, "    pop rsp\n");
#endif
}

// Map binary operator token to runtime function name
static const char *binop_runtime_func(TokenType op) {
  switch (op) {
  case TOKEN_PLUS:
    return "stola_add";
  case TOKEN_MINUS:
    return "stola_sub";
  case TOKEN_TIMES:
    return "stola_mul";
  case TOKEN_DIVIDED_BY:
    return "stola_div";
  case TOKEN_MODULO:
    return "stola_mod";
  case TOKEN_EQUALS:
    return "stola_eq";
  case TOKEN_NOT_EQUALS:
    return "stola_neq";
  case TOKEN_LESS_THAN:
    return "stola_lt";
  case TOKEN_GREATER_THAN:
    return "stola_gt";
  case TOKEN_LESS_OR_EQUALS:
    return "stola_le";
  case TOKEN_GREATER_OR_EQUALS:
    return "stola_ge";
  case TOKEN_AND:
    return "stola_and";
  case TOKEN_OR:
    return "stola_or";
  default:
    return "stola_add";
  }
}

// Map built-in function names to their C runtime function names
// Returns NULL if not a built-in
typedef struct {
  const char *stola_name;
  const char *c_name;
  int arg_count;
} BuiltinEntry;

static BuiltinEntry builtins[] = {
    {"print", "stola_print_value", 1},
    {"length", "stola_length", 1},
    {"len", "stola_length", 1},
    {"push", "stola_push", 2},
    {"pop", "stola_pop", 1},
    {"shift", "stola_shift", 1},
    {"unshift", "stola_unshift", 2},
    {"to_string", "stola_to_string", 1},
    {"to_number", "stola_to_number", 1},
    {"string_split", "stola_string_split", 2},
    {"string_starts_with", "stola_string_starts_with", 2},
    {"string_ends_with", "stola_string_ends_with", 2},
    {"string_contains", "stola_string_contains", 2},
    {"string_substring", "stola_string_substring", 3},
    {"string_index_of", "stola_string_index_of", 2},
    {"string_replace", "stola_string_replace", 3},
    {"string_trim", "stola_string_trim", 1},
    {"uppercase", "stola_uppercase", 1},
    {"lowercase", "stola_lowercase", 1},
    {"socket_connect", "stola_socket_connect", 2},
    {"socket_send", "stola_socket_send", 2},
    {"socket_receive", "stola_socket_receive", 1},
    {"socket_close", "stola_socket_close", 1},
    {"ws_connect", "stola_ws_connect", 1},
    {"ws_send", "stola_ws_send", 2},
    {"ws_receive", "stola_ws_receive", 1},
    {"ws_close", "stola_ws_close", 1},
    {"ws_server_create", "stola_ws_server_create", 1},
    {"ws_server_accept", "stola_ws_server_accept", 1},
    {"ws_server_close", "stola_ws_server_close", 1},
    {"json_encode", "stola_json_encode", 1},
    {"json_decode", "stola_json_decode", 1},
    {"current_time", "stola_current_time", 0},
    {"sleep", "stola_sleep", 1},
    {"random", "stola_random", 0},
    {"floor", "stola_floor", 1},
    {"ceil", "stola_ceil", 1},
    {"round", "stola_round", 1},
    {"read_file", "stola_read_file", 1},
    {"write_file", "stola_write_file", 2},
    {"append_file", "stola_append_file", 2},
    {"file_exists", "stola_file_exists", 1},
    {"http_fetch", "stola_http_fetch", 1},
    {"thread_join", "stola_thread_join", 1},
    {"mutex_create", "stola_mutex_create", 0},
    {"mutex_lock", "stola_mutex_lock", 1},
    {"mutex_unlock", "stola_mutex_unlock", 1},
    {NULL, NULL, 0}};

static BuiltinEntry *find_builtin(const char *name) {
  for (int i = 0; builtins[i].stola_name; i++) {
    if (strcmp(builtins[i].stola_name, name) == 0)
      return &builtins[i];
  }
  return NULL;
}

void codegen_generate(ASTNode *program, SemanticAnalyzer *analyzer,
                      const char *output_file, int is_freestanding) {
  FILE *out = fopen(output_file, "w");
  if (!out) {
    printf("Error: Could not open output file %s\n", output_file);
    return;
  }

  label_counter = 0;
  string_counter = 0;
  string_table_count = 0;

  fprintf(out, ".intel_syntax noprefix\n");
  fprintf(out, ".global main\n\n");

  // Declare all external runtime functions (skip in freestanding)
  if (!is_freestanding) {
    for (int i = 0; builtins[i].stola_name; i++) {
      fprintf(out, ".extern %s\n", builtins[i].c_name);
    }
    fprintf(out, ".extern stola_thread_spawn\n");
    fprintf(out, ".extern stola_register_method\n");
    fprintf(out, ".extern stola_invoke_method\n");
    fprintf(out, ".extern stola_load_dll\n");
    fprintf(out, ".extern stola_bind_c_function\n");
    fprintf(out, ".extern stola_invoke_c_function\n");
    fprintf(out, ".extern stola_new_int\n");
    fprintf(out, ".extern stola_new_bool\n");
    fprintf(out, ".extern stola_new_string\n");
    fprintf(out, ".extern stola_new_null\n");
    fprintf(out, ".extern stola_new_array\n");
    fprintf(out, ".extern stola_new_dict\n");
    fprintf(out, ".extern stola_new_struct\n");
    fprintf(out, ".extern stola_is_truthy\n");
    fprintf(out, ".extern stola_add\n");
    fprintf(out, ".extern stola_sub\n");
    fprintf(out, ".extern stola_mul\n");
    fprintf(out, ".extern stola_div\n");
    fprintf(out, ".extern stola_mod\n");
    fprintf(out, ".extern stola_neg\n");
    fprintf(out, ".extern stola_eq\n");
    fprintf(out, ".extern stola_neq\n");
    fprintf(out, ".extern stola_lt\n");
    fprintf(out, ".extern stola_gt\n");
    fprintf(out, ".extern stola_le\n");
    fprintf(out, ".extern stola_ge\n");
    fprintf(out, ".extern stola_and\n");
    fprintf(out, ".extern stola_or\n");
    fprintf(out, ".extern stola_not\n");
    fprintf(out, ".extern stola_struct_get\n");
    fprintf(out, ".extern stola_struct_set\n");
    fprintf(out, ".extern stola_array_get\n");
    fprintf(out, ".extern stola_array_set\n");
    fprintf(out, ".extern stola_dict_get\n");
    fprintf(out, ".extern stola_dict_set\n");
    fprintf(out, ".extern stola_push\n");
    fprintf(out, ".extern stola_push_try\n");
    fprintf(out, ".extern stola_pop_try\n");
    fprintf(out, ".extern stola_throw\n");
    fprintf(out, ".extern stola_get_error\n");
    fprintf(out, ".extern stola_register_longjmp\n");
  }

  fprintf(out, "\n.text\n");

  fprintf(out, "main:\n");
  fprintf(out, "    push rbp\n");
  fprintf(out, "    mov rbp, rsp\n");
  fprintf(out, "    sub rsp, 512\n");

  if (!is_freestanding) {
    // Register the longjmp asm routine with the C runtime
    fprintf(out, "    lea " ARG0 ", [rip + stola_longjmp]\n");
    emit_call(out, "stola_register_longjmp");
  }

  if (program && program->type == AST_PROGRAM) {
    if (!is_freestanding) {
      // 1. Register all methods first
      for (int i = 0; i < program->as.program.statement_count; i++) {
        ASTNode *stmt = program->as.program.statements[i];
        if (stmt->type == AST_CLASS_DECL) {
          for (int j = 0; j < stmt->as.class_decl.method_count; j++) {
            ASTNode *m = stmt->as.class_decl.methods[j];
            int cid = add_string_literal(stmt->as.class_decl.name);
            int mid = add_string_literal(m->as.function_decl.name);
            fprintf(out, "    lea " ARG0 ", [rip + .str%d]\n", cid);
            fprintf(out, "    lea " ARG1 ", [rip + .str%d]\n", mid);
            fprintf(out, "    lea " ARG2 ", [rip + %s_%s]\n",
                    stmt->as.class_decl.name, m->as.function_decl.name);
            emit_call(out, "stola_register_method");
          }
        } else if (stmt->type == AST_IMPORT_NATIVE) {
          int sid = add_string_literal(stmt->as.import_native.dll_name);
          fprintf(out, "    lea " ARG0 ", [rip + .str%d]\n", sid);
          emit_call(out, "stola_load_dll");
        } else if (stmt->type == AST_C_FUNCTION_DECL) {
          int sid = add_string_literal(stmt->as.c_function_decl.name);
          fprintf(out, "    lea " ARG0 ", [rip + .str%d]\n", sid);
          emit_call(out, "stola_bind_c_function");
        }
      }
    }

    // 2. Generate top level statements
    for (int i = 0; i < program->as.program.statement_count; i++) {
      ASTNode *stmt = program->as.program.statements[i];
      if (stmt->type != AST_FUNCTION_DECL && stmt->type != AST_STRUCT_DECL &&
          stmt->type != AST_CLASS_DECL && stmt->type != AST_IMPORT_NATIVE &&
          stmt->type != AST_C_FUNCTION_DECL)
        generate_node(stmt, out, analyzer, is_freestanding);
    }
  }

  fprintf(out, "    xor eax, eax\n");
  fprintf(out, "    add rsp, 512\n");
  fprintf(out, "    pop rbp\n");
  fprintf(out, "    ret\n");

  // User-defined functions & Classes
  if (program && program->type == AST_PROGRAM && !is_freestanding) {
    for (int i = 0; i < program->as.program.statement_count; i++) {
      ASTNode *stmt = program->as.program.statements[i];
      if (stmt->type == AST_FUNCTION_DECL) {
        generate_node(stmt, out, analyzer, is_freestanding);
      } else if (stmt->type == AST_CLASS_DECL) {
        for (int j = 0; j < stmt->as.class_decl.method_count; j++) {
          ASTNode *m = stmt->as.class_decl.methods[j];
          char mangled[256];
          snprintf(mangled, sizeof(mangled), "%s_%s", stmt->as.class_decl.name,
                   m->as.function_decl.name);
          char *old_name = m->as.function_decl.name;
          m->as.function_decl.name = stola_strdup(mangled);

          int old_count = m->as.function_decl.param_count;
          m->as.function_decl.param_count = old_count + 1;
          m->as.function_decl.parameters = realloc(
              m->as.function_decl.parameters, sizeof(char *) * (old_count + 1));
          for (int k = old_count; k > 0; k--) {
            m->as.function_decl.parameters[k] =
                m->as.function_decl.parameters[k - 1];
          }
          m->as.function_decl.parameters[0] = stola_strdup("this");

          generate_node(m, out, analyzer, is_freestanding);

          free(m->as.function_decl.name);
          m->as.function_decl.name = old_name;
        }
      }
    }
  } else if (program && program->type == AST_PROGRAM && is_freestanding) {
    // In freestanding, we only support functions, no classes.
    for (int i = 0; i < program->as.program.statement_count; i++) {
      ASTNode *stmt = program->as.program.statements[i];
      if (stmt->type == AST_FUNCTION_DECL) {
        generate_node(stmt, out, analyzer, is_freestanding);
      }
    }
  }

  // Emit string literal data
  if (string_table_count > 0) {
    fprintf(out, "\n.data\n");
    for (int i = 0; i < string_table_count; i++) {
      fprintf(out, ".str%d: .asciz \"%s\"\n", string_table[i].label_id,
              string_table[i].value);
      free(string_table[i].value);
    }
  }

  fprintf(out, "\n");
  if (!is_freestanding) {
    fprintf(out, "    .text\n");
    fprintf(out, "// Custom setjmp / longjmp for exception handling\n");
    fprintf(out, ".global stola_setjmp\n");
    fprintf(out, "stola_setjmp:\n");
    fprintf(out, "    mov [" ARG0 "], rbx\n");
    fprintf(out, "    mov [" ARG0 "+8], rbp\n");
    fprintf(out, "    mov [" ARG0 "+16], r12\n");
    fprintf(out, "    mov [" ARG0 "+24], r13\n");
    fprintf(out, "    mov [" ARG0 "+32], r14\n");
    fprintf(out, "    mov [" ARG0 "+40], r15\n");
    fprintf(out, "    mov [" ARG0 "+48], rsi\n");
    fprintf(out, "    mov [" ARG0 "+56], rdi\n");
    fprintf(out, "    lea " ARG1 ", [rsp+8]\n");
    fprintf(out, "    mov [" ARG0 "+64], " ARG1 "\n");
    fprintf(out, "    mov " ARG1 ", [rsp]\n");
    fprintf(out, "    mov [" ARG0 "+72], " ARG1 "\n");
    fprintf(out, "    xor rax, rax\n");
    fprintf(out, "    ret\n\n");

    fprintf(out, ".global stola_longjmp\n");
    fprintf(out, "stola_longjmp:\n");
    fprintf(out, "    mov rbx, [" ARG0 "]\n");
    fprintf(out, "    mov rbp, [" ARG0 "+8]\n");
    fprintf(out, "    mov r12, [" ARG0 "+16]\n");
    fprintf(out, "    mov r13, [" ARG0 "+24]\n");
    fprintf(out, "    mov r14, [" ARG0 "+32]\n");
    fprintf(out, "    mov r15, [" ARG0 "+40]\n");
    fprintf(out, "    mov rsi, [" ARG0 "+48]\n");
    fprintf(out, "    mov rdi, [" ARG0 "+56]\n");
    fprintf(out, "    mov rsp, [" ARG0 "+64]\n");
    fprintf(out, "    mov " ARG1 ", [" ARG0 "+72]\n");
    fprintf(out, "    mov rax, 1\n");
    fprintf(out, "    jmp " ARG1 "\n");
  }

  fclose(out);
}

static void generate_node(ASTNode *node, FILE *out, SemanticAnalyzer *analyzer,
                          int is_freestanding) {
  if (!node)
    return;

  switch (node->type) {
  // --- Literals ---
  case AST_NUMBER_LITERAL: {
    if (is_freestanding) {
      fprintf(out, "    push %s\n", node->as.number_literal.value);
    } else {
      fprintf(out, "    mov " ARG0 ", %s\n", node->as.number_literal.value);
      emit_call(out, "stola_new_int");
      fprintf(out, "    push rax\n");
    }
    break;
  }
  case AST_STRING_LITERAL: {
    if (is_freestanding) {
      // For now, we don't support strings in freestanding as they require
      // StolaValue*
      fprintf(out, "    push 0 ; Strings not supported in freestanding\n");
    } else {
      int sid = add_string_literal(node->as.string_literal.value);
      fprintf(out, "    lea " ARG0 ", [rip + .str%d]\n", sid);
      emit_call(out, "stola_new_string");
      fprintf(out, "    push rax\n");
    }
    break;
  }
  case AST_BOOLEAN_LITERAL: {
    if (is_freestanding) {
      fprintf(out, "    push %d\n", node->as.boolean_literal.value);
    } else {
      fprintf(out, "    mov " ARG0 ", %d\n", node->as.boolean_literal.value);
      emit_call(out, "stola_new_bool");
      fprintf(out, "    push rax\n");
    }
    break;
  }
  case AST_NULL_LITERAL: {
    if (is_freestanding) {
      fprintf(out, "    push 0\n");
    } else {
      emit_call(out, "stola_new_null");
      fprintf(out, "    push rax\n");
    }
    break;
  }

  case AST_NEW_EXPR: {
    const char *cname = node->as.new_expr.class_name->as.identifier.value;
    int cid = add_string_literal(cname);
    fprintf(out, "    lea " ARG0 ", [rip + .str%d]\n", cid);
    emit_call(out, "stola_new_struct"); // Create instance!
    fprintf(out, "    push rax\n");     // save instance

    // Evaluate constructor arguments (max 2 for now, mapping to ARG2 and ARG3)
    for (int i = 0; i < node->as.new_expr.arg_count && i < 2; i++) {
      generate_node(node->as.new_expr.args[i], out, analyzer, is_freestanding);
    }
    if (node->as.new_expr.arg_count > 1)
      fprintf(out, "    pop " ARG3 "\n");
    if (node->as.new_expr.arg_count > 0)
      fprintf(out, "    pop " ARG2 "\n");

    // Prepare Call to init
    fprintf(out, "    mov " ARG0 ", [rsp]\n"); // fetch instance (this) into ARG0
    int init_id = add_string_literal("init");
    fprintf(out, "    lea " ARG1 ", [rip + .str%d]\n", init_id);
    emit_call(out, "stola_invoke_method");

    // Result of AST_NEW_EXPR is the pushed instance, we ignore init()'s return.
    break;
  }

  case AST_THIS: {
    int offset = get_var_offset("this");
    fprintf(out, "    mov rax, [rbp - %d]\n", offset);
    fprintf(out, "    push rax\n");
    break;
  }

  // --- Identifier: load StolaValue* from stack slot ---
  case AST_IDENTIFIER: {
    int offset = get_var_offset(node->as.identifier.value);
    fprintf(out, "    mov rax, [rbp - %d]\n", offset);
    fprintf(out, "    push rax\n");
    break;
  }

  // --- Assignment: eval value, store StolaValue* in stack slot ---
  case AST_ASSIGNMENT: {
    generate_node(node->as.assignment.value, out, analyzer, is_freestanding);
    fprintf(out, "    pop rax\n");

    if (node->as.assignment.target->type == AST_IDENTIFIER) {
      int offset =
          get_var_offset(node->as.assignment.target->as.identifier.value);
      fprintf(out, "    mov [rbp - %d], rax\n", offset);
    } else if (node->as.assignment.target->type == AST_MEMBER_ACCESS) {
      // obj.field = value
      // rax = value (StolaValue*)
      fprintf(out, "    push rax\n"); // save value
      generate_node(node->as.assignment.target->as.member_access.object, out,
                    analyzer, is_freestanding);
      fprintf(out, "    pop " ARG0 "\n"); // obj
      const char *field = node->as.assignment.target->as.member_access.property
                              ->as.identifier.value;
      int fid = add_string_literal(field);
      fprintf(out, "    lea " ARG1 ", [rip + .str%d]\n", fid);
      fprintf(out, "    pop " ARG2 "\n"); // value
      emit_call(out, "stola_struct_set");
    }
    break;
  }

  // --- Binary Op: dispatch through runtime or native ---
  case AST_BINARY_OP: {
    generate_node(node->as.binary_op.left, out, analyzer, is_freestanding);
    generate_node(node->as.binary_op.right, out, analyzer, is_freestanding);
    fprintf(out, "    pop " ARG1 "\n"); // right
    fprintf(out, "    pop " ARG0 "\n"); // left

    if (is_freestanding) {
      switch (node->as.binary_op.op.type) {
      case TOKEN_PLUS:
        fprintf(out, "    add " ARG0 ", " ARG1 "\n");
        fprintf(out, "    push " ARG0 "\n");
        break;
      case TOKEN_MINUS:
        fprintf(out, "    sub " ARG0 ", " ARG1 "\n");
        fprintf(out, "    push " ARG0 "\n");
        break;
      case TOKEN_TIMES:
        fprintf(out, "    imul " ARG0 ", " ARG1 "\n");
        fprintf(out, "    push " ARG0 "\n");
        break;
      case TOKEN_DIVIDED_BY:
        fprintf(out, "    mov rax, " ARG0 "\n");
        fprintf(out, "    cqo\n");
        fprintf(out, "    idiv " ARG1 "\n");
        fprintf(out, "    push rax\n");
        break;
      case TOKEN_LESS_THAN:
        fprintf(out, "    cmp " ARG0 ", " ARG1 "\n");
        fprintf(out, "    setl al\n");
        fprintf(out, "    movzx rax, al\n");
        fprintf(out, "    push rax\n");
        break;
      case TOKEN_GREATER_THAN:
        fprintf(out, "    cmp " ARG0 ", " ARG1 "\n");
        fprintf(out, "    setg al\n");
        fprintf(out, "    movzx rax, al\n");
        fprintf(out, "    push rax\n");
        break;
      case TOKEN_EQUALS:
        fprintf(out, "    cmp " ARG0 ", " ARG1 "\n");
        fprintf(out, "    sete al\n");
        fprintf(out, "    movzx rax, al\n");
        fprintf(out, "    push rax\n");
        break;
      default:
        fprintf(out, "    add " ARG0 ", " ARG1 "\n");
        fprintf(out, "    push " ARG0 "\n");
        break;
      }
    } else {
      const char *func = binop_runtime_func(node->as.binary_op.op.type);
      emit_call(out, func);
      fprintf(out, "    push rax\n"); // result = StolaValue*
    }
    break;
  }

  // --- Unary Op ---
  case AST_UNARY_OP: {
    generate_node(node->as.unary_op.right, out, analyzer, is_freestanding);
    fprintf(out, "    pop " ARG0 "\n");
    if (node->as.unary_op.op.type == TOKEN_MINUS) {
      emit_call(out, "stola_neg");
    } else if (node->as.unary_op.op.type == TOKEN_NOT) {
      emit_call(out, "stola_not");
    }
    fprintf(out, "    push rax\n");
    break;
  }

  // --- Expression Statement ---
  case AST_EXPRESSION_STMT: {
    generate_node(node->as.expression_stmt.expression, out, analyzer,
                  is_freestanding);
    fprintf(out, "    pop rax\n"); // discard
    break;
  }

  // --- Block ---
  case AST_BLOCK: {
    for (int i = 0; i < node->as.block.statement_count; i++)
      generate_node(node->as.block.statements[i], out, analyzer,
                    is_freestanding);
    break;
  }

  // --- If / Elif / Else ---
  case AST_IF_STMT: {
    int end_label = get_label();
    int next_label = get_label();

    generate_node(node->as.if_stmt.condition, out, analyzer, is_freestanding);
    fprintf(out, "    pop " ARG0 "\n");
    emit_call(out, "stola_is_truthy");
    fprintf(out, "    cmp rax, 0\n");
    fprintf(out, "    je .L%d\n", next_label);

    generate_node(node->as.if_stmt.consequence, out, analyzer, is_freestanding);
    fprintf(out, "    jmp .L%d\n", end_label);

    for (int i = 0; i < node->as.if_stmt.elif_count; i++) {
      fprintf(out, ".L%d:\n", next_label);
      next_label = get_label();
      generate_node(node->as.if_stmt.elif_conditions[i], out, analyzer,
                    is_freestanding);
      fprintf(out, "    pop " ARG0 "\n");
      emit_call(out, "stola_is_truthy");
      fprintf(out, "    cmp rax, 0\n");
      fprintf(out, "    je .L%d\n", next_label);
      generate_node(node->as.if_stmt.elif_consequences[i], out, analyzer,
                    is_freestanding);
      fprintf(out, "    jmp .L%d\n", end_label);
    }

    fprintf(out, ".L%d:\n", next_label);
    if (node->as.if_stmt.alternative)
      generate_node(node->as.if_stmt.alternative, out, analyzer,
                    is_freestanding);
    fprintf(out, ".L%d:\n", end_label);
    break;
  }

  // --- While ---
  case AST_WHILE_STMT: {
    int loop_start = get_label();
    int loop_end = get_label();

    fprintf(out, ".L%d:\n", loop_start);
    generate_node(node->as.while_stmt.condition, out, analyzer,
                  is_freestanding);
    fprintf(out, "    pop " ARG0 "\n");
    emit_call(out, "stola_is_truthy");
    fprintf(out, "    cmp rax, 0\n");
    fprintf(out, "    je .L%d\n", loop_end);

    generate_node(node->as.while_stmt.body, out, analyzer, is_freestanding);
    fprintf(out, "    jmp .L%d\n", loop_start);
    fprintf(out, ".L%d:\n", loop_end);
    break;
  }

  // --- Loop (counter) ---
  case AST_LOOP_STMT: {
    int loop_start = get_label();
    int loop_end = get_label();
    int iter_offset = get_var_offset(node->as.loop_stmt.iterator_name);

    // Initialize iterator with start value
    generate_node(node->as.loop_stmt.start_expr, out, analyzer,
                  is_freestanding);
    fprintf(out, "    pop rax\n");
    fprintf(out, "    mov [rbp - %d], rax\n", iter_offset);

    fprintf(out, ".L%d:\n", loop_start);
    // Condition: iterator < end  (use stola_lt)
    fprintf(out, "    mov " ARG0 ", [rbp - %d]\n", iter_offset);
    fprintf(out, "    push " ARG0 "\n");
    generate_node(node->as.loop_stmt.end_expr, out, analyzer, is_freestanding);
    fprintf(out, "    pop " ARG1 "\n"); // end
    fprintf(out, "    pop " ARG0 "\n"); // iterator
    emit_call(out, "stola_lt");
    fprintf(out, "    mov " ARG0 ", rax\n");
    emit_call(out, "stola_is_truthy");
    fprintf(out, "    cmp rax, 0\n");
    fprintf(out, "    je .L%d\n", loop_end);

    generate_node(node->as.loop_stmt.body, out, analyzer, is_freestanding);

    // Increment: iterator = iterator + step (default step = 1)
    fprintf(out, "    mov " ARG0 ", [rbp - %d]\n", iter_offset); // current
    fprintf(out, "    push " ARG0 "\n");
    if (node->as.loop_stmt.step_expr) {
      generate_node(node->as.loop_stmt.step_expr, out, analyzer,
                    is_freestanding);
    } else {
      fprintf(out, "    mov " ARG0 ", 1\n");
      emit_call(out, "stola_new_int");
      fprintf(out, "    push rax\n");
    }
    fprintf(out, "    pop " ARG1 "\n"); // step
    fprintf(out, "    pop " ARG0 "\n"); // current
    emit_call(out, "stola_add");
    fprintf(out, "    mov [rbp - %d], rax\n", iter_offset);
    fprintf(out, "    jmp .L%d\n", loop_start);
    fprintf(out, ".L%d:\n", loop_end);
    break;
  }

  // --- Match ---
  case AST_MATCH_STMT: {
    int end_label = get_label();
    generate_node(node->as.match_stmt.condition, out, analyzer,
                  is_freestanding);
    fprintf(out, "    pop r11\n"); // match value

    for (int i = 0; i < node->as.match_stmt.case_count; i++) {
      int next_case = get_label();
      fprintf(out, "    push r11\n"); // preserve match value
      fprintf(out, "    mov " ARG0 ", r11\n");
      fprintf(out, "    push " ARG0 "\n");
      generate_node(node->as.match_stmt.cases[i], out, analyzer,
                    is_freestanding);
      fprintf(out, "    pop " ARG1 "\n"); // case value
      fprintf(out, "    pop " ARG0 "\n"); // match value
      emit_call(out, "stola_eq");
      fprintf(out, "    mov " ARG0 ", rax\n");
      emit_call(out, "stola_is_truthy");
      fprintf(out, "    pop r11\n"); // restore match value
      fprintf(out, "    cmp rax, 0\n");
      fprintf(out, "    je .L%d\n", next_case);
      generate_node(node->as.match_stmt.consequences[i], out, analyzer,
                    is_freestanding);
      fprintf(out, "    jmp .L%d\n", end_label);
      fprintf(out, ".L%d:\n", next_case);
    }
    if (node->as.match_stmt.default_consequence)
      generate_node(node->as.match_stmt.default_consequence, out, analyzer,
                    is_freestanding);
    fprintf(out, ".L%d:\n", end_label);
    break;
  }

  // --- Inline Assembly Block ---
  case AST_ASM_BLOCK: {
    const char *p = node->as.asm_block.code;
    if (!p)
      break;
    fprintf(out, "    /* asm block */\n");
    // Emit each non-empty line with 4-space indentation
    while (*p) {
      while (*p == ' ' || *p == '\t')
        p++;
      if (*p == '\n') {
        p++;
        continue;
      }
      if (*p == '\0')
        break;
      fprintf(out, "    ");
      while (*p && *p != '\n')
        fputc(*p++, out);
      fprintf(out, "\n");
      if (*p == '\n')
        p++;
    }
    break;
  }

  // --- Function Declaration ---
  case AST_FUNCTION_DECL: {
    if (node->as.function_decl.is_interrupt) {
      // ISR: exported global symbol, save/restore caller-saved regs, ends with iretq
      fprintf(out, "\n.global %s\n", node->as.function_decl.name);
      fprintf(out, "%s:\n", node->as.function_decl.name);
      // Save caller-saved registers (hardware pushed RIP/CS/RFLAGS/RSP/SS on entry)
      fprintf(out, "    push rax\n");
      fprintf(out, "    push rcx\n");
      fprintf(out, "    push rdx\n");
      fprintf(out, "    push r8\n");
      fprintf(out, "    push r9\n");
      fprintf(out, "    push r10\n");
      fprintf(out, "    push r11\n");
      // Stack frame so asm {} variable offsets work
      fprintf(out, "    push rbp\n");
      fprintf(out, "    mov rbp, rsp\n");
      fprintf(out, "    sub rsp, 256\n");

      generate_node(node->as.function_decl.body, out, analyzer, is_freestanding);

      fprintf(out, "    add rsp, 256\n");
      fprintf(out, "    pop rbp\n");
      // Restore caller-saved registers in reverse order
      fprintf(out, "    pop r11\n");
      fprintf(out, "    pop r10\n");
      fprintf(out, "    pop r9\n");
      fprintf(out, "    pop r8\n");
      fprintf(out, "    pop rdx\n");
      fprintf(out, "    pop rcx\n");
      fprintf(out, "    pop rax\n");
      fprintf(out, "    iretq\n");
      break;
    }

    fprintf(out, "\n%s:\n", node->as.function_decl.name);
    fprintf(out, "    push rbp\n");
    fprintf(out, "    mov rbp, rsp\n");
    fprintf(out, "    sub rsp, 512\n");

    const char *regs[] = {ARG0, ARG1, ARG2, ARG3};
    for (int i = 0; i < node->as.function_decl.param_count && i < 4; i++) {
      int offset = get_var_offset(node->as.function_decl.parameters[i]);
      fprintf(out, "    mov [rbp - %d], %s\n", offset, regs[i]);
    }

    generate_node(node->as.function_decl.body, out, analyzer, is_freestanding);

    emit_call(out, "stola_new_null");
    fprintf(out, "    add rsp, 512\n");
    fprintf(out, "    pop rbp\n");
    fprintf(out, "    ret\n");
    break;
  }

  // --- Return ---
  case AST_RETURN_STMT: {
    if (node->as.return_stmt.return_value) {
      generate_node(node->as.return_stmt.return_value, out, analyzer,
                    is_freestanding);
      fprintf(out, "    pop rax\n");
    } else {
      emit_call(out, "stola_new_null");
    }
    fprintf(out, "    add rsp, 512\n");
    fprintf(out, "    pop rbp\n");
    fprintf(out, "    ret\n");
    break;
  }

  // --- Function Call ---
  case AST_CALL_EXPR: {
    if (node->as.call_expr.function->type == AST_MEMBER_ACCESS) {
      // METHOD CALL! obj.method(...)
      ASTNode *obj = node->as.call_expr.function->as.member_access.object;
      ASTNode *prop = node->as.call_expr.function->as.member_access.property;
      const char *mname = prop->as.identifier.value;

      generate_node(obj, out, analyzer, is_freestanding);

      for (int i = 0; i < node->as.call_expr.arg_count && i < 2; i++) {
        generate_node(node->as.call_expr.args[i], out, analyzer,
                      is_freestanding);
      }
      if (node->as.call_expr.arg_count > 1)
        fprintf(out, "    pop " ARG3 "\n");
      if (node->as.call_expr.arg_count > 0)
        fprintf(out, "    pop " ARG2 "\n");

      fprintf(out, "    pop " ARG0 "\n"); // pop obj (this)

      int mid = add_string_literal(mname);
      fprintf(out, "    lea " ARG1 ", [rip + .str%d]\n", mid);
      emit_call(out, "stola_invoke_method");
      fprintf(out, "    push rax\n"); // method return value
    } else if (node->as.call_expr.function->type == AST_IDENTIFIER) {
      const char *name = node->as.call_expr.function->as.identifier.value;
      BuiltinEntry *bi = find_builtin(name);
      Symbol *sym = resolve_symbol(analyzer, name);

      if (sym && sym->type == SYMBOL_C_FUNCTION) {
        for (int i = 0; i < node->as.call_expr.arg_count && i < 4; i++) {
          generate_node(node->as.call_expr.args[i], out, analyzer,
                        is_freestanding);
        }

        if (node->as.call_expr.arg_count > 2)
          fprintf(out, "    pop " ARG3 "\n");
        if (node->as.call_expr.arg_count > 1)
          fprintf(out, "    pop " ARG2 "\n");
        if (node->as.call_expr.arg_count > 0)
          fprintf(out, "    pop " ARG1 "\n");

        int sid = add_string_literal(name);
        fprintf(out, "    lea " ARG0 ", [rip + .str%d]\n", sid);
        emit_call(out, "stola_invoke_c_function");
        fprintf(out, "    push rax\n");

      } else if (strcmp(name, "thread_spawn") == 0 &&
                 node->as.call_expr.arg_count == 2) {
        // Arg 1 is function identifier!
        const char *fn_name = node->as.call_expr.args[0]->as.identifier.value;
        fprintf(out, "    lea " ARG0 ", [rip + %s]\n", fn_name);
        // Arg 2 is the actual argument to pass
        generate_node(node->as.call_expr.args[1], out, analyzer,
                      is_freestanding);
        fprintf(out, "    pop " ARG1 "\n");

        emit_call(out, "stola_thread_spawn");
        fprintf(out, "    push rax\n");

      } else if (bi) {
        // Built-in: evaluate args, put in ABI registers, call C function
        const char *regs[] = {ARG0, ARG1, ARG2, ARG3};
        for (int i = 0; i < node->as.call_expr.arg_count && i < 4; i++)
          generate_node(node->as.call_expr.args[i], out, analyzer,
                        is_freestanding);
        for (int i = node->as.call_expr.arg_count - 1; i >= 0 && i < 4; i--)
          fprintf(out, "    pop %s\n", regs[i]);
        emit_call(out, bi->c_name);
        fprintf(out, "    push rax\n");
      } else {
        // User-defined function call
        const char *regs[] = {ARG0, ARG1, ARG2, ARG3};
        for (int i = 0; i < node->as.call_expr.arg_count && i < 4; i++)
          generate_node(node->as.call_expr.args[i], out, analyzer,
                        is_freestanding);
        for (int i = node->as.call_expr.arg_count - 1; i >= 0 && i < 4; i--)
          fprintf(out, "    pop %s\n", regs[i]);
        emit_call(out, name);
        fprintf(out, "    push rax\n");
      }
    }
    break;
  }

  // --- Member Access (obj.field) ---
  case AST_MEMBER_ACCESS: {
    generate_node(node->as.member_access.object, out, analyzer,
                  is_freestanding);
    fprintf(out, "    pop " ARG0 "\n"); // obj
    const char *field = node->as.member_access.property->as.identifier.value;
    int fid = add_string_literal(field);
    fprintf(out, "    lea " ARG1 ", [rip + .str%d]\n", fid);
    emit_call(out, "stola_struct_get");
    fprintf(out, "    push rax\n");
    break;
  }

  // --- Array Literal ---
  case AST_ARRAY_LITERAL: {
    emit_call(out, "stola_new_array");
    fprintf(out, "    push rax\n"); // array on stack

    for (int i = 0; i < node->as.array_literal.element_count; i++) {
      generate_node(node->as.array_literal.elements[i], out, analyzer,
                    is_freestanding);
      // Stack: [..., array, element]
      fprintf(out, "    pop " ARG1 "\n");  // element
      fprintf(out, "    pop " ARG0 "\n");  // array
      fprintf(out, "    push " ARG0 "\n"); // keep array
      fprintf(out, "    push " ARG1 "\n"); // save element
      fprintf(out, "    pop " ARG1 "\n");  // element in ARG1
      // ARG0 was popped and repushed, need it in ARG0
      fprintf(out, "    mov " ARG0 ", [rsp]\n"); // peek array from stack top
      emit_call(out, "stola_push");
    }
    // Array pointer is still on stack top
    break;
  }

  // --- Dict Literal ---
  case AST_DICT_LITERAL: {
    emit_call(out, "stola_new_dict");
    fprintf(out, "    push rax\n"); // dict on stack

    for (int i = 0; i < node->as.dict_literal.pair_count; i++) {
      const char *key_str = node->as.dict_literal.keys[i]->as.identifier.value;
      int kid = add_string_literal(key_str);
      fprintf(out, "    lea " ARG0 ", [rip + .str%d]\n", kid);
      emit_call(out, "stola_new_string");
      fprintf(out, "    push rax\n"); // key

      // Generate value
      generate_node(node->as.dict_literal.values[i], out, analyzer,
                    is_freestanding);
      // Stack: [..., dict, key, value]
      fprintf(out, "    pop " ARG2 "\n");         // value
      fprintf(out, "    pop " ARG1 "\n");        // key
      fprintf(out, "    mov " ARG0 ", [rsp]\n"); // peek dict
      emit_call(out, "stola_dict_set");
    }
    break;
  }

  // --- Try Catch ---
  case AST_TRY_CATCH: {
    int catch_label = get_label();
    int end_label = get_label();

    emit_call(out, "stola_push_try"); // returns int64_t* in rax
    fprintf(out, "    mov " ARG0 ", rax\n");

    // CRITICAL: Call our custom assembly setjmp DIRECTLY without emit_call
    // because emit_call creates an ephemeral stack frame that gets overwritten
    // by the try block, corrupting the restored stack pointer on longjmp!
    fprintf(out, "    call stola_setjmp\n");

    fprintf(out, "    cmp rax, 0\n");
    fprintf(out, "    jne .L%d\n", catch_label); // longjmp sets rax=1

    // Try block
    generate_node(node->as.try_catch_stmt.try_block, out, analyzer,
                  is_freestanding);

    // Normal exit: pop handler
    emit_call(out, "stola_pop_try");
    fprintf(out, "    jmp .L%d\n", end_label);

    // Catch block
    fprintf(out, ".L%d:\n", catch_label);
    emit_call(out, "stola_pop_try");   // pop the handler we just jumped from
    emit_call(out, "stola_get_error"); // rax = StolaValue* Error Data

    int offset = get_var_offset(node->as.try_catch_stmt.catch_var);
    fprintf(out, "    mov [rbp - %d], rax\n", offset);

    generate_node(node->as.try_catch_stmt.catch_block, out, analyzer,
                  is_freestanding);

    fprintf(out, ".L%d:\n", end_label);
    break;
  }

  // --- Throw Statement ---
  case AST_THROW: {
    generate_node(node->as.throw_stmt.exception_value, out, analyzer,
                  is_freestanding);
    fprintf(out, "    pop " ARG0 "\n"); // exception value
    emit_call(out, "stola_throw"); // does not return
    break;
  }

  default:
    break;
  }
}
