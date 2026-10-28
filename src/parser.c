#include "parser.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// StolasScript parser — Pratt parser for expressions, recursive
// descent for statements. Blocks are closed with `end`.
// ============================================================

typedef enum {
  PREC_LOWEST = 1,
  PREC_TERNARY,     // a if c else b
  PREC_OR,          // or
  PREC_AND,         // and
  PREC_NOT,         // not x
  PREC_EQUALS,      // ==, !=, in, not in
  PREC_LESSGREATER, // >, <, >=, <=
  PREC_BIT_OR,      // |
  PREC_BIT_XOR,     // ^
  PREC_BIT_AND,     // &
  PREC_SHIFT,       // << >>
  PREC_SUM,         // + -
  PREC_PRODUCT,     // * / %
  PREC_PREFIX,      // -x, ~x
  PREC_POWER,       // ** (binds tighter than unary minus on its left)
  PREC_CALL,        // f(x)
  PREC_INDEX        // a[i], a.b, a at i
} Precedence;

static int lambda_counter = 0;

static Precedence get_precedence(TokenType type) {
  switch (type) {
  case TOKEN_IF:
    return PREC_TERNARY;
  case TOKEN_OR:
    return PREC_OR;
  case TOKEN_AND:
    return PREC_AND;
  case TOKEN_EQUALS:
  case TOKEN_NOT_EQUALS:
  case TOKEN_IN:
  case TOKEN_NOT_IN:
    return PREC_EQUALS;
  case TOKEN_LESS_THAN:
  case TOKEN_GREATER_THAN:
  case TOKEN_LESS_OR_EQUALS:
  case TOKEN_GREATER_OR_EQUALS:
    return PREC_LESSGREATER;
  case TOKEN_BIT_OR:
    return PREC_BIT_OR;
  case TOKEN_BIT_XOR:
    return PREC_BIT_XOR;
  case TOKEN_BIT_AND:
    return PREC_BIT_AND;
  case TOKEN_SHL:
  case TOKEN_SHR:
    return PREC_SHIFT;
  case TOKEN_PLUS:
  case TOKEN_MINUS:
    return PREC_SUM;
  case TOKEN_TIMES:
  case TOKEN_DIVIDED_BY:
  case TOKEN_MODULO:
    return PREC_PRODUCT;
  case TOKEN_POWER:
    return PREC_POWER;
  case TOKEN_LPAREN:
    return PREC_CALL;
  case TOKEN_LBRACKET:
  case TOKEN_DOT:
  case TOKEN_AT:
    return PREC_INDEX;
  default:
    return PREC_LOWEST;
  }
}

typedef ASTNode *(*PrefixParseFn)(Parser *parser);
typedef ASTNode *(*InfixParseFn)(Parser *parser, ASTNode *left);

static ASTNode *parse_expression(Parser *parser, Precedence precedence);
static ASTNode *parse_statement(Parser *parser);
static ASTNode *parse_block(Parser *parser);
static ASTNode *parse_function_rest(Parser *parser, char *name, int line,
                                    int is_lambda);

// ------------------------------------------------------------
// Token helpers
// ------------------------------------------------------------

static Token *fetch_token(Parser *parser) {
  Token *t = lexer_next_token(parser->lexer);
  while (parser->paren_depth > 0 && t->type == TOKEN_NEWLINE) {
    token_free(t);
    t = lexer_next_token(parser->lexer);
  }
  return t;
}

static void parser_next_token(Parser *parser) {
  if (parser->current_token)
    token_free(parser->current_token);
  parser->current_token = parser->peek_token;
  parser->peek_token = fetch_token(parser);
}

// Re-apply newline skipping to the lookahead after paren_depth changes.
static void refresh_peek(Parser *parser) {
  while (parser->paren_depth > 0 && parser->peek_token->type == TOKEN_NEWLINE) {
    token_free(parser->peek_token);
    parser->peek_token = lexer_next_token(parser->lexer);
  }
}

static void open_paren(Parser *parser) {
  parser->paren_depth++;
  refresh_peek(parser);
}

static void close_paren(Parser *parser) {
  if (parser->paren_depth > 0)
    parser->paren_depth--;
}

