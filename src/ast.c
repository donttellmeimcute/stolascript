#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *ast_strdup(const char *s) {
  if (!s)
    return NULL;
  size_t n = strlen(s);
  char *d = malloc(n + 1);
  memcpy(d, s, n + 1);
  return d;
}

const char *ast_current_file = "<input>";

ASTNode *ast_new(ASTNodeType type, int line) {
  ASTNode *node = calloc(1, sizeof(ASTNode));
  node->type = type;
  node->line = line;
  node->file = ast_current_file;
  return node;
}

ASTNode *ast_create_identifier(const char *value, int line) {
  ASTNode *node = ast_new(AST_IDENTIFIER, line);
  node->as.identifier.value = ast_strdup(value);
  return node;
}

ASTNode *ast_create_number(const char *value, int line) {
  ASTNode *node = ast_new(AST_NUMBER_LITERAL, line);
  node->as.number_literal.value = ast_strdup(value);
  return node;
}

ASTNode *ast_create_string(const char *value, int length, int line) {
  ASTNode *node = ast_new(AST_STRING_LITERAL, line);
  node->as.string_literal.value = malloc(length + 1);
  memcpy(node->as.string_literal.value, value, length);
  node->as.string_literal.value[length] = '\0';
  node->as.string_literal.length = length;
  return node;
}

ASTNode *ast_create_binary_op(TokenType op, ASTNode *left, ASTNode *right,
                              int line) {
  ASTNode *node = ast_new(AST_BINARY_OP, line);
  node->as.binary_op.op = op;
  node->as.binary_op.left = left;
  node->as.binary_op.right = right;
  return node;
}

ASTNode *ast_create_call(ASTNode *function, int line) {
  ASTNode *node = ast_new(AST_CALL_EXPR, line);
  node->as.call_expr.function = function;
  return node;
}

ASTNode *ast_create_member_access(ASTNode *object, ASTNode *property,
                                  int is_computed, int line) {
  ASTNode *node = ast_new(AST_MEMBER_ACCESS, line);
  node->as.member_access.object = object;
  node->as.member_access.property = property;
  node->as.member_access.is_computed = is_computed;
  return node;
}

void ast_list_push(void ***arr, int *count, void *item) {
  *arr = realloc(*arr, sizeof(void *) * (*count + 1));
  (*arr)[*count] = item;
  (*count)++;
}

static void free_list(ASTNode **list, int count) {
  for (int i = 0; i < count; i++)
    ast_free(list[i]);
  free(list);
}

static void free_strings(char **list, int count) {
  for (int i = 0; i < count; i++)
    free(list[i]);
  free(list);
}

