#include "lexer.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================
// StolasScript lexer
//  - Word operators ("plus", "less than", ...) and symbolic ones (+, <, ...)
//  - Comments: //, /* */ and #
//  - Strings: "..." '...' """...""" with escapes, f"..." interpolation
//  - Numbers: 123, 1_000, 0xFF, 0b1010, 0o17, 3.14, 1e9
//  - ';' behaves like a newline (several statements on one line)
// ============================================================

void lexer_init(Lexer *lexer, const char *source) {
  lexer->source = source;
  lexer->position = 0;
  lexer->read_position = 0;
  lexer->line = 1;
  lexer->column = 0;

  if (lexer->source && lexer->source[0] != '\0') {
    lexer->ch = lexer->source[0];
    lexer->read_position = 1;
    lexer->column = 1;
  } else {
    lexer->ch = '\0';
  }
}

static void lexer_read_char(Lexer *lexer) {
  lexer->ch = lexer->source[lexer->read_position];
  lexer->position = lexer->read_position;
  if (lexer->ch != '\0')
    lexer->read_position++;
  lexer->column++;
}

static char lexer_peek_char(Lexer *lexer) {
  if (lexer->ch == '\0')
    return '\0';
  return lexer->source[lexer->read_position];
}

static char lexer_peek_char2(Lexer *lexer) {
  if (lexer_peek_char(lexer) == '\0')
    return '\0';
  return lexer->source[lexer->read_position + 1];
}

static void lexer_skip_whitespace(Lexer *lexer) {
  while (lexer->ch == ' ' || lexer->ch == '\t' || lexer->ch == '\r') {
    lexer_read_char(lexer);
  }
  // Line continuation: backslash followed by newline
  if (lexer->ch == '\\' && (lexer_peek_char(lexer) == '\n' ||
                            (lexer_peek_char(lexer) == '\r' &&
                             lexer_peek_char2(lexer) == '\n'))) {
    lexer_read_char(lexer);
    if (lexer->ch == '\r')
      lexer_read_char(lexer);
    lexer_read_char(lexer);
    lexer->line++;
    lexer->column = 1;
    lexer_skip_whitespace(lexer);
  }
}

static Token *create_token(TokenType type, const char *literal, int len,
                           int line, int column) {
  Token *token = malloc(sizeof(Token));
  token->type = type;
  token->line = line;
  token->column = column;
  token->length = len;

  if (literal) {
    token->literal = malloc(len + 1);
    memcpy(token->literal, literal, len);
    token->literal[len] = '\0';
  } else {
    token->literal = NULL;
    token->length = 0;
  }

  return token;
}

static Token *create_char_token(TokenType type, Lexer *lexer) {
  char str[2] = {lexer->ch, '\0'};
  Token *tok = create_token(type, str, 1, lexer->line, lexer->column);
  lexer_read_char(lexer);
  return tok;
}

// Peek the identifier-like word that follows the current position (after
// spaces/tabs) without consuming anything. Returns its length, 0 if none.
static int peek_word_at(const char *src, int pos, char *buf, int bufsz,
                        int *end_pos) {
  while (src[pos] == ' ' || src[pos] == '\t')
    pos++;
  int start = pos;
  while (isalpha((unsigned char)src[pos]) || src[pos] == '_')
    pos++;
  int len = pos - start;
  if (len <= 0 || len >= bufsz)
    return 0;
  if (isdigit((unsigned char)src[pos]))
    return 0; // part of a longer identifier
  memcpy(buf, src + start, len);
  buf[len] = '\0';
  *end_pos = pos;
  return len;
}

// Move the lexer so that `position` == pos (pos must be ahead).
static void lexer_seek(Lexer *lexer, int pos) {
  while (lexer->position < pos && lexer->ch != '\0')
    lexer_read_char(lexer);
}

typedef struct {
  const char *word;
  TokenType type;
} Keyword;

