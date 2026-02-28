#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

ASTNode *ast_create_node(ASTNodeType type) {
  ASTNode *node = malloc(sizeof(ASTNode));
  node->type = type;
  return node;
}

ASTNode *ast_create_program() {
  ASTNode *node = ast_create_node(AST_PROGRAM);
  node->as.program.statements = NULL;
  node->as.program.statement_count = 0;
  return node;
}

ASTNode *ast_create_block() {
  ASTNode *node = ast_create_node(AST_BLOCK);
  node->as.block.statements = NULL;
  node->as.block.statement_count = 0;
  return node;
}

ASTNode *ast_create_identifier(const char *value) {
  ASTNode *node = ast_create_node(AST_IDENTIFIER);
  if (value) {
    node->as.identifier.value = strdup(value);
  } else {
    node->as.identifier.value = NULL;
  }
  return node;
}

ASTNode *ast_create_number_literal(const char *value) {
  ASTNode *node = ast_create_node(AST_NUMBER_LITERAL);
  if (value) {
    node->as.number_literal.value = strdup(value);
  } else {
    node->as.number_literal.value = NULL;
  }
  return node;
}

ASTNode *ast_create_string_literal(const char *value) {
  ASTNode *node = ast_create_node(AST_STRING_LITERAL);
  if (value) {
    node->as.string_literal.value = strdup(value);
  } else {
    node->as.string_literal.value = NULL;
  }
  return node;
}

ASTNode *ast_create_boolean_literal(int value) {
  ASTNode *node = ast_create_node(AST_BOOLEAN_LITERAL);
  node->as.boolean_literal.value = value;
  return node;
}

ASTNode *ast_create_null_literal() { return ast_create_node(AST_NULL_LITERAL); }

ASTNode *ast_create_binary_op(Token op, ASTNode *left, ASTNode *right) {
  ASTNode *node = ast_create_node(AST_BINARY_OP);

  // Copy token op
  node->as.binary_op.op.type = op.type;
  node->as.binary_op.op.line = op.line;
  node->as.binary_op.op.column = op.column;
  if (op.literal) {
    node->as.binary_op.op.literal = strdup(op.literal);
  } else {
    node->as.binary_op.op.literal = NULL;
  }

  node->as.binary_op.left = left;
  node->as.binary_op.right = right;
  return node;
}

ASTNode *ast_create_unary_op(Token op, ASTNode *right) {
  ASTNode *node = ast_create_node(AST_UNARY_OP);

  // Copy token op
  node->as.unary_op.op.type = op.type;
  node->as.unary_op.op.line = op.line;
  node->as.unary_op.op.column = op.column;
  if (op.literal) {
    node->as.unary_op.op.literal = strdup(op.literal);
  } else {
    node->as.unary_op.op.literal = NULL;
  }

  node->as.unary_op.right = right;
  return node;
}

ASTNode *ast_create_call_expr(ASTNode *function) {
  ASTNode *node = ast_create_node(AST_CALL_EXPR);
  node->as.call_expr.function = function;
  node->as.call_expr.args = NULL;
  node->as.call_expr.arg_count = 0;
  return node;
}

ASTNode *ast_create_expression_stmt(ASTNode *expression) {
  ASTNode *node = ast_create_node(AST_EXPRESSION_STMT);
  node->as.expression_stmt.expression = expression;
  return node;
}

ASTNode *ast_create_assignment(ASTNode *target, ASTNode *value) {
  ASTNode *node = ast_create_node(AST_ASSIGNMENT);
  node->as.assignment.target = target;
  node->as.assignment.value = value;
  node->as.assignment.type_annotation = strdup("any");
  return node;
}

ASTNode *ast_create_if_stmt(ASTNode *condition, ASTNode *consequence,
                            ASTNode *alternative) {
  ASTNode *node = ast_create_node(AST_IF_STMT);
  node->as.if_stmt.condition = condition;
  node->as.if_stmt.consequence = consequence;
  node->as.if_stmt.elif_conditions = NULL;
  node->as.if_stmt.elif_consequences = NULL;
  node->as.if_stmt.elif_count = 0;
  node->as.if_stmt.alternative = alternative;
  return node;
}

