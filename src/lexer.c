#include "lexer.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void lexer_init(Lexer *lexer, const char *source) {
  lexer->source = source;
  lexer->position = 0;
  lexer->read_position = 0;
  lexer->line = 1;
  lexer->column = 0;

  // Read first char
  if (lexer->source && lexer->source[0] != '\0') {
    lexer->ch = lexer->source[0];
    lexer->read_position = 1;
    lexer->column = 1;
  } else {
    lexer->ch = '\0';
  }
}

static void lexer_read_char(Lexer *lexer) {
  if (lexer->source[lexer->read_position] == '\0') {
    lexer->ch = '\0';
  } else {
    lexer->ch = lexer->source[lexer->read_position];
  }
  lexer->position = lexer->read_position;
  lexer->read_position++;
  lexer->column++;
}

static char lexer_peek_char(Lexer *lexer) {
  if (lexer->source[lexer->read_position] == '\0') {
    return '\0';
  } else {
    return lexer->source[lexer->read_position];
  }
}

static void lexer_skip_whitespace(Lexer *lexer) {
  while (lexer->ch == ' ' || lexer->ch == '\t' || lexer->ch == '\r') {
    lexer_read_char(lexer);
  }
}

static Token *create_token(TokenType type, const char *literal, int len,
                           int line, int column) {
  Token *token = malloc(sizeof(Token));
  token->type = type;
  token->line = line;
  token->column = column;

  if (literal) {
    token->literal = malloc(len + 1);
    strncpy(token->literal, literal, len);
    token->literal[len] = '\0';
  } else {
    token->literal = NULL;
  }

  return token;
}

static Token *create_char_token(TokenType type, Lexer *lexer) {
  char str[2] = {lexer->ch, '\0'};
  Token *tok = create_token(type, str, 1, lexer->line, lexer->column);
  lexer_read_char(lexer);
  return tok;
}