static const Keyword keywords[] = {
    {"if", TOKEN_IF},
    {"else", TOKEN_ELSE},
    {"elif", TOKEN_ELIF},
    {"while", TOKEN_WHILE},
    {"for", TOKEN_FOR},
    {"loop", TOKEN_LOOP},
    {"function", TOKEN_FUNCTION},
    {"def", TOKEN_FUNCTION},
    {"match", TOKEN_MATCH},
    {"case", TOKEN_CASE},
    {"default", TOKEN_DEFAULT},
    {"struct", TOKEN_STRUCT},
    {"class", TOKEN_CLASS},
    {"extends", TOKEN_EXTENDS},
    {"super", TOKEN_SUPER},
    {"this", TOKEN_THIS},
    {"self", TOKEN_THIS},
    {"new", TOKEN_NEW},
    {"try", TOKEN_TRY},
    {"catch", TOKEN_CATCH},
    {"throw", TOKEN_THROW},
    {"raise", TOKEN_THROW},
    {"import_native", TOKEN_IMPORT_NATIVE},
    {"c_function", TOKEN_C_FUNCTION},
    {"end", TOKEN_END},
    {"return", TOKEN_RETURN},
    {"in", TOKEN_IN},
    {"and", TOKEN_AND},
    {"or", TOKEN_OR},
    {"not", TOKEN_NOT},
    {"true", TOKEN_TRUE},
    {"True", TOKEN_TRUE},
    {"false", TOKEN_FALSE},
    {"False", TOKEN_FALSE},
    {"null", TOKEN_NULL},
    {"None", TOKEN_NULL},
    {"break", TOKEN_BREAK},
    {"continue", TOKEN_CONTINUE},
    {"from", TOKEN_FROM},
    {"to", TOKEN_TO},
    {"step", TOKEN_STEP},
    {"import", TOKEN_IMPORT},
    {"at", TOKEN_AT},
    {"interrupt", TOKEN_INTERRUPT},
    {"asm", TOKEN_ASM},
    {"global", TOKEN_GLOBAL},
    {"const", TOKEN_CONST},
    {"static", TOKEN_STATIC},
    {"pass", TOKEN_PASS},
    // Single-word operators
    {"plus", TOKEN_PLUS},
    {"minus", TOKEN_MINUS},
    {"times", TOKEN_TIMES},
    {"modulo", TOKEN_MODULO},
    {"power", TOKEN_POWER},
    {"equals", TOKEN_EQUALS},
    {"is_greater_than", TOKEN_GREATER_THAN},
    {"is_less_than", TOKEN_LESS_THAN},
    {"is_equal_to", TOKEN_EQUALS},
    {NULL, TOKEN_ERROR}};

// Two/three word operators: first word, second word, [third], token
typedef struct {
  const char *w1, *w2, *w3;
  TokenType type;
  const char *text;
} MultiWordOp;

static const MultiWordOp multi_ops[] = {
    {"greater", "or", "equals", TOKEN_GREATER_OR_EQUALS, ">="},
    {"less", "or", "equals", TOKEN_LESS_OR_EQUALS, "<="},
    {"less", "than", NULL, TOKEN_LESS_THAN, "<"},
    {"greater", "than", NULL, TOKEN_GREATER_THAN, ">"},
    {"divided", "by", NULL, TOKEN_DIVIDED_BY, "/"},
    {"not", "equals", NULL, TOKEN_NOT_EQUALS, "!="},
    {"not", "in", NULL, TOKEN_NOT_IN, "not in"},
    {NULL, NULL, NULL, TOKEN_ERROR, NULL}};

