#define _CRT_SECURE_NO_WARNINGS
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#endif
#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
// windows.h is avoided on purpose: it defines TokenType, which clashes
// with the lexer's token enum.
__declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(void *module,
                                                                 char *name,
                                                                 unsigned long size);
#define PATH_SEP '\\'
#define EXE_SUFFIX ".exe"
#else
#include <limits.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define PATH_SEP '/'
#define EXE_SUFFIX ""
#endif

#define STOLA_VERSION "2.0.0"

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wformat-truncation"
#endif

// ============================================================
// StolasScript compiler driver
//
//   s build program.stola [-o out] [--target hosted|linux|kernel]
//   s run   program.stola [args...]
//   s program.stola out.s            (legacy: emit assembly only)
// ============================================================

static char exe_dir[4096];

static char *read_source_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file)
    return NULL;
  fseek(file, 0L, SEEK_END);
  long file_size = ftell(file);
  rewind(file);
  char *buffer = (char *)malloc(file_size + 1);
  size_t bytes_read = fread(buffer, 1, file_size, file);
  buffer[bytes_read] = '\0';
  fclose(file);
  return buffer;
}

static int file_exists(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return 0;
  fclose(f);
  return 1;
}

static void init_exe_dir(const char *argv0) {
#ifdef _WIN32
  GetModuleFileNameA(NULL, exe_dir, (unsigned long)sizeof(exe_dir));
  (void)argv0;
#else
  ssize_t n = readlink("/proc/self/exe", exe_dir, sizeof(exe_dir) - 1);
  if (n > 0)
    exe_dir[n] = 0;
  else
    snprintf(exe_dir, sizeof(exe_dir), "%s", argv0);
#endif
  char *sep = strrchr(exe_dir, PATH_SEP);
  if (!sep)
    sep = strrchr(exe_dir, '/');
  if (sep)
    sep[1] = 0;
  else
    snprintf(exe_dir, sizeof(exe_dir), ".%c", PATH_SEP);
}

static char *dir_of(const char *path) {
  char *d = ast_strdup(path);
  char *s1 = strrchr(d, '/');
  char *s2 = strrchr(d, '\\');
  char *s = s1 > s2 ? s1 : s2;
  if (s)
    s[1] = 0;
  else
    d[0] = 0;
  return d;
}

static char *canonical(const char *path) {
#ifdef _WIN32
  char buf[4096];
  if (_fullpath(buf, path, sizeof(buf)))
    return ast_strdup(buf);
  return ast_strdup(path);
#else
  char *r = realpath(path, NULL);
  return r ? r : ast_strdup(path);
#endif
}

// ------------------------------------------------------------
// Parsing + imports
// ------------------------------------------------------------

typedef struct {
  char **paths;
  int count;
  ASTNode *merged; // AST_PROGRAM collecting statements in dependency order
  int errors;
} ImportCtx;

static int parse_file_into(ImportCtx *ctx, const char *path);

static char *resolve_module(const char *from_file, ASTNode *imp) {
  const char *name = imp->as.import_stmt.module_name;
  char *dir = dir_of(from_file);
  char cand[4096];
  const char *ext = strstr(name, ".stola") ? "" : ".stola";
  // 1. relative to the importing file
  snprintf(cand, sizeof(cand), "%s%s%s", dir, name, ext);
  free(dir);
  if (file_exists(cand))
    return ast_strdup(cand);
  if (imp->as.import_stmt.is_path && file_exists(name))
    return ast_strdup(name);
  // 2. STOLA_PATH
  const char *sp = getenv("STOLA_PATH");
  if (sp) {
    snprintf(cand, sizeof(cand), "%s%c%s%s", sp, PATH_SEP, name, ext);
    if (file_exists(cand))
      return ast_strdup(cand);
  }
  // 3. stdlib next to the compiler, 4. ./stdlib
  snprintf(cand, sizeof(cand), "%sstdlib%c%s%s", exe_dir, PATH_SEP, name, ext);
  if (file_exists(cand))
    return ast_strdup(cand);
  snprintf(cand, sizeof(cand), "stdlib%c%s%s", PATH_SEP, name, ext);
  if (file_exists(cand))
    return ast_strdup(cand);
  return NULL;
}

