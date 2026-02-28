#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Precedences
typedef enum {
  PREC_LOWEST = 1,
  PREC_OR,          // or (\n)
  PREC_AND,         // and
  PREC_EQUALS,      // ==, !=, equals
  PREC_LESSGREATER, // >, <, >=, <=
  PREC_SUM,         // +, -, plus, minus
  PREC_PRODUCT,     // *, /, %, times, divided by
  PREC_POWER,       // **
  PREC_PREFIX,      // -X, not X
  PREC_CALL,        // myFunction(X)
  PREC_INDEX        // array at index
} Precedence;

static Precedence get_precedence(TokenType type) {
  switch (type) {
  case TOKEN_EQUALS:
  case TOKEN_NOT_EQUALS:
    return PREC_EQUALS;

  case TOKEN_LESS_THAN:
  case TOKEN_GREATER_THAN:
  case TOKEN_LESS_OR_EQUALS:
  case TOKEN_GREATER_OR_EQUALS:
    return PREC_LESSGREATER;

  case TOKEN_PLUS:
  case TOKEN_MINUS:
    return PREC_SUM;

  case TOKEN_TIMES:
  case TOKEN_DIVIDED_BY:
  case TOKEN_MODULO:
    return PREC_PRODUCT;

  case TOKEN_POWER:
    return PREC_POWER;

  case TOKEN_LPAREN: // Function call
    return PREC_CALL;

  case TOKEN_LBRACKET: // Index access [x]
  case TOKEN_DOT:      // Member access .x
  case TOKEN_AT:       // Index access: arr at i
    return PREC_INDEX;

  case TOKEN_AND:
    return PREC_AND;
  case TOKEN_OR:
    return PREC_OR;

  default:
    // "at" handled via IDENTIFIER check if necessary
    return PREC_LOWEST;
  }
}

// Pratt Parser function types
typedef ASTNode *(*PrefixParseFn)(Parser *parser);
typedef ASTNode *(*InfixParseFn)(Parser *parser, ASTNode *left);

// For simplicity, we just declare prototypes and route them in a function
static ASTNode *parse_expression(Parser *parser, Precedence precedence);
static ASTNode *parse_statement(Parser *parser);
static ASTNode *parse_block_statement(Parser *parser);

static void parser_next_token(Parser *parser) {
  if (parser->current_token) {
    token_free(parser->current_token);
  }
  parser->current_token = parser->peek_token;
  parser->peek_token = lexer_next_token(parser->lexer);
}

static void parser_add_error(Parser *parser, const char *msg) {
  parser->error_count++;
  parser->errors =
      realloc(parser->errors, sizeof(char *) * parser->error_count);
  parser->errors[parser->error_count - 1] = strdup(msg);
}

static int current_token_is(Parser *parser, TokenType type) {
  return parser->current_token->type == type;
}

static int peek_token_is(Parser *parser, TokenType type) {
  return parser->peek_token->type == type;
}

static int expect_peek(Parser *parser, TokenType type) {
  if (peek_token_is(parser, type)) {
    parser_next_token(parser);
    return 1;
  } else {
    char buf[256];
    snprintf(buf, sizeof(buf), "[Line %d] Expected token %s, got %s",
             parser->current_token->line, token_type_to_string(type),
             token_type_to_string(parser->peek_token->type));
    parser_add_error(parser, buf);
    return 0;
  }
}

// ------ EXPRESSION PARSERS ------

static ASTNode *parse_identifier(Parser *parser) {
  return ast_create_identifier(parser->current_token->literal);
}

static ASTNode *parse_number_literal(Parser *parser) {
  return ast_create_number_literal(parser->current_token->literal);
}

static ASTNode *parse_string_literal(Parser *parser) {
  return ast_create_string_literal(parser->current_token->literal);
}

static ASTNode *parse_boolean_literal(Parser *parser) {
  return ast_create_boolean_literal(
      parser->current_token->type == TOKEN_TRUE ? 1 : 0);
}

static ASTNode *parse_null_literal(Parser *parser) {
  return ast_create_null_literal();
}

static ASTNode *parse_prefix_expression(Parser *parser) {
  Token op = *parser->current_token;
  parser_next_token(parser);
  ASTNode *right = parse_expression(parser, PREC_PREFIX);
  return ast_create_unary_op(op, right);
}

