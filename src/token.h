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
  TOKEN_EXTENDS,
  TOKEN_SUPER,
  TOKEN_THIS,
  TOKEN_NEW,
  TOKEN_TRY,
  TOKEN_CATCH,
  TOKEN_THROW,
  TOKEN_INTERRUPT,
  TOKEN_ASM,
  TOKEN_GLOBAL,
  TOKEN_CONST,
  TOKEN_STATIC,
  TOKEN_PASS,
  TOKEN_FN,

  TOKEN_END,
  TOKEN_RETURN,
  TOKEN_IN,
  TOKEN_AND,
  TOKEN_OR,
  TOKEN_NOT,
  TOKEN_NOT_IN,
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
  TOKEN_NUMBER, // integer literal (decimal, 0x, 0b, 0o)
  TOKEN_FLOAT,  // floating point literal
  TOKEN_STRING, // escapes already decoded
  TOKEN_FSTRING, // f"..." raw body (escapes decoded), {expr} parsed later
  TOKEN_IMPORT_NATIVE,
  TOKEN_C_FUNCTION,

  // Operators
  TOKEN_PLUS,
  TOKEN_MINUS,
  TOKEN_TIMES,
  TOKEN_DIVIDED_BY,
  TOKEN_MODULO,
  TOKEN_POWER,
  TOKEN_BIT_AND,
  TOKEN_BIT_OR,
  TOKEN_BIT_XOR,
  TOKEN_BIT_NOT,
  TOKEN_SHL,
  TOKEN_SHR,
  TOKEN_ARROW,     // ->
  TOKEN_FAT_ARROW, // =>
  TOKEN_EQUALS,
  TOKEN_NOT_EQUALS,
  TOKEN_GREATER_THAN,
  TOKEN_LESS_THAN,
  TOKEN_GREATER_OR_EQUALS,
  TOKEN_LESS_OR_EQUALS,
  TOKEN_ASSIGN,     // "="
  TOKEN_OP_ASSIGN,  // "+=", "-=", ... (literal holds the operator)

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
  int length;    // Byte length of literal (strings may contain NUL bytes)
  int line;      // Line number for error reporting
  int column;    // Column number
} Token;

#endif // TOKEN_H
