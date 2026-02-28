#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"

typedef enum {
  SYMBOL_GLOBAL,
  SYMBOL_LOCAL,
  SYMBOL_FUNCTION,
  SYMBOL_STRUCT
} SymbolType;

typedef struct Symbol {
  char *name;
  SymbolType type;
  int index; // Local var index or global var offset
  int arity; // For functions
  struct Symbol *next;
} Symbol;

typedef struct SymbolTable {
  Symbol *symbols;
  int local_count;
  int is_function_scope;
  struct SymbolTable *outer;
} SymbolTable;

typedef struct {
  SymbolTable *current_scope;
  char **errors;
  int error_count;
} SemanticAnalyzer;

void semantic_init(SemanticAnalyzer *analyzer);
void semantic_free(SemanticAnalyzer *analyzer);
int semantic_analyze(SemanticAnalyzer *analyzer, ASTNode *program);
void semantic_print_errors(SemanticAnalyzer *analyzer);

#endif // SEMANTIC_H