static Token *read_identifier(Lexer *lexer) {
  int start_pos = lexer->position;
  int col = lexer->column;

  while (isalpha(lexer->ch) || lexer->ch == '_' || isdigit(lexer->ch)) {
    lexer_read_char(lexer);
  }

  int len = lexer->position - start_pos;
  char *literal = malloc(len + 1);
  strncpy(literal, lexer->source + start_pos, len);
  literal[len] = '\0';

  TokenType type = TOKEN_IDENTIFIER;

  // Check keywords
  if (strcmp(literal, "if") == 0)
    type = TOKEN_IF;
  else if (strcmp(literal, "else") == 0)
    type = TOKEN_ELSE;
  else if (strcmp(literal, "elif") == 0)
    type = TOKEN_ELIF;
  else if (strcmp(literal, "while") == 0)
    type = TOKEN_WHILE;
  else if (strcmp(literal, "for") == 0)
    type = TOKEN_FOR;
  else if (strcmp(literal, "loop") == 0)
    type = TOKEN_LOOP;
  else if (strcmp(literal, "function") == 0)
    type = TOKEN_FUNCTION;
  else if (strcmp(literal, "match") == 0)
    type = TOKEN_MATCH;
  else if (strcmp(literal, "case") == 0)
    type = TOKEN_CASE;
  else if (strcmp(literal, "default") == 0)
    type = TOKEN_DEFAULT;
  else if (strcmp(literal, "struct") == 0)
    type = TOKEN_STRUCT;
  else if (strcmp(literal, "class") == 0)
    type = TOKEN_CLASS;
  else if (strcmp(literal, "this") == 0)
    type = TOKEN_THIS;
  else if (strcmp(literal, "new") == 0)
    type = TOKEN_NEW;
  else if (strcmp(literal, "import_native") == 0)
    type = TOKEN_IMPORT_NATIVE;
  else if (strcmp(literal, "c_function") == 0)
    type = TOKEN_C_FUNCTION;
  else if (strcmp(literal, "end") == 0)
    type = TOKEN_END;
  else if (strcmp(literal, "return") == 0)
    type = TOKEN_RETURN;
  else if (strcmp(literal, "in") == 0)
    type = TOKEN_IN;
  else if (strcmp(literal, "and") == 0)
    type = TOKEN_AND;
  else if (strcmp(literal, "or") == 0)
    type = TOKEN_OR;
  else if (strcmp(literal, "not") == 0)
    type = TOKEN_NOT;
  else if (strcmp(literal, "true") == 0)
    type = TOKEN_TRUE;
  else if (strcmp(literal, "false") == 0)
    type = TOKEN_FALSE;
  else if (strcmp(literal, "null") == 0)
    type = TOKEN_NULL;
  else if (strcmp(literal, "break") == 0)
    type = TOKEN_BREAK;
  else if (strcmp(literal, "continue") == 0)
    type = TOKEN_CONTINUE;
  else if (strcmp(literal, "from") == 0)
    type = TOKEN_FROM;
  else if (strcmp(literal, "to") == 0)
    type = TOKEN_TO;
  else if (strcmp(literal, "step") == 0)
    type = TOKEN_STEP;
  else if (strcmp(literal, "import") == 0)
    type = TOKEN_IMPORT;
  else if (strcmp(literal, "at") == 0)
    type = TOKEN_AT;

  // Single-word operators
  else if (strcmp(literal, "plus") == 0)
    type = TOKEN_PLUS;
  else if (strcmp(literal, "minus") == 0)
    type = TOKEN_MINUS;
  else if (strcmp(literal, "times") == 0)
    type = TOKEN_TIMES;
  else if (strcmp(literal, "modulo") == 0)
    type = TOKEN_MODULO;
  else if (strcmp(literal, "power") == 0)
    type = TOKEN_POWER;
  else if (strcmp(literal, "equals") == 0)
    type = TOKEN_EQUALS;

  // Handle multi-word operators via lookahead
  // e.g. "less than", "greater than", "divided by", "not equals"
  if (strcmp(literal, "less") == 0 || strcmp(literal, "greater") == 0 ||
      strcmp(literal, "divided") == 0 || strcmp(literal, "not") == 0) {
    // Save current position in case lookahead fails
    int saved_pos = lexer->position;
    int saved_read = lexer->read_position;
    char saved_ch = lexer->ch;
    int saved_col = lexer->column;

    // Skip whitespace
    while (lexer->ch == ' ' || lexer->ch == '\t') {
      lexer_read_char(lexer);
    }

    // Read next word
    int word2_start = lexer->position;
    while (isalpha(lexer->ch) || lexer->ch == '_') {
      lexer_read_char(lexer);
    }
    int word2_len = lexer->position - word2_start;

    if (word2_len > 0) {
      char word2[64];
      int copy_len = word2_len < 63 ? word2_len : 63;
      strncpy(word2, lexer->source + word2_start, copy_len);
      word2[copy_len] = '\0';

      if (strcmp(literal, "less") == 0 && strcmp(word2, "than") == 0) {
        free(literal);
        Token *t =
            create_token(TOKEN_LESS_THAN, "less than", 9, lexer->line, col);
        return t;
      } else if (strcmp(literal, "greater") == 0 &&
                 strcmp(word2, "than") == 0) {
        free(literal);
        Token *t = create_token(TOKEN_GREATER_THAN, "greater than", 12,
                                lexer->line, col);
        return t;
      } else if (strcmp(literal, "divided") == 0 && strcmp(word2, "by") == 0) {
        free(literal);
        Token *t =
            create_token(TOKEN_DIVIDED_BY, "divided by", 10, lexer->line, col);
        return t;
      } else if (strcmp(literal, "not") == 0 && strcmp(word2, "equals") == 0) {
        free(literal);
        Token *t =
            create_token(TOKEN_NOT_EQUALS, "not equals", 10, lexer->line, col);
        return t;
      } else if (strcmp(literal, "greater") == 0 && strcmp(word2, "or") == 0) {
        // Check for "greater or equals" (3 words)
        int saved2_pos = lexer->position;
        int saved2_read = lexer->read_position;
        char saved2_ch = lexer->ch;
        int saved2_col = lexer->column;
        while (lexer->ch == ' ' || lexer->ch == '\t')
          lexer_read_char(lexer);
        int w3_start = lexer->position;
        while (isalpha(lexer->ch) || lexer->ch == '_')
          lexer_read_char(lexer);
        int w3_len = lexer->position - w3_start;
        if (w3_len > 0) {
          char w3[64];
          int c3 = w3_len < 63 ? w3_len : 63;
          strncpy(w3, lexer->source + w3_start, c3);
          w3[c3] = '\0';
          if (strcmp(w3, "equals") == 0) {
            free(literal);
            return create_token(TOKEN_GREATER_OR_EQUALS, "greater or equals",
                                17, lexer->line, col);
          }
        }
        // Restore to after "or"
        lexer->position = saved2_pos;
        lexer->read_position = saved2_read;
        lexer->ch = saved2_ch;
        lexer->column = saved2_col;
      } else if (strcmp(literal, "less") == 0 && strcmp(word2, "or") == 0) {
        int saved2_pos = lexer->position;
        int saved2_read = lexer->read_position;
        char saved2_ch = lexer->ch;
        int saved2_col = lexer->column;
        while (lexer->ch == ' ' || lexer->ch == '\t')
          lexer_read_char(lexer);
        int w3_start = lexer->position;
        while (isalpha(lexer->ch) || lexer->ch == '_')
          lexer_read_char(lexer);
        int w3_len = lexer->position - w3_start;
        if (w3_len > 0) {
          char w3[64];
          int c3 = w3_len < 63 ? w3_len : 63;
          strncpy(w3, lexer->source + w3_start, c3);
          w3[c3] = '\0';
          if (strcmp(w3, "equals") == 0) {
            free(literal);
            return create_token(TOKEN_LESS_OR_EQUALS, "less or equals", 14,
                                lexer->line, col);
          }
        }
        lexer->position = saved2_pos;
        lexer->read_position = saved2_read;
        lexer->ch = saved2_ch;
        lexer->column = saved2_col;
      }
    }

    // No match — restore lexer to after the first word
    lexer->position = saved_pos;
    lexer->read_position = saved_read;
    lexer->ch = saved_ch;
    lexer->column = saved_col;

    // "not" alone is TOKEN_NOT keyword (already handled above)
  }

  Token *tok = create_token(type, literal, len, lexer->line, col);
  free(literal);
  return tok;
}