static ASTNode *parse_infix_expression(Parser *parser, ASTNode *left) {
  Token op = *parser->current_token;
  Precedence precedence = get_precedence(parser->current_token->type);
  parser_next_token(parser);
  ASTNode *right = parse_expression(parser, precedence);
  return ast_create_binary_op(op, left, right);
}

static ASTNode *parse_grouped_expression(Parser *parser) {
  parser_next_token(parser); // consume '('
  ASTNode *exp = parse_expression(parser, PREC_LOWEST);
  if (!expect_peek(parser, TOKEN_RPAREN)) {
    ast_free(exp);
    return NULL;
  }
  return exp;
}

static ASTNode *parse_call_arguments(Parser *parser, ASTNode *call) {
  if (peek_token_is(parser, TOKEN_RPAREN)) {
    parser_next_token(parser);
    return call;
  }

  parser_next_token(parser);
  ast_call_add_arg(call, parse_expression(parser, PREC_LOWEST));

  while (peek_token_is(parser, TOKEN_COMMA)) {
    parser_next_token(parser);
    parser_next_token(parser);
    ast_call_add_arg(call, parse_expression(parser, PREC_LOWEST));
  }

  if (!expect_peek(parser, TOKEN_RPAREN)) {
    ast_free(call);
    return NULL;
  }

  return call;
}

static ASTNode *parse_call_expression(Parser *parser, ASTNode *function) {
  ASTNode *call = ast_create_call_expr(function);
  return parse_call_arguments(parser, call);
}

static ASTNode *parse_array_literal(Parser *parser) {
  ASTNode *array = ast_create_array_literal();

  if (peek_token_is(parser, TOKEN_RBRACKET)) {
    parser_next_token(parser);
    return array;
  }

  parser_next_token(parser);
  ast_array_add_element(array, parse_expression(parser, PREC_LOWEST));

  while (peek_token_is(parser, TOKEN_COMMA)) {
    parser_next_token(parser);
    parser_next_token(parser);
    ast_array_add_element(array, parse_expression(parser, PREC_LOWEST));
  }

  if (!expect_peek(parser, TOKEN_RBRACKET)) {
    ast_free(array);
    return NULL;
  }

  return array;
}

static ASTNode *parse_dict_literal(Parser *parser) {
  ASTNode *dict = ast_create_dict_literal();

  // Empty dict: {}
  if (peek_token_is(parser, TOKEN_RBRACE)) {
    parser_next_token(parser); // consume '}'
    return dict;
  }

  // Parse first key-value pair
  parser_next_token(parser); // move to first key

  for (;;) {
    if (current_token_is(parser, TOKEN_RBRACE) ||
        current_token_is(parser, TOKEN_EOF)) {
      break;
    }

    // Key must be an identifier or string
    ASTNode *key = NULL;
    if (current_token_is(parser, TOKEN_IDENTIFIER)) {
      key = ast_create_identifier(parser->current_token->literal);
    } else if (current_token_is(parser, TOKEN_STRING)) {
      key = ast_create_string_literal(parser->current_token->literal);
    } else {
      char err[128];
      snprintf(err, sizeof(err),
               "[Line %d] Expected identifier or string as dict key, got %s",
               parser->current_token->line,
               token_type_to_string(parser->current_token->type));
      parser_add_error(parser, err);
      ast_free(dict);
      return NULL;
    }

    // Expect colon
    if (!expect_peek(parser, TOKEN_COLON)) {
      ast_free(key);
      ast_free(dict);
      return NULL;
    }
    parser_next_token(parser); // move past ':' to value

    // Parse value (stop before comma/rbrace)
    ASTNode *value = parse_expression(parser, PREC_LOWEST);
    ast_dict_add_pair(dict, key, value);

    // After value: expect ',' or '}'
    if (peek_token_is(parser, TOKEN_COMMA)) {
      parser_next_token(parser); // consume ','
      parser_next_token(parser); // move to next key
    } else if (peek_token_is(parser, TOKEN_RBRACE)) {
      parser_next_token(parser); // consume '}'
      break;
    } else {
      char err[128];
      snprintf(err, sizeof(err),
               "[Line %d] Expected ',' or '}' in dictionary, got %s",
               parser->current_token->line,
               token_type_to_string(parser->peek_token->type));
      parser_add_error(parser, err);
      ast_free(dict);
      return NULL;
    }
  }

  return dict;
}