static void parser_error(Parser *parser, int line, const char *fmt, ...) {
  char msg[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  char full[700];
  snprintf(full, sizeof(full), "%s:%d: error: %s",
           parser->filename ? parser->filename : "<input>", line, msg);
  // Avoid cascades of identical errors on the same line
  if (parser->error_count > 0 &&
      strcmp(parser->errors[parser->error_count - 1], full) == 0)
    return;
  AST_PUSH(parser->errors, parser->error_count, ast_strdup(full));
}

static int cur_is(Parser *parser, TokenType type) {
  return parser->current_token->type == type;
}

static int peek_is(Parser *parser, TokenType type) {
  return parser->peek_token->type == type;
}

static int expect_peek(Parser *parser, TokenType type) {
  if (peek_is(parser, type)) {
    parser_next_token(parser);
    return 1;
  }
  parser_error(parser, parser->peek_token->line, "expected %s, got %s",
               token_type_to_string(type),
               token_type_to_string(parser->peek_token->type));
  return 0;
}

static int is_block_terminator(TokenType t) {
  return t == TOKEN_END || t == TOKEN_ELSE || t == TOKEN_ELIF ||
         t == TOKEN_CASE || t == TOKEN_DEFAULT || t == TOKEN_CATCH ||
         t == TOKEN_EOF;
}

// Skip tokens until the start of the next line (error recovery).
static void skip_to_next_line(Parser *parser) {
  int saved = parser->paren_depth;
  parser->paren_depth = 0;
  while (!cur_is(parser, TOKEN_NEWLINE) && !cur_is(parser, TOKEN_EOF))
    parser_next_token(parser);
  if (cur_is(parser, TOKEN_NEWLINE))
    parser_next_token(parser);
  (void)saved;
}

// After the last token of a simple statement: consume the newline.
static int finish_simple_stmt(Parser *parser) {
  if (peek_is(parser, TOKEN_NEWLINE)) {
    parser_next_token(parser);
    parser_next_token(parser);
    return 1;
  }
  if (is_block_terminator(parser->peek_token->type)) {
    parser_next_token(parser);
    return 1;
  }
  parser_error(parser, parser->peek_token->line,
               "expected end of line, got %s",
               token_type_to_string(parser->peek_token->type));
  parser_next_token(parser);
  skip_to_next_line(parser);
  return 0;
}

// After a block header (`if cond`, `while cond`...): optional ':' + newline.
static int finish_header(Parser *parser) {
  if (peek_is(parser, TOKEN_COLON))
    parser_next_token(parser);
  if (peek_is(parser, TOKEN_NEWLINE)) {
    parser_next_token(parser);
    parser_next_token(parser);
    return 1;
  }
  parser_error(parser, parser->peek_token->line,
               "expected newline after block header, got %s",
               token_type_to_string(parser->peek_token->type));
  return 0;
}

// Consume `end` (current) and the following newline.
static int finish_block(Parser *parser, const char *what, int line) {
  if (!cur_is(parser, TOKEN_END)) {
    parser_error(parser, parser->current_token->line,
                 "expected 'end' to close %s started on line %d, got %s",
                 what, line, token_type_to_string(parser->current_token->type));
    return 0;
  }
  parser_next_token(parser);
  if (cur_is(parser, TOKEN_NEWLINE))
    parser_next_token(parser);
  return 1;
}

// ------------------------------------------------------------
// Expressions
// ------------------------------------------------------------

static ASTNode *parse_identifier(Parser *parser) {
  return ast_create_identifier(parser->current_token->literal,
                               parser->current_token->line);
}

static ASTNode *parse_number_literal(Parser *parser) {
  return ast_create_number(parser->current_token->literal,
                           parser->current_token->line);
}

static ASTNode *parse_float_literal(Parser *parser) {
  ASTNode *n = ast_new(AST_FLOAT_LITERAL, parser->current_token->line);
  n->as.number_literal.value = ast_strdup(parser->current_token->literal);
  return n;
}

static ASTNode *parse_string_literal(Parser *parser) {
  return ast_create_string(parser->current_token->literal,
                           parser->current_token->length,
                           parser->current_token->line);
}

static ASTNode *make_str_call(ASTNode *expr, int line) {
  ASTNode *call = ast_create_call(ast_create_identifier("str", line), line);
  AST_PUSH(call->as.call_expr.args, call->as.call_expr.arg_count, expr);
  return call;
}

// f"Hola {nombre}, tienes {edad + 1} años"  →  "Hola " + str(nombre) + ...
static ASTNode *parse_fstring(Parser *parser) {
  const char *s = parser->current_token->literal;
  int n = parser->current_token->length;
  int line = parser->current_token->line;
  ASTNode *result = NULL;

  char *lit = malloc(n + 1);
  int lit_len = 0;

  for (int i = 0; i <= n; i++) {
    int flush = (i == n);
    if (!flush && s[i] == '{' && i + 1 < n && s[i + 1] == '{') {
      lit[lit_len++] = '{';
      i++;
      continue;
    }
    if (!flush && s[i] == '}' && i + 1 < n && s[i + 1] == '}') {
      lit[lit_len++] = '}';
      i++;
      continue;
    }
    if (!flush && s[i] != '{') {
      lit[lit_len++] = s[i];
      continue;
    }
    // flush pending literal text
    if (lit_len > 0 || (!result && flush)) {
      ASTNode *str = ast_create_string(lit, lit_len, line);
      result = result ? ast_create_binary_op(TOKEN_PLUS, result, str, line)
                      : str;
      lit_len = 0;
    }
    if (flush)
      break;
    // parse {expr}
    int depth = 1, j = i + 1;
    while (j < n && depth > 0) {
      if (s[j] == '{')
        depth++;
      else if (s[j] == '}')
        depth--;
      if (depth > 0)
        j++;
    }
    if (j >= n) {
      parser_error(parser, line, "unterminated '{' in f-string");
      break;
    }
    char *expr_text = malloc(j - i);
    memcpy(expr_text, s + i + 1, j - i - 1);
    expr_text[j - i - 1] = '\0';
    ASTNode *expr =
        parser_parse_expression_text(expr_text, parser->filename, line, parser);
    free(expr_text);
    if (expr) {
      ASTNode *piece = make_str_call(expr, line);
      if (!result) // ensure string context: "" + str(x)
        result = piece;
      else
        result = ast_create_binary_op(TOKEN_PLUS, result, piece, line);
    }
    i = j;
  }
  free(lit);
  if (!result)
    result = ast_create_string("", 0, line);
  return result;
}

static ASTNode *parse_boolean_literal(Parser *parser) {
  ASTNode *n = ast_new(AST_BOOLEAN_LITERAL, parser->current_token->line);
  n->as.boolean_literal.value = cur_is(parser, TOKEN_TRUE);
  return n;
}

static ASTNode *parse_null_literal(Parser *parser) {
  return ast_new(AST_NULL_LITERAL, parser->current_token->line);
}

static ASTNode *parse_this(Parser *parser) {
  return ast_new(AST_THIS, parser->current_token->line);
}

static ASTNode *parse_super(Parser *parser) {
  return ast_new(AST_SUPER, parser->current_token->line);
}

// Parses "(a, b, c)" — current token is '('. Leaves current at ')'.
static void parse_arg_list(Parser *parser, ASTNode ***args, int *count) {
  open_paren(parser);
  if (peek_is(parser, TOKEN_RPAREN)) {
    close_paren(parser);
    parser_next_token(parser);
    return;
  }
  parser_next_token(parser);
  for (;;) {
    ASTNode *arg = parse_expression(parser, PREC_LOWEST);
    if (!arg)
      break;
    AST_PUSH(*args, *count, arg);
    if (peek_is(parser, TOKEN_COMMA)) {
      parser_next_token(parser);
      if (peek_is(parser, TOKEN_RPAREN))
        break; // trailing comma
      parser_next_token(parser);
      continue;
    }
    break;
  }
  close_paren(parser);
  expect_peek(parser, TOKEN_RPAREN);
}

static ASTNode *parse_new_expr(Parser *parser) {
  int line = parser->current_token->line;
  if (!expect_peek(parser, TOKEN_IDENTIFIER))
    return NULL;
  ASTNode *node = ast_new(AST_NEW_EXPR, line);
  node->as.new_expr.class_name = parse_identifier(parser);
  if (!expect_peek(parser, TOKEN_LPAREN)) {
    ast_free(node);
    return NULL;
  }
  parse_arg_list(parser, &node->as.new_expr.args, &node->as.new_expr.arg_count);
  return node;
}

static ASTNode *parse_prefix_expression(Parser *parser) {
  TokenType op = parser->current_token->type;
  int line = parser->current_token->line;
  parser_next_token(parser);
  ASTNode *right =
      parse_expression(parser, op == TOKEN_NOT ? PREC_NOT : PREC_PREFIX);
  if (!right)
    return NULL;
  // Fold negative numeric literals directly
  if (op == TOKEN_MINUS && (right->type == AST_NUMBER_LITERAL ||
                            right->type == AST_FLOAT_LITERAL)) {
    char *v = right->as.number_literal.value;
    char *nv = malloc(strlen(v) + 2);
    if (v[0] == '-')
      strcpy(nv, v + 1);
    else {
      nv[0] = '-';
      strcpy(nv + 1, v);
    }
    free(v);
    right->as.number_literal.value = nv;
    return right;
  }
  ASTNode *node = ast_new(AST_UNARY_OP, line);
  node->as.unary_op.op = op;
  node->as.unary_op.right = right;
  return node;
}

static ASTNode *parse_infix_expression(Parser *parser, ASTNode *left) {
  TokenType op = parser->current_token->type;
  int line = parser->current_token->line;
  Precedence precedence = get_precedence(op);
  parser_next_token(parser);
  // ** is right associative
  ASTNode *right = parse_expression(
      parser, op == TOKEN_POWER ? (Precedence)(precedence - 1) : precedence);
  if (!right) {
    ast_free(left);
    return NULL;
  }
  return ast_create_binary_op(op, left, right, line);
}

// a if cond else b
static ASTNode *parse_ternary(Parser *parser, ASTNode *left) {
  int line = parser->current_token->line;
  parser_next_token(parser); // consume 'if'
  ASTNode *cond = parse_expression(parser, PREC_TERNARY);
  if (!cond || !expect_peek(parser, TOKEN_ELSE)) {
    ast_free(left);
    ast_free(cond);
    return NULL;
  }
  parser_next_token(parser);
  ASTNode *other = parse_expression(parser, PREC_LOWEST);
  ASTNode *node = ast_new(AST_TERNARY, line);
  node->as.ternary.cond = cond;
  node->as.ternary.then_expr = left;
  node->as.ternary.else_expr = other;
  return node;
}

static ASTNode *parse_grouped_expression(Parser *parser) {
  open_paren(parser);
  parser_next_token(parser);
  ASTNode *exp = parse_expression(parser, PREC_LOWEST);
  close_paren(parser);
  if (!expect_peek(parser, TOKEN_RPAREN)) {
    ast_free(exp);
    return NULL;
  }
  return exp;
}

static ASTNode *parse_call_expression(Parser *parser, ASTNode *function) {
  ASTNode *call = ast_create_call(function, parser->current_token->line);
  parse_arg_list(parser, &call->as.call_expr.args,
                 &call->as.call_expr.arg_count);
  return call;
}

static ASTNode *parse_array_literal(Parser *parser) {
  ASTNode *array = ast_new(AST_ARRAY_LITERAL, parser->current_token->line);
  open_paren(parser);
  if (peek_is(parser, TOKEN_RBRACKET)) {
    close_paren(parser);
    parser_next_token(parser);
    return array;
  }
  parser_next_token(parser);
  for (;;) {
    ASTNode *e = parse_expression(parser, PREC_LOWEST);
    if (!e)
      break;
    AST_PUSH(array->as.array_literal.elements,
             array->as.array_literal.element_count, e);
    if (peek_is(parser, TOKEN_COMMA)) {
      parser_next_token(parser);
      if (peek_is(parser, TOKEN_RBRACKET))
        break;
      parser_next_token(parser);
      continue;
    }
    break;
  }
  close_paren(parser);
  if (!expect_peek(parser, TOKEN_RBRACKET)) {
    ast_free(array);
    return NULL;
  }
  return array;
}

static ASTNode *parse_dict_literal(Parser *parser) {
  ASTNode *dict = ast_new(AST_DICT_LITERAL, parser->current_token->line);
  open_paren(parser);
  if (peek_is(parser, TOKEN_RBRACE)) {
    close_paren(parser);
    parser_next_token(parser);
    return dict;
  }
  parser_next_token(parser);
  for (;;) {
    ASTNode *key;
    // Bare identifiers as keys are string keys: {nombre: "x"}
    if (cur_is(parser, TOKEN_IDENTIFIER) && peek_is(parser, TOKEN_COLON)) {
      key = ast_create_string(parser->current_token->literal,
                              parser->current_token->length,
                              parser->current_token->line);
    } else {
      key = parse_expression(parser, PREC_LOWEST);
    }
    if (!key || !expect_peek(parser, TOKEN_COLON)) {
      ast_free(key);
      close_paren(parser);
      ast_free(dict);
      return NULL;
    }
    parser_next_token(parser);
    ASTNode *value = parse_expression(parser, PREC_LOWEST);
    AST_PUSH(dict->as.dict_literal.keys, dict->as.dict_literal.pair_count, key);
    dict->as.dict_literal.pair_count--;
    AST_PUSH(dict->as.dict_literal.values, dict->as.dict_literal.pair_count,
             value);
    if (peek_is(parser, TOKEN_COMMA)) {
      parser_next_token(parser);
      if (peek_is(parser, TOKEN_RBRACE))
        break;
      parser_next_token(parser);
      continue;
    }
    break;
  }
  close_paren(parser);
  if (!expect_peek(parser, TOKEN_RBRACE)) {
    ast_free(dict);
    return NULL;
  }
  return dict;
}

// a[i]  or  a[start:end]
static ASTNode *parse_index_access(Parser *parser, ASTNode *left) {
  int line = parser->current_token->line;
  open_paren(parser);
  parser_next_token(parser); // consume '['
  ASTNode *start = NULL;
  if (!cur_is(parser, TOKEN_COLON)) {
    start = parse_expression(parser, PREC_LOWEST);
    if (peek_is(parser, TOKEN_COLON))
      parser_next_token(parser);
  }
  if (cur_is(parser, TOKEN_COLON)) {
    ASTNode *end = NULL;
    if (!peek_is(parser, TOKEN_RBRACKET)) {
      parser_next_token(parser);
      end = parse_expression(parser, PREC_LOWEST);
    }
    close_paren(parser);
    if (!expect_peek(parser, TOKEN_RBRACKET)) {
      ast_free(left);
      ast_free(start);
      ast_free(end);
      return NULL;
    }
    ASTNode *slice = ast_new(AST_SLICE, line);
    slice->as.slice.object = left;
    slice->as.slice.start = start;
    slice->as.slice.end = end;
    return slice;
  }
  close_paren(parser);
  if (!expect_peek(parser, TOKEN_RBRACKET)) {
    ast_free(left);
    ast_free(start);
    return NULL;
  }
  return ast_create_member_access(left, start, 1, line);
}

static ASTNode *parse_at_index(Parser *parser, ASTNode *left) {
  int line = parser->current_token->line;
  parser_next_token(parser);
  ASTNode *index = parse_expression(parser, PREC_INDEX);
  if (!index) {
    ast_free(left);
    return NULL;
  }
  return ast_create_member_access(left, index, 1, line);
}

static ASTNode *parse_member_access(Parser *parser, ASTNode *left) {
  int line = parser->current_token->line;
  parser_next_token(parser); // consume '.'
  if (parser->current_token->type == TOKEN_EOF ||
      parser->current_token->type == TOKEN_NEWLINE ||
      !parser->current_token->literal ||
      !(cur_is(parser, TOKEN_IDENTIFIER) ||
        // allow keywords as member names: obj.end, obj.default ...
        (parser->current_token->literal[0] >= 'a' &&
         parser->current_token->literal[0] <= 'z'))) {
    parser_error(parser, line, "expected member name after '.', got %s",
                 token_type_to_string(parser->current_token->type));
    return left;
  }
  ASTNode *property = parse_identifier(parser);
  return ast_create_member_access(left, property, 0, line);
}

// Is the current `fn` identifier the start of `fn(params) => expr`?
// The lexer already consumed the '(' (peek token); scan the raw source for
// the matching ')' and check that '=>' follows.
static int looks_like_arrow_lambda(Parser *parser) {
  if (!cur_is(parser, TOKEN_IDENTIFIER) || !peek_is(parser, TOKEN_LPAREN) ||
      strcmp(parser->current_token->literal, "fn") != 0)
    return 0;
  const char *src = parser->lexer->source;
  int i = parser->lexer->position;
  int depth = 1;
  while (src[i] && depth > 0) {
    char c = src[i];
    if (c == '"' || c == '\'') {
      char q = c;
      i++;
      while (src[i] && src[i] != q && src[i] != '\n') {
        if (src[i] == '\\' && src[i + 1])
          i++;
        i++;
      }
    } else if (c == '(') {
      depth++;
    } else if (c == ')') {
      depth--;
    } else if (c == '\n' && parser->paren_depth == 0) {
      return 0;
    }
    if (src[i])
      i++;
  }
  while (src[i] == ' ' || src[i] == '\t')
    i++;
  return src[i] == '=' && src[i + 1] == '>';
}

// function (a, b) ... end    |    fn(a, b) => expr
static ASTNode *parse_lambda(Parser *parser) {
  int line = parser->current_token->line;
  int is_short = cur_is(parser, TOKEN_IDENTIFIER);
  char name[64];
  snprintf(name, sizeof(name), "__lambda_%d", lambda_counter++);
  if (!expect_peek(parser, TOKEN_LPAREN))
    return NULL;

  ASTNode *func;
  if (is_short) {
    // Peek ahead: parse params, then decide between '=>' and block body
    func = parse_function_rest(parser, ast_strdup(name), line, 2);
  } else {
    func = parse_function_rest(parser, ast_strdup(name), line, 1);
  }
  if (!func)
    return NULL;
  ASTNode *node = ast_new(AST_LAMBDA, line);
  node->as.lambda.func = func;
  return node;
}

static PrefixParseFn get_prefix_fn(Parser *parser) {
  switch (parser->current_token->type) {
  case TOKEN_IDENTIFIER:
    if (looks_like_arrow_lambda(parser))
      return parse_lambda;
    return parse_identifier;
  case TOKEN_NUMBER:
    return parse_number_literal;
  case TOKEN_FLOAT:
    return parse_float_literal;
  case TOKEN_STRING:
    return parse_string_literal;
  case TOKEN_FSTRING:
    return parse_fstring;
  case TOKEN_TRUE:
  case TOKEN_FALSE:
    return parse_boolean_literal;
  case TOKEN_NULL:
    return parse_null_literal;
  case TOKEN_THIS:
    return parse_this;
  case TOKEN_SUPER:
    return parse_super;
  case TOKEN_NEW:
    return parse_new_expr;
  case TOKEN_NOT:
  case TOKEN_MINUS:
  case TOKEN_BIT_NOT:
    return parse_prefix_expression;
  case TOKEN_LPAREN:
    return parse_grouped_expression;
  case TOKEN_LBRACKET:
    return parse_array_literal;
  case TOKEN_LBRACE:
    return parse_dict_literal;
  case TOKEN_FUNCTION:
    if (peek_is(parser, TOKEN_LPAREN))
      return parse_lambda;
    return NULL;
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
  case TOKEN_BIT_AND:
  case TOKEN_BIT_OR:
  case TOKEN_BIT_XOR:
  case TOKEN_SHL:
  case TOKEN_SHR:
  case TOKEN_IN:
  case TOKEN_NOT_IN:
    return parse_infix_expression;
  case TOKEN_IF:
    return parse_ternary;
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
  PrefixParseFn prefix = get_prefix_fn(parser);
  if (!prefix) {
    parser_error(parser, parser->current_token->line,
                 "unexpected %s in expression",
                 token_type_to_string(parser->current_token->type));
    return NULL;
  }

  ASTNode *left_exp = prefix(parser);
  if (!left_exp)
    return NULL;

  while (!peek_is(parser, TOKEN_NEWLINE) && !peek_is(parser, TOKEN_EOF) &&
         precedence < get_precedence(parser->peek_token->type)) {
    InfixParseFn infix = get_infix_fn(parser->peek_token->type);
    if (!infix)
      return left_exp;
    // A ternary needs an 'else' further on the same line; a bare `x if`
    // would otherwise swallow statements like `return x if ...`. We accept.
    parser_next_token(parser);
    left_exp = infix(parser, left_exp);
    if (!left_exp)
      return NULL;
  }

  return left_exp;
}

// ------------------------------------------------------------
// Statements
// ------------------------------------------------------------

static char *parse_type_name(Parser *parser) {
  // current token is the type identifier; accepts `name` or `name[]`
  char buf[128];
  snprintf(buf, sizeof(buf), "%s",
           parser->current_token->literal ? parser->current_token->literal
                                          : "any");
  if (peek_is(parser, TOKEN_LBRACKET)) {
    parser_next_token(parser);
    if (expect_peek(parser, TOKEN_RBRACKET))
      strncat(buf, "[]", sizeof(buf) - strlen(buf) - 1);
  }
  return ast_strdup(buf);
}

static ASTNode *parse_return_statement(Parser *parser) {
  ASTNode *node = ast_new(AST_RETURN_STMT, parser->current_token->line);
  if (peek_is(parser, TOKEN_NEWLINE) || is_block_terminator(parser->peek_token->type)) {
    finish_simple_stmt(parser);
    return node;
  }
  parser_next_token(parser);
  node->as.return_stmt.return_value = parse_expression(parser, PREC_LOWEST);
  if (!node->as.return_stmt.return_value) {
    ast_free(node);
    return NULL;
  }
  finish_simple_stmt(parser);
  return node;
}

static TokenType op_from_text(const char *op) {
  if (strcmp(op, "+") == 0) return TOKEN_PLUS;
  if (strcmp(op, "-") == 0) return TOKEN_MINUS;
  if (strcmp(op, "*") == 0) return TOKEN_TIMES;
  if (strcmp(op, "/") == 0) return TOKEN_DIVIDED_BY;
  if (strcmp(op, "%") == 0) return TOKEN_MODULO;
  if (strcmp(op, "**") == 0) return TOKEN_POWER;
  if (strcmp(op, "&") == 0) return TOKEN_BIT_AND;
  if (strcmp(op, "|") == 0) return TOKEN_BIT_OR;
  if (strcmp(op, "^") == 0) return TOKEN_BIT_XOR;
  if (strcmp(op, "<<") == 0) return TOKEN_SHL;
  if (strcmp(op, ">>") == 0) return TOKEN_SHR;
  return TOKEN_PLUS;
}

static ASTNode *parse_expression_statement(Parser *parser) {
  int line = parser->current_token->line;
  ASTNode *target = parse_expression(parser, PREC_LOWEST);
  if (!target) {
    skip_to_next_line(parser);
    return NULL;
  }

  char *type_annotation = NULL;
  if (peek_is(parser, TOKEN_COLON) && target->type == AST_IDENTIFIER) {
    parser_next_token(parser);
    if (expect_peek(parser, TOKEN_IDENTIFIER))
      type_annotation = parse_type_name(parser);
  }

  if (peek_is(parser, TOKEN_ASSIGN) || peek_is(parser, TOKEN_OP_ASSIGN)) {
    TokenType op = TOKEN_ASSIGN;
    if (peek_is(parser, TOKEN_OP_ASSIGN))
      op = op_from_text(parser->peek_token->literal);
    if (target->type != AST_IDENTIFIER && target->type != AST_MEMBER_ACCESS) {
      parser_error(parser, line, "invalid assignment target");
    }
    parser_next_token(parser);
    parser_next_token(parser);
    ASTNode *value = parse_expression(parser, PREC_LOWEST);
    if (!value) {
      ast_free(target);
      free(type_annotation);
      skip_to_next_line(parser);
      return NULL;
    }
    ASTNode *assign = ast_new(AST_ASSIGNMENT, line);
    assign->as.assignment.target = target;
    assign->as.assignment.value = value;
    assign->as.assignment.type_annotation = type_annotation;
    assign->as.assignment.op = op;
    finish_simple_stmt(parser);
    return assign;
  }

  if (type_annotation) {
    // `x: int` alone declares a variable initialised to 0 / null
    ASTNode *assign = ast_new(AST_ASSIGNMENT, line);
    assign->as.assignment.target = target;
    assign->as.assignment.value = NULL;
    assign->as.assignment.type_annotation = type_annotation;
    assign->as.assignment.op = TOKEN_ASSIGN;
    finish_simple_stmt(parser);
    return assign;
  }

  ASTNode *stmt = ast_new(AST_EXPRESSION_STMT, line);
  stmt->as.expression_stmt.expression = target;
  finish_simple_stmt(parser);
  return stmt;
}

static ASTNode *parse_block(Parser *parser) {
  ASTNode *block = ast_new(AST_BLOCK, parser->current_token->line);

  while (cur_is(parser, TOKEN_NEWLINE))
    parser_next_token(parser);

  while (!is_block_terminator(parser->current_token->type)) {
    ASTNode *stmt = parse_statement(parser);
    if (stmt && stmt->type == AST_FUNCTION_DECL &&
        !stmt->as.function_decl.is_interrupt) {
      // Nested `function name(...)`: a local variable holding a closure
      ASTNode *target =
          ast_create_identifier(stmt->as.function_decl.name, stmt->line);
      char lname[64];
      snprintf(lname, sizeof(lname), "__lambda_%d", lambda_counter++);
      free(stmt->as.function_decl.name);
      stmt->as.function_decl.name = ast_strdup(lname);
      stmt->as.function_decl.is_lambda = 1;
      ASTNode *lam = ast_new(AST_LAMBDA, stmt->line);
      lam->as.lambda.func = stmt;
      ASTNode *assign = ast_new(AST_ASSIGNMENT, stmt->line);
      assign->as.assignment.target = target;
      assign->as.assignment.value = lam;
      assign->as.assignment.op = TOKEN_ASSIGN;
      stmt = assign;
    }
    if (stmt)
      AST_PUSH(block->as.block.statements, block->as.block.statement_count,
               stmt);
    while (cur_is(parser, TOKEN_NEWLINE))
      parser_next_token(parser);
  }
  return block;
}

static ASTNode *parse_if_statement(Parser *parser) {
  int line = parser->current_token->line;
  ASTNode *node = ast_new(AST_IF_STMT, line);
  parser_next_token(parser);
  node->as.if_stmt.condition = parse_expression(parser, PREC_LOWEST);
  if (!node->as.if_stmt.condition || !finish_header(parser)) {
    ast_free(node);
    skip_to_next_line(parser);
    return NULL;
  }
  node->as.if_stmt.consequence = parse_block(parser);

  while (cur_is(parser, TOKEN_ELIF) ||
         (cur_is(parser, TOKEN_ELSE) && peek_is(parser, TOKEN_IF))) {
    if (cur_is(parser, TOKEN_ELSE))
      parser_next_token(parser); // `else if` == `elif`
    parser_next_token(parser);
    ASTNode *cond = parse_expression(parser, PREC_LOWEST);
    if (!cond || !finish_header(parser)) {
      ast_free(cond);
      ast_free(node);
      return NULL;
    }
    ASTNode *cons = parse_block(parser);
    AST_PUSH(node->as.if_stmt.elif_conditions, node->as.if_stmt.elif_count,
             cond);
    node->as.if_stmt.elif_count--;
    AST_PUSH(node->as.if_stmt.elif_consequences, node->as.if_stmt.elif_count,
             cons);
  }

  if (cur_is(parser, TOKEN_ELSE)) {
    if (!finish_header(parser)) {
      ast_free(node);
      return NULL;
    }
    node->as.if_stmt.alternative = parse_block(parser);
  }

  finish_block(parser, "'if'", line);
  return node;
}

static ASTNode *parse_while_statement(Parser *parser) {
  int line = parser->current_token->line;
  parser_next_token(parser);
  ASTNode *cond = parse_expression(parser, PREC_LOWEST);
  if (!cond || !finish_header(parser)) {
    ast_free(cond);
    skip_to_next_line(parser);
    return NULL;
  }
  ASTNode *node = ast_new(AST_WHILE_STMT, line);
  node->as.while_stmt.condition = cond;
  node->as.while_stmt.body = parse_block(parser);
  finish_block(parser, "'while'", line);
  return node;
}

// loop i from 0 to 10 [step 2]
static ASTNode *parse_loop_statement(Parser *parser) {
  int line = parser->current_token->line;
  if (!expect_peek(parser, TOKEN_IDENTIFIER))
    return NULL;
  ASTNode *node = ast_new(AST_LOOP_STMT, line);
  node->as.loop_stmt.iterator_name = ast_strdup(parser->current_token->literal);
  if (!expect_peek(parser, TOKEN_FROM)) {
    ast_free(node);
    return NULL;
  }
  parser_next_token(parser);
  node->as.loop_stmt.start_expr = parse_expression(parser, PREC_LOWEST);
  if (!expect_peek(parser, TOKEN_TO)) {
    ast_free(node);
    return NULL;
  }
  parser_next_token(parser);
  node->as.loop_stmt.end_expr = parse_expression(parser, PREC_LOWEST);
  if (peek_is(parser, TOKEN_STEP)) {
    parser_next_token(parser);
    parser_next_token(parser);
    node->as.loop_stmt.step_expr = parse_expression(parser, PREC_LOWEST);
  }
  if (!node->as.loop_stmt.start_expr || !node->as.loop_stmt.end_expr ||
      !finish_header(parser)) {
    ast_free(node);
    return NULL;
  }
  node->as.loop_stmt.body = parse_block(parser);
  finish_block(parser, "'loop'", line);
  return node;
}

// for x in iterable   |   for k, v in dict
static ASTNode *parse_for_statement(Parser *parser) {
  int line = parser->current_token->line;
  if (!expect_peek(parser, TOKEN_IDENTIFIER))
    return NULL;
  ASTNode *node = ast_new(AST_FOR_STMT, line);
  node->as.for_stmt.iterator_name = ast_strdup(parser->current_token->literal);
  if (peek_is(parser, TOKEN_COMMA)) {
    parser_next_token(parser);
    if (!expect_peek(parser, TOKEN_IDENTIFIER)) {
      ast_free(node);
      return NULL;
    }
    node->as.for_stmt.value_name = ast_strdup(parser->current_token->literal);
  }
  if (!expect_peek(parser, TOKEN_IN)) {
    ast_free(node);
    return NULL;
  }
  parser_next_token(parser);
  node->as.for_stmt.iterable = parse_expression(parser, PREC_LOWEST);
  if (!node->as.for_stmt.iterable || !finish_header(parser)) {
    ast_free(node);
    return NULL;
  }
  node->as.for_stmt.body = parse_block(parser);
  finish_block(parser, "'for'", line);
  return node;
}

// Parses the remainder of a function after its name. current token is '('.
// is_lambda: 0 = named function, 1 = `function(...) ... end` lambda,
//            2 = `fn(...)` lambda (accepts `=> expr` or a block body)
static ASTNode *parse_function_rest(Parser *parser, char *name, int line,
                                    int is_lambda) {
  ASTNode *func = ast_new(AST_FUNCTION_DECL, line);
  FunctionDeclNode *fd = &func->as.function_decl;
  fd->name = name;
  fd->is_lambda = is_lambda != 0;

  int saved_depth = parser->paren_depth;
  open_paren(parser);
  if (!peek_is(parser, TOKEN_RPAREN)) {
    for (;;) {
      if (peek_is(parser, TOKEN_THIS)) {
        // explicit `self` / `this` receiver (Python style) is implicit here
        parser_next_token(parser);
        if (peek_is(parser, TOKEN_COMMA)) {
          parser_next_token(parser);
          continue;
        }
        break;
      }
      if (!expect_peek(parser, TOKEN_IDENTIFIER))
        break;
      int count = fd->param_count;
      AST_PUSH(fd->parameters, count, ast_strdup(parser->current_token->literal));
      char *ptype = NULL;
      ASTNode *pdefault = NULL;
      if (peek_is(parser, TOKEN_COLON)) {
        parser_next_token(parser);
        if (expect_peek(parser, TOKEN_IDENTIFIER))
          ptype = parse_type_name(parser);
      }
      if (peek_is(parser, TOKEN_ASSIGN)) {
        parser_next_token(parser);
        parser_next_token(parser);
        pdefault = parse_expression(parser, PREC_LOWEST);
      }
      count = fd->param_count;
      AST_PUSH(fd->param_types, count, ptype ? ptype : ast_strdup("any"));
      count = fd->param_count;
      AST_PUSH(fd->param_defaults, count, pdefault);
      fd->param_count++;
      if (peek_is(parser, TOKEN_COMMA)) {
        parser_next_token(parser);
        if (peek_is(parser, TOKEN_RPAREN))
          break;
        continue;
      }
      break;
    }
  }
  close_paren(parser);
  if (!expect_peek(parser, TOKEN_RPAREN)) {
    parser->paren_depth = saved_depth;
    ast_free(func);
    return NULL;
  }

  if (peek_is(parser, TOKEN_ARROW)) {
    parser_next_token(parser);
    if (expect_peek(parser, TOKEN_IDENTIFIER))
      fd->return_type = parse_type_name(parser);
  }

  // Short lambda: fn(x) => expr
  if (is_lambda == 2 && peek_is(parser, TOKEN_FAT_ARROW)) {
    parser_next_token(parser);
    parser_next_token(parser);
    ASTNode *expr = parse_expression(parser, PREC_LOWEST);
    if (!expr) {
      ast_free(func);
      return NULL;
    }
    ASTNode *ret = ast_new(AST_RETURN_STMT, line);
    ret->as.return_stmt.return_value = expr;
    fd->body = ast_new(AST_BLOCK, line);
    AST_PUSH(fd->body->as.block.statements, fd->body->as.block.statement_count,
             ret);
    return func;
  }

  // Block body: newlines become significant again inside the body even if
  // the lambda itself is nested in parentheses.
  parser->paren_depth = 0;
  if (peek_is(parser, TOKEN_COLON))
    parser_next_token(parser);
  parser_next_token(parser);
  fd->body = parse_block(parser);
  if (!cur_is(parser, TOKEN_END)) {
    parser_error(parser, parser->current_token->line,
                 "expected 'end' to close function '%s' started on line %d",
                 fd->is_lambda ? "<lambda>" : fd->name, line);
    parser->paren_depth = saved_depth;
    return func;
  }
  parser->paren_depth = saved_depth;
  if (is_lambda) {
    // Leave current at 'end' — it is the last token of the expression.
    refresh_peek(parser);
    return func;
  }
  parser_next_token(parser);
  if (cur_is(parser, TOKEN_NEWLINE))
    parser_next_token(parser);
  return func;
}

static ASTNode *parse_function_decl(Parser *parser) {
  int line = parser->current_token->line;
  if (!expect_peek(parser, TOKEN_IDENTIFIER)) {
    skip_to_next_line(parser);
    return NULL;
  }
  char *name = ast_strdup(parser->current_token->literal);
  if (!expect_peek(parser, TOKEN_LPAREN)) {
    free(name);
    skip_to_next_line(parser);
    return NULL;
  }
  return parse_function_rest(parser, name, line, 0);
}

static ASTNode *parse_import_native_stmt(Parser *parser) {
  int line = parser->current_token->line;
  if (!expect_peek(parser, TOKEN_STRING))
    return NULL;
  ASTNode *node = ast_new(AST_IMPORT_NATIVE, line);
  node->as.import_native.dll_name = ast_strdup(parser->current_token->literal);
  finish_simple_stmt(parser);
  return node;
}

// c_function name(p: type, ...) -> type
static ASTNode *parse_c_function_decl(Parser *parser) {
  int line = parser->current_token->line;
  if (!expect_peek(parser, TOKEN_IDENTIFIER))
    return NULL;
  ASTNode *node = ast_new(AST_C_FUNCTION_DECL, line);
  ASTCFunctionDecl *cf = &node->as.c_function_decl;
  cf->name = ast_strdup(parser->current_token->literal);
  if (!expect_peek(parser, TOKEN_LPAREN)) {
    ast_free(node);
    return NULL;
  }
  open_paren(parser);
  while (!peek_is(parser, TOKEN_RPAREN) && !peek_is(parser, TOKEN_EOF)) {
    parser_next_token(parser);
    if (cur_is(parser, TOKEN_COMMA))
      continue;
    const char *type = "any";
    if (peek_is(parser, TOKEN_COLON)) {
      parser_next_token(parser);
      parser_next_token(parser);
      type = parser->current_token->literal;
    }
    AST_PUSH(cf->param_types, cf->param_count, ast_strdup(type));
  }
  close_paren(parser);
  expect_peek(parser, TOKEN_RPAREN);
  if (peek_is(parser, TOKEN_ARROW)) {
    parser_next_token(parser);
    parser_next_token(parser);
    cf->return_type = ast_strdup(parser->current_token->literal);
  } else {
    cf->return_type = ast_strdup("any");
  }
  finish_simple_stmt(parser);
  return node;
}

static ASTNode *parse_class_decl(Parser *parser) {
  int line = parser->current_token->line;
  if (!expect_peek(parser, TOKEN_IDENTIFIER))
    return NULL;
  ASTNode *node = ast_new(AST_CLASS_DECL, line);
  node->as.class_decl.name = ast_strdup(parser->current_token->literal);
  if (peek_is(parser, TOKEN_EXTENDS) || peek_is(parser, TOKEN_LPAREN)) {
    int paren = peek_is(parser, TOKEN_LPAREN);
    parser_next_token(parser);
    if (expect_peek(parser, TOKEN_IDENTIFIER))
      node->as.class_decl.parent = ast_strdup(parser->current_token->literal);
    if (paren)
      expect_peek(parser, TOKEN_RPAREN);
  }
  if (!finish_header(parser)) {
    ast_free(node);
    return NULL;
  }

  while (!cur_is(parser, TOKEN_END) && !cur_is(parser, TOKEN_EOF)) {
    if (cur_is(parser, TOKEN_NEWLINE) || cur_is(parser, TOKEN_PASS)) {
      parser_next_token(parser);
      continue;
    }
    if (cur_is(parser, TOKEN_FUNCTION)) {
      ASTNode *method = parse_function_decl(parser);
      if (method) {
        method->as.function_decl.class_name =
            ast_strdup(node->as.class_decl.name);
        AST_PUSH(node->as.class_decl.methods, node->as.class_decl.method_count,
                 method);
      }
      continue;
    }
    parser_error(parser, parser->current_token->line,
                 "expected method declaration inside class '%s', got %s",
                 node->as.class_decl.name,
                 token_type_to_string(parser->current_token->type));
    skip_to_next_line(parser);
  }
  finish_block(parser, "'class'", line);
  return node;
}

static ASTNode *parse_struct_decl(Parser *parser) {
  int line = parser->current_token->line;
  if (!expect_peek(parser, TOKEN_IDENTIFIER))
    return NULL;
  ASTNode *node = ast_new(AST_STRUCT_DECL, line);
  node->as.struct_decl.name = ast_strdup(parser->current_token->literal);
  if (!finish_header(parser)) {
    ast_free(node);
    return NULL;
  }
  while (!cur_is(parser, TOKEN_END) && !cur_is(parser, TOKEN_EOF)) {
    if (cur_is(parser, TOKEN_NEWLINE) || cur_is(parser, TOKEN_COMMA)) {
      parser_next_token(parser);
      continue;
    }
    if (cur_is(parser, TOKEN_IDENTIFIER)) {
      AST_PUSH(node->as.struct_decl.fields, node->as.struct_decl.field_count,
               ast_strdup(parser->current_token->literal));
      parser_next_token(parser);
      if (cur_is(parser, TOKEN_COLON)) { // optional field type
        parser_next_token(parser);
        parser_next_token(parser);
      }
      continue;
    }
    parser_error(parser, parser->current_token->line,
                 "expected field name in struct");
    skip_to_next_line(parser);
  }
  finish_block(parser, "'struct'", line);
  return node;
}

// match x
//   case 1 => stmt          (single line)
//   case 2                  (block)
//     ...
//   default => stmt
// end
static ASTNode *parse_case_body(Parser *parser) {
  if (peek_is(parser, TOKEN_FAT_ARROW) || peek_is(parser, TOKEN_COLON)) {
    parser_next_token(parser);
    if (!peek_is(parser, TOKEN_NEWLINE)) {
      parser_next_token(parser);
      ASTNode *block = ast_new(AST_BLOCK, parser->current_token->line);
      ASTNode *stmt = parse_statement(parser);
      if (stmt)
        AST_PUSH(block->as.block.statements, block->as.block.statement_count,
                 stmt);
      return block;
    }
  }
  if (!expect_peek(parser, TOKEN_NEWLINE))
    return NULL;
  parser_next_token(parser);
  return parse_block(parser);
}

static ASTNode *parse_match_statement(Parser *parser) {
  int line = parser->current_token->line;
  parser_next_token(parser);
  ASTNode *node = ast_new(AST_MATCH_STMT, line);
  node->as.match_stmt.condition = parse_expression(parser, PREC_LOWEST);
  if (!node->as.match_stmt.condition || !finish_header(parser)) {
    ast_free(node);
    return NULL;
  }
  while (cur_is(parser, TOKEN_NEWLINE))
    parser_next_token(parser);

  while (cur_is(parser, TOKEN_CASE)) {
    parser_next_token(parser);
    ASTNode *case_expr = parse_expression(parser, PREC_LOWEST);
    ASTNode *body = case_expr ? parse_case_body(parser) : NULL;
    if (!case_expr || !body) {
      ast_free(case_expr);
      ast_free(body);
      ast_free(node);
      return NULL;
    }
    AST_PUSH(node->as.match_stmt.cases, node->as.match_stmt.case_count,
             case_expr);
    node->as.match_stmt.case_count--;
    AST_PUSH(node->as.match_stmt.consequences, node->as.match_stmt.case_count,
             body);
    while (cur_is(parser, TOKEN_NEWLINE))
      parser_next_token(parser);
  }
  if (cur_is(parser, TOKEN_DEFAULT) || cur_is(parser, TOKEN_ELSE)) {
    node->as.match_stmt.default_consequence = parse_case_body(parser);
    while (cur_is(parser, TOKEN_NEWLINE))
      parser_next_token(parser);
  }
  finish_block(parser, "'match'", line);
  return node;
}

static ASTNode *parse_try_catch_statement(Parser *parser) {
  int line = parser->current_token->line;
  if (peek_is(parser, TOKEN_COLON))
    parser_next_token(parser);
  parser_next_token(parser);
  ASTNode *node = ast_new(AST_TRY_CATCH, line);
  node->as.try_catch_stmt.try_block = parse_block(parser);
  if (!cur_is(parser, TOKEN_CATCH)) {
    parser_error(parser, parser->current_token->line,
                 "expected 'catch' after try block (try started on line %d)",
                 line);
    ast_free(node);
    return NULL;
  }
  if (peek_is(parser, TOKEN_IDENTIFIER)) {
    parser_next_token(parser);
    node->as.try_catch_stmt.catch_var =
        ast_strdup(parser->current_token->literal);
  } else {
    node->as.try_catch_stmt.catch_var = ast_strdup("__error");
  }
  if (peek_is(parser, TOKEN_COLON))
    parser_next_token(parser);
  parser_next_token(parser);
  node->as.try_catch_stmt.catch_block = parse_block(parser);
  finish_block(parser, "'try'", line);
  return node;
}

static ASTNode *parse_throw_statement(Parser *parser) {
  int line = parser->current_token->line;
  parser_next_token(parser);
  ASTNode *expr = parse_expression(parser, PREC_LOWEST);
  if (!expr) {
    skip_to_next_line(parser);
    return NULL;
  }
  ASTNode *node = ast_new(AST_THROW, line);
  node->as.throw_stmt.exception_value = expr;
  finish_simple_stmt(parser);
  return node;
}

static ASTNode *parse_asm_block(Parser *parser) {
  ASTNode *node = ast_new(AST_ASM_BLOCK, parser->current_token->line);
  node->as.asm_block.code = ast_strdup(parser->current_token->literal);
  parser_next_token(parser);
  if (cur_is(parser, TOKEN_NEWLINE))
    parser_next_token(parser);
  return node;
}

static ASTNode *parse_global_decl(Parser *parser) {
  ASTNode *node = ast_new(AST_GLOBAL_DECL, parser->current_token->line);
  while (expect_peek(parser, TOKEN_IDENTIFIER)) {
    AST_PUSH(node->as.global_decl.names, node->as.global_decl.count,
             ast_strdup(parser->current_token->literal));
    if (!peek_is(parser, TOKEN_COMMA))
      break;
    parser_next_token(parser);
  }
  finish_simple_stmt(parser);
  return node;
}

// const NAME = expr
static ASTNode *parse_const_decl(Parser *parser) {
  int line = parser->current_token->line;
  if (!expect_peek(parser, TOKEN_IDENTIFIER))
    return NULL;
  ASTNode *node = ast_new(AST_CONST_DECL, line);
  node->as.const_decl.name = ast_strdup(parser->current_token->literal);
  if (peek_is(parser, TOKEN_COLON)) { // optional type, ignored
    parser_next_token(parser);
    parser_next_token(parser);
  }
  if (!expect_peek(parser, TOKEN_ASSIGN)) {
    ast_free(node);
    skip_to_next_line(parser);
    return NULL;
  }
  parser_next_token(parser);
  node->as.const_decl.value = parse_expression(parser, PREC_LOWEST);
  finish_simple_stmt(parser);
  return node;
}

// static buffer[4096] [align 16]
static ASTNode *parse_static_decl(Parser *parser) {
  int line = parser->current_token->line;
  if (!expect_peek(parser, TOKEN_IDENTIFIER))
    return NULL;
  ASTNode *node = ast_new(AST_STATIC_DECL, line);
  node->as.static_decl.name = ast_strdup(parser->current_token->literal);
  if (!expect_peek(parser, TOKEN_LBRACKET)) {
    ast_free(node);
    skip_to_next_line(parser);
    return NULL;
  }
  parser_next_token(parser);
  node->as.static_decl.size = parse_expression(parser, PREC_LOWEST);
  expect_peek(parser, TOKEN_RBRACKET);
  if (peek_is(parser, TOKEN_IDENTIFIER) &&
      strcmp(parser->peek_token->literal, "align") == 0) {
    parser_next_token(parser);
    parser_next_token(parser);
    node->as.static_decl.align = parse_expression(parser, PREC_LOWEST);
  }
  finish_simple_stmt(parser);
  return node;
}

static ASTNode *parse_import(Parser *parser) {
  int line = parser->current_token->line;
  parser_next_token(parser);
  ASTNode *node = ast_new(AST_IMPORT_STMT, line);
  if (cur_is(parser, TOKEN_STRING)) {
    node->as.import_stmt.module_name =
        ast_strdup(parser->current_token->literal);
    node->as.import_stmt.is_path = 1;
  } else if (cur_is(parser, TOKEN_IDENTIFIER)) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", parser->current_token->literal);
    // dotted module names: import os.vga → os/vga
    while (peek_is(parser, TOKEN_DOT)) {
      parser_next_token(parser);
      parser_next_token(parser);
      strncat(buf, "/", sizeof(buf) - strlen(buf) - 1);
      strncat(buf, parser->current_token->literal ? parser->current_token->literal : "",
              sizeof(buf) - strlen(buf) - 1);
    }
    node->as.import_stmt.module_name = ast_strdup(buf);
  } else {
    parser_error(parser, line, "expected module name after 'import'");
    ast_free(node);
    skip_to_next_line(parser);
    return NULL;
  }
  finish_simple_stmt(parser);
  return node;
}

static ASTNode *parse_simple_keyword(Parser *parser, ASTNodeType type) {
  ASTNode *node = ast_new(type, parser->current_token->line);
  finish_simple_stmt(parser);
  return node;
}

static ASTNode *parse_statement(Parser *parser) {
  switch (parser->current_token->type) {
  case TOKEN_TRY:
    return parse_try_catch_statement(parser);
  case TOKEN_THROW:
    return parse_throw_statement(parser);
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
  case TOKEN_CLASS:
    return parse_class_decl(parser);
  case TOKEN_FUNCTION:
    if (peek_is(parser, TOKEN_LPAREN))
      return parse_expression_statement(parser); // lambda expression
    return parse_function_decl(parser);
  case TOKEN_IMPORT_NATIVE:
    return parse_import_native_stmt(parser);
  case TOKEN_C_FUNCTION:
    return parse_c_function_decl(parser);
  case TOKEN_INTERRUPT: {
    if (!expect_peek(parser, TOKEN_FUNCTION))
      return NULL;
    ASTNode *func = parse_function_decl(parser);
    if (func)
      func->as.function_decl.is_interrupt = 1;
    return func;
  }
  case TOKEN_ASM:
    return parse_asm_block(parser);
  case TOKEN_IMPORT:
    return parse_import(parser);
  case TOKEN_GLOBAL:
    return parse_global_decl(parser);
  case TOKEN_CONST:
    return parse_const_decl(parser);
  case TOKEN_STATIC:
    return parse_static_decl(parser);
  case TOKEN_BREAK:
    return parse_simple_keyword(parser, AST_BREAK_STMT);
  case TOKEN_CONTINUE:
    return parse_simple_keyword(parser, AST_CONTINUE_STMT);
  case TOKEN_PASS:
    return parse_simple_keyword(parser, AST_PASS);
  case TOKEN_NEWLINE:
    parser_next_token(parser);
    return NULL;
  case TOKEN_ERROR:
    parser_error(parser, parser->current_token->line, "%s",
                 parser->current_token->length > 1
                     ? parser->current_token->literal
                     : "invalid character");
    skip_to_next_line(parser);
    return NULL;
  default:
    return parse_expression_statement(parser);
  }
}

// ------------------------------------------------------------
// Entry points
// ------------------------------------------------------------

void parser_init(Parser *parser, Lexer *lexer, const char *filename) {
  parser->lexer = lexer;
  parser->filename = filename;
  if (filename)
    ast_current_file = filename;
  parser->errors = NULL;
  parser->error_count = 0;
  parser->current_token = NULL;
  parser->peek_token = NULL;
  parser->paren_depth = 0;
  parser->peek_token = fetch_token(parser);
  parser_next_token(parser);
}

ASTNode *parser_parse_program(Parser *parser) {
  ASTNode *program = ast_new(AST_PROGRAM, 1);
  while (!cur_is(parser, TOKEN_EOF)) {
    while (cur_is(parser, TOKEN_NEWLINE))
      parser_next_token(parser);
    if (cur_is(parser, TOKEN_EOF))
      break;
    if (is_block_terminator(parser->current_token->type)) {
      parser_error(parser, parser->current_token->line, "unexpected %s",
                   token_type_to_string(parser->current_token->type));
      parser_next_token(parser);
      continue;
    }
    ASTNode *stmt = parse_statement(parser);
    if (stmt)
      AST_PUSH(program->as.program.statements,
               program->as.program.statement_count, stmt);
  }
  return program;
}

ASTNode *parser_parse_expression_text(const char *text, const char *filename,
                                      int line, Parser *errors_to) {
  Lexer lx;
  lexer_init(&lx, text);
  lx.line = line;
  Parser p;
  parser_init(&p, &lx, filename);
  p.paren_depth = 1;
  ASTNode *expr = parse_expression(&p, PREC_LOWEST);
  if (!peek_is(&p, TOKEN_EOF))
    parser_error(&p, line, "unexpected %s in f-string expression",
                 token_type_to_string(p.peek_token->type));
  for (int i = 0; i < p.error_count; i++)
    AST_PUSH(errors_to->errors, errors_to->error_count, ast_strdup(p.errors[i]));
  parser_free(&p);
  return expr;
}

void parser_free(Parser *parser) {
  if (parser->current_token)
    token_free(parser->current_token);
  if (parser->peek_token)
    token_free(parser->peek_token);
  parser->current_token = parser->peek_token = NULL;
  for (int i = 0; i < parser->error_count; i++)
    free(parser->errors[i]);
  free(parser->errors);
  parser->errors = NULL;
  parser->error_count = 0;
}

void parser_print_errors(Parser *parser) {
  for (int i = 0; i < parser->error_count; i++)
    fprintf(stderr, "%s\n", parser->errors[i]);
}