static Token *read_number(Lexer *lexer) {
  int start_pos = lexer->position;
  int col = lexer->column;

  while (isdigit(lexer->ch) || lexer->ch == '.') {
    lexer_read_char(lexer);
  }

  int len = lexer->position - start_pos;
  Token *tok = create_token(TOKEN_NUMBER, lexer->source + start_pos, len,
                            lexer->line, col);
  return tok;
}

static Token *read_string(Lexer *lexer, char quote_char) {
  int start_pos = lexer->position + 1; // skip quote
  int col = lexer->column;
  lexer_read_char(lexer);

  while (lexer->ch != quote_char && lexer->ch != '\0') {
    // TODO: Handle escape sequences
    lexer_read_char(lexer);
  }

  int len = lexer->position - start_pos;
  Token *tok = create_token(TOKEN_STRING, lexer->source + start_pos, len,
                            lexer->line, col);

  if (lexer->ch == quote_char) {
    lexer_read_char(lexer); // consume closing quote
  }

  return tok;
}

static void skip_comment(Lexer *lexer) {
  if (lexer->ch == '/' && lexer_peek_char(lexer) == '/') {
    // Single line comment
    while (lexer->ch != '\n' && lexer->ch != '\0') {
      lexer_read_char(lexer);
    }
  } else if (lexer->ch == '/' && lexer_peek_char(lexer) == '*') {
    // Multi-line comment
    lexer_read_char(lexer); // consume '/'
    lexer_read_char(lexer); // consume '*'
    while (lexer->ch != '\0') {
      if (lexer->ch == '*' && lexer_peek_char(lexer) == '/') {
        lexer_read_char(lexer); // consume '*'
        lexer_read_char(lexer); // consume '/'
        break;
      }
      if (lexer->ch == '\n') {
        lexer->line++;
        lexer->column = 0;
      }
      lexer_read_char(lexer);
    }
  }
}