static ASTNode *parse_index_access(Parser *parser, ASTNode *left) {
  parser_next_token(parser); // consume '['
  ASTNode *index = parse_expression(parser, PREC_LOWEST);

  if (!expect_peek(parser, TOKEN_RBRACKET)) {
    ast_free(left);
    ast_free(index);
    return NULL;
  }

  return ast_create_member_access(left, index, 1);
}

static ASTNode *parse_at_index(Parser *parser, ASTNode *left) {
  // 'arr at i' → same semantic as arr[i]
  // current_token is now TOKEN_AT; advance to the index expression
  parser_next_token(parser); // consume 'at', move to index expression
  ASTNode *index = parse_expression(parser, PREC_INDEX);
  return ast_create_member_access(left, index, 1); // 1 = subscript
}

static ASTNode *parse_member_access(Parser *parser, ASTNode *left) {
  parser_next_token(parser); // consume '.'
  // A property is ALWAYS a simple identifier — never call parse_expression here
  // because that would cause the Pratt loop to consume tokens like '=' that
  // belong to the parent assignment statement.
  if (!current_token_is(parser, TOKEN_IDENTIFIER)) {
    char err[128];
    snprintf(err, sizeof(err),
             "[Line %d] Expected identifier after '.', got %s",
             parser->current_token->line,
             token_type_to_string(parser->current_token->type));
    parser_add_error(parser, err);
    return left; // return what we have so far, don't crash
  }
  ASTNode *property = ast_create_identifier(parser->current_token->literal);
  return ast_create_member_access(left, property, 0);
}

static PrefixParseFn get_prefix_fn(TokenType type) {
  switch (type) {
  case TOKEN_IDENTIFIER:
    return parse_identifier;
  case TOKEN_NUMBER:
    return parse_number_literal;
  case TOKEN_STRING:
    return parse_string_literal;
  case TOKEN_TRUE:
  case TOKEN_FALSE:
    return parse_boolean_literal;
  case TOKEN_NULL:
    return parse_null_literal;
  case TOKEN_NOT:
  case TOKEN_MINUS:
    return parse_prefix_expression;
  case TOKEN_LPAREN:
    return parse_grouped_expression;
  case TOKEN_LBRACKET:
    return parse_array_literal;
  case TOKEN_LBRACE:
    return parse_dict_literal;
  default:
    return NULL;
  }
}

static InfixParseFn get_infix_fn(TokenType type) {
  switch (type) {
  case TOKEN_PLUS:
  case TOKEN_MINUS:
  case TOKEN_TIMES:
  case TOKEN_DIVIDED_BY:
  case TOKEN_MODULO:
  case TOKEN_POWER:
  case TOKEN_EQUALS:
  case TOKEN_NOT_EQUALS:
  case TOKEN_LESS_THAN:
  case TOKEN_GREATER_THAN:
  case TOKEN_LESS_OR_EQUALS:
  case TOKEN_GREATER_OR_EQUALS:
  case TOKEN_AND:
  case TOKEN_OR:
    return parse_infix_expression;
  case TOKEN_LPAREN:
    return parse_call_expression;
  case TOKEN_LBRACKET:
    return parse_index_access;
  case TOKEN_DOT:
    return parse_member_access;
  case TOKEN_AT:
    return parse_at_index;
  default:
    return NULL;
  }
}

static ASTNode *parse_expression(Parser *parser, Precedence precedence) {
  PrefixParseFn prefix = get_prefix_fn(parser->current_token->type);
  if (!prefix) {
    char err[200];
    snprintf(err, sizeof(err),
             "[Line %d] No prefix parse function for %s (peek: %s)",
             parser->current_token->line,
             token_type_to_string(parser->current_token->type),
             token_type_to_string(parser->peek_token->type));
    parser_add_error(parser, err);
    return NULL;
  }

  ASTNode *left_exp = prefix(parser);

  // Check for pseudo-infix like "at" which is IDENTIFIER
  while (!peek_token_is(parser, TOKEN_NEWLINE) &&
         !peek_token_is(parser, TOKEN_EOF) &&
         precedence < get_precedence(parser->peek_token->type)) {
    InfixParseFn infix = get_infix_fn(parser->peek_token->type);
    if (!infix) {
      return left_exp;
    }

    parser_next_token(parser);
    left_exp = infix(parser, left_exp);
  }

  return left_exp;
}

