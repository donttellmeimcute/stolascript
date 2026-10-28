#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "lexer.h"

typedef struct {
  Lexer *lexer;
  Token *current_token;
  Token *peek_token;
  const char *filename;

  // Newlines are insignificant inside (), [] and {} (implicit line joining)
  int paren_depth;

  // Error tracking
  char **errors;
  int error_count;
} Parser;

void parser_init(Parser *parser, Lexer *lexer, const char *filename);
ASTNode *parser_parse_program(Parser *parser);
// Parse a single expression from source text (used by f-strings)
ASTNode *parser_parse_expression_text(const char *text, const char *filename,
                                      int line, Parser *errors_to);
void parser_free(Parser *parser);
void parser_print_errors(Parser *parser);

#endif // PARSER_H