Token *lexer_next_token(Lexer *lexer) {
  Token *tok = NULL;

  lexer_skip_whitespace(lexer);

  // Handle comments
  while (lexer->ch == '/' &&
         (lexer_peek_char(lexer) == '/' || lexer_peek_char(lexer) == '*')) {
    skip_comment(lexer);
    lexer_skip_whitespace(lexer);
  }

  int col = lexer->column;

  switch (lexer->ch) {
  case '\n':
    tok = create_token(TOKEN_NEWLINE, "\n", 1, lexer->line, col);
    lexer->line++;
    lexer->column = 0;
    lexer_read_char(lexer);
    return tok;
  case '=':
    if (lexer_peek_char(lexer) == '=') {
      char ch = lexer->ch;
      lexer_read_char(lexer);
      tok = create_token(TOKEN_EQUALS, "==", 2, lexer->line, col);
      lexer_read_char(lexer);
      return tok;
    } else {
      tok = create_char_token(TOKEN_ASSIGN, lexer);
    }
    break;
  case '+':
    tok = create_char_token(TOKEN_PLUS, lexer);
    break;
  case '-':
    if (lexer_peek_char(lexer) == '>') {
      lexer_read_char(lexer); // consume '-'
      tok = create_token(TOKEN_ARROW, "->", 2, lexer->line, col);
      lexer_read_char(lexer); // consume '>'
      return tok;
    } else {
      tok = create_char_token(TOKEN_MINUS, lexer);
    }
    break;
  case '*':
    if (lexer_peek_char(lexer) == '*') {
      lexer_read_char(lexer);
      tok = create_token(TOKEN_POWER, "**", 2, lexer->line, col);
      lexer_read_char(lexer);
      return tok;
    } else {
      tok = create_char_token(TOKEN_TIMES, lexer);
    }
    break;
  case '/':
    tok = create_char_token(TOKEN_DIVIDED_BY, lexer);
    break;
  case '%':
    tok = create_char_token(TOKEN_MODULO, lexer);
    break;
  case '<':
    if (lexer_peek_char(lexer) == '=') {
      lexer_read_char(lexer);
      tok = create_token(TOKEN_LESS_OR_EQUALS, "<=", 2, lexer->line, col);
      lexer_read_char(lexer);
      return tok;
    } else {
      tok = create_char_token(TOKEN_LESS_THAN, lexer);
    }
    break;
  case '>':
    if (lexer_peek_char(lexer) == '=') {
      lexer_read_char(lexer);
      tok = create_token(TOKEN_GREATER_OR_EQUALS, ">=", 2, lexer->line, col);
      lexer_read_char(lexer);
      return tok;
    } else {
      tok = create_char_token(TOKEN_GREATER_THAN, lexer);
    }
    break;
  case '!':
    if (lexer_peek_char(lexer) == '=') {
      lexer_read_char(lexer);
      tok = create_token(TOKEN_NOT_EQUALS, "!=", 2, lexer->line, col);
      lexer_read_char(lexer);
      return tok;
    } else {
      // Not supported plain '!' in spec, but good error handling
      tok = create_char_token(TOKEN_ERROR, lexer);
    }
    break;
  case '(':
    tok = create_char_token(TOKEN_LPAREN, lexer);
    break;
  case ')':
    tok = create_char_token(TOKEN_RPAREN, lexer);
    break;
  case '{':
    tok = create_char_token(TOKEN_LBRACE, lexer);
    break;
  case '}':
    tok = create_char_token(TOKEN_RBRACE, lexer);
    break;
  case '[':
    tok = create_char_token(TOKEN_LBRACKET, lexer);
    break;
  case ']':
    tok = create_char_token(TOKEN_RBRACKET, lexer);
    break;
  case ',':
    tok = create_char_token(TOKEN_COMMA, lexer);
    break;
  case '.':
    tok = create_char_token(TOKEN_DOT, lexer);
    break;
  case ':':
    tok = create_char_token(TOKEN_COLON, lexer);
    break;
  case '"':
  case '\'':
    tok = read_string(lexer, lexer->ch);
    break;
  case '\0':
    tok = create_token(TOKEN_EOF, "", 0, lexer->line, col);
    break;
  default:
    if (isalpha(lexer->ch) || lexer->ch == '_') {
      tok = read_identifier(lexer);
    } else if (isdigit(lexer->ch)) {
      tok = read_number(lexer);
    } else {
      tok = create_char_token(TOKEN_ERROR, lexer);
    }
    break;
  }

  return tok;
}