// ------ STATEMENT PARSERS ------

static ASTNode *parse_return_statement(Parser *parser) {
  parser_next_token(parser); // Consume 'return'

  ASTNode *returnValue = NULL;
  if (!current_token_is(parser, TOKEN_NEWLINE) &&
      !current_token_is(parser, TOKEN_EOF)) {
    returnValue = parse_expression(parser, PREC_LOWEST);
  }

  if (peek_token_is(parser, TOKEN_NEWLINE)) {
    parser_next_token(parser);
  }

  return ast_create_return_stmt(returnValue);
}

static ASTNode *parse_assignment_statement(Parser *parser) {
  ASTNode *target = parse_expression(parser, PREC_LOWEST);

  if (!target) {
    return NULL; // This forces the caller to advance the token on error
  }

  if (peek_token_is(parser, TOKEN_ASSIGN)) {
    parser_next_token(parser); // advance: peek '=' becomes current '='
    parser_next_token(parser); // advance: move to the value start

    ASTNode *value = parse_expression(parser, PREC_LOWEST);

    if (peek_token_is(parser, TOKEN_NEWLINE)) {
      parser_next_token(parser);
    }

    return ast_create_assignment(target, value);
  }

  // It was just an expression
  if (peek_token_is(parser, TOKEN_NEWLINE)) {
    parser_next_token(parser);
    return ast_create_expression_stmt(target);
  } else if (peek_token_is(parser, TOKEN_EOF)) {
    return ast_create_expression_stmt(target);
  }

  // If not followed by newline or EOF, it's a syntax error
  char err[128];
  snprintf(err, sizeof(err),
           "[Line %d] Expected newline after expression, got %s",
           parser->current_token->line,
           token_type_to_string(parser->peek_token->type));
  parser_add_error(parser, err);
  ast_free(target);
  return NULL;
}

static ASTNode *parse_expression_statement(Parser *parser) {
  // It could be an assignment
  // We can peek ahead to see if there's an ASSIGN token...
  // To handle `arr at 0 = 5`, we need to parse expression first, then check if
  // next token is =.
  return parse_assignment_statement(parser);
}

static ASTNode *parse_block_statement(Parser *parser) {
  ASTNode *block = ast_create_block();

  // Skip leading newlines
  while (current_token_is(parser, TOKEN_NEWLINE)) {
    parser_next_token(parser);
  }

  while (!current_token_is(parser, TOKEN_END) &&
         !current_token_is(parser, TOKEN_EOF) &&
         !current_token_is(parser, TOKEN_ELSE) &&
         !current_token_is(parser, TOKEN_ELIF) &&
         !current_token_is(parser, TOKEN_CASE) &&
         !current_token_is(parser, TOKEN_DEFAULT)) {
    ASTNode *stmt = parse_statement(parser);
    if (stmt) {
      ast_block_add_statement(block, stmt);
    } else {
      if (!current_token_is(parser, TOKEN_NEWLINE) &&
          !current_token_is(parser, TOKEN_EOF)) {
        parser_next_token(parser);
      }
    }

    // Consume all newlines between statements
    while (current_token_is(parser, TOKEN_NEWLINE)) {
      parser_next_token(parser);
    }
  }

  return block;
}

static ASTNode *parse_if_statement(Parser *parser) {
  parser_next_token(parser); // Consume 'if'

  ASTNode *condition = parse_expression(parser, PREC_LOWEST);

  // Expect newline
  if (!expect_peek(parser, TOKEN_NEWLINE)) {
    return NULL;
  }
  parser_next_token(parser); // move to start of block

  ASTNode *consequence = parse_block_statement(parser);
  ASTNode *if_node = ast_create_if_stmt(condition, consequence, NULL);

  while (current_token_is(parser, TOKEN_ELIF)) {
    parser_next_token(parser); // Consume 'elif'
    ASTNode *elif_cond = parse_expression(parser, PREC_LOWEST);
    if (!expect_peek(parser, TOKEN_NEWLINE))
      return NULL;
    parser_next_token(parser);
    ASTNode *elif_cons = parse_block_statement(parser);
    ast_if_add_elif(if_node, elif_cond, elif_cons);
  }

  if (current_token_is(parser, TOKEN_ELSE)) {
    if (!expect_peek(parser, TOKEN_NEWLINE))
      return NULL;
    parser_next_token(parser); // Move past NEWLINE to the start of the block
    if_node->as.if_stmt.alternative = parse_block_statement(parser);
  }

  if (!current_token_is(parser, TOKEN_END)) {
    parser_add_error(parser, "Expected 'end' at end of if statement");
    return NULL;
  }
  parser_next_token(parser); // Consume 'end'

  // Skip trailing newline
  if (current_token_is(parser, TOKEN_NEWLINE)) {
    parser_next_token(parser);
  }

  return if_node;
}