static int parse_file_into(ImportCtx *ctx, const char *path) {
  char *canon = canonical(path);
  for (int i = 0; i < ctx->count; i++)
    if (strcmp(ctx->paths[i], canon) == 0) {
      free(canon);
      return 1; // already imported
    }
  AST_PUSH(ctx->paths, ctx->count, canon);

  char *source = read_source_file(path);
  if (!source) {
    fprintf(stderr, "error: could not open file \"%s\"\n", path);
    ctx->errors++;
    return 0;
  }
  Lexer lexer;
  lexer_init(&lexer, source);
  Parser parser;
  char *fname = ast_strdup(path);
  parser_init(&parser, &lexer, fname);
  ASTNode *program = parser_parse_program(&parser);
  if (parser.error_count > 0) {
    parser_print_errors(&parser);
    ctx->errors += parser.error_count;
    parser_free(&parser);
    return 0;
  }
  parser_free(&parser);

  // dependencies first
  for (int i = 0; i < program->as.program.statement_count; i++) {
    ASTNode *s = program->as.program.statements[i];
    if (s->type != AST_IMPORT_STMT)
      continue;
    char *dep = resolve_module(path, s);
    if (!dep) {
      fprintf(stderr, "%s:%d: error: module '%s' not found\n", path, s->line,
              s->as.import_stmt.module_name);
      ctx->errors++;
      continue;
    }
    parse_file_into(ctx, dep);
    free(dep);
  }
  for (int i = 0; i < program->as.program.statement_count; i++) {
    ASTNode *s = program->as.program.statements[i];
    if (s->type == AST_IMPORT_STMT)
      continue;
    AST_PUSH(ctx->merged->as.program.statements,
             ctx->merged->as.program.statement_count, s);
  }
  return 1;
}

// ------------------------------------------------------------
// Toolchain invocation
// ------------------------------------------------------------

static int verbose = 0;

static int run_cmd(const char *cmd) {
  if (verbose)
    fprintf(stderr, "+ %s\n", cmd);
  int rc = system(cmd);
  return rc;
}

static const char *find_cc(void) {
  const char *cc = getenv("STOLA_CC");
  if (cc)
    return cc;
#ifdef _WIN32
  return "clang";
#else
  if (run_cmd("command -v gcc >/dev/null 2>&1") == 0)
    return "gcc";
  return "cc";
#endif
}

static const char *target_name(Target t) {
  switch (t) {
  case TARGET_HOSTED: return "hosted";
  case TARGET_LINUX: return "linux";
  case TARGET_KERNEL: return "kernel";
  case TARGET_FREESTANDING: return "freestanding";
  }
  return "?";
}

static int link_output(Target target, const char *asm_path, const char *out,
                       const char *tmpbase) {
  char cmd[16384];
  if (target == TARGET_HOSTED) {
    const char *cc = find_cc();
    char lib[4096], rt[4096], bi[4096];
    snprintf(lib, sizeof(lib), "%slib%clibstola.a", exe_dir, PATH_SEP);
    snprintf(rt, sizeof(rt), "%ssrc%cruntime.c", exe_dir, PATH_SEP);
    snprintf(bi, sizeof(bi), "%ssrc%cbuiltins.c", exe_dir, PATH_SEP);
#ifdef _WIN32
    const char *libs = "-lws2_32 -lwinhttp";
#else
    const char *libs = "-lm -lpthread -ldl";
#endif
    if (file_exists(lib))
      snprintf(cmd, sizeof(cmd), "%s -o \"%s\" \"%s\" \"%s\" %s", cc, out,
               asm_path, lib, libs);
    else if (file_exists(rt))
      snprintf(cmd, sizeof(cmd), "%s -O2 -o \"%s\" \"%s\" \"%s\" \"%s\" %s", cc,
               out, asm_path, rt, bi, libs);
    else {
      fprintf(stderr,
              "error: StolasScript runtime not found (looked for %s and %s)\n",
              lib, rt);
      return 1;
    }
    return run_cmd(cmd) == 0 ? 0 : 1;
  }
  char obj[4096];
  snprintf(obj, sizeof(obj), "%s.o", tmpbase);
  snprintf(cmd, sizeof(cmd), "as --64 -o \"%s\" \"%s\"", obj, asm_path);
  if (run_cmd(cmd) != 0)
    return 1;
  if (target == TARGET_LINUX) {
    snprintf(cmd, sizeof(cmd), "ld -static -nostdlib -o \"%s\" \"%s\"", out,
             obj);
    int rc = run_cmd(cmd);
    remove(obj);
    return rc == 0 ? 0 : 1;
  }
  if (target == TARGET_KERNEL) {
    char ld_script[4096];
    snprintf(ld_script, sizeof(ld_script), "%s.ld", tmpbase);
    FILE *f = fopen(ld_script, "w");
    if (!f)
      return 1;
    fprintf(f,
            "ENTRY(_start)\n"
            "SECTIONS {\n"
            "  . = 1M;\n"
            "  __stola_load_start = .;\n"
            "  .multiboot : { KEEP(*(.multiboot)) }\n"
            "  .boot : { *(.boot) }\n"
            "  .text : { *(.text*) }\n"
            "  .rodata : { *(.rodata*) }\n"
            "  .data : { *(.data*) }\n"
            "  __stola_load_end = .;\n"
            "  .bss (NOLOAD) : { __stola_bss_start = .; *(.bss*) *(COMMON) }\n"
            "  __stola_bss_end = .;\n"
            "  /DISCARD/ : { *(.note*) *(.comment) *(.eh_frame) }\n"
            "}\n");
    fclose(f);
    char elf[4096];
    snprintf(elf, sizeof(elf), "%s.elf", tmpbase);
    // newer binutils warn about the single RWX segment of a flat kernel
    const char *nowarn =
        run_cmd("ld --help 2>/dev/null | grep -q no-warn-rwx-segments") == 0
            ? "--no-warn-rwx-segments "
            : "";
    snprintf(cmd, sizeof(cmd),
             "ld -nostdlib %s-z max-page-size=0x1000 -T \"%s\" -o \"%s\" "
             "\"%s\"",
             nowarn, ld_script, elf, obj);
    int rc = run_cmd(cmd);
    if (rc == 0) {
      snprintf(cmd, sizeof(cmd), "objcopy -O binary \"%s\" \"%s\"", elf, out);
      rc = run_cmd(cmd);
    }
    remove(obj);
    remove(ld_script);
    if (!verbose)
      remove(elf);
    return rc == 0 ? 0 : 1;
  }
  // freestanding: object file only
  snprintf(cmd, sizeof(cmd), "cp \"%s\" \"%s\"", obj, out);
  int rc = run_cmd(cmd);
  remove(obj);
  return rc == 0 ? 0 : 1;
}

