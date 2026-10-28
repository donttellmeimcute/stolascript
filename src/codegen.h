#ifndef CODEGEN_H
#define CODEGEN_H

#include "semantic.h"

// Generates x86-64 assembly (GNU as, Intel syntax) for the analysed program.
// Returns 0 on success.
int codegen_generate(Program *prog, const char *output_file);

#endif // CODEGEN_H
