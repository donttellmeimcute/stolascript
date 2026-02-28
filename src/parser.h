#ifndef PARSER_H
#define PARSER_H

#include "ast.h"
#include "lexer.h"


typedef struct {
  Lexer *lexer;
  Token *current_token;
  Token *peek_token;

  // Error tracking
  char **errors;
  int error_count;
} Parser;

void parser_init(Parser *parser, Lexer *lexer);
ASTNode *parser_parse_program(Parser *parser);
void parser_free(Parser *parser);
void parser_print_errors(Parser *parser);

#endif // PARSER_H