static Token *read_asm_block(Lexer *lexer, int col) {
  int line = lexer->line;
  // skip whitespace and newlines before '{'
  while (lexer->ch == ' ' || lexer->ch == '\t' || lexer->ch == '\r' ||
         lexer->ch == '\n') {
    if (lexer->ch == '\n') {
      lexer->line++;
      lexer->column = 0;
    }
    lexer_read_char(lexer);
  }
  if (lexer->ch != '{') {
    const char *msg = "Expected '{' after 'asm'";
    return create_token(TOKEN_ERROR, msg, (int)strlen(msg), lexer->line, col);
  }
  lexer_read_char(lexer); // consume '{'

  int asm_start = lexer->position;
  int depth = 1;
  while (lexer->ch != '\0') {
    if (lexer->ch == '{')
      depth++;
    else if (lexer->ch == '}') {
      depth--;
      if (depth == 0)
        break;
    }
    if (lexer->ch == '\n') {
      lexer->line++;
      lexer->column = 0;
    }
    lexer_read_char(lexer);
  }
  int asm_len = lexer->position - asm_start;
  Token *t = create_token(TOKEN_ASM, lexer->source + asm_start, asm_len, line,
                          col);
  if (lexer->ch == '}')
    lexer_read_char(lexer);
  return t;
}

static Token *read_identifier(Lexer *lexer) {
  int start_pos = lexer->position;
  int col = lexer->column;
  int line = lexer->line;

  while (isalnum((unsigned char)lexer->ch) || lexer->ch == '_' ||
         (unsigned char)lexer->ch >= 0x80) {
    lexer_read_char(lexer);
  }

  int len = lexer->position - start_pos;
  char literal[256];
  if (len > 255)
    len = 255;
  memcpy(literal, lexer->source + start_pos, len);
  literal[len] = '\0';

  // Multi-word operators ("less than", "greater or equals", ...)
  for (int i = 0; multi_ops[i].w1; i++) {
    if (strcmp(literal, multi_ops[i].w1) != 0)
      continue;
    char w2[32];
    int end2;
    if (!peek_word_at(lexer->source, lexer->position, w2, sizeof(w2), &end2))
      continue;
    if (strcmp(w2, multi_ops[i].w2) != 0)
      continue;
    if (multi_ops[i].w3) {
      char w3[32];
      int end3;
      if (!peek_word_at(lexer->source, end2, w3, sizeof(w3), &end3))
        continue;
      if (strcmp(w3, multi_ops[i].w3) != 0)
        continue;
      lexer_seek(lexer, end3);
    } else {
      lexer_seek(lexer, end2);
    }
    return create_token(multi_ops[i].type, multi_ops[i].text,
                        (int)strlen(multi_ops[i].text), line, col);
  }

  TokenType type = TOKEN_IDENTIFIER;
  for (int i = 0; keywords[i].word; i++) {
    if (strcmp(literal, keywords[i].word) == 0) {
      type = keywords[i].type;
      break;
    }
  }

  if (type == TOKEN_ASM)
    return read_asm_block(lexer, col);

  return create_token(type, literal, len, line, col);
}

