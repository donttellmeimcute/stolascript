# ──────────────────────────────────────────────────────────────────────────────
# StolasScript — Makefile
# Targets:
#   make            → build the compiler  (s / s.exe)
#   make release    → stripped release binary
#   make runtime    → compile runtime + builtins object files
#   make clean      → remove all generated files
#   make test       → quick smoke test
# ──────────────────────────────────────────────────────────────────────────────

SRC_DIR  = src
OBJ_DIR  = obj

# Compiler front-end sources (excludes runtime.c / builtins.c)
COMPILER_SRCS = \
	$(SRC_DIR)/main.c     \
	$(SRC_DIR)/lexer.c    \
	$(SRC_DIR)/parser.c   \
	$(SRC_DIR)/ast.c      \
	$(SRC_DIR)/semantic.c \
	$(SRC_DIR)/codegen.c

# Runtime sources (linked together with user .s programs)
RUNTIME_SRCS = \
	$(SRC_DIR)/runtime.c  \
	$(SRC_DIR)/builtins.c

COMPILER_OBJS  = $(COMPILER_SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)
RUNTIME_OBJS   = $(RUNTIME_SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

# ── Platform detection ──────────────────────────────────────────────────────
ifeq ($(OS),Windows_NT)
  CC       = clang
  CFLAGS   = -Wall -Wextra -std=c11 -O2
  EXEC     = s.exe
  LDFLAGS  = -lws2_32 -lwinhttp
  MKDIR    = if not exist $(OBJ_DIR) mkdir $(OBJ_DIR)
  RM       = del /Q /F
  RMDIR    = rmdir /S /Q
else
  CC       = gcc
  CFLAGS   = -Wall -Wextra -std=c11 -O2
  EXEC     = s
  LDFLAGS  = -lpthread -ldl -rdynamic
  MKDIR    = mkdir -p $(OBJ_DIR)
  RM       = rm -f
  RMDIR    = rm -rf
endif

RELEASE_FLAGS = -O2 -DNDEBUG -s

.PHONY: all release runtime clean test

# ── Default: build the compiler ─────────────────────────────────────────────
all: $(OBJ_DIR) $(EXEC)

$(EXEC): $(COMPILER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ── Stripped release binary ─────────────────────────────────────────────────
release: $(OBJ_DIR)
	$(CC) $(RELEASE_FLAGS) $(COMPILER_SRCS) -o $(EXEC) $(LDFLAGS)

# ── Compile runtime object files ────────────────────────────────────────────
runtime: $(OBJ_DIR) $(RUNTIME_OBJS)

# ── Object file rule ────────────────────────────────────────────────────────
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	$(MKDIR)

# ── Cleanup ─────────────────────────────────────────────────────────────────
clean:
ifeq ($(OS),Windows_NT)
	-$(RMDIR) $(OBJ_DIR)
	-$(RM) s.exe s
else
	$(RMDIR) $(OBJ_DIR) $(EXEC) s.exe
endif

# ── Quick smoke test ─────────────────────────────────────────────────────────
test: $(EXEC)
ifeq ($(OS),Windows_NT)
	.\$(EXEC) --help
else
	./$(EXEC) --help
endif
