# ──────────────────────────────────────────────────────────────────────────────
# StolasScript — Makefile
#   make           → compiler (./s) + runtime library (lib/libstola.a)
#   make test      → run the test suite
#   make bench     → run benchmarks (StolasScript vs C vs Python)
#   make os        → build StolaOS (bootable kernel written in StolasScript)
#   make run-os    → boot StolaOS in QEMU
#   make clean
# ──────────────────────────────────────────────────────────────────────────────

CC      ?= gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2
SRC     = src
OBJ     = obj

COMPILER_SRCS = $(SRC)/main.c $(SRC)/lexer.c $(SRC)/parser.c $(SRC)/ast.c \
                $(SRC)/semantic.c $(SRC)/codegen.c $(SRC)/builtin_table.c
RUNTIME_SRCS  = $(SRC)/runtime.c $(SRC)/builtins.c

ifeq ($(OS),Windows_NT)
  CC       = clang
  EXEC     = s.exe
  LDFLAGS  =
  RT_CFLAGS = -std=gnu11 -O2
else
  EXEC     = s
  LDFLAGS  =
  RT_CFLAGS = -std=gnu11 -O2 -Wall
endif

COMPILER_OBJS = $(COMPILER_SRCS:$(SRC)/%.c=$(OBJ)/%.o)
RUNTIME_OBJS  = $(RUNTIME_SRCS:$(SRC)/%.c=$(OBJ)/rt_%.o)

.PHONY: all clean test bench os run-os

all: $(EXEC) lib/libstola.a

$(EXEC): $(COMPILER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ)/%.o: $(SRC)/%.c $(wildcard $(SRC)/*.h) | $(OBJ)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ)/rt_%.o: $(SRC)/%.c $(SRC)/runtime.h | $(OBJ)
	$(CC) $(RT_CFLAGS) -c $< -o $@

lib/libstola.a: $(RUNTIME_OBJS)
	@mkdir -p lib
	ar rcs $@ $^

$(OBJ):
	mkdir -p $(OBJ)

test: all
	./tests/run_tests.sh

bench: all
	./bench/run_bench.sh

os: all
	./s build os/kernel.stola --target kernel -o os/stolaos.bin

run-os: os
	qemu-system-x86_64 -kernel os/stolaos.bin -serial stdio

clean:
	rm -rf $(OBJ) lib $(EXEC) s.exe os/stolaos.bin