static Token *read_number(Lexer *lexer) {
  int col = lexer->column;
  char buf[128];
  int n = 0;
  int is_float = 0;

  if (lexer->ch == '0' &&
      (lexer_peek_char(lexer) == 'x' || lexer_peek_char(lexer) == 'X' ||
       lexer_peek_char(lexer) == 'b' || lexer_peek_char(lexer) == 'B' ||
       lexer_peek_char(lexer) == 'o' || lexer_peek_char(lexer) == 'O')) {
    char kind = (char)tolower((unsigned char)lexer_peek_char(lexer));
    lexer_read_char(lexer);
    lexer_read_char(lexer);
    int base = kind == 'x' ? 16 : (kind == 'b' ? 2 : 8);
    unsigned long long v = 0;
    while (isxdigit((unsigned char)lexer->ch) || lexer->ch == '_') {
      if (lexer->ch != '_') {
        int d = isdigit((unsigned char)lexer->ch)
                    ? lexer->ch - '0'
                    : tolower((unsigned char)lexer->ch) - 'a' + 10;
        if (d >= base)
          break;
        v = v * base + d;
      }
      lexer_read_char(lexer);
    }
    n = snprintf(buf, sizeof(buf), "%lld", (long long)v);
    return create_token(TOKEN_NUMBER, buf, n, lexer->line, col);
  }

  while (n < 120) {
    if (isdigit((unsigned char)lexer->ch)) {
      buf[n++] = lexer->ch;
    } else if (lexer->ch == '_') {
      // digit separator
    } else if (lexer->ch == '.' && !is_float &&
               isdigit((unsigned char)lexer_peek_char(lexer))) {
      is_float = 1;
      buf[n++] = '.';
    } else if ((lexer->ch == 'e' || lexer->ch == 'E') &&
               (isdigit((unsigned char)lexer_peek_char(lexer)) ||
                ((lexer_peek_char(lexer) == '-' ||
                  lexer_peek_char(lexer) == '+') &&
                 isdigit((unsigned char)lexer_peek_char2(lexer))))) {
      is_float = 1;
      buf[n++] = 'e';
      lexer_read_char(lexer);
      buf[n++] = lexer->ch;
    } else {
      break;
    }
    lexer_read_char(lexer);
  }
  buf[n] = '\0';
  return create_token(is_float ? TOKEN_FLOAT : TOKEN_NUMBER, buf, n,
                      lexer->line, col);
}

static int hexval(char c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  c = (char)tolower((unsigned char)c);
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return -1;
}

// Reads "...", '...', or """...""" (triple) strings with escape sequences.
// Inside f-strings, "{{" and "}}" are kept verbatim for the parser.
static Token *read_string(Lexer *lexer, char quote_char, int is_fstring) {
  int col = lexer->column;
  int line = lexer->line;
  int triple = 0;
  if (lexer_peek_char(lexer) == quote_char &&
      lexer_peek_char2(lexer) == quote_char) {
    triple = 1;
    lexer_read_char(lexer);
    lexer_read_char(lexer);
  }
  lexer_read_char(lexer); // skip opening quote

  size_t cap = 64, len = 0;
  char *buf = malloc(cap);

  for (;;) {
    if (lexer->ch == '\0')
      break;
    if (!triple && lexer->ch == quote_char)
      break;
    if (triple && lexer->ch == quote_char &&
        lexer_peek_char(lexer) == quote_char &&
        lexer_peek_char2(lexer) == quote_char)
      break;
    if (!triple && lexer->ch == '\n')
      break; // unterminated

    char c = lexer->ch;
    if (c == '\n') {
      lexer->line++;
      lexer->column = 0;
    }
    if (c == '\\') {
      lexer_read_char(lexer);
      switch (lexer->ch) {
      case 'n': c = '\n'; break;
      case 't': c = '\t'; break;
      case 'r': c = '\r'; break;
      case '0': c = '\0'; break;
      case 'a': c = '\a'; break;
      case 'b': c = '\b'; break;
      case 'e': c = 27; break;
      case '\\': c = '\\'; break;
      case '\'': c = '\''; break;
      case '"': c = '"'; break;
      case '{':
      case '}':
        if (is_fstring) {
          // escaped brace inside an f-string: emit doubled brace
          if (len + 2 >= cap) {
            cap *= 2;
            buf = realloc(buf, cap);
          }
          buf[len++] = lexer->ch;
        }
        c = lexer->ch;
        break;
      case 'x': {
        int h1 = hexval(lexer_peek_char(lexer));
        int h2 = h1 >= 0 ? hexval(lexer_peek_char2(lexer)) : -1;
        if (h1 >= 0 && h2 >= 0) {
          lexer_read_char(lexer);
          lexer_read_char(lexer);
          c = (char)(h1 * 16 + h2);
        } else {
          c = 'x';
        }
        break;
      }
      case '\n':
        lexer->line++;
        lexer->column = 0;
        lexer_read_char(lexer);
        continue; // line continuation inside string
      default:
        c = lexer->ch;
        break;
      }
    }
    if (len + 2 >= cap) {
      cap *= 2;
      buf = realloc(buf, cap);
    }
    buf[len++] = c;
    lexer_read_char(lexer);
  }

  Token *tok = create_token(is_fstring ? TOKEN_FSTRING : TOKEN_STRING, buf,
                            (int)len, line, col);
  free(buf);

  if (lexer->ch == quote_char) {
    lexer_read_char(lexer);
    if (triple) {
      lexer_read_char(lexer);
      lexer_read_char(lexer);
    }
  }
  return tok;
}

