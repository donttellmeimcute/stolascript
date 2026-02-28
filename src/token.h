#ifndef TOKEN_H
#define TOKEN_H

typedef enum {
  // Keywords
  TOKEN_IF,
  TOKEN_ELSE,
  TOKEN_ELIF,
  TOKEN_WHILE,
  TOKEN_FOR,
  TOKEN_LOOP,
  TOKEN_FUNCTION,
  TOKEN_MATCH,
  TOKEN_CASE,
  TOKEN_DEFAULT,
  TOKEN_STRUCT,
  TOKEN_CLASS,
  TOKEN_THIS,
  TOKEN_NEW,
  TOKEN_TRY,
  TOKEN_CATCH,
  TOKEN_THROW,

  TOKEN_END,
  TOKEN_RETURN,
  TOKEN_IN,
  TOKEN_AND,
  TOKEN_OR,
  TOKEN_NOT,
  TOKEN_TRUE,
  TOKEN_FALSE,
  TOKEN_NULL,
  TOKEN_BREAK,
  TOKEN_CONTINUE,
  TOKEN_FROM,
  TOKEN_TO,
  TOKEN_STEP,
  TOKEN_IMPORT,
  TOKEN_AT,

  // Identifiers and Literals
  TOKEN_IDENTIFIER,
  TOKEN_NUMBER, // Handles ints and floats for simplicity during lexical
                // analysis
  TOKEN_STRING,
  TOKEN_IMPORT_NATIVE,
  TOKEN_C_FUNCTION,

  // Operators
  TOKEN_PLUS,
  TOKEN_MINUS,
  TOKEN_TIMES,
  TOKEN_DIVIDED_BY,
  TOKEN_MODULO,
  TOKEN_POWER,
  TOKEN_ARROW, // ->
  TOKEN_EQUALS,
  TOKEN_NOT_EQUALS,
  TOKEN_GREATER_THAN,
  TOKEN_LESS_THAN,
  TOKEN_GREATER_OR_EQUALS,
  TOKEN_LESS_OR_EQUALS,
  TOKEN_ASSIGN, // "="

  // Delimiters
  TOKEN_LPAREN,
  TOKEN_RPAREN, // ()
  TOKEN_LBRACE,
  TOKEN_RBRACE, // {}
  TOKEN_LBRACKET,
  TOKEN_RBRACKET, // []
  TOKEN_COMMA,
  TOKEN_DOT,
  TOKEN_COLON,

  // Special
  TOKEN_NEWLINE,
  TOKEN_EOF,
  TOKEN_ERROR
} TokenType;

typedef struct {
  TokenType type;
  char *literal; // Null-terminated string representing the token's text
  int line;      // Line number for error reporting
  int column;    // Column number
} Token;

#endif // TOKEN_H
