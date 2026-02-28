#ifndef CODEGEN_H
#define CODEGEN_H

#include "ast.h"
#include "semantic.h"

void codegen_generate(ASTNode *program, SemanticAnalyzer *analyzer,
                      const char *output_file, int is_freestanding);

#endif // CODEGEN_H