static int skip_comment(Lexer *lexer) {
  if (lexer->ch == '#' ||
      (lexer->ch == '/' && lexer_peek_char(lexer) == '/')) {
    while (lexer->ch != '\n' && lexer->ch != '\0') {
      lexer_read_char(lexer);
    }
    return 1;
  }
  if (lexer->ch == '/' && lexer_peek_char(lexer) == '*') {
    lexer_read_char(lexer);
    lexer_read_char(lexer);
    while (lexer->ch != '\0') {
      if (lexer->ch == '*' && lexer_peek_char(lexer) == '/') {
        lexer_read_char(lexer);
        lexer_read_char(lexer);
        break;
      }
      if (lexer->ch == '\n') {
        lexer->line++;
        lexer->column = 0;
      }
      lexer_read_char(lexer);
    }
    return 1;
  }
  return 0;
}

static Token *two_char(Lexer *lexer, TokenType type, const char *text,
                       int col) {
  lexer_read_char(lexer);
  lexer_read_char(lexer);
  return create_token(type, text, (int)strlen(text), lexer->line, col);
}

static Token *op_assign(Lexer *lexer, const char *op, int consumed, int col) {
  // consumes `consumed` chars of operator plus '='
  for (int i = 0; i <= consumed; i++)
    lexer_read_char(lexer);
  return create_token(TOKEN_OP_ASSIGN, op, (int)strlen(op), lexer->line, col);
}

