#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"

typedef enum {
  SYMBOL_GLOBAL,
  SYMBOL_LOCAL,
  SYMBOL_FUNCTION,
  SYMBOL_STRUCT,
  SYMBOL_CLASS,
  SYMBOL_C_FUNCTION
} SymbolType;

typedef struct Symbol {
  char *name;
  SymbolType type;
  int index;          // Local var index or global var offset
  int arity;          // For functions
  char *value_type;   // Variable type e.g "number"
  char *return_type;  // Func return type
  char **param_types; // Func param types
  struct Symbol *next;
} Symbol;

typedef struct SymbolTable {
  Symbol *symbols;
  int local_count;
  int is_function_scope;
  struct SymbolTable *outer;
} SymbolTable;

typedef struct SemanticAnalyzer {
  SymbolTable *current_scope;
  char **errors;
  int error_count;
  int in_class; // Track if we are inside a class context
  int is_freestanding;
} SemanticAnalyzer;

// Inicializa el analizador semántico (crea scope global)
void semantic_init(SemanticAnalyzer *analyzer, int is_freestanding);
void semantic_free(SemanticAnalyzer *analyzer);
int semantic_analyze(SemanticAnalyzer *analyzer, ASTNode *program);
void semantic_print_errors(SemanticAnalyzer *analyzer);
Symbol *resolve_symbol(SemanticAnalyzer *analyzer, const char *name);

#endif // SEMANTIC_H
