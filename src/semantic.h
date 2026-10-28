#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"
#include <stdint.h>

// ============================================================
// Static types used by the optimiser / code generator.
//
//  TY_NONE  — not yet known during inference (bottom of the lattice)
//  TY_BOOL  — raw machine 0/1
//  TY_INT   — raw machine int64 (C speed)
//  TY_STR   — native targets only: pointer to a NUL-terminated string
//  TY_DYN   — boxed StolaValue* handled by the runtime (Python-like)
// ============================================================
enum { TY_NONE = 0, TY_BOOL, TY_INT, TY_STR, TY_DYN };

typedef enum {
  TARGET_HOSTED,       // dynamic language + C runtime (default)
  TARGET_LINUX,        // native code, Linux syscalls, no libc, no C
  TARGET_KERNEL,       // native code, bootable multiboot x86-64 kernel
  TARGET_FREESTANDING  // native code, no entry glue (link it yourself)
} Target;

typedef enum {
  SYM_LOCAL,
  SYM_GLOBAL,
  SYM_FUNC,
  SYM_CLASS,
  SYM_STRUCT,
  SYM_CONST,
  SYM_STATIC,
  SYM_CFUNC
} SymKind;

struct FuncInfo;
struct ClassInfo;

typedef struct Var {
  char *name;
  SymKind kind;
  int ty;            // current (inferred or declared) type
  int declared;      // 1 if type is fixed by annotation / role
  int slot;          // frame offset (locals): [rbp - slot]
  const char *reg;   // allocated callee-saved register or NULL
  double weight;     // use frequency (for register allocation)
  int capture_index; // >=0 if this local is captured from outer scope
  int is_param;
  int addr_taken; // address taken with addr_of → never in a register
  // constants
  int const_is_str;
  int64_t const_int;
  ASTNode *const_str; // AST_STRING_LITERAL
  // statics
  int64_t static_size;
  int64_t static_align;
  struct FuncInfo *func; // for SYM_FUNC
  struct ClassInfo *cls; // for SYM_CLASS / SYM_STRUCT
  ASTNode *decl;
} Var;

typedef struct LoopInfo {
  int break_label;
  int continue_label;
  int try_depth;
} LoopInfo;

typedef struct FuncInfo {
  char *name;     // source name (method: Class.method)
  char *label;    // assembler label
  ASTNode *decl;  // AST_FUNCTION_DECL (NULL for program main)
  int is_main;
  int is_method;
  int is_lambda;
  int is_interrupt;
  int has_error_code; // interrupt with CPU error code
  struct ClassInfo *cls;
  struct FuncInfo *outer; // enclosing function (lambdas)

  int nparams;
  Var **params;  // params[i] (also present in locals)
  int ret_ty;
  int ret_declared;
  int escapes;       // used as a value / dynamically → params must be DYN
  int needs_wrapper; // typed function referenced as value
  int has_try;
  int has_asm;
  int falls_through; // may reach end of body without return

  Var **locals;
  int nlocals;
  Var **captures; // lambda captured vars (locals of this lambda)
  int ncaptures;
  char **global_names; // names declared `global`
  int nglobal_names;
  int hidden_counter;

  // codegen
  int frame_size;
  const char *used_regs[8];
  int nused_regs;
} FuncInfo;

typedef struct ClassInfo {
  char *name;
  char *parent_name;
  struct ClassInfo *parent;
  int is_struct;
  char **fields; // struct fields
  int nfields;
  FuncInfo **methods;
  int nmethods;
  ASTNode *decl;
} ClassInfo;

typedef struct {
  Target target;
  int native; // target != HOSTED
  int win64;  // Windows x64 ABI for hosted output
  const char *filename;

  Var **globals;
  int nglobals;
  FuncInfo **funcs; // all functions incl. methods, lambdas and main
  int nfuncs;
  ClassInfo **classes;
  int nclasses;
  FuncInfo *main_func;
  char **native_libs; // import_native "lib" (hosted)
  int nnative_libs;

  char **errors;
  int error_count;
  char **warnings;
  int warning_count;
} Program;

Program *semantic_analyze(ASTNode *program, Target target, int win64,
                          const char *filename);
void semantic_print_diagnostics(Program *prog);

// Lookup helpers shared with codegen
Var *sem_lookup(Program *prog, FuncInfo *fn, const char *name);
FuncInfo *sem_find_method(ClassInfo *cls, const char *name);
int sem_is_builtin(Program *prog, const char *name);

#endif // SEMANTIC_H