ASTNode *ast_create_while_stmt(ASTNode *condition, ASTNode *body) {
  ASTNode *node = ast_create_node(AST_WHILE_STMT);
  node->as.while_stmt.condition = condition;
  node->as.while_stmt.body = body;
  return node;
}

ASTNode *ast_create_loop_stmt(const char *iterator_name, ASTNode *start_expr,
                              ASTNode *end_expr, ASTNode *step_expr,
                              ASTNode *body) {
  ASTNode *node = ast_create_node(AST_LOOP_STMT);
  if (iterator_name) {
    node->as.loop_stmt.iterator_name = strdup(iterator_name);
  } else {
    node->as.loop_stmt.iterator_name = NULL;
  }
  node->as.loop_stmt.start_expr = start_expr;
  node->as.loop_stmt.end_expr = end_expr;
  node->as.loop_stmt.step_expr = step_expr;
  node->as.loop_stmt.body = body;
  return node;
}

ASTNode *ast_create_for_stmt(const char *iterator_name, ASTNode *iterable,
                             ASTNode *body) {
  ASTNode *node = ast_create_node(AST_FOR_STMT);
  if (iterator_name) {
    node->as.for_stmt.iterator_name = strdup(iterator_name);
  } else {
    node->as.for_stmt.iterator_name = NULL;
  }
  node->as.for_stmt.iterable = iterable;
  node->as.for_stmt.body = body;
  return node;
}

ASTNode *ast_create_match_stmt(ASTNode *condition) {
  ASTNode *node = ast_create_node(AST_MATCH_STMT);
  node->as.match_stmt.condition = condition;
  node->as.match_stmt.cases = NULL;
  node->as.match_stmt.consequences = NULL;
  node->as.match_stmt.case_count = 0;
  node->as.match_stmt.default_consequence = NULL;
  return node;
}

ASTNode *ast_create_array_literal() {
  ASTNode *node = ast_create_node(AST_ARRAY_LITERAL);
  node->as.array_literal.elements = NULL;
  node->as.array_literal.element_count = 0;
  return node;
}

ASTNode *ast_create_dict_literal() {
  ASTNode *node = ast_create_node(AST_DICT_LITERAL);
  node->as.dict_literal.keys = NULL;
  node->as.dict_literal.values = NULL;
  node->as.dict_literal.pair_count = 0;
  return node;
}

ASTNode *ast_create_member_access(ASTNode *object, ASTNode *property,
                                  int is_computed) {
  ASTNode *node = ast_create_node(AST_MEMBER_ACCESS);
  node->as.member_access.object = object;
  node->as.member_access.property = property;
  node->as.member_access.is_computed = is_computed;
  return node;
}

ASTNode *ast_create_return_stmt(ASTNode *return_value) {
  ASTNode *node = ast_create_node(AST_RETURN_STMT);
  node->as.return_stmt.return_value = return_value;
  return node;
}

ASTNode *ast_create_function_decl(const char *name, ASTNode *body) {
  ASTNode *node = ast_create_node(AST_FUNCTION_DECL);
  if (name) {
    node->as.function_decl.name = strdup(name);
  } else {
    node->as.function_decl.name = NULL;
  }
  node->as.function_decl.parameters = NULL;
  node->as.function_decl.param_types = NULL;
  node->as.function_decl.param_count = 0;
  node->as.function_decl.body = body;
  node->as.function_decl.return_type = strdup("any");
  node->as.function_decl.is_interrupt = 0;
  return node;
}

ASTNode *ast_create_struct_decl(const char *name) {
  ASTNode *node = ast_create_node(AST_STRUCT_DECL);
  if (name) {
    node->as.struct_decl.name = strdup(name);
  } else {
    node->as.struct_decl.name = NULL;
  }
  node->as.struct_decl.fields = NULL;
  node->as.struct_decl.field_count = 0;
  return node;
}

ASTNode *ast_create_class_decl(const char *name) {
  ASTNode *node = ast_create_node(AST_CLASS_DECL);
  if (name) {
    node->as.class_decl.name = strdup(name);
  } else {
    node->as.class_decl.name = NULL;
  }
  node->as.class_decl.methods = NULL;
  node->as.class_decl.method_count = 0;
  return node;
}