static void usage(void) {
  printf(
      "StolasScript %s — compiler\n\n"
      "Usage:\n"
      "  s build <file.stola> [-o output] [--target T]   compile to an executable\n"
      "  s run   <file.stola> [args...]                   compile and run (hosted)\n"
      "  s <file.stola> <output.s> [--target T]          emit assembly only\n\n"
      "Targets (--target):\n"
      "  hosted        dynamic language + runtime (default)\n"
      "  linux         native code, Linux syscalls, no libc, no C at all\n"
      "  kernel        bootable x86-64 multiboot kernel (run with qemu -kernel)\n"
      "  freestanding  native object file without entry glue\n\n"
      "Options:\n"
      "  -o <path>     output file\n"
      "  -S            emit assembly only\n"
      "  --keep-asm    keep the generated .s file\n"
      "  --freestanding   same as --target freestanding\n"
      "  -v            print toolchain commands\n"
      "  --version\n",
      STOLA_VERSION);
}

int main(int argc, char **argv) {
  init_exe_dir(argv[0]);
  if (argc < 2) {
    usage();
    return 1;
  }

  int mode_build = 0, mode_run = 0, emit_asm_only = 0, keep_asm = 0;
  Target target = TARGET_HOSTED;
  const char *input = NULL, *output = NULL;
  int argi = 1;
  int run_args_start = -1;

  if (strcmp(argv[1], "build") == 0) {
    mode_build = 1;
    argi = 2;
  } else if (strcmp(argv[1], "run") == 0) {
    mode_run = 1;
    argi = 2;
  } else if (strcmp(argv[1], "--version") == 0) {
    printf("StolasScript %s\n", STOLA_VERSION);
    return 0;
  } else if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
    usage();
    return 0;
  }

  for (int i = argi; i < argc; i++) {
    const char *a = argv[i];
    if (mode_run && input) { // remaining args go to the program
      run_args_start = i;
      break;
    }
    if (strcmp(a, "--freestanding") == 0)
      target = TARGET_FREESTANDING;
    else if (strcmp(a, "--target") == 0 && i + 1 < argc) {
      const char *t = argv[++i];
      if (strcmp(t, "hosted") == 0) target = TARGET_HOSTED;
      else if (strcmp(t, "linux") == 0) target = TARGET_LINUX;
      else if (strcmp(t, "kernel") == 0) target = TARGET_KERNEL;
      else if (strcmp(t, "freestanding") == 0) target = TARGET_FREESTANDING;
      else {
        fprintf(stderr, "error: unknown target '%s'\n", t);
        return 1;
      }
    } else if (strncmp(a, "--target=", 9) == 0) {
      const char *t = a + 9;
      if (strcmp(t, "hosted") == 0) target = TARGET_HOSTED;
      else if (strcmp(t, "linux") == 0) target = TARGET_LINUX;
      else if (strcmp(t, "kernel") == 0) target = TARGET_KERNEL;
      else if (strcmp(t, "freestanding") == 0) target = TARGET_FREESTANDING;
      else {
        fprintf(stderr, "error: unknown target '%s'\n", t);
        return 1;
      }
    } else if (strcmp(a, "-o") == 0 && i + 1 < argc)
      output = argv[++i];
    else if (strcmp(a, "-S") == 0)
      emit_asm_only = 1;
    else if (strcmp(a, "--keep-asm") == 0)
      keep_asm = 1;
    else if (strcmp(a, "-v") == 0)
      verbose = 1;
    else if (a[0] == '-' && a[1]) {
      fprintf(stderr, "error: unknown option '%s'\n", a);
      return 1;
    } else if (!input)
      input = a;
    else if (!output)
      output = a;
    else {
      fprintf(stderr, "error: unexpected argument '%s'\n", a);
      return 1;
    }
  }
  if (!input) {
    usage();
    return 1;
  }
  // Legacy mode: `s in.stola out.s` emits assembly only
  if (!mode_build && !mode_run)
    emit_asm_only = 1;
  if (mode_run && target != TARGET_HOSTED && target != TARGET_LINUX) {
    fprintf(stderr, "error: 'run' supports the hosted and linux targets\n");
    return 1;
  }

  // Parse program + imports
  ImportCtx ctx = {0};
  ctx.merged = ast_new(AST_PROGRAM, 1);
  if (target == TARGET_LINUX) {
    // the linux target gets its tiny console/syscall library automatically
    ASTNode imp = {0};
    imp.type = AST_IMPORT_STMT;
    imp.as.import_stmt.module_name = "linux";
    char *dep = resolve_module(input, &imp);
    if (dep) {
      parse_file_into(&ctx, dep);
      free(dep);
    }
  }
  parse_file_into(&ctx, input);
  if (ctx.errors) {
    fprintf(stderr, "compilation failed (%d error%s)\n", ctx.errors,
            ctx.errors == 1 ? "" : "s");
    return 1;
  }

