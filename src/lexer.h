#ifndef LEXER_H
#define LEXER_H

#include "token.h"

typedef struct {
  const char *source;
  int position;
  int read_position;
  char ch;
  int line;
  int column;
} Lexer;

void lexer_init(Lexer *lexer, const char *source);
Token *lexer_next_token(Lexer *lexer);
void token_free(Token *token);
const char *token_type_to_string(TokenType type);

#endif // LEXER_H