Token *lexer_next_token(Lexer *lexer) {
  lexer_skip_whitespace(lexer);
  while (skip_comment(lexer))
    lexer_skip_whitespace(lexer);

  int col = lexer->column;
  char p = lexer_peek_char(lexer);

  switch (lexer->ch) {
  case '\n': {
    Token *tok = create_token(TOKEN_NEWLINE, "\n", 1, lexer->line, col);
    lexer->line++;
    lexer->column = 0;
    lexer_read_char(lexer);
    return tok;
  }
  case ';':
    return create_char_token(TOKEN_NEWLINE, lexer);
  case '=':
    if (p == '=')
      return two_char(lexer, TOKEN_EQUALS, "==", col);
    if (p == '>')
      return two_char(lexer, TOKEN_FAT_ARROW, "=>", col);
    return create_char_token(TOKEN_ASSIGN, lexer);
  case '+':
    if (p == '=')
      return op_assign(lexer, "+", 1, col);
    return create_char_token(TOKEN_PLUS, lexer);
  case '-':
    if (p == '>')
      return two_char(lexer, TOKEN_ARROW, "->", col);
    if (p == '=')
      return op_assign(lexer, "-", 1, col);
    return create_char_token(TOKEN_MINUS, lexer);
  case '*':
    if (p == '*') {
      if (lexer_peek_char2(lexer) == '=')
        return op_assign(lexer, "**", 2, col);
      return two_char(lexer, TOKEN_POWER, "**", col);
    }
    if (p == '=')
      return op_assign(lexer, "*", 1, col);
    return create_char_token(TOKEN_TIMES, lexer);
  case '/':
    if (p == '=')
      return op_assign(lexer, "/", 1, col);
    return create_char_token(TOKEN_DIVIDED_BY, lexer);
  case '%':
    if (p == '=')
      return op_assign(lexer, "%", 1, col);
    return create_char_token(TOKEN_MODULO, lexer);
  case '&':
    if (p == '&')
      return two_char(lexer, TOKEN_AND, "and", col);
    if (p == '=')
      return op_assign(lexer, "&", 1, col);
    return create_char_token(TOKEN_BIT_AND, lexer);
  case '|':
    if (p == '|')
      return two_char(lexer, TOKEN_OR, "or", col);
    if (p == '=')
      return op_assign(lexer, "|", 1, col);
    return create_char_token(TOKEN_BIT_OR, lexer);
  case '^':
    if (p == '=')
      return op_assign(lexer, "^", 1, col);
    return create_char_token(TOKEN_BIT_XOR, lexer);
  case '~':
    return create_char_token(TOKEN_BIT_NOT, lexer);
  case '<':
    if (p == '<') {
      if (lexer_peek_char2(lexer) == '=')
        return op_assign(lexer, "<<", 2, col);
      return two_char(lexer, TOKEN_SHL, "<<", col);
    }
    if (p == '=')
      return two_char(lexer, TOKEN_LESS_OR_EQUALS, "<=", col);
    return create_char_token(TOKEN_LESS_THAN, lexer);
  case '>':
    if (p == '>') {
      if (lexer_peek_char2(lexer) == '=')
        return op_assign(lexer, ">>", 2, col);
      return two_char(lexer, TOKEN_SHR, ">>", col);
    }
    if (p == '=')
      return two_char(lexer, TOKEN_GREATER_OR_EQUALS, ">=", col);
    return create_char_token(TOKEN_GREATER_THAN, lexer);
  case '!':
    if (p == '=')
      return two_char(lexer, TOKEN_NOT_EQUALS, "!=", col);
    return create_char_token(TOKEN_NOT, lexer);
  case '(':
    return create_char_token(TOKEN_LPAREN, lexer);
  case ')':
    return create_char_token(TOKEN_RPAREN, lexer);
  case '{':
    return create_char_token(TOKEN_LBRACE, lexer);
  case '}':
    return create_char_token(TOKEN_RBRACE, lexer);
  case '[':
    return create_char_token(TOKEN_LBRACKET, lexer);
  case ']':
    return create_char_token(TOKEN_RBRACKET, lexer);
  case ',':
    return create_char_token(TOKEN_COMMA, lexer);
  case '.':
    if (isdigit((unsigned char)p))
      return read_number(lexer);
    return create_char_token(TOKEN_DOT, lexer);
  case ':':
    return create_char_token(TOKEN_COLON, lexer);
  case '"':
  case '\'':
    return read_string(lexer, lexer->ch, 0);
  case '\0':
    return create_token(TOKEN_EOF, "", 0, lexer->line, col);
  default:
    if ((lexer->ch == 'f' || lexer->ch == 'F') && (p == '"' || p == '\'')) {
      lexer_read_char(lexer);
      return read_string(lexer, lexer->ch, 1);
    }
    if (isalpha((unsigned char)lexer->ch) || lexer->ch == '_' ||
        (unsigned char)lexer->ch >= 0x80)
      return read_identifier(lexer);
    if (isdigit((unsigned char)lexer->ch))
      return read_number(lexer);
    return create_char_token(TOKEN_ERROR, lexer);
  }
}

void token_free(Token *token) {
  if (token) {
    free(token->literal);
    free(token);
  }
}