ASTNode *ast_create_new_expr(ASTNode *class_name) {
  ASTNode *node = ast_create_node(AST_NEW_EXPR);
  node->as.new_expr.class_name = class_name;
  node->as.new_expr.args = NULL;
  node->as.new_expr.arg_count = 0;
  return node;
}

ASTNode *ast_create_this() { return ast_create_node(AST_THIS); }

ASTNode *ast_create_import_native(const char *dll_name) {
  ASTNode *node = ast_create_node(AST_IMPORT_NATIVE);
  node->as.import_native.dll_name = strdup(dll_name);
  return node;
}

ASTNode *ast_create_c_function_decl(const char *name, const char *return_type) {
  ASTNode *node = ast_create_node(AST_C_FUNCTION_DECL);
  node->as.c_function_decl.name = strdup(name);
  node->as.c_function_decl.return_type =
      return_type ? strdup(return_type) : strdup("any");
  node->as.c_function_decl.param_types = NULL;
  node->as.c_function_decl.param_count = 0;
  return node;
}

ASTNode *ast_create_try_catch(ASTNode *try_block, const char *catch_var,
                              ASTNode *catch_block) {
  ASTNode *node = ast_create_node(AST_TRY_CATCH);
  node->as.try_catch_stmt.try_block = try_block;
  node->as.try_catch_stmt.catch_var = strdup(catch_var);
  node->as.try_catch_stmt.catch_block = catch_block;
  return node;
}

ASTNode *ast_create_throw(ASTNode *exception_value) {
  ASTNode *node = ast_create_node(AST_THROW);
  node->as.throw_stmt.exception_value = exception_value;
  return node;
}

ASTNode *ast_create_asm_block(const char *code) {
  ASTNode *node = ast_create_node(AST_ASM_BLOCK);
  node->as.asm_block.code = code ? strdup(code) : NULL;
  return node;
}

// --- List modifiers ---

void ast_program_add_statement(ASTNode *program, ASTNode *stmt) {
  if (program->type != AST_PROGRAM)
    return;
  program->as.program.statement_count++;
  program->as.program.statements =
      realloc(program->as.program.statements,
              sizeof(ASTNode *) * program->as.program.statement_count);
  program->as.program.statements[program->as.program.statement_count - 1] =
      stmt;
}

void ast_block_add_statement(ASTNode *block, ASTNode *stmt) {
  if (block->type != AST_BLOCK)
    return;
  block->as.block.statement_count++;
  block->as.block.statements =
      realloc(block->as.block.statements,
              sizeof(ASTNode *) * block->as.block.statement_count);
  block->as.block.statements[block->as.block.statement_count - 1] = stmt;
}

void ast_call_add_arg(ASTNode *call, ASTNode *arg) {
  if (call->type != AST_CALL_EXPR)
    return;
  call->as.call_expr.arg_count++;
  call->as.call_expr.args =
      realloc(call->as.call_expr.args,
              sizeof(ASTNode *) * call->as.call_expr.arg_count);
  call->as.call_expr.args[call->as.call_expr.arg_count - 1] = arg;
}

void ast_function_add_param(ASTNode *func, const char *param_name) {
  if (func->type != AST_FUNCTION_DECL)
    return;
  func->as.function_decl.param_count++;
  func->as.function_decl.parameters =
      realloc(func->as.function_decl.parameters,
              sizeof(char *) * func->as.function_decl.param_count);
  func->as.function_decl.parameters[func->as.function_decl.param_count - 1] =
      strdup(param_name);
}

void ast_function_add_param_type(ASTNode *func, const char *type_name) {
  if (func->type != AST_FUNCTION_DECL)
    return;
  // param_count was already incremented by ast_function_add_param
  func->as.function_decl.param_types =
      realloc(func->as.function_decl.param_types,
              sizeof(char *) * func->as.function_decl.param_count);
  func->as.function_decl.param_types[func->as.function_decl.param_count - 1] =
      strdup(type_name);
}