void ast_free(ASTNode *node) {
  if (!node)
    return;
  switch (node->type) {
  case AST_PROGRAM:
    free_list(node->as.program.statements, node->as.program.statement_count);
    break;
  case AST_BLOCK:
    free_list(node->as.block.statements, node->as.block.statement_count);
    break;
  case AST_EXPRESSION_STMT:
    ast_free(node->as.expression_stmt.expression);
    break;
  case AST_ASSIGNMENT:
    ast_free(node->as.assignment.target);
    ast_free(node->as.assignment.value);
    free(node->as.assignment.type_annotation);
    break;
  case AST_IF_STMT:
    ast_free(node->as.if_stmt.condition);
    ast_free(node->as.if_stmt.consequence);
    free_list(node->as.if_stmt.elif_conditions, node->as.if_stmt.elif_count);
    free_list(node->as.if_stmt.elif_consequences, node->as.if_stmt.elif_count);
    ast_free(node->as.if_stmt.alternative);
    break;
  case AST_WHILE_STMT:
    ast_free(node->as.while_stmt.condition);
    ast_free(node->as.while_stmt.body);
    break;
  case AST_LOOP_STMT:
    free(node->as.loop_stmt.iterator_name);
    ast_free(node->as.loop_stmt.start_expr);
    ast_free(node->as.loop_stmt.end_expr);
    ast_free(node->as.loop_stmt.step_expr);
    ast_free(node->as.loop_stmt.body);
    break;
  case AST_FOR_STMT:
    free(node->as.for_stmt.iterator_name);
    free(node->as.for_stmt.value_name);
    ast_free(node->as.for_stmt.iterable);
    ast_free(node->as.for_stmt.body);
    break;
  case AST_MATCH_STMT:
    ast_free(node->as.match_stmt.condition);
    free_list(node->as.match_stmt.cases, node->as.match_stmt.case_count);
    free_list(node->as.match_stmt.consequences,
              node->as.match_stmt.case_count);
    ast_free(node->as.match_stmt.default_consequence);
    break;
  case AST_RETURN_STMT:
    ast_free(node->as.return_stmt.return_value);
    break;
  case AST_FUNCTION_DECL:
    free(node->as.function_decl.name);
    free_strings(node->as.function_decl.parameters,
                 node->as.function_decl.param_count);
    free_strings(node->as.function_decl.param_types,
                 node->as.function_decl.param_count);
    free_list(node->as.function_decl.param_defaults,
              node->as.function_decl.param_count);
    ast_free(node->as.function_decl.body);
    free(node->as.function_decl.return_type);
    free(node->as.function_decl.class_name);
    break;
  case AST_STRUCT_DECL:
    free(node->as.struct_decl.name);
    free_strings(node->as.struct_decl.fields, node->as.struct_decl.field_count);
    break;
  case AST_CLASS_DECL:
    free(node->as.class_decl.name);
    free(node->as.class_decl.parent);
    free_list(node->as.class_decl.methods, node->as.class_decl.method_count);
    break;
  case AST_IMPORT_STMT:
    free(node->as.import_stmt.module_name);
    break;
  case AST_IMPORT_NATIVE:
    free(node->as.import_native.dll_name);
    break;
  case AST_C_FUNCTION_DECL:
    free(node->as.c_function_decl.name);
    free_strings(node->as.c_function_decl.param_types,
                 node->as.c_function_decl.param_count);
    free(node->as.c_function_decl.return_type);
    break;
  case AST_TRY_CATCH:
    ast_free(node->as.try_catch_stmt.try_block);
    free(node->as.try_catch_stmt.catch_var);
    ast_free(node->as.try_catch_stmt.catch_block);
    break;
  case AST_THROW:
    ast_free(node->as.throw_stmt.exception_value);
    break;
  case AST_ASM_BLOCK:
    free(node->as.asm_block.code);
    break;
  case AST_GLOBAL_DECL:
    free_strings(node->as.global_decl.names, node->as.global_decl.count);
    break;
  case AST_CONST_DECL:
    free(node->as.const_decl.name);
    ast_free(node->as.const_decl.value);
    break;
  case AST_STATIC_DECL:
    free(node->as.static_decl.name);
    ast_free(node->as.static_decl.size);
    ast_free(node->as.static_decl.align);
    break;
  case AST_IDENTIFIER:
    free(node->as.identifier.value);
    break;
  case AST_NUMBER_LITERAL:
  case AST_FLOAT_LITERAL:
    free(node->as.number_literal.value);
    break;
  case AST_STRING_LITERAL:
    free(node->as.string_literal.value);
    break;
  case AST_BINARY_OP:
    ast_free(node->as.binary_op.left);
    ast_free(node->as.binary_op.right);
    break;
  case AST_UNARY_OP:
    ast_free(node->as.unary_op.right);
    break;
  case AST_CALL_EXPR:
    ast_free(node->as.call_expr.function);
    free_list(node->as.call_expr.args, node->as.call_expr.arg_count);
    break;
  case AST_ARRAY_LITERAL:
    free_list(node->as.array_literal.elements,
              node->as.array_literal.element_count);
    break;
  case AST_DICT_LITERAL:
    free_list(node->as.dict_literal.keys, node->as.dict_literal.pair_count);
    free_list(node->as.dict_literal.values, node->as.dict_literal.pair_count);
    break;
  case AST_MEMBER_ACCESS:
    ast_free(node->as.member_access.object);
    ast_free(node->as.member_access.property);
    break;
  case AST_SLICE:
    ast_free(node->as.slice.object);
    ast_free(node->as.slice.start);
    ast_free(node->as.slice.end);
    break;
  case AST_NEW_EXPR:
    ast_free(node->as.new_expr.class_name);
    free_list(node->as.new_expr.args, node->as.new_expr.arg_count);
    break;
  case AST_LAMBDA:
    ast_free(node->as.lambda.func);
    break;
  case AST_TERNARY:
    ast_free(node->as.ternary.cond);
    ast_free(node->as.ternary.then_expr);
    ast_free(node->as.ternary.else_expr);
    break;
  default:
    break;
  }
  free(node);
}