const char *token_type_to_string(TokenType type) {
  switch (type) {
  case TOKEN_IF: return "'if'";
  case TOKEN_ELSE: return "'else'";
  case TOKEN_ELIF: return "'elif'";
  case TOKEN_WHILE: return "'while'";
  case TOKEN_FOR: return "'for'";
  case TOKEN_LOOP: return "'loop'";
  case TOKEN_FUNCTION: return "'function'";
  case TOKEN_FN: return "'fn'";
  case TOKEN_MATCH: return "'match'";
  case TOKEN_CASE: return "'case'";
  case TOKEN_DEFAULT: return "'default'";
  case TOKEN_STRUCT: return "'struct'";
  case TOKEN_CLASS: return "'class'";
  case TOKEN_EXTENDS: return "'extends'";
  case TOKEN_SUPER: return "'super'";
  case TOKEN_THIS: return "'this'";
  case TOKEN_NEW: return "'new'";
  case TOKEN_TRY: return "'try'";
  case TOKEN_CATCH: return "'catch'";
  case TOKEN_THROW: return "'throw'";
  case TOKEN_INTERRUPT: return "'interrupt'";
  case TOKEN_ASM: return "'asm'";
  case TOKEN_GLOBAL: return "'global'";
  case TOKEN_CONST: return "'const'";
  case TOKEN_STATIC: return "'static'";
  case TOKEN_PASS: return "'pass'";
  case TOKEN_IMPORT_NATIVE: return "'import_native'";
  case TOKEN_C_FUNCTION: return "'c_function'";
  case TOKEN_END: return "'end'";
  case TOKEN_RETURN: return "'return'";
  case TOKEN_IN: return "'in'";
  case TOKEN_AND: return "'and'";
  case TOKEN_OR: return "'or'";
  case TOKEN_NOT: return "'not'";
  case TOKEN_NOT_IN: return "'not in'";
  case TOKEN_TRUE: return "'true'";
  case TOKEN_FALSE: return "'false'";
  case TOKEN_NULL: return "'null'";
  case TOKEN_BREAK: return "'break'";
  case TOKEN_CONTINUE: return "'continue'";
  case TOKEN_FROM: return "'from'";
  case TOKEN_TO: return "'to'";
  case TOKEN_STEP: return "'step'";
  case TOKEN_IMPORT: return "'import'";
  case TOKEN_AT: return "'at'";
  case TOKEN_IDENTIFIER: return "identifier";
  case TOKEN_NUMBER: return "number";
  case TOKEN_FLOAT: return "float";
  case TOKEN_STRING: return "string";
  case TOKEN_FSTRING: return "f-string";
  case TOKEN_PLUS: return "'+'";
  case TOKEN_MINUS: return "'-'";
  case TOKEN_TIMES: return "'*'";
  case TOKEN_DIVIDED_BY: return "'/'";
  case TOKEN_MODULO: return "'%'";
  case TOKEN_POWER: return "'**'";
  case TOKEN_BIT_AND: return "'&'";
  case TOKEN_BIT_OR: return "'|'";
  case TOKEN_BIT_XOR: return "'^'";
  case TOKEN_BIT_NOT: return "'~'";
  case TOKEN_SHL: return "'<<'";
  case TOKEN_SHR: return "'>>'";
  case TOKEN_EQUALS: return "'=='";
  case TOKEN_NOT_EQUALS: return "'!='";
  case TOKEN_GREATER_THAN: return "'>'";
  case TOKEN_LESS_THAN: return "'<'";
  case TOKEN_GREATER_OR_EQUALS: return "'>='";
  case TOKEN_LESS_OR_EQUALS: return "'<='";
  case TOKEN_ASSIGN: return "'='";
  case TOKEN_OP_ASSIGN: return "compound assignment";
  case TOKEN_ARROW: return "'->'";
  case TOKEN_FAT_ARROW: return "'=>'";
  case TOKEN_LPAREN: return "'('";
  case TOKEN_RPAREN: return "')'";
  case TOKEN_LBRACE: return "'{'";
  case TOKEN_RBRACE: return "'}'";
  case TOKEN_LBRACKET: return "'['";
  case TOKEN_RBRACKET: return "']'";
  case TOKEN_COMMA: return "','";
  case TOKEN_DOT: return "'.'";
  case TOKEN_COLON: return "':'";
  case TOKEN_NEWLINE: return "newline";
  case TOKEN_EOF: return "end of file";
  case TOKEN_ERROR: return "invalid character";
  default: return "unknown";
  }
}