void ast_if_add_elif(ASTNode *if_node, ASTNode *cond, ASTNode *cons) {
  if (if_node->type != AST_IF_STMT)
    return;
  if_node->as.if_stmt.elif_count++;
  if_node->as.if_stmt.elif_conditions =
      realloc(if_node->as.if_stmt.elif_conditions,
              sizeof(ASTNode *) * if_node->as.if_stmt.elif_count);
  if_node->as.if_stmt.elif_consequences =
      realloc(if_node->as.if_stmt.elif_consequences,
              sizeof(ASTNode *) * if_node->as.if_stmt.elif_count);
  if_node->as.if_stmt.elif_conditions[if_node->as.if_stmt.elif_count - 1] =
      cond;
  if_node->as.if_stmt.elif_consequences[if_node->as.if_stmt.elif_count - 1] =
      cons;
}

void ast_match_add_case(ASTNode *match_node, ASTNode *case_expr,
                        ASTNode *consequence) {
  if (match_node->type != AST_MATCH_STMT)
    return;
  match_node->as.match_stmt.case_count++;
  match_node->as.match_stmt.cases =
      realloc(match_node->as.match_stmt.cases,
              sizeof(ASTNode *) * match_node->as.match_stmt.case_count);
  match_node->as.match_stmt.consequences =
      realloc(match_node->as.match_stmt.consequences,
              sizeof(ASTNode *) * match_node->as.match_stmt.case_count);
  match_node->as.match_stmt.cases[match_node->as.match_stmt.case_count - 1] =
      case_expr;
  match_node->as.match_stmt
      .consequences[match_node->as.match_stmt.case_count - 1] = consequence;
}

void ast_array_add_element(ASTNode *array_node, ASTNode *element) {
  if (array_node->type != AST_ARRAY_LITERAL)
    return;
  array_node->as.array_literal.element_count++;
  array_node->as.array_literal.elements =
      realloc(array_node->as.array_literal.elements,
              sizeof(ASTNode *) * array_node->as.array_literal.element_count);
  array_node->as.array_literal
      .elements[array_node->as.array_literal.element_count - 1] = element;
}

void ast_dict_add_pair(ASTNode *dict_node, ASTNode *key, ASTNode *value) {
  if (dict_node->type != AST_DICT_LITERAL)
    return;
  dict_node->as.dict_literal.pair_count++;
  dict_node->as.dict_literal.keys =
      realloc(dict_node->as.dict_literal.keys,
              sizeof(ASTNode *) * dict_node->as.dict_literal.pair_count);
  dict_node->as.dict_literal.values =
      realloc(dict_node->as.dict_literal.values,
              sizeof(ASTNode *) * dict_node->as.dict_literal.pair_count);
  dict_node->as.dict_literal.keys[dict_node->as.dict_literal.pair_count - 1] =
      key;
  dict_node->as.dict_literal.values[dict_node->as.dict_literal.pair_count - 1] =
      value;
}

void ast_struct_add_field(ASTNode *struc, const char *field) {
  if (struc->type != AST_STRUCT_DECL)
    return;
  struc->as.struct_decl.field_count++;
  struc->as.struct_decl.fields =
      realloc(struc->as.struct_decl.fields,
              sizeof(char *) * struc->as.struct_decl.field_count);
  struc->as.struct_decl.fields[struc->as.struct_decl.field_count - 1] =
      strdup(field);
}

void ast_class_add_method(ASTNode *class_decl, ASTNode *method) {
  if (class_decl->type != AST_CLASS_DECL)
    return;
  class_decl->as.class_decl.method_count++;
  class_decl->as.class_decl.methods =
      realloc(class_decl->as.class_decl.methods,
              sizeof(ASTNode *) * class_decl->as.class_decl.method_count);
  class_decl->as.class_decl
      .methods[class_decl->as.class_decl.method_count - 1] = method;
}

void ast_new_expr_add_arg(ASTNode *new_expr, ASTNode *arg) {
  if (new_expr->type != AST_NEW_EXPR)
    return;
  new_expr->as.new_expr.arg_count++;
  new_expr->as.new_expr.args =
      realloc(new_expr->as.new_expr.args,
              sizeof(ASTNode *) * new_expr->as.new_expr.arg_count);
  new_expr->as.new_expr.args[new_expr->as.new_expr.arg_count - 1] = arg;
}