void token_free(Token *token) {
  if (token) {
    if (token->literal) {
      free(token->literal);
    }
    free(token);
  }
}

const char *token_type_to_string(TokenType type) {
  switch (type) {
  case TOKEN_IF:
    return "IF";
  case TOKEN_ELSE:
    return "ELSE";
  case TOKEN_ELIF:
    return "ELIF";
  case TOKEN_WHILE:
    return "WHILE";
  case TOKEN_FOR:
    return "FOR";
  case TOKEN_LOOP:
    return "LOOP";
  case TOKEN_FUNCTION:
    return "FUNCTION";
  case TOKEN_MATCH:
    return "MATCH";
  case TOKEN_CASE:
    return "CASE";
  case TOKEN_DEFAULT:
    return "DEFAULT";
  case TOKEN_STRUCT:
    return "STRUCT";
  case TOKEN_CLASS:
    return "CLASS";
  case TOKEN_THIS:
    return "THIS";
  case TOKEN_NEW:
    return "NEW";
  case TOKEN_IMPORT_NATIVE:
    return "IMPORT_NATIVE";
  case TOKEN_C_FUNCTION:
    return "C_FUNCTION";
  case TOKEN_END:
    return "END";
  case TOKEN_RETURN:
    return "RETURN";
  case TOKEN_IN:
    return "IN";
  case TOKEN_AND:
    return "AND";
  case TOKEN_OR:
    return "OR";
  case TOKEN_NOT:
    return "NOT";
  case TOKEN_TRUE:
    return "TRUE";
  case TOKEN_FALSE:
    return "FALSE";
  case TOKEN_NULL:
    return "NULL";
  case TOKEN_BREAK:
    return "BREAK";
  case TOKEN_CONTINUE:
    return "CONTINUE";
  case TOKEN_FROM:
    return "FROM";
  case TOKEN_TO:
    return "TO";
  case TOKEN_STEP:
    return "STEP";
  case TOKEN_IMPORT:
    return "IMPORT";
  case TOKEN_AT:
    return "AT";
  case TOKEN_IDENTIFIER:
    return "IDENTIFIER";
  case TOKEN_NUMBER:
    return "NUMBER";
  case TOKEN_STRING:
    return "STRING";
  case TOKEN_PLUS:
    return "PLUS";
  case TOKEN_MINUS:
    return "MINUS";
  case TOKEN_TIMES:
    return "TIMES";
  case TOKEN_DIVIDED_BY:
    return "DIVIDED_BY";
  case TOKEN_MODULO:
    return "MODULO";
  case TOKEN_POWER:
    return "POWER";
  case TOKEN_EQUALS:
    return "EQUALS";
  case TOKEN_NOT_EQUALS:
    return "NOT_EQUALS";
  case TOKEN_GREATER_THAN:
    return "GREATER_THAN";
  case TOKEN_LESS_THAN:
    return "LESS_THAN";
  case TOKEN_GREATER_OR_EQUALS:
    return "GREATER_OR_EQUALS";
  case TOKEN_LESS_OR_EQUALS:
    return "LESS_OR_EQUALS";
  case TOKEN_ASSIGN:
    return "ASSIGN";
  case TOKEN_ARROW:
    return "ARROW";
  case TOKEN_LPAREN:
    return "LPAREN";
  case TOKEN_RPAREN:
    return "RPAREN";
  case TOKEN_LBRACE:
    return "LBRACE";
  case TOKEN_RBRACE:
    return "RBRACE";
  case TOKEN_LBRACKET:
    return "LBRACKET";
  case TOKEN_RBRACKET:
    return "RBRACKET";
  case TOKEN_COMMA:
    return "COMMA";
  case TOKEN_DOT:
    return "DOT";
  case TOKEN_COLON:
    return "COLON";
  case TOKEN_NEWLINE:
    return "NEWLINE";
  case TOKEN_EOF:
    return "EOF";
  case TOKEN_ERROR:
    return "ERROR";
  default:
    return "UNKNOWN";
  }
}