#ifdef _WIN32
  int win64 = target == TARGET_HOSTED;
#else
  int win64 = 0;
#endif
  Program *prog = semantic_analyze(ctx.merged, target, win64, input);
  semantic_print_diagnostics(prog);
  if (prog->error_count) {
    fprintf(stderr, "compilation failed (%d error%s)\n", prog->error_count,
            prog->error_count == 1 ? "" : "s");
    return 1;
  }

  // Output names
  char base[4096];
  snprintf(base, sizeof(base), "%s", input);
  char *dot = strrchr(base, '.');
  if (dot && strcmp(dot, ".stola") == 0)
    *dot = 0;
  char asm_path[4096];
  char out_path[4096];
  if (emit_asm_only) {
    if (output)
      snprintf(asm_path, sizeof(asm_path), "%s", output);
    else
      snprintf(asm_path, sizeof(asm_path), "%s.s", base);
  } else {
    if (output)
      snprintf(out_path, sizeof(out_path), "%s", output);
    else if (target == TARGET_KERNEL)
      snprintf(out_path, sizeof(out_path), "%s.bin", base);
    else if (target == TARGET_FREESTANDING)
      snprintf(out_path, sizeof(out_path), "%s.o", base);
    else
      snprintf(out_path, sizeof(out_path), "%s%s", base, EXE_SUFFIX);
    snprintf(asm_path, sizeof(asm_path), "%s.s", out_path);
  }

  if (codegen_generate(prog, asm_path) != 0) {
    fprintf(stderr, "compilation failed during code generation\n");
    return 1;
  }
  if (emit_asm_only)
    return 0;

  if (link_output(target, asm_path, out_path, out_path) != 0) {
    fprintf(stderr, "error: assembling/linking failed (target %s)\n",
            target_name(target));
    return 1;
  }
  if (!keep_asm)
    remove(asm_path);

  if (mode_run) {
    char cmd[16384];
    int n = 0;
#ifdef _WIN32
    n += snprintf(cmd + n, sizeof(cmd) - n, "\"%s\"", out_path);
#else
    if (strchr(out_path, '/'))
      n += snprintf(cmd + n, sizeof(cmd) - n, "\"%s\"", out_path);
    else
      n += snprintf(cmd + n, sizeof(cmd) - n, "./\"%s\"", out_path);
#endif
    for (int i = run_args_start; run_args_start > 0 && i < argc; i++)
      n += snprintf(cmd + n, sizeof(cmd) - n, " \"%s\"", argv[i]);
    int rc = system(cmd);
    remove(out_path);
#ifndef _WIN32
    if (rc != -1 && WIFEXITED(rc))
      return WEXITSTATUS(rc);
#endif
    return rc == 0 ? 0 : 1;
  }
  return 0;
}