void ast_c_function_add_param_type(ASTNode *c_func, const char *type_name) {
  if (c_func->type != AST_C_FUNCTION_DECL)
    return;
  c_func->as.c_function_decl.param_count++;
  c_func->as.c_function_decl.param_types =
      realloc(c_func->as.c_function_decl.param_types,
              sizeof(char *) * c_func->as.c_function_decl.param_count);
  c_func->as.c_function_decl
      .param_types[c_func->as.c_function_decl.param_count - 1] =
      strdup(type_name);
}

// --- Cleanup ---

void ast_free(ASTNode *node) {
  if (!node)
    return;

  switch (node->type) {
  case AST_PROGRAM:
    for (int i = 0; i < node->as.program.statement_count; i++) {
      ast_free(node->as.program.statements[i]);
    }
    free(node->as.program.statements);
    break;

  case AST_BLOCK:
    for (int i = 0; i < node->as.block.statement_count; i++) {
      ast_free(node->as.block.statements[i]);
    }
    free(node->as.block.statements);
    break;

  case AST_EXPRESSION_STMT:
    ast_free(node->as.expression_stmt.expression);
    break;

  case AST_ASSIGNMENT:
    ast_free(node->as.assignment.target);
    ast_free(node->as.assignment.value);
    if (node->as.assignment.type_annotation)
      free(node->as.assignment.type_annotation);
    break;

  case AST_IF_STMT:
    ast_free(node->as.if_stmt.condition);
    ast_free(node->as.if_stmt.consequence);
    for (int i = 0; i < node->as.if_stmt.elif_count; i++) {
      ast_free(node->as.if_stmt.elif_conditions[i]);
      ast_free(node->as.if_stmt.elif_consequences[i]);
    }
    if (node->as.if_stmt.elif_conditions)
      free(node->as.if_stmt.elif_conditions);
    if (node->as.if_stmt.elif_consequences)
      free(node->as.if_stmt.elif_consequences);
    if (node->as.if_stmt.alternative)
      ast_free(node->as.if_stmt.alternative);
    break;

  case AST_WHILE_STMT:
    ast_free(node->as.while_stmt.condition);
    ast_free(node->as.while_stmt.body);
    break;

  case AST_LOOP_STMT:
    if (node->as.loop_stmt.iterator_name)
      free(node->as.loop_stmt.iterator_name);
    ast_free(node->as.loop_stmt.start_expr);
    ast_free(node->as.loop_stmt.end_expr);
    if (node->as.loop_stmt.step_expr)
      ast_free(node->as.loop_stmt.step_expr);
    ast_free(node->as.loop_stmt.body);
    break;

  case AST_FOR_STMT:
    if (node->as.for_stmt.iterator_name)
      free(node->as.for_stmt.iterator_name);
    ast_free(node->as.for_stmt.iterable);
    ast_free(node->as.for_stmt.body);
    break;

  case AST_MATCH_STMT:
    ast_free(node->as.match_stmt.condition);
    for (int i = 0; i < node->as.match_stmt.case_count; i++) {
      ast_free(node->as.match_stmt.cases[i]);
      ast_free(node->as.match_stmt.consequences[i]);
    }
    if (node->as.match_stmt.cases)
      free(node->as.match_stmt.cases);
    if (node->as.match_stmt.consequences)
      free(node->as.match_stmt.consequences);
    if (node->as.match_stmt.default_consequence)
      ast_free(node->as.match_stmt.default_consequence);
    break;

  case AST_RETURN_STMT:
    if (node->as.return_stmt.return_value) {
      ast_free(node->as.return_stmt.return_value);
    }
    break;

  case AST_FUNCTION_DECL:
    if (node->as.function_decl.name)
      free(node->as.function_decl.name);
    if (node->as.function_decl.return_type)
      free(node->as.function_decl.return_type);
    for (int i = 0; i < node->as.function_decl.param_count; i++) {
      free(node->as.function_decl.parameters[i]);
      if (node->as.function_decl.param_types &&
          node->as.function_decl.param_types[i])
        free(node->as.function_decl.param_types[i]);
    }
    if (node->as.function_decl.parameters)
      free(node->as.function_decl.parameters);
    if (node->as.function_decl.param_types)
      free(node->as.function_decl.param_types);
    ast_free(node->as.function_decl.body);
    break;

  case AST_STRUCT_DECL:
    if (node->as.struct_decl.name)
      free(node->as.struct_decl.name);
    for (int i = 0; i < node->as.struct_decl.field_count; i++) {
      free(node->as.struct_decl.fields[i]);
    }
    if (node->as.struct_decl.fields)
      free(node->as.struct_decl.fields);
    break;

  case AST_TRY_CATCH:
    ast_free(node->as.try_catch_stmt.try_block);
    free(node->as.try_catch_stmt.catch_var);
    ast_free(node->as.try_catch_stmt.catch_block);
    break;

  case AST_THROW:
    ast_free(node->as.throw_stmt.exception_value);
    break;

  case AST_CLASS_DECL:
    if (node->as.class_decl.name)
      free(node->as.class_decl.name);
    for (int i = 0; i < node->as.class_decl.method_count; i++) {
      ast_free(node->as.class_decl.methods[i]);
    }
    if (node->as.class_decl.methods)
      free(node->as.class_decl.methods);
    break;

  case AST_IDENTIFIER:
    if (node->as.identifier.value)
      free(node->as.identifier.value);
    break;

  case AST_NUMBER_LITERAL:
    if (node->as.number_literal.value)
      free(node->as.number_literal.value);
    break;

  case AST_STRING_LITERAL:
    if (node->as.string_literal.value)
      free(node->as.string_literal.value);
    break;

  case AST_BINARY_OP:
    if (node->as.binary_op.op.literal)
      free(node->as.binary_op.op.literal);
    ast_free(node->as.binary_op.left);
    ast_free(node->as.binary_op.right);
    break;

  case AST_UNARY_OP:
    if (node->as.unary_op.op.literal)
      free(node->as.unary_op.op.literal);
    ast_free(node->as.unary_op.right);
    break;

  case AST_CALL_EXPR:
    ast_free(node->as.call_expr.function);
    for (int i = 0; i < node->as.call_expr.arg_count; i++) {
      ast_free(node->as.call_expr.args[i]);
    }
    if (node->as.call_expr.args)
      free(node->as.call_expr.args);
    break;

  case AST_MEMBER_ACCESS:
    ast_free(node->as.member_access.object);
    ast_free(node->as.member_access.property);
    break;

  case AST_ARRAY_LITERAL:
    for (int i = 0; i < node->as.array_literal.element_count; i++) {
      ast_free(node->as.array_literal.elements[i]);
    }
    if (node->as.array_literal.elements)
      free(node->as.array_literal.elements);
    break;

  case AST_DICT_LITERAL:
    for (int i = 0; i < node->as.dict_literal.pair_count; i++) {
      ast_free(node->as.dict_literal.keys[i]);
      ast_free(node->as.dict_literal.values[i]);
    }
    if (node->as.dict_literal.keys)
      free(node->as.dict_literal.keys);
    if (node->as.dict_literal.values)
      free(node->as.dict_literal.values);
    break;

  case AST_ASM_BLOCK:
    if (node->as.asm_block.code)
      free(node->as.asm_block.code);
    break;

  // Todo: structs, etc.
  case AST_NULL_LITERAL:
  case AST_BOOLEAN_LITERAL:
  case AST_THIS:
    break;

  case AST_NEW_EXPR:
    ast_free(node->as.new_expr.class_name);
    for (int i = 0; i < node->as.new_expr.arg_count; i++) {
      ast_free(node->as.new_expr.args[i]);
    }
    if (node->as.new_expr.args)
      free(node->as.new_expr.args);
    break;

  case AST_IMPORT_NATIVE:
    if (node->as.import_native.dll_name)
      free(node->as.import_native.dll_name);
    break;

  case AST_C_FUNCTION_DECL:
    if (node->as.c_function_decl.name)
      free(node->as.c_function_decl.name);
    if (node->as.c_function_decl.return_type)
      free(node->as.c_function_decl.return_type);
    for (int i = 0; i < node->as.c_function_decl.param_count; i++) {
      free(node->as.c_function_decl.param_types[i]);
    }
    if (node->as.c_function_decl.param_types)
      free(node->as.c_function_decl.param_types);
    break;

  default:
    // Placeholder for other types
    break;
  }

  free(node);
}
