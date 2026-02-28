#include "codegen.h"
#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define PATH_SEP '\\'
#else
#define PATH_SEP '/'
#endif

char *read_source_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (!file) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    return NULL;
  }

  fseek(file, 0L, SEEK_END);
  size_t file_size = ftell(file);
  rewind(file);

  char *buffer = (char *)malloc(file_size + 1);
  if (!buffer) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    fclose(file);
    return NULL;
  }

  size_t bytes_read = fread(buffer, sizeof(char), file_size, file);
  buffer[bytes_read] = '\0';
  fclose(file);
  return buffer;
}

// Find the directory of the compiler executable
static void get_exe_dir(char *buf, size_t buf_size) {
#ifdef _WIN32
  extern unsigned long __stdcall GetModuleFileNameA(void *, char *,
                                                    unsigned long);
  GetModuleFileNameA(NULL, buf, (unsigned long)buf_size);
  // Strip filename to get directory
  char *last_sep = strrchr(buf, '\\');
  if (last_sep)
    *(last_sep + 1) = '\0';
#else
  // Fallback: just use "./"
  strncpy(buf, "./", buf_size);
#endif
}

// Build stdlib path: <exe_dir>/stdlib/<module>.stola
static char *build_stdlib_path(const char *module_name) {
  char exe_dir[512];
  get_exe_dir(exe_dir, sizeof(exe_dir));

  size_t len = strlen(exe_dir) + strlen("stdlib/") + strlen(module_name) +
               strlen(".stola") + 2;
  char *path = (char *)malloc(len);
  snprintf(path, len, "%sstdlib%c%s.stola", exe_dir, PATH_SEP, module_name);
  return path;
}

// Resolve imports: parse imported .stola files and prepend their functions
static void resolve_imports(ASTNode *program) {
  if (!program || program->type != AST_PROGRAM)
    return;

  // Collect import module names
  char *modules[32];
  int module_count = 0;

  for (int i = 0; i < program->as.program.statement_count; i++) {
    ASTNode *stmt = program->as.program.statements[i];
    if (stmt && stmt->type == AST_IMPORT_STMT) {
      if (module_count < 32) {
        modules[module_count++] = stmt->as.import_stmt.module_name;
      }
    }
  }

  if (module_count == 0)
    return;

  // Parse each imported module and collect function declarations
  ASTNode **imported_funcs = NULL;
  int imported_count = 0;

  for (int m = 0; m < module_count; m++) {
    char *path = build_stdlib_path(modules[m]);
    char *source = read_source_file(path);

    if (!source) {
      fprintf(stderr, "Warning: Could not import module '%s' (tried %s)\n",
              modules[m], path);
      free(path);
      continue;
    }

    printf("Importing %s...\n", modules[m]);

    Lexer lib_lexer;
    lexer_init(&lib_lexer, source);

    Parser lib_parser;
    parser_init(&lib_parser, &lib_lexer);

    ASTNode *lib_program = parser_parse_program(&lib_parser);

    if (lib_parser.error_count > 0) {
      fprintf(stderr, "Parse errors in imported module '%s':\n", modules[m]);
      parser_print_errors(&lib_parser);
      parser_free(&lib_parser);
      free(source);
      free(path);
      continue;
    }

    // Extract all function declarations from the imported module
    if (lib_program && lib_program->type == AST_PROGRAM) {
      for (int i = 0; i < lib_program->as.program.statement_count; i++) {
        ASTNode *stmt = lib_program->as.program.statements[i];
        if (stmt && stmt->type == AST_FUNCTION_DECL) {
          imported_count++;
          imported_funcs = (ASTNode **)realloc(
              imported_funcs, sizeof(ASTNode *) * imported_count);
          imported_funcs[imported_count - 1] = stmt;
          // Remove from lib_program so it doesn't get freed
          lib_program->as.program.statements[i] = NULL;
        }
      }
    }

    parser_free(&lib_parser);
    free(source);
    free(path);
  }

  if (imported_count == 0)
    return;

  // Build new statement list: imported functions first, then original
  // statements (minus import statements)
  int orig_count = program->as.program.statement_count;
  int new_capacity = imported_count + orig_count;
  ASTNode **new_stmts = (ASTNode **)malloc(sizeof(ASTNode *) * new_capacity);

  // Prepend imported functions
  int idx = 0;
  for (int i = 0; i < imported_count; i++) {
    new_stmts[idx++] = imported_funcs[i];
  }

  // Copy original statements (skip imports)
  for (int i = 0; i < orig_count; i++) {
    ASTNode *stmt = program->as.program.statements[i];
    if (stmt && stmt->type != AST_IMPORT_STMT) {
      new_stmts[idx++] = stmt;
    }
  }

  free(program->as.program.statements);
  program->as.program.statements = new_stmts;
  program->as.program.statement_count = idx;

  free(imported_funcs);
}

int main(int argc, char **argv) {
  if (argc < 3) {
    printf("Usage: stolascript [options] <input.stola> <output.s>\n");
    printf("Options:\n");
    printf("  --freestanding    Compile for bare-metal without runtime.c "
           "dependencies\n");
    return 1;
  }

  int is_freestanding = 0;
  const char *input_path = NULL;
  const char *output_path = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--freestanding") == 0) {
      is_freestanding = 1;
    } else if (!input_path) {
      input_path = argv[i];
    } else if (!output_path) {
      output_path = argv[i];
    }
  }

  if (!input_path || !output_path) {
    printf("Error: Missing input or output file paths.\n");
    return 1;
  }

  char *source = read_source_file(input_path);
  if (!source)
    return 1;

  printf("Compiling %s %s...\n", input_path,
         is_freestanding ? "(Freestanding Mode)" : "");

  Lexer lexer;
  lexer_init(&lexer, source);

  Parser parser;
  parser_init(&parser, &lexer);

  ASTNode *program = parser_parse_program(&parser);

  if (parser.error_count > 0) {
    printf("Parser failed.\n");
    parser_print_errors(&parser);
    free(source);
    return 1;
  }

  // Resolve imports before semantic analysis (skip in freestanding since stdlib
  // uses runtime)
  if (!is_freestanding) {
    resolve_imports(program);
  }

  SemanticAnalyzer analyzer;
  semantic_init(&analyzer, is_freestanding);

  if (!semantic_analyze(&analyzer, program)) {
    printf("Semantic Analyzer failed.\n");
    semantic_print_errors(&analyzer);
    semantic_free(&analyzer);
    free(source);
    return 1;
  }

  printf("Generating assembly to %s...\n", output_path);
  codegen_generate(program, &analyzer, output_path, is_freestanding);

  semantic_free(&analyzer);
  ast_free(program);
  parser_free(&parser);
  free(source);

  printf("Compilation successful!\n");
  return 0;
}
