#ifndef BUILTIN_TABLE_H
#define BUILTIN_TABLE_H

// ============================================================
// Builtin functions known to the compiler.
//
// Hosted builtins are C functions in the runtime taking StolaValue*
// arguments (unless ARG_INT bits say the argument is a raw int64).
// Native builtins (load8, outb, syscall...) are expanded inline.
// ============================================================

typedef enum { RK_DYN, RK_VOID, RK_INT, RK_BOOL } RetKind;

typedef struct {
  const char *name;
  const char *cname;  // runtime symbol (hosted) or NULL (inline native)
  int min_args;
  int max_args;       // -1 = unlimited
  int variadic;       // 1: called as cname(argc, argv_reversed)
  RetKind ret;
  int native;         // 1: available in native targets (inline expansion)
  int hosted;         // 1: available in hosted target
} BuiltinInfo;

const BuiltinInfo *builtin_lookup(const char *name, int native);

#endif // BUILTIN_TABLE_H