static ASTNode *parse_function_parameters(Parser *parser, ASTNode *func) {
  if (peek_token_is(parser, TOKEN_RPAREN)) {
    parser_next_token(parser);
    return func;
  }

  parser_next_token(parser); // move to first param (identifier)
  if (!current_token_is(parser, TOKEN_IDENTIFIER)) {
    parser_add_error(parser, "Expected identifier for parameter");
    return NULL;
  }
  ast_function_add_param(func, parser->current_token->literal);

  while (peek_token_is(parser, TOKEN_COMMA)) {
    parser_next_token(parser); // comma
    parser_next_token(parser); // identifier
    if (!current_token_is(parser, TOKEN_IDENTIFIER)) {
      parser_add_error(parser, "Expected identifier for parameter");
      return NULL;
    }
    ast_function_add_param(func, parser->current_token->literal);
  }

  if (!expect_peek(parser, TOKEN_RPAREN)) {
    return NULL;
  }

  return func;
}

static ASTNode *parse_function_decl(Parser *parser) {
  parser_next_token(parser); // consume 'function'

  if (!current_token_is(parser, TOKEN_IDENTIFIER)) {
    parser_add_error(parser, "Expected function name");
    return NULL;
  }

  char *name = strdup(parser->current_token->literal);

  if (!expect_peek(parser, TOKEN_LPAREN)) {
    free(name);
    return NULL;
  }

  ASTNode *func = ast_create_function_decl(name, NULL);
  free(name);

  if (!parse_function_parameters(parser, func)) {
    ast_free(func);
    return NULL;
  }

  if (!expect_peek(parser, TOKEN_NEWLINE)) {
    ast_free(func);
    return NULL;
  }
  parser_next_token(parser); // move to block start

  func->as.function_decl.body = parse_block_statement(parser);

  if (!current_token_is(parser, TOKEN_END)) {
    parser_add_error(parser, "Expected 'end' at end of function declaration");
  }

  parser_next_token(parser); // consume 'end'

  if (current_token_is(parser, TOKEN_NEWLINE)) {
    parser_next_token(parser);
  }

  return func;
}

static ASTNode *parse_while_statement(Parser *parser) {
  parser_next_token(parser); // Consume 'while'

  ASTNode *condition = parse_expression(parser, PREC_LOWEST);

  if (!expect_peek(parser, TOKEN_NEWLINE))
    return NULL;
  parser_next_token(parser);

  ASTNode *body = parse_block_statement(parser);

  if (!current_token_is(parser, TOKEN_END)) {
    parser_add_error(parser, "Expected 'end' at end of while statement");
    return NULL;
  }
  parser_next_token(parser);

  if (current_token_is(parser, TOKEN_NEWLINE)) {
    parser_next_token(parser);
  }

  return ast_create_while_stmt(condition, body);
}

