#include "codegen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// Code Generator for StolasScript — Windows x64 ABI, Intel syntax
// All values are StolaValue* pointers (8 bytes) on the stack.
// Literals call runtime constructors; ops dispatch through runtime.
// ============================================================

static void generate_node(ASTNode *node, FILE *out, SemanticAnalyzer *analyzer);

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
  string_table[string_table_count].value = _strdup(value);
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

// Emit a safe Windows x64 ABI call: align stack, shadow space, call, restore
static void emit_call(FILE *out, const char *func_name) {
  fprintf(out, "    mov r10, rsp\n");
  fprintf(out, "    and rsp, -16\n");
  fprintf(out, "    sub rsp, 48\n");
  fprintf(out, "    mov [rsp + 32], r10\n");
  fprintf(out, "    call %s\n", func_name);
  fprintf(out, "    mov rsp, [rsp + 32]\n");
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
    {NULL, NULL, 0}};

static BuiltinEntry *find_builtin(const char *name) {
  for (int i = 0; builtins[i].stola_name; i++) {
    if (strcmp(builtins[i].stola_name, name) == 0)
      return &builtins[i];
  }
  return NULL;
}

void codegen_generate(ASTNode *program, SemanticAnalyzer *analyzer,
                      const char *output_file) {
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

  // Declare all external runtime functions
  for (int i = 0; builtins[i].stola_name; i++) {
    fprintf(out, ".extern %s\n", builtins[i].c_name);
  }
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

  fprintf(out, "\n.text\n");

  // Main function
  fprintf(out, "main:\n");
  fprintf(out, "    push rbp\n");
  fprintf(out, "    mov rbp, rsp\n");
  fprintf(out, "    sub rsp, 512\n");

  if (program && program->type == AST_PROGRAM) {
    for (int i = 0; i < program->as.program.statement_count; i++) {
      ASTNode *stmt = program->as.program.statements[i];
      if (stmt->type != AST_FUNCTION_DECL && stmt->type != AST_STRUCT_DECL)
        generate_node(stmt, out, analyzer);
    }
  }

  fprintf(out, "    xor eax, eax\n");
  fprintf(out, "    add rsp, 512\n");
  fprintf(out, "    pop rbp\n");
  fprintf(out, "    ret\n");

  // User-defined functions
  if (program && program->type == AST_PROGRAM) {
    for (int i = 0; i < program->as.program.statement_count; i++) {
      ASTNode *stmt = program->as.program.statements[i];
      if (stmt->type == AST_FUNCTION_DECL)
        generate_node(stmt, out, analyzer);
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

  fclose(out);
}

static void generate_node(ASTNode *node, FILE *out,
                          SemanticAnalyzer *analyzer) {
  if (!node)
    return;

  switch (node->type) {
  // --- Literals: create heap StolaValue* via runtime ---
  case AST_NUMBER_LITERAL: {
    fprintf(out, "    mov rcx, %s\n", node->as.number_literal.value);
    emit_call(out, "stola_new_int");
    fprintf(out, "    push rax\n");
    break;
  }
  case AST_STRING_LITERAL: {
    int sid = add_string_literal(node->as.string_literal.value);
    fprintf(out, "    lea rcx, [rip + .str%d]\n", sid);
    emit_call(out, "stola_new_string");
    fprintf(out, "    push rax\n");
    break;
  }
  case AST_BOOLEAN_LITERAL: {
    fprintf(out, "    mov rcx, %d\n", node->as.boolean_literal.value);
    emit_call(out, "stola_new_bool");
    fprintf(out, "    push rax\n");
    break;
  }
  case AST_NULL_LITERAL: {
    emit_call(out, "stola_new_null");
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
    generate_node(node->as.assignment.value, out, analyzer);
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
                    analyzer);
      fprintf(out, "    pop rcx\n"); // obj
      const char *field = node->as.assignment.target->as.member_access.property
                              ->as.identifier.value;
      int fid = add_string_literal(field);
      fprintf(out, "    lea rdx, [rip + .str%d]\n", fid);
      fprintf(out, "    pop r8\n"); // value
      emit_call(out, "stola_struct_set");
    }
    break;
  }

  // --- Binary Op: dispatch through runtime ---
  case AST_BINARY_OP: {
    generate_node(node->as.binary_op.left, out, analyzer);
    generate_node(node->as.binary_op.right, out, analyzer);
    fprintf(out, "    pop rdx\n"); // right = StolaValue*
    fprintf(out, "    pop rcx\n"); // left  = StolaValue*
    const char *func = binop_runtime_func(node->as.binary_op.op.type);
    emit_call(out, func);
    fprintf(out, "    push rax\n"); // result = StolaValue*
    break;
  }

  // --- Unary Op ---
  case AST_UNARY_OP: {
    generate_node(node->as.unary_op.right, out, analyzer);
    fprintf(out, "    pop rcx\n");
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
    generate_node(node->as.expression_stmt.expression, out, analyzer);
    fprintf(out, "    pop rax\n"); // discard
    break;
  }

  // --- Block ---
  case AST_BLOCK: {
    for (int i = 0; i < node->as.block.statement_count; i++)
      generate_node(node->as.block.statements[i], out, analyzer);
    break;
  }

  // --- If / Elif / Else ---
  case AST_IF_STMT: {
    int end_label = get_label();
    int next_label = get_label();

    generate_node(node->as.if_stmt.condition, out, analyzer);
    fprintf(out, "    pop rcx\n");
    emit_call(out, "stola_is_truthy");
    fprintf(out, "    cmp rax, 0\n");
    fprintf(out, "    je .L%d\n", next_label);

    generate_node(node->as.if_stmt.consequence, out, analyzer);
    fprintf(out, "    jmp .L%d\n", end_label);

    for (int i = 0; i < node->as.if_stmt.elif_count; i++) {
      fprintf(out, ".L%d:\n", next_label);
      next_label = get_label();
      generate_node(node->as.if_stmt.elif_conditions[i], out, analyzer);
      fprintf(out, "    pop rcx\n");
      emit_call(out, "stola_is_truthy");
      fprintf(out, "    cmp rax, 0\n");
      fprintf(out, "    je .L%d\n", next_label);
      generate_node(node->as.if_stmt.elif_consequences[i], out, analyzer);
      fprintf(out, "    jmp .L%d\n", end_label);
    }

    fprintf(out, ".L%d:\n", next_label);
    if (node->as.if_stmt.alternative)
      generate_node(node->as.if_stmt.alternative, out, analyzer);
    fprintf(out, ".L%d:\n", end_label);
    break;
  }

  // --- While ---
  case AST_WHILE_STMT: {
    int loop_start = get_label();
    int loop_end = get_label();

    fprintf(out, ".L%d:\n", loop_start);
    generate_node(node->as.while_stmt.condition, out, analyzer);
    fprintf(out, "    pop rcx\n");
    emit_call(out, "stola_is_truthy");
    fprintf(out, "    cmp rax, 0\n");
    fprintf(out, "    je .L%d\n", loop_end);

    generate_node(node->as.while_stmt.body, out, analyzer);
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
    generate_node(node->as.loop_stmt.start_expr, out, analyzer);
    fprintf(out, "    pop rax\n");
    fprintf(out, "    mov [rbp - %d], rax\n", iter_offset);

    fprintf(out, ".L%d:\n", loop_start);
    // Condition: iterator < end  (use stola_lt)
    fprintf(out, "    mov rcx, [rbp - %d]\n", iter_offset);
    fprintf(out, "    push rcx\n");
    generate_node(node->as.loop_stmt.end_expr, out, analyzer);
    fprintf(out, "    pop rdx\n"); // end
    fprintf(out, "    pop rcx\n"); // iterator
    emit_call(out, "stola_lt");
    fprintf(out, "    mov rcx, rax\n");
    emit_call(out, "stola_is_truthy");
    fprintf(out, "    cmp rax, 0\n");
    fprintf(out, "    je .L%d\n", loop_end);

    generate_node(node->as.loop_stmt.body, out, analyzer);

    // Increment: iterator = iterator + step (default step = 1)
    fprintf(out, "    mov rcx, [rbp - %d]\n", iter_offset); // current
    fprintf(out, "    push rcx\n");
    if (node->as.loop_stmt.step_expr) {
      generate_node(node->as.loop_stmt.step_expr, out, analyzer);
    } else {
      fprintf(out, "    mov rcx, 1\n");
      emit_call(out, "stola_new_int");
      fprintf(out, "    push rax\n");
    }
    fprintf(out, "    pop rdx\n"); // step
    fprintf(out, "    pop rcx\n"); // current
    emit_call(out, "stola_add");
    fprintf(out, "    mov [rbp - %d], rax\n", iter_offset);
    fprintf(out, "    jmp .L%d\n", loop_start);
    fprintf(out, ".L%d:\n", loop_end);
    break;
  }

  // --- Match ---
  case AST_MATCH_STMT: {
    int end_label = get_label();
    generate_node(node->as.match_stmt.condition, out, analyzer);
    fprintf(out, "    pop r11\n"); // match value

    for (int i = 0; i < node->as.match_stmt.case_count; i++) {
      int next_case = get_label();
      fprintf(out, "    push r11\n"); // preserve match value
      fprintf(out, "    mov rcx, r11\n");
      fprintf(out, "    push rcx\n");
      generate_node(node->as.match_stmt.cases[i], out, analyzer);
      fprintf(out, "    pop rdx\n"); // case value
      fprintf(out, "    pop rcx\n"); // match value
      emit_call(out, "stola_eq");
      fprintf(out, "    mov rcx, rax\n");
      emit_call(out, "stola_is_truthy");
      fprintf(out, "    pop r11\n"); // restore match value
      fprintf(out, "    cmp rax, 0\n");
      fprintf(out, "    je .L%d\n", next_case);
      generate_node(node->as.match_stmt.consequences[i], out, analyzer);
      fprintf(out, "    jmp .L%d\n", end_label);
      fprintf(out, ".L%d:\n", next_case);
    }
    if (node->as.match_stmt.default_consequence)
      generate_node(node->as.match_stmt.default_consequence, out, analyzer);
    fprintf(out, ".L%d:\n", end_label);
    break;
  }

  // --- Function Declaration ---
  case AST_FUNCTION_DECL: {
    fprintf(out, "\n%s:\n", node->as.function_decl.name);
    fprintf(out, "    push rbp\n");
    fprintf(out, "    mov rbp, rsp\n");
    fprintf(out, "    sub rsp, 512\n");

    const char *regs[] = {"rcx", "rdx", "r8", "r9"};
    for (int i = 0; i < node->as.function_decl.param_count && i < 4; i++) {
      int offset = get_var_offset(node->as.function_decl.parameters[i]);
      fprintf(out, "    mov [rbp - %d], %s\n", offset, regs[i]);
    }

    generate_node(node->as.function_decl.body, out, analyzer);

    emit_call(out, "stola_new_null");
    fprintf(out, "    add rsp, 512\n");
    fprintf(out, "    pop rbp\n");
    fprintf(out, "    ret\n");
    break;
  }

  // --- Return ---
  case AST_RETURN_STMT: {
    if (node->as.return_stmt.return_value) {
      generate_node(node->as.return_stmt.return_value, out, analyzer);
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
    if (node->as.call_expr.function->type == AST_IDENTIFIER) {
      const char *name = node->as.call_expr.function->as.identifier.value;
      BuiltinEntry *bi = find_builtin(name);

      if (bi) {
        // Built-in: evaluate args, put in ABI registers, call C function
        const char *regs[] = {"rcx", "rdx", "r8", "r9"};
        for (int i = 0; i < node->as.call_expr.arg_count && i < 4; i++)
          generate_node(node->as.call_expr.args[i], out, analyzer);
        for (int i = node->as.call_expr.arg_count - 1; i >= 0 && i < 4; i--)
          fprintf(out, "    pop %s\n", regs[i]);
        emit_call(out, bi->c_name);
        fprintf(out, "    push rax\n");
      } else {
        // User-defined function call
        const char *regs[] = {"rcx", "rdx", "r8", "r9"};
        for (int i = 0; i < node->as.call_expr.arg_count && i < 4; i++)
          generate_node(node->as.call_expr.args[i], out, analyzer);
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
    generate_node(node->as.member_access.object, out, analyzer);
    fprintf(out, "    pop rcx\n"); // obj
    const char *field = node->as.member_access.property->as.identifier.value;
    int fid = add_string_literal(field);
    fprintf(out, "    lea rdx, [rip + .str%d]\n", fid);
    emit_call(out, "stola_struct_get");
    fprintf(out, "    push rax\n");
    break;
  }

  // --- Array Literal ---
  case AST_ARRAY_LITERAL: {
    emit_call(out, "stola_new_array");
    fprintf(out, "    push rax\n"); // array on stack

    for (int i = 0; i < node->as.array_literal.element_count; i++) {
      generate_node(node->as.array_literal.elements[i], out, analyzer);
      // Stack: [..., array, element]
      fprintf(out, "    pop rdx\n");  // element
      fprintf(out, "    pop rcx\n");  // array
      fprintf(out, "    push rcx\n"); // keep array
      fprintf(out, "    push rdx\n"); // save element
      fprintf(out, "    pop rdx\n");  // element in rdx
      // rcx was popped and repushed, need it in rcx
      fprintf(out, "    mov rcx, [rsp]\n"); // peek array from stack top
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
      fprintf(out, "    lea rcx, [rip + .str%d]\n", kid);
      emit_call(out, "stola_new_string");
      fprintf(out, "    push rax\n"); // key

      // Generate value
      generate_node(node->as.dict_literal.values[i], out, analyzer);
      // Stack: [..., dict, key, value]
      fprintf(out, "    pop r8\n");         // value
      fprintf(out, "    pop rdx\n");        // key
      fprintf(out, "    mov rcx, [rsp]\n"); // peek dict
      emit_call(out, "stola_dict_set");
    }
    break;
  }

  default:
    break;
  }
}