static ASTNode *parse_loop_statement(Parser *parser) {
  parser_next_token(parser); // Consume 'loop'

  if (!current_token_is(parser, TOKEN_IDENTIFIER)) {
    parser_add_error(parser, "Expected identifier after loop");
    return NULL;
  }

  char *iterator_name = strdup(parser->current_token->literal);
  parser_next_token(parser);

  if (!current_token_is(parser, TOKEN_FROM)) {
    parser_add_error(parser, "Expected 'from' in loop statement");
    free(iterator_name);
    return NULL;
  }
  parser_next_token(parser);

  ASTNode *start_expr = parse_expression(parser, PREC_LOWEST);

  if (!expect_peek(parser, TOKEN_TO)) {
    free(iterator_name);
    ast_free(start_expr);
    return NULL;
  }
  parser_next_token(parser);

  ASTNode *end_expr = parse_expression(parser, PREC_LOWEST);

  ASTNode *step_expr = NULL;
  if (peek_token_is(parser, TOKEN_STEP)) {
    parser_next_token(parser); // to step
    parser_next_token(parser); // past step
    step_expr = parse_expression(parser, PREC_LOWEST);
  }

  if (!expect_peek(parser, TOKEN_NEWLINE)) {
    free(iterator_name);
    ast_free(start_expr);
    ast_free(end_expr);
    if (step_expr)
      ast_free(step_expr);
    return NULL;
  }
  parser_next_token(parser);

  ASTNode *body = parse_block_statement(parser);

  if (!current_token_is(parser, TOKEN_END)) {
    parser_add_error(parser, "Expected 'end' at end of loop statement");
  } else {
    parser_next_token(parser);
  }

  if (current_token_is(parser, TOKEN_NEWLINE)) {
    parser_next_token(parser);
  }

  ASTNode *loop_stmt = ast_create_loop_stmt(iterator_name, start_expr, end_expr,
                                            step_expr, body);
  free(iterator_name);

  return loop_stmt;
}

static ASTNode *parse_for_statement(Parser *parser) {
  parser_next_token(parser); // Consume 'for'

  if (!current_token_is(parser, TOKEN_IDENTIFIER)) {
    parser_add_error(parser, "Expected identifier after for");
    return NULL;
  }

  char *iterator_name = strdup(parser->current_token->literal);
  parser_next_token(parser);

  if (!current_token_is(parser, TOKEN_IN)) {
    parser_add_error(parser, "Expected 'in' in for statement");
    free(iterator_name);
    return NULL;
  }
  parser_next_token(parser);

  ASTNode *iterable = parse_expression(parser, PREC_LOWEST);

  if (!expect_peek(parser, TOKEN_NEWLINE)) {
    free(iterator_name);
    ast_free(iterable);
    return NULL;
  }
  parser_next_token(parser);

  ASTNode *body = parse_block_statement(parser);

  if (!current_token_is(parser, TOKEN_END)) {
    parser_add_error(parser, "Expected 'end' at end of for statement");
  } else {
    parser_next_token(parser);
  }

  if (current_token_is(parser, TOKEN_NEWLINE)) {
    parser_next_token(parser);
  }

  ASTNode *for_stmt = ast_create_for_stmt(iterator_name, iterable, body);
  free(iterator_name);

  return for_stmt;
}

static ASTNode *parse_struct_decl(Parser *parser) {
  parser_next_token(parser); // consume 'struct'

  if (!current_token_is(parser, TOKEN_IDENTIFIER)) {
    parser_add_error(parser, "Expected struct name");
    return NULL;
  }

  ASTNode *struct_node = ast_create_struct_decl(parser->current_token->literal);

  if (!expect_peek(parser, TOKEN_NEWLINE))
    return struct_node;
  parser_next_token(parser);

  while (!current_token_is(parser, TOKEN_END) &&
         !current_token_is(parser, TOKEN_EOF)) {
    if (current_token_is(parser, TOKEN_NEWLINE)) {
      parser_next_token(parser);
      continue;
    }

    if (current_token_is(parser, TOKEN_IDENTIFIER)) {
      ast_struct_add_field(struct_node, parser->current_token->literal);
      parser_next_token(parser);
    } else {
      parser_add_error(parser, "Expected identifier for struct field");
      break;
    }
  }

  if (current_token_is(parser, TOKEN_END)) {
    parser_next_token(parser);
  }

  if (current_token_is(parser, TOKEN_NEWLINE)) {
    parser_next_token(parser);
  }

  return struct_node;
}

static ASTNode *parse_match_statement(Parser *parser) {
  parser_next_token(parser); // consume 'match'

  ASTNode *condition = parse_expression(parser, PREC_LOWEST);

  if (!expect_peek(parser, TOKEN_NEWLINE))
    return NULL;
  parser_next_token(parser);

  ASTNode *match_node = ast_create_match_stmt(condition);

  // Skip extra newlines
  while (current_token_is(parser, TOKEN_NEWLINE)) {
    parser_next_token(parser);
  }

  while (current_token_is(parser, TOKEN_CASE)) {
    parser_next_token(parser); // consume 'case'
    ASTNode *case_expr = parse_expression(parser, PREC_LOWEST);

    if (!expect_peek(parser, TOKEN_NEWLINE))
      return match_node;
    parser_next_token(parser);

    ASTNode *consequence = parse_block_statement(parser);
    ast_match_add_case(match_node, case_expr, consequence);

    while (current_token_is(parser, TOKEN_NEWLINE)) {
      parser_next_token(parser);
    }
  }

  if (current_token_is(parser, TOKEN_DEFAULT)) {
    if (!expect_peek(parser, TOKEN_NEWLINE))
      return match_node;
    parser_next_token(parser);

    match_node->as.match_stmt.default_consequence =
        parse_block_statement(parser);
  }

  if (!current_token_is(parser, TOKEN_END)) {
    parser_add_error(parser, "Expected 'end' at end of match statement");
  } else {
    parser_next_token(parser);
  }

  if (current_token_is(parser, TOKEN_NEWLINE)) {
    parser_next_token(parser);
  }

  return match_node;
}

static ASTNode *parse_statement(Parser *parser) {
  switch (parser->current_token->type) {
  case TOKEN_RETURN:
    return parse_return_statement(parser);
  case TOKEN_IF:
    return parse_if_statement(parser);
  case TOKEN_WHILE:
    return parse_while_statement(parser);
  case TOKEN_LOOP:
    return parse_loop_statement(parser);
  case TOKEN_FOR:
    return parse_for_statement(parser);
  case TOKEN_MATCH:
    return parse_match_statement(parser);
  case TOKEN_STRUCT:
    return parse_struct_decl(parser);
  case TOKEN_FUNCTION:
    return parse_function_decl(parser);
  case TOKEN_IMPORT: {
    // import module_name
    parser_next_token(parser); // consume 'import'
    if (!current_token_is(parser, TOKEN_IDENTIFIER)) {
      parser_add_error(parser, "Expected module name after 'import'");
      return NULL;
    }
    ASTNode *import_node = malloc(sizeof(ASTNode));
    import_node->type = AST_IMPORT_STMT;
    import_node->as.import_stmt.module_name =
        strdup(parser->current_token->literal);
    parser_next_token(parser); // consume module name
    if (current_token_is(parser, TOKEN_NEWLINE))
      parser_next_token(parser);
    return import_node;
  }
  case TOKEN_NEWLINE:
    // Do NOT consume here — callers (parse_block_statement,
    // parser_parse_program) have their own NEWLINE-consuming loops. If we
    // advance here, the caller's NULL-return error-recovery logic will skip the
    // FIRST token of the next statement.
    return NULL; // ignore empty lines

  default:
    return parse_expression_statement(parser);
  }
}

// --- MAIN ENTRY ---

void parser_init(Parser *parser, Lexer *lexer) {
  parser->lexer = lexer;
  parser->errors = NULL;
  parser->error_count = 0;
  parser->current_token = NULL;
  parser->peek_token = NULL;

  // Read two tokens, so current and peek are both set
  parser->peek_token = lexer_next_token(parser->lexer);
  parser_next_token(parser);
}

ASTNode *parser_parse_program(Parser *parser) {
  ASTNode *program = ast_create_program();

  while (parser->current_token->type != TOKEN_EOF) {
    // Skip all leading / between-statement newlines
    while (current_token_is(parser, TOKEN_NEWLINE)) {
      parser_next_token(parser);
    }
    if (current_token_is(parser, TOKEN_EOF))
      break;

    ASTNode *stmt = parse_statement(parser);
    if (stmt) {
      ast_program_add_statement(program, stmt);
    } else {
      // Error recovery: if parse_statement returned NULL without consuming the
      // problematic token, advance past it to avoid infinite loop.
      if (!current_token_is(parser, TOKEN_NEWLINE) &&
          !current_token_is(parser, TOKEN_EOF)) {
        parser_next_token(parser);
      }
    }
  }

  return program;
}

void parser_free(Parser *parser) {
  if (parser->current_token)
    token_free(parser->current_token);
  if (parser->peek_token)
    token_free(parser->peek_token);

  for (int i = 0; i < parser->error_count; i++) {
    free(parser->errors[i]);
  }
  if (parser->errors)
    free(parser->errors);
}

void parser_print_errors(Parser *parser) {
  if (parser->error_count == 0)
    return;
  printf("Parser errors:\n");
  for (int i = 0; i < parser->error_count; i++) {
    printf("\t%s\n", parser->errors[i]);
  }
}
