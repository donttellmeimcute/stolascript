#define _CRT_SECURE_NO_WARNINGS
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#endif
#include "runtime.h"
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#define stola_strdup _strdup
#ifdef _MSC_VER
#define THREAD_LOCAL __declspec(thread)
#else
#define THREAD_LOCAL __thread
#endif
#else
#include <signal.h>
#include <unistd.h>
#define stola_strdup strdup
#define THREAD_LOCAL __thread
#endif

// ============================================================
// Singletons and small-integer cache
// ============================================================

StolaValue stola_null_value = {STOLA_NULL, {0}};
StolaValue stola_true_value = {STOLA_BOOL, {.bool_val = 1}};
StolaValue stola_false_value = {STOLA_BOOL, {.bool_val = 0}};
StolaValue stola_missing_value = {STOLA_MISSING, {0}};

#define SMALL_INT_MIN (-256)
#define SMALL_INT_MAX 1024
static StolaValue small_ints[SMALL_INT_MAX - SMALL_INT_MIN + 1];

static int64_t g_argc;
static char **g_argv;
static void (*stola_longjmp_ptr)(int64_t *env) = NULL;

static void *xmalloc(size_t n) {
  void *p = malloc(n ? n : 1);
  if (!p) {
    fprintf(stderr, "MemoryError: out of memory\n");
    exit(1);
  }
  return p;
}

static void *xrealloc(void *p, size_t n) {
  void *q = realloc(p, n ? n : 1);
  if (!q) {
    fprintf(stderr, "MemoryError: out of memory\n");
    exit(1);
  }
  return q;
}

static StolaValue *alloc_value(StolaType t) {
  StolaValue *v = (StolaValue *)xmalloc(sizeof(StolaValue));
  v->type = t;
  return v;
}

// ============================================================
// Constructors
// ============================================================

StolaValue *stola_new_int(int64_t val) {
  if (val >= SMALL_INT_MIN && val <= SMALL_INT_MAX)
    return &small_ints[val - SMALL_INT_MIN];
  StolaValue *v = alloc_value(STOLA_INT);
  v->as.int_val = val;
  return v;
}

StolaValue *stola_new_float(double val) {
  StolaValue *v = alloc_value(STOLA_FLOAT);
  v->as.float_val = val;
  return v;
}

StolaValue *stola_new_float_bits(int64_t bits) {
  double d;
  memcpy(&d, &bits, sizeof(d));
  return stola_new_float(d);
}

StolaValue *stola_new_bool(int val) {
  return val ? &stola_true_value : &stola_false_value;
}

StolaValue *stola_bool_value(int64_t val) {
  return val ? &stola_true_value : &stola_false_value;
}

StolaValue *stola_new_string_len(const char *str, int64_t len) {
  StolaValue *v = alloc_value(STOLA_STRING);
  v->as.str_val = (char *)xmalloc((size_t)len + 1);
  memcpy(v->as.str_val, str, (size_t)len);
  v->as.str_val[len] = '\0';
  return v;
}

StolaValue *stola_new_string(const char *str) {
  if (!str)
    str = "";
  return stola_new_string_len(str, (int64_t)strlen(str));
}

StolaValue *stola_new_string_owned(char *str) {
  StolaValue *v = alloc_value(STOLA_STRING);
  v->as.str_val = str ? str : stola_strdup("");
  return v;
}

StolaValue *stola_new_null(void) { return &stola_null_value; }

StolaValue *stola_new_array_cap(int64_t cap) {
  StolaValue *v = alloc_value(STOLA_ARRAY);
  v->as.array_val.count = 0;
  v->as.array_val.capacity = cap > 0 ? (int)cap : 0;
  v->as.array_val.items =
      cap > 0 ? (StolaValue **)xmalloc(sizeof(StolaValue *) * cap) : NULL;
  return v;
}

StolaValue *stola_new_array(void) { return stola_new_array_cap(0); }

StolaValue *stola_new_dict(void) {
  StolaValue *v = alloc_value(STOLA_DICT);
  memset(&v->as.dict_val, 0, sizeof(StolaDict));
  return v;
}

StolaValue *stola_new_struct(const char *type_name) {
  StolaValue *v = alloc_value(STOLA_STRUCT);
  v->as.struct_val.type_name = type_name;
  memset(&v->as.struct_val.fields, 0, sizeof(StolaDict));
  return v;
}

StolaValue *stola_new_object(const char *class_name) {
  return stola_new_struct(class_name);
}

StolaValue *stola_new_function(void *fn, int64_t arity, const char *name) {
  StolaValue *v = alloc_value(STOLA_FUNCTION);
  v->as.fn_val.fn = fn;
  v->as.fn_val.arity = (int)arity;
  v->as.fn_val.ncap = -1;
  v->as.fn_val.env = NULL;
  v->as.fn_val.name = name;
  return v;
}

StolaValue *stola_new_closure(void *fn, int64_t arity, int64_t ncap) {
  StolaValue *v = alloc_value(STOLA_FUNCTION);
  v->as.fn_val.fn = fn;
  v->as.fn_val.arity = (int)arity;
  v->as.fn_val.ncap = (int)ncap;
  v->as.fn_val.env =
      ncap > 0 ? (StolaValue **)xmalloc(sizeof(StolaValue *) * ncap) : NULL;
  v->as.fn_val.name = "<lambda>";
  return v;
}

void stola_closure_set(StolaValue *clo, int64_t i, StolaValue *v) {
  clo->as.fn_val.env[i] = v;
}

StolaValue *stola_closure_get(StolaValue *clo, int64_t i) {
  return clo->as.fn_val.env[i];
}

// ============================================================
// Errors / exceptions
// ============================================================

#define MAX_TRY 256
typedef struct {
  int64_t buf[10];
} JmpBuf;
static THREAD_LOCAL JmpBuf try_stack[MAX_TRY];
static THREAD_LOCAL int try_top = 0;
static THREAD_LOCAL StolaValue *current_error = NULL;

int64_t *stola_push_try(void) {
  if (try_top >= MAX_TRY) {
    fprintf(stderr, "RuntimeError: too many nested try blocks\n");
    exit(1);
  }
  return try_stack[try_top++].buf;
}

void stola_pop_try(void) {
  if (try_top > 0)
    try_top--;
}

void stola_throw(StolaValue *err) {
  current_error = err ? err : &stola_null_value;
  if (try_top == 0 || !stola_longjmp_ptr) {
    char *s = stola_value_to_cstr(current_error);
    fflush(stdout);
    fprintf(stderr, "Uncaught exception: %s\n", s);
    free(s);
    exit(1);
  }
  try_top--;
  stola_longjmp_ptr(try_stack[try_top].buf);
}

void stola_raise(const char *fmt, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  stola_throw(stola_new_string(buf));
}

StolaValue *stola_get_error(void) {
  return current_error ? current_error : &stola_null_value;
}

void stola_raise_zero_division(void) {
  stola_raise("ZeroDivisionError: division by zero");
}

// ============================================================
// Lifecycle
// ============================================================

#ifndef _WIN32
static void stola_sigsegv_handler(int sig, siginfo_t *info, void *ctx) {
  (void)sig;
  (void)ctx;
  const char *msg =
      "\n[StolasScript] Segmentation fault (invalid memory access or stack "
      "overflow from too deep recursion)\n";
  (void)info;
  ssize_t w = write(2, msg, strlen(msg));
  (void)w;
  _exit(139);
}

static void stola_sigint_handler(int sig) {
  (void)sig;
  const char *msg = "\n[StolasScript] Interrupted (SIGINT).\n";
  ssize_t w = write(2, msg, strlen(msg));
  (void)w;
  _exit(130);
}
#endif

void stola_setup_runtime(void) {
#ifndef _WIN32
  static char altstack[64 * 1024];
  stack_t ss;
  ss.ss_sp = altstack;
  ss.ss_size = sizeof(altstack);
  ss.ss_flags = 0;
  sigaltstack(&ss, NULL);
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = stola_sigsegv_handler;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGBUS, &sa, NULL);
  signal(SIGINT, stola_sigint_handler);
#endif
}

void stola_runtime_init(int64_t argc, char **argv, void *longjmp_fn) {
  for (int64_t i = SMALL_INT_MIN; i <= SMALL_INT_MAX; i++) {
    small_ints[i - SMALL_INT_MIN].type = STOLA_INT;
    small_ints[i - SMALL_INT_MIN].as.int_val = i;
  }
  g_argc = argc;
  g_argv = argv;
  stola_longjmp_ptr = (void (*)(int64_t *))longjmp_fn;
  srand((unsigned)time(NULL));
  stola_setup_runtime();
}

int stola_runtime_exit(void) {
  fflush(stdout);
  return 0;
}

// ============================================================
// Inspection and string conversion
// ============================================================

int stola_is_truthy(StolaValue *val) {
  if (!val)
    return 0;
  switch (val->type) {
  case STOLA_NULL:
  case STOLA_MISSING:
    return 0;
  case STOLA_BOOL:
    return val->as.bool_val;
  case STOLA_INT:
    return val->as.int_val != 0;
  case STOLA_FLOAT:
    return val->as.float_val != 0.0;
  case STOLA_STRING:
    return val->as.str_val[0] != '\0';
  case STOLA_ARRAY:
    return val->as.array_val.count > 0;
  case STOLA_DICT:
    return val->as.dict_val.count > 0;
  default:
    return 1;
  }
}

const char *stola_type_name(StolaValue *val) {
  if (!val)
    return "null";
  switch (val->type) {
  case STOLA_INT: return "int";
  case STOLA_FLOAT: return "float";
  case STOLA_BOOL: return "bool";
  case STOLA_STRING: return "string";
  case STOLA_ARRAY: return "array";
  case STOLA_DICT: return "dict";
  case STOLA_STRUCT: return val->as.struct_val.type_name;
  case STOLA_FUNCTION: return "function";
  case STOLA_NULL: return "null";
  default: return "missing";
  }
}

int64_t stola_to_int64(StolaValue *v) {
  if (!v)
    return 0;
  switch (v->type) {
  case STOLA_INT:
    return v->as.int_val;
  case STOLA_BOOL:
    return v->as.bool_val;
  case STOLA_FLOAT:
    return (int64_t)v->as.float_val;
  case STOLA_NULL:
    stola_raise("TypeError: expected int, got null");
    return 0;
  default:
    stola_raise("TypeError: expected int, got %s", stola_type_name(v));
    return 0;
  }
}

static int is_num(StolaValue *v) {
  return v->type == STOLA_INT || v->type == STOLA_FLOAT ||
         v->type == STOLA_BOOL;
}

static double to_double(StolaValue *v) {
  switch (v->type) {
  case STOLA_INT: return (double)v->as.int_val;
  case STOLA_FLOAT: return v->as.float_val;
  case STOLA_BOOL: return v->as.bool_val;
  default:
    stola_raise("TypeError: expected a number, got %s", stola_type_name(v));
    return 0;
  }
}

// Growable string buffer
typedef struct {
  char *d;
  size_t n, cap;
} SBuf;

static void sb_putn(SBuf *b, const char *s, size_t n) {
  if (b->n + n + 1 > b->cap) {
    b->cap = (b->n + n + 1) * 2;
    b->d = (char *)xrealloc(b->d, b->cap);
  }
  memcpy(b->d + b->n, s, n);
  b->n += n;
  b->d[b->n] = 0;
}
static void sb_puts(SBuf *b, const char *s) { sb_putn(b, s, strlen(s)); }
static void sb_printf(SBuf *b, const char *fmt, ...) {
  char tmp[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  sb_putn(b, tmp, n < (int)sizeof(tmp) ? (size_t)n : sizeof(tmp) - 1);
}
static char *sb_take(SBuf *b) {
  if (!b->d)
    return stola_strdup("");
  return b->d;
}

static void format_float(double d, char *out, size_t n) {
  if (isnan(d)) {
    snprintf(out, n, "nan");
    return;
  }
  if (isinf(d)) {
    snprintf(out, n, d > 0 ? "inf" : "-inf");
    return;
  }
  snprintf(out, n, "%.15g", d);
  if (strtod(out, NULL) != d)
    snprintf(out, n, "%.17g", d);
  if (!strchr(out, '.') && !strchr(out, 'e') && !strchr(out, 'n') &&
      !strchr(out, 'i'))
    strncat(out, ".0", n - strlen(out) - 1);
}

static void repr_into(SBuf *b, StolaValue *v, int nested, int depth);

static void repr_string(SBuf *b, const char *s) {
  sb_puts(b, "\"");
  for (; *s; s++) {
    switch (*s) {
    case '"': sb_puts(b, "\\\""); break;
    case '\\': sb_puts(b, "\\\\"); break;
    case '\n': sb_puts(b, "\\n"); break;
    case '\t': sb_puts(b, "\\t"); break;
    case '\r': sb_puts(b, "\\r"); break;
    default: sb_putn(b, s, 1);
    }
  }
  sb_puts(b, "\"");
}

static void repr_into(SBuf *b, StolaValue *v, int nested, int depth) {
  if (!v) {
    sb_puts(b, "null");
    return;
  }
  if (depth > 64) {
    sb_puts(b, "...");
    return;
  }
  switch (v->type) {
  case STOLA_INT:
    sb_printf(b, "%lld", (long long)v->as.int_val);
    break;
  case STOLA_FLOAT: {
    char tmp[64];
    format_float(v->as.float_val, tmp, sizeof(tmp));
    sb_puts(b, tmp);
    break;
  }
  case STOLA_BOOL:
    sb_puts(b, v->as.bool_val ? "true" : "false");
    break;
  case STOLA_STRING:
    if (nested)
      repr_string(b, v->as.str_val);
    else
      sb_puts(b, v->as.str_val);
    break;
  case STOLA_NULL:
  case STOLA_MISSING:
    sb_puts(b, "null");
    break;
  case STOLA_ARRAY:
    sb_puts(b, "[");
    for (int i = 0; i < v->as.array_val.count; i++) {
      if (i)
        sb_puts(b, ", ");
      repr_into(b, v->as.array_val.items[i], 1, depth + 1);
    }
    sb_puts(b, "]");
    break;
  case STOLA_DICT:
    sb_puts(b, "{");
    for (int i = 0; i < v->as.dict_val.count; i++) {
      if (i)
        sb_puts(b, ", ");
      repr_string(b, v->as.dict_val.entries[i].key);
      sb_puts(b, ": ");
      repr_into(b, v->as.dict_val.entries[i].value, 1, depth + 1);
    }
    sb_puts(b, "}");
    break;
  case STOLA_STRUCT:
    sb_puts(b, v->as.struct_val.type_name);
    sb_puts(b, "{");
    for (int i = 0; i < v->as.struct_val.fields.count; i++) {
      if (i)
        sb_puts(b, ", ");
      sb_puts(b, v->as.struct_val.fields.entries[i].key);
      sb_puts(b, ": ");
      repr_into(b, v->as.struct_val.fields.entries[i].value, 1, depth + 1);
    }
    sb_puts(b, "}");
    break;
  case STOLA_FUNCTION:
    sb_printf(b, "<function %s>",
              v->as.fn_val.name ? v->as.fn_val.name : "?");
    break;
  }
}

char *stola_value_to_cstr(StolaValue *val) {
  SBuf b = {0};
  repr_into(&b, val, 0, 0);
  return sb_take(&b);
}

void stola_print_value(StolaValue *val) {
  char *s = stola_value_to_cstr(val);
  fputs(s, stdout);
  fputc('\n', stdout);
  free(s);
}

void stola_print_args(int64_t argc, StolaValue **argv_rev) {
  for (int64_t i = 0; i < argc; i++) {
    if (i)
      fputc(' ', stdout);
    StolaValue *v = argv_rev[argc - 1 - i];
    if (v && v->type == STOLA_STRING) {
      fputs(v->as.str_val, stdout);
    } else if (v && v->type == STOLA_INT) {
      fprintf(stdout, "%lld", (long long)v->as.int_val);
    } else {
      char *s = stola_value_to_cstr(v);
      fputs(s, stdout);
      free(s);
    }
  }
  fputc('\n', stdout);
}

// ============================================================
// Operators
// ============================================================

StolaValue *stola_string_concat(StolaValue *a, StolaValue *b) {
  char *sa = a->type == STOLA_STRING ? a->as.str_val : stola_value_to_cstr(a);
  char *sb = b->type == STOLA_STRING ? b->as.str_val : stola_value_to_cstr(b);
  size_t la = strlen(sa), lb = strlen(sb);
  char *r = (char *)xmalloc(la + lb + 1);
  memcpy(r, sa, la);
  memcpy(r + la, sb, lb + 1);
  if (a->type != STOLA_STRING)
    free(sa);
  if (b->type != STOLA_STRING)
    free(sb);
  return stola_new_string_owned(r);
}

static void binop_type_error(const char *op, StolaValue *a, StolaValue *b) {
  stola_raise("TypeError: unsupported operand types for %s: %s and %s", op,
              stola_type_name(a), stola_type_name(b));
}

StolaValue *stola_add(StolaValue *a, StolaValue *b) {
  if (a->type == STOLA_INT && b->type == STOLA_INT)
    return stola_new_int((int64_t)((uint64_t)a->as.int_val +
                                   (uint64_t)b->as.int_val));
  if (a->type == STOLA_STRING || b->type == STOLA_STRING)
    return stola_string_concat(a, b);
  if (is_num(a) && is_num(b)) {
    if (a->type == STOLA_FLOAT || b->type == STOLA_FLOAT)
      return stola_new_float(to_double(a) + to_double(b));
    return stola_new_int(stola_to_int64(a) + stola_to_int64(b));
  }
  if (a->type == STOLA_ARRAY && b->type == STOLA_ARRAY) {
    int n = a->as.array_val.count + b->as.array_val.count;
    StolaValue *r = stola_new_array_cap(n);
    for (int i = 0; i < a->as.array_val.count; i++)
      r->as.array_val.items[r->as.array_val.count++] = a->as.array_val.items[i];
    for (int i = 0; i < b->as.array_val.count; i++)
      r->as.array_val.items[r->as.array_val.count++] = b->as.array_val.items[i];
    return r;
  }
  binop_type_error("+", a, b);
  return &stola_null_value;
}

StolaValue *stola_sub(StolaValue *a, StolaValue *b) {
  if (a->type == STOLA_INT && b->type == STOLA_INT)
    return stola_new_int((int64_t)((uint64_t)a->as.int_val -
                                   (uint64_t)b->as.int_val));
  if (is_num(a) && is_num(b)) {
    if (a->type == STOLA_FLOAT || b->type == STOLA_FLOAT)
      return stola_new_float(to_double(a) - to_double(b));
    return stola_new_int(stola_to_int64(a) - stola_to_int64(b));
  }
  binop_type_error("-", a, b);
  return &stola_null_value;
}

static StolaValue *repeat_seq(StolaValue *seq, int64_t n) {
  if (n < 0)
    n = 0;
  if (seq->type == STOLA_STRING) {
    size_t l = strlen(seq->as.str_val);
    char *r = (char *)xmalloc(l * (size_t)n + 1);
    for (int64_t i = 0; i < n; i++)
      memcpy(r + l * i, seq->as.str_val, l);
    r[l * n] = 0;
    return stola_new_string_owned(r);
  }
  int c = seq->as.array_val.count;
  StolaValue *r = stola_new_array_cap(c * n);
  for (int64_t i = 0; i < n; i++)
    for (int j = 0; j < c; j++)
      r->as.array_val.items[r->as.array_val.count++] = seq->as.array_val.items[j];
  return r;
}

StolaValue *stola_mul(StolaValue *a, StolaValue *b) {
  if (a->type == STOLA_INT && b->type == STOLA_INT)
    return stola_new_int((int64_t)((uint64_t)a->as.int_val *
                                   (uint64_t)b->as.int_val));
  if (is_num(a) && is_num(b)) {
    if (a->type == STOLA_FLOAT || b->type == STOLA_FLOAT)
      return stola_new_float(to_double(a) * to_double(b));
    return stola_new_int(stola_to_int64(a) * stola_to_int64(b));
  }
  if ((a->type == STOLA_STRING || a->type == STOLA_ARRAY) && b->type == STOLA_INT)
    return repeat_seq(a, b->as.int_val);
  if ((b->type == STOLA_STRING || b->type == STOLA_ARRAY) && a->type == STOLA_INT)
    return repeat_seq(b, a->as.int_val);
  binop_type_error("*", a, b);
  return &stola_null_value;
}

StolaValue *stola_div(StolaValue *a, StolaValue *b) {
  if (!is_num(a) || !is_num(b))
    binop_type_error("/", a, b);
  if (a->type == STOLA_FLOAT || b->type == STOLA_FLOAT) {
    double d = to_double(b);
    if (d == 0.0)
      stola_raise_zero_division();
    return stola_new_float(to_double(a) / d);
  }
  int64_t d = stola_to_int64(b);
  if (d == 0)
    stola_raise_zero_division();
  return stola_new_int(stola_to_int64(a) / d);
}

StolaValue *stola_mod(StolaValue *a, StolaValue *b) {
  if (!is_num(a) || !is_num(b))
    binop_type_error("%", a, b);
  if (a->type == STOLA_FLOAT || b->type == STOLA_FLOAT) {
    double d = to_double(b);
    if (d == 0.0)
      stola_raise_zero_division();
    return stola_new_float(fmod(to_double(a), d));
  }
  int64_t d = stola_to_int64(b);
  if (d == 0)
    stola_raise_zero_division();
  return stola_new_int(stola_to_int64(a) % d);
}

StolaValue *stola_pow(StolaValue *a, StolaValue *b) {
  if (!is_num(a) || !is_num(b))
    binop_type_error("**", a, b);
  if (a->type != STOLA_FLOAT && b->type != STOLA_FLOAT) {
    int64_t base = stola_to_int64(a), e = stola_to_int64(b);
    if (e >= 0) {
      int64_t r = 1;
      while (e > 0) {
        if (e & 1)
          r *= base;
        base *= base;
        e >>= 1;
      }
      return stola_new_int(r);
    }
  }
  return stola_new_float(pow(to_double(a), to_double(b)));
}

#define INT_BINOP(name, sym, expr)                                             \
  StolaValue *name(StolaValue *a, StolaValue *b) {                             \
    if ((a->type != STOLA_INT && a->type != STOLA_BOOL) ||                     \
        (b->type != STOLA_INT && b->type != STOLA_BOOL))                       \
      binop_type_error(sym, a, b);                                             \
    int64_t x = stola_to_int64(a), y = stola_to_int64(b);                      \
    return stola_new_int(expr);                                                \
  }
INT_BINOP(stola_band, "&", x & y)
INT_BINOP(stola_bor, "|", x | y)
INT_BINOP(stola_bxor, "^", x ^ y)
INT_BINOP(stola_shl, "<<", (int64_t)((uint64_t)x << (y & 63)))
INT_BINOP(stola_shr, ">>", x >> (y & 63))

StolaValue *stola_neg(StolaValue *a) {
  if (a->type == STOLA_FLOAT)
    return stola_new_float(-a->as.float_val);
  if (a->type == STOLA_INT || a->type == STOLA_BOOL)
    return stola_new_int(-stola_to_int64(a));
  stola_raise("TypeError: bad operand type for unary -: %s",
              stola_type_name(a));
  return &stola_null_value;
}

StolaValue *stola_bnot(StolaValue *a) {
  if (a->type == STOLA_INT || a->type == STOLA_BOOL)
    return stola_new_int(~stola_to_int64(a));
  stola_raise("TypeError: bad operand type for unary ~: %s",
              stola_type_name(a));
  return &stola_null_value;
}

static int values_equal(StolaValue *a, StolaValue *b, int depth) {
  if (a == b)
    return 1;
  if (is_num(a) && is_num(b)) {
    if (a->type == STOLA_FLOAT || b->type == STOLA_FLOAT)
      return to_double(a) == to_double(b);
    return stola_to_int64(a) == stola_to_int64(b);
  }
  if (a->type != b->type)
    return 0;
  switch (a->type) {
  case STOLA_STRING:
    return strcmp(a->as.str_val, b->as.str_val) == 0;
  case STOLA_NULL:
  case STOLA_MISSING:
    return 1;
  case STOLA_ARRAY:
    if (depth > 64 || a->as.array_val.count != b->as.array_val.count)
      return 0;
    for (int i = 0; i < a->as.array_val.count; i++)
      if (!values_equal(a->as.array_val.items[i], b->as.array_val.items[i],
                        depth + 1))
        return 0;
    return 1;
  case STOLA_DICT:
    if (depth > 64 || a->as.dict_val.count != b->as.dict_val.count)
      return 0;
    for (int i = 0; i < a->as.dict_val.count; i++) {
      StolaValue key = {STOLA_STRING, {.str_val = a->as.dict_val.entries[i].key}};
      StolaValue *bv = stola_dict_get(b, &key);
      if (!values_equal(a->as.dict_val.entries[i].value, bv, depth + 1))
        return 0;
    }
    return 1;
  case STOLA_FUNCTION:
    return a->as.fn_val.fn == b->as.fn_val.fn && a->as.fn_val.ncap < 0 &&
           b->as.fn_val.ncap < 0;
  default:
    return 0;
  }
}

// <0, 0, >0 ordering; raises for unordered types
static int values_order(StolaValue *a, StolaValue *b) {
  if (is_num(a) && is_num(b)) {
    if (a->type == STOLA_FLOAT || b->type == STOLA_FLOAT) {
      double x = to_double(a), y = to_double(b);
      return x < y ? -1 : x > y ? 1 : 0;
    }
    int64_t x = stola_to_int64(a), y = stola_to_int64(b);
    return x < y ? -1 : x > y ? 1 : 0;
  }
  if (a->type == STOLA_STRING && b->type == STOLA_STRING) {
    int c = strcmp(a->as.str_val, b->as.str_val);
    return c < 0 ? -1 : c > 0 ? 1 : 0;
  }
  if (a->type == STOLA_ARRAY && b->type == STOLA_ARRAY) {
    int n = a->as.array_val.count < b->as.array_val.count
                ? a->as.array_val.count
                : b->as.array_val.count;
    for (int i = 0; i < n; i++) {
      int c = values_order(a->as.array_val.items[i], b->as.array_val.items[i]);
      if (c)
        return c;
    }
    return a->as.array_val.count < b->as.array_val.count   ? -1
           : a->as.array_val.count > b->as.array_val.count ? 1
                                                           : 0;
  }
  stola_raise("TypeError: cannot compare %s with %s", stola_type_name(a),
              stola_type_name(b));
  return 0;
}

int stola_compare(StolaValue *a, StolaValue *b, int64_t op) {
  switch (op) {
  case 0: return values_equal(a, b, 0);
  case 1: return !values_equal(a, b, 0);
  case 2: return values_order(a, b) < 0;
  case 3: return values_order(a, b) > 0;
  case 4: return values_order(a, b) <= 0;
  case 5: return values_order(a, b) >= 0;
  }
  return 0;
}

static StolaDictEntry *dict_find(StolaDict *d, const char *key);

int stola_contains(StolaValue *c, StolaValue *item) {
  switch (c->type) {
  case STOLA_ARRAY:
    for (int i = 0; i < c->as.array_val.count; i++)
      if (values_equal(c->as.array_val.items[i], item, 0))
        return 1;
    return 0;
  case STOLA_STRING:
    if (item->type != STOLA_STRING)
      stola_raise("TypeError: 'in <string>' requires a string, got %s",
                  stola_type_name(item));
    return strstr(c->as.str_val, item->as.str_val) != NULL;
  case STOLA_DICT:
  case STOLA_STRUCT: {
    StolaDict *d = c->type == STOLA_DICT ? &c->as.dict_val
                                         : &c->as.struct_val.fields;
    if (item->type == STOLA_STRING)
      return dict_find(d, item->as.str_val) != NULL;
    char *k = stola_value_to_cstr(item);
    int r = dict_find(d, k) != NULL;
    free(k);
    return r;
  }
  default:
    stola_raise("TypeError: argument of type %s is not iterable",
                stola_type_name(c));
    return 0;
  }
}

// ============================================================
// Dictionaries
// ============================================================

static uint64_t hash_str(const char *s) {
  uint64_t h = 1469598103934665603ULL;
  for (; *s; s++) {
    h ^= (unsigned char)*s;
    h *= 1099511628211ULL;
  }
  return h;
}

static void dict_reindex(StolaDict *d) {
  int cap = 8;
  while (cap < d->count * 2 + 2)
    cap *= 2;
  free(d->index);
  d->index = (int *)calloc(cap, sizeof(int));
  d->index_cap = cap;
  for (int i = 0; i < d->count; i++) {
    uint64_t h = hash_str(d->entries[i].key) & (cap - 1);
    while (d->index[h])
      h = (h + 1) & (cap - 1);
    d->index[h] = i + 1;
  }
}

static StolaDictEntry *dict_find(StolaDict *d, const char *key) {
  if (d->count < 8) {
    for (int i = 0; i < d->count; i++)
      if (strcmp(d->entries[i].key, key) == 0)
        return &d->entries[i];
    return NULL;
  }
  if (!d->index || d->index_cap < d->count * 2)
    dict_reindex(d);
  uint64_t h = hash_str(key) & (d->index_cap - 1);
  while (d->index[h]) {
    StolaDictEntry *e = &d->entries[d->index[h] - 1];
    if (strcmp(e->key, key) == 0)
      return e;
    h = (h + 1) & (d->index_cap - 1);
  }
  return NULL;
}

static void dict_put(StolaDict *d, const char *key, StolaValue *val) {
  StolaDictEntry *e = dict_find(d, key);
  if (e) {
    e->value = val;
    return;
  }
  if (d->count == d->capacity) {
    d->capacity = d->capacity ? d->capacity * 2 : 4;
    d->entries = (StolaDictEntry *)xrealloc(
        d->entries, sizeof(StolaDictEntry) * d->capacity);
  }
  d->entries[d->count].key = stola_strdup(key);
  d->entries[d->count].value = val;
  d->count++;
  if (d->index) {
    if (d->index_cap < d->count * 2) {
      dict_reindex(d);
    } else {
      uint64_t h = hash_str(key) & (d->index_cap - 1);
      while (d->index[h])
        h = (h + 1) & (d->index_cap - 1);
      d->index[h] = d->count;
    }
  }
}

static int dict_remove(StolaDict *d, const char *key) {
  for (int i = 0; i < d->count; i++) {
    if (strcmp(d->entries[i].key, key) == 0) {
      free(d->entries[i].key);
      memmove(&d->entries[i], &d->entries[i + 1],
              sizeof(StolaDictEntry) * (d->count - i - 1));
      d->count--;
      if (d->index)
        dict_reindex(d);
      return 1;
    }
  }
  return 0;
}

static char *key_of(StolaValue *k, int *owned) {
  if (k->type == STOLA_STRING) {
    *owned = 0;
    return k->as.str_val;
  }
  *owned = 1;
  return stola_value_to_cstr(k);
}

StolaValue *stola_dict_get(StolaValue *dict, StolaValue *key) {
  StolaDict *d = dict->type == STOLA_DICT     ? &dict->as.dict_val
                 : dict->type == STOLA_STRUCT ? &dict->as.struct_val.fields
                                              : NULL;
  if (!d)
    return &stola_null_value;
  int owned;
  char *k = key_of(key, &owned);
  StolaDictEntry *e = dict_find(d, k);
  if (owned)
    free(k);
  return e ? e->value : &stola_null_value;
}

void stola_dict_set(StolaValue *dict, StolaValue *key, StolaValue *val) {
  StolaDict *d = dict->type == STOLA_DICT     ? &dict->as.dict_val
                 : dict->type == STOLA_STRUCT ? &dict->as.struct_val.fields
                                              : NULL;
  if (!d) {
    stola_raise("TypeError: %s does not support item assignment",
                stola_type_name(dict));
    return;
  }
  int owned;
  char *k = key_of(key, &owned);
  dict_put(d, k, val);
  if (owned)
    free(k);
}

// ============================================================
// Arrays, indexing, slicing
// ============================================================

static void array_reserve(StolaValue *arr, int need) {
  if (need <= arr->as.array_val.capacity)
    return;
  int cap = arr->as.array_val.capacity ? arr->as.array_val.capacity * 2 : 4;
  while (cap < need)
    cap *= 2;
  arr->as.array_val.items = (StolaValue **)xrealloc(
      arr->as.array_val.items, sizeof(StolaValue *) * cap);
  arr->as.array_val.capacity = cap;
}

void stola_push(StolaValue *arr, StolaValue *val) {
  if (arr->type != STOLA_ARRAY) {
    stola_raise("TypeError: push() expects an array, got %s",
                stola_type_name(arr));
    return;
  }
  array_reserve(arr, arr->as.array_val.count + 1);
  arr->as.array_val.items[arr->as.array_val.count++] = val;
}

StolaValue *stola_pop(StolaValue *arr) {
  if (arr->type != STOLA_ARRAY)
    stola_raise("TypeError: pop() expects an array");
  if (arr->as.array_val.count == 0)
    stola_raise("IndexError: pop from empty array");
  return arr->as.array_val.items[--arr->as.array_val.count];
}

StolaValue *stola_shift(StolaValue *arr) {
  if (arr->type != STOLA_ARRAY)
    stola_raise("TypeError: shift() expects an array");
  if (arr->as.array_val.count == 0)
    stola_raise("IndexError: shift from empty array");
  StolaValue *first = arr->as.array_val.items[0];
  memmove(arr->as.array_val.items, arr->as.array_val.items + 1,
          sizeof(StolaValue *) * (arr->as.array_val.count - 1));
  arr->as.array_val.count--;
  return first;
}

static void array_insert(StolaValue *arr, int64_t idx, StolaValue *val) {
  int n = arr->as.array_val.count;
  if (idx < 0)
    idx += n;
  if (idx < 0)
    idx = 0;
  if (idx > n)
    idx = n;
  array_reserve(arr, n + 1);
  memmove(arr->as.array_val.items + idx + 1, arr->as.array_val.items + idx,
          sizeof(StolaValue *) * (n - idx));
  arr->as.array_val.items[idx] = val;
  arr->as.array_val.count++;
}

void stola_unshift(StolaValue *arr, StolaValue *val) {
  if (arr->type != STOLA_ARRAY)
    stola_raise("TypeError: unshift() expects an array");
  array_insert(arr, 0, val);
}

int64_t stola_len_i(StolaValue *val) {
  switch (val->type) {
  case STOLA_STRING:
    return (int64_t)strlen(val->as.str_val);
  case STOLA_ARRAY:
    return val->as.array_val.count;
  case STOLA_DICT:
    return val->as.dict_val.count;
  case STOLA_STRUCT:
    return val->as.struct_val.fields.count;
  default:
    stola_raise("TypeError: object of type %s has no len()",
                stola_type_name(val));
    return 0;
  }
}

StolaValue *stola_length(StolaValue *val) {
  return stola_new_int(stola_len_i(val));
}

static int64_t norm_index(int64_t i, int64_t n, const char *what) {
  if (i < 0)
    i += n;
  if (i < 0 || i >= n)
    stola_raise("IndexError: %s index out of range", what);
  return i;
}

StolaValue *stola_getitem(StolaValue *obj, StolaValue *key) {
  switch (obj->type) {
  case STOLA_ARRAY: {
    int64_t i = norm_index(stola_to_int64(key), obj->as.array_val.count, "array");
    return obj->as.array_val.items[i];
  }
  case STOLA_STRING: {
    int64_t n = (int64_t)strlen(obj->as.str_val);
    int64_t i = norm_index(stola_to_int64(key), n, "string");
    return stola_new_string_len(obj->as.str_val + i, 1);
  }
  case STOLA_DICT:
  case STOLA_STRUCT:
    return stola_dict_get(obj, key);
  default:
    stola_raise("TypeError: %s is not subscriptable", stola_type_name(obj));
    return &stola_null_value;
  }
}

void stola_setitem(StolaValue *obj, StolaValue *key, StolaValue *val) {
  switch (obj->type) {
  case STOLA_ARRAY: {
    int64_t i = norm_index(stola_to_int64(key), obj->as.array_val.count, "array");
    obj->as.array_val.items[i] = val;
    return;
  }
  case STOLA_DICT:
  case STOLA_STRUCT:
    stola_dict_set(obj, key, val);
    return;
  default:
    stola_raise("TypeError: %s does not support item assignment",
                stola_type_name(obj));
  }
}

static void slice_bounds(StolaValue *s, StolaValue *e, int64_t n, int64_t *a,
                         int64_t *b) {
  *a = (s->type == STOLA_NULL) ? 0 : stola_to_int64(s);
  *b = (e->type == STOLA_NULL) ? n : stola_to_int64(e);
  if (*a < 0)
    *a += n;
  if (*b < 0)
    *b += n;
  if (*a < 0)
    *a = 0;
  if (*b > n)
    *b = n;
  if (*b < *a)
    *b = *a;
}

StolaValue *stola_slice(StolaValue *obj, StolaValue *start, StolaValue *end) {
  int64_t a, b;
  if (obj->type == STOLA_STRING) {
    int64_t n = (int64_t)strlen(obj->as.str_val);
    slice_bounds(start, end, n, &a, &b);
    return stola_new_string_len(obj->as.str_val + a, b - a);
  }
  if (obj->type == STOLA_ARRAY) {
    slice_bounds(start, end, obj->as.array_val.count, &a, &b);
    StolaValue *r = stola_new_array_cap(b - a);
    for (int64_t i = a; i < b; i++)
      r->as.array_val.items[r->as.array_val.count++] = obj->as.array_val.items[i];
    return r;
  }
  stola_raise("TypeError: %s cannot be sliced", stola_type_name(obj));
  return &stola_null_value;
}

StolaValue *stola_iter_get(StolaValue *it, int64_t idx) {
  switch (it->type) {
  case STOLA_ARRAY:
    return it->as.array_val.items[idx];
  case STOLA_STRING:
    return stola_new_string_len(it->as.str_val + idx, 1);
  case STOLA_DICT:
    return stola_new_string(it->as.dict_val.entries[idx].key);
  case STOLA_STRUCT:
    return stola_new_string(it->as.struct_val.fields.entries[idx].key);
  default:
    stola_raise("TypeError: %s is not iterable", stola_type_name(it));
    return &stola_null_value;
  }
}

StolaValue *stola_iter_value(StolaValue *it, int64_t idx) {
  if (it->type == STOLA_DICT)
    return it->as.dict_val.entries[idx].value;
  if (it->type == STOLA_STRUCT)
    return it->as.struct_val.fields.entries[idx].value;
  if (it->type == STOLA_ARRAY) // for i, x in array → index, value
    return it->as.array_val.items[idx];
  stola_raise("TypeError: cannot unpack %s in for loop", stola_type_name(it));
  return &stola_null_value;
}

// ============================================================
// Objects / classes / method dispatch
// ============================================================

typedef struct ClassMethod {
  const char *name;
  void *fn;
  int nparams;
} ClassMethod;

typedef struct ClassRec {
  const char *name;
  struct ClassRec *parent;
  const char *parent_name;
  ClassMethod *methods;
  int nmethods, cap;
} ClassRec;

static ClassRec *classes;
static int nclasses, capclasses;

static ClassRec *find_class(const char *name) {
  for (int i = 0; i < nclasses; i++)
    if (classes[i].name == name || strcmp(classes[i].name, name) == 0)
      return &classes[i];
  return NULL;
}

void stola_register_class(const char *name, const char *parent) {
  if (find_class(name))
    return;
  if (nclasses == capclasses) {
    capclasses = capclasses ? capclasses * 2 : 16;
    classes = (ClassRec *)xrealloc(classes, sizeof(ClassRec) * capclasses);
  }
  ClassRec *c = &classes[nclasses++];
  memset(c, 0, sizeof(*c));
  c->name = name;
  c->parent_name = parent;
  // resolve parents lazily (classes may register in any order)
}

void stola_register_method(const char *class_name, const char *method_name,
                           void *func_ptr, int64_t nparams) {
  ClassRec *c = find_class(class_name);
  if (!c) {
    stola_register_class(class_name, NULL);
    c = find_class(class_name);
  }
  if (c->nmethods == c->cap) {
    c->cap = c->cap ? c->cap * 2 : 8;
    c->methods = (ClassMethod *)xrealloc(c->methods, sizeof(ClassMethod) * c->cap);
  }
  c->methods[c->nmethods].name = method_name;
  c->methods[c->nmethods].fn = func_ptr;
  c->methods[c->nmethods].nparams = (int)nparams;
  c->nmethods++;
}

static ClassMethod *find_method(const char *class_name, const char *name) {
  ClassRec *c = find_class(class_name);
  int guard = 0;
  while (c && guard++ < 256) {
    for (int i = 0; i < c->nmethods; i++)
      if (c->methods[i].name == name || strcmp(c->methods[i].name, name) == 0)
        return &c->methods[i];
    if (!c->parent && c->parent_name)
      c->parent = find_class(c->parent_name);
    c = c->parent;
  }
  return NULL;
}

typedef StolaValue *(*F0)(void);
typedef StolaValue *(*F1)(StolaValue *);
typedef StolaValue *(*F2)(StolaValue *, StolaValue *);
typedef StolaValue *(*F3)(StolaValue *, StolaValue *, StolaValue *);
typedef StolaValue *(*F4)(StolaValue *, StolaValue *, StolaValue *, StolaValue *);
typedef StolaValue *(*F5)(StolaValue *, StolaValue *, StolaValue *, StolaValue *,
                          StolaValue *);
typedef StolaValue *(*F6)(StolaValue *, StolaValue *, StolaValue *, StolaValue *,
                          StolaValue *, StolaValue *);
typedef StolaValue *(*F7)(StolaValue *, StolaValue *, StolaValue *, StolaValue *,
                          StolaValue *, StolaValue *, StolaValue *);
typedef StolaValue *(*F8)(StolaValue *, StolaValue *, StolaValue *, StolaValue *,
                          StolaValue *, StolaValue *, StolaValue *, StolaValue *);
typedef StolaValue *(*F9)(StolaValue *, StolaValue *, StolaValue *, StolaValue *,
                          StolaValue *, StolaValue *, StolaValue *, StolaValue *,
                          StolaValue *);
typedef StolaValue *(*F10)(StolaValue *, StolaValue *, StolaValue *,
                           StolaValue *, StolaValue *, StolaValue *,
                           StolaValue *, StolaValue *, StolaValue *,
                           StolaValue *);

static StolaValue *invoke_raw(void *fn, int n, StolaValue **a) {
  switch (n) {
  case 0: return ((F0)fn)();
  case 1: return ((F1)fn)(a[0]);
  case 2: return ((F2)fn)(a[0], a[1]);
  case 3: return ((F3)fn)(a[0], a[1], a[2]);
  case 4: return ((F4)fn)(a[0], a[1], a[2], a[3]);
  case 5: return ((F5)fn)(a[0], a[1], a[2], a[3], a[4]);
  case 6: return ((F6)fn)(a[0], a[1], a[2], a[3], a[4], a[5]);
  case 7: return ((F7)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
  case 8: return ((F8)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
  case 9: return ((F9)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8]);
  case 10:
    return ((F10)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
  default:
    stola_raise("TypeError: too many arguments (max 10 for dynamic calls)");
    return &stola_null_value;
  }
}

// Calls fn with arguments in natural order.
StolaValue *stola_call_n(StolaValue *fn, int argc, StolaValue **args) {
  if (fn->type != STOLA_FUNCTION) {
    stola_raise("TypeError: %s is not callable", stola_type_name(fn));
    return &stola_null_value;
  }
  StolaFunction *f = &fn->as.fn_val;
  if (argc > f->arity)
    stola_raise("TypeError: %s() takes %d argument%s but %d were given",
                f->name ? f->name : "function", f->arity,
                f->arity == 1 ? "" : "s", argc);
  StolaValue *a[12];
  int n = 0;
  if (f->ncap >= 0)
    a[n++] = fn; // closure environment
  for (int i = 0; i < f->arity && n < 11; i++)
    a[n++] = i < argc ? args[i] : &stola_missing_value;
  return invoke_raw(f->fn, n, a);
}

StolaValue *stola_call_value(StolaValue *fn, int64_t argc,
                             StolaValue **argv_rev) {
  StolaValue *args[12];
  if (argc > 11)
    stola_raise("TypeError: too many arguments");
  for (int64_t i = 0; i < argc; i++)
    args[i] = argv_rev[argc - 1 - i];
  return stola_call_n(fn, (int)argc, args);
}

static StolaValue *call1(StolaValue *fn, StolaValue *a) {
  return stola_call_n(fn, 1, &a);
}

static StolaValue *call2(StolaValue *fn, StolaValue *a, StolaValue *b) {
  StolaValue *args[2] = {a, b};
  return stola_call_n(fn, 2, args);
}

StolaValue *stola_struct_get(StolaValue *s, const char *field) {
  if (s->type == STOLA_STRUCT || s->type == STOLA_DICT) {
    StolaDict *d = s->type == STOLA_STRUCT ? &s->as.struct_val.fields
                                           : &s->as.dict_val;
    StolaDictEntry *e = dict_find(d, field);
    if (e)
      return e->value;
    if (strcmp(field, "length") == 0 && s->type == STOLA_DICT)
      return stola_new_int(d->count);
    if (s->type == STOLA_STRUCT) {
      if (find_method(s->as.struct_val.type_name, field))
        stola_raise("AttributeError: '%s.%s' is a method; call it as "
                    "obj.%s(...)",
                    s->as.struct_val.type_name, field, field);
      stola_raise("AttributeError: '%s' object has no attribute '%s'",
                  s->as.struct_val.type_name, field);
    }
    return &stola_null_value;
  }
  if ((s->type == STOLA_STRING || s->type == STOLA_ARRAY) &&
      strcmp(field, "length") == 0)
    return stola_new_int(stola_len_i(s));
  stola_raise("AttributeError: %s has no attribute '%s'", stola_type_name(s),
              field);
  return &stola_null_value;
}

void stola_struct_set(StolaValue *s, const char *field, StolaValue *val) {
  if (s->type == STOLA_STRUCT) {
    dict_put(&s->as.struct_val.fields, field, val);
    return;
  }
  if (s->type == STOLA_DICT) {
    dict_put(&s->as.dict_val, field, val);
    return;
  }
  stola_raise("AttributeError: cannot set attribute '%s' on %s", field,
              stola_type_name(s));
}

// ---- builtin methods on strings / arrays / dicts ----

static StolaValue *arg_or(int64_t argc, StolaValue **args, int i,
                          StolaValue *dflt) {
  return i < argc ? args[i] : dflt;
}

static void need_args(const char *name, int64_t argc, int lo, int hi) {
  if (argc < lo || argc > hi)
    stola_raise("TypeError: %s() takes %d-%d arguments (%d given)", name, lo,
                hi, (int)argc);
}

static StolaValue *string_method(StolaValue *s, const char *m, int64_t argc,
                                 StolaValue **a) {
  const char *str = s->as.str_val;
  if (!strcmp(m, "upper") || !strcmp(m, "uppercase"))
    return stola_uppercase(s);
  if (!strcmp(m, "lower") || !strcmp(m, "lowercase"))
    return stola_lowercase(s);
  if (!strcmp(m, "strip") || !strcmp(m, "trim"))
    return stola_string_trim(s);
  if (!strcmp(m, "lstrip") || !strcmp(m, "rstrip")) {
    const char *b = str, *e = str + strlen(str);
    if (m[0] == 'l')
      while (*b && isspace((unsigned char)*b)) b++;
    else
      while (e > b && isspace((unsigned char)e[-1])) e--;
    return stola_new_string_len(b, e - b);
  }
  if (!strcmp(m, "split")) {
    if (argc == 0) { // split on whitespace
      StolaValue *r = stola_new_array();
      const char *p = str;
      while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        const char *b = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (p > b)
          stola_push(r, stola_new_string_len(b, p - b));
      }
      return r;
    }
    return stola_string_split(s, a[0]);
  }
  if (!strcmp(m, "replace")) {
    need_args(m, argc, 2, 2);
    return stola_string_replace(s, a[0], a[1]);
  }
  if (!strcmp(m, "contains") || !strcmp(m, "includes")) {
    need_args(m, argc, 1, 1);
    return stola_string_contains(s, a[0]);
  }
  if (!strcmp(m, "startswith") || !strcmp(m, "starts_with")) {
    need_args(m, argc, 1, 1);
    return stola_string_starts_with(s, a[0]);
  }
  if (!strcmp(m, "endswith") || !strcmp(m, "ends_with")) {
    need_args(m, argc, 1, 1);
    return stola_string_ends_with(s, a[0]);
  }
  if (!strcmp(m, "find") || !strcmp(m, "index_of") || !strcmp(m, "index")) {
    need_args(m, argc, 1, 1);
    return stola_string_index_of(s, a[0]);
  }
  if (!strcmp(m, "substring")) {
    need_args(m, argc, 1, 2);
    return stola_slice(s, a[0], arg_or(argc, a, 1, &stola_null_value));
  }
  if (!strcmp(m, "len") || !strcmp(m, "length") || !strcmp(m, "size"))
    return stola_new_int((int64_t)strlen(str));
  if (!strcmp(m, "join")) {
    need_args(m, argc, 1, 1);
    return stola_join(a[0], s);
  }
  if (!strcmp(m, "repeat")) {
    need_args(m, argc, 1, 1);
    return repeat_seq(s, stola_to_int64(a[0]));
  }
  if (!strcmp(m, "count")) {
    need_args(m, argc, 1, 1);
    if (a[0]->type != STOLA_STRING || !a[0]->as.str_val[0])
      return stola_new_int(0);
    int64_t c = 0;
    size_t l = strlen(a[0]->as.str_val);
    for (const char *p = strstr(str, a[0]->as.str_val); p;
         p = strstr(p + l, a[0]->as.str_val))
      c++;
    return stola_new_int(c);
  }
  if (!strcmp(m, "chars")) {
    StolaValue *r = stola_new_array_cap((int64_t)strlen(str));
    for (const char *p = str; *p; p++)
      stola_push(r, stola_new_string_len(p, 1));
    return r;
  }
  if (!strcmp(m, "isdigit") || !strcmp(m, "is_digit") ||
      !strcmp(m, "isalpha") || !strcmp(m, "is_alpha") ||
      !strcmp(m, "isspace") || !strcmp(m, "is_space")) {
    if (!*str)
      return &stola_false_value;
    for (const char *p = str; *p; p++) {
      unsigned char c = (unsigned char)*p;
      int ok = strstr(m, "digit") ? isdigit(c)
               : strstr(m, "alpha") ? isalpha(c)
                                    : isspace(c);
      if (!ok)
        return &stola_false_value;
    }
    return &stola_true_value;
  }
  if (!strcmp(m, "to_int"))
    return stola_new_int(stola_to_int_parse(s));
  if (!strcmp(m, "to_number"))
    return stola_to_number(s);
  if (!strcmp(m, "format")) {
    // "{} y {}".format(a, b)
    SBuf b = {0};
    int k = 0;
    for (const char *p = str; *p; p++) {
      if (p[0] == '{' && p[1] == '}') {
        if (k < argc) {
          char *t = stola_value_to_cstr(a[k++]);
          sb_puts(&b, t);
          free(t);
        }
        p++;
      } else {
        sb_putn(&b, p, 1);
      }
    }
    return stola_new_string_owned(sb_take(&b));
  }
  stola_raise("AttributeError: string has no method '%s'", m);
  return &stola_null_value;
}

static int cmp_values_qsort(const void *x, const void *y) {
  return values_order(*(StolaValue *const *)x, *(StolaValue *const *)y);
}

static StolaValue *sort_key_fn;
static int cmp_by_key(const void *x, const void *y) {
  StolaValue *a = call1(sort_key_fn, *(StolaValue *const *)x);
  StolaValue *b = call1(sort_key_fn, *(StolaValue *const *)y);
  return values_order(a, b);
}

static void sort_array(StolaValue *arr, StolaValue *key) {
  if (key && key->type == STOLA_FUNCTION) {
    StolaValue *saved = sort_key_fn;
    sort_key_fn = key;
    qsort(arr->as.array_val.items, arr->as.array_val.count,
          sizeof(StolaValue *), cmp_by_key);
    sort_key_fn = saved;
  } else {
    qsort(arr->as.array_val.items, arr->as.array_val.count,
          sizeof(StolaValue *), cmp_values_qsort);
  }
}

static StolaValue *array_method(StolaValue *arr, const char *m, int64_t argc,
                                StolaValue **a) {
  int n = arr->as.array_val.count;
  StolaValue **it = arr->as.array_val.items;
  if (!strcmp(m, "push") || !strcmp(m, "append")) {
    for (int64_t i = 0; i < argc; i++)
      stola_push(arr, a[i]);
    return &stola_null_value;
  }
  if (!strcmp(m, "pop")) {
    if (argc == 0)
      return stola_pop(arr);
    int64_t i = norm_index(stola_to_int64(a[0]), n, "pop");
    StolaValue *v = it[i];
    memmove(it + i, it + i + 1, sizeof(StolaValue *) * (n - i - 1));
    arr->as.array_val.count--;
    return v;
  }
  if (!strcmp(m, "shift"))
    return stola_shift(arr);
  if (!strcmp(m, "unshift")) {
    need_args(m, argc, 1, 1);
    stola_unshift(arr, a[0]);
    return &stola_null_value;
  }
  if (!strcmp(m, "insert")) {
    need_args(m, argc, 2, 2);
    array_insert(arr, stola_to_int64(a[0]), a[1]);
    return &stola_null_value;
  }
  if (!strcmp(m, "remove")) {
    need_args(m, argc, 1, 1);
    for (int i = 0; i < n; i++)
      if (values_equal(it[i], a[0], 0)) {
        memmove(it + i, it + i + 1, sizeof(StolaValue *) * (n - i - 1));
        arr->as.array_val.count--;
        return &stola_true_value;
      }
    return &stola_false_value;
  }
  if (!strcmp(m, "index_of") || !strcmp(m, "index") || !strcmp(m, "find")) {
    need_args(m, argc, 1, 1);
    for (int i = 0; i < n; i++)
      if (values_equal(it[i], a[0], 0))
        return stola_new_int(i);
    return stola_new_int(-1);
  }
  if (!strcmp(m, "contains") || !strcmp(m, "includes")) {
    need_args(m, argc, 1, 1);
    return stola_bool_value(stola_contains(arr, a[0]));
  }
  if (!strcmp(m, "join"))
    return stola_join(arr, arg_or(argc, a, 0, &stola_null_value));
  if (!strcmp(m, "len") || !strcmp(m, "length") || !strcmp(m, "size"))
    return stola_new_int(n);
  if (!strcmp(m, "reverse")) {
    for (int i = 0; i < n / 2; i++) {
      StolaValue *t = it[i];
      it[i] = it[n - 1 - i];
      it[n - 1 - i] = t;
    }
    return &stola_null_value;
  }
  if (!strcmp(m, "reversed"))
    return stola_reversed(arr);
  if (!strcmp(m, "sort")) {
    sort_array(arr, arg_or(argc, a, 0, NULL));
    return &stola_null_value;
  }
  if (!strcmp(m, "sorted")) {
    StolaValue *c = stola_copy(arr);
    sort_array(c, arg_or(argc, a, 0, NULL));
    return c;
  }
  if (!strcmp(m, "slice")) {
    need_args(m, argc, 1, 2);
    return stola_slice(arr, a[0], arg_or(argc, a, 1, &stola_null_value));
  }
  if (!strcmp(m, "copy"))
    return stola_copy(arr);
  if (!strcmp(m, "clear")) {
    arr->as.array_val.count = 0;
    return &stola_null_value;
  }
  if (!strcmp(m, "extend")) {
    need_args(m, argc, 1, 1);
    if (a[0]->type != STOLA_ARRAY)
      stola_raise("TypeError: extend() expects an array");
    int m2 = a[0]->as.array_val.count;
    for (int i = 0; i < m2; i++)
      stola_push(arr, a[0]->as.array_val.items[i]);
    return &stola_null_value;
  }
  if (!strcmp(m, "map")) {
    need_args(m, argc, 1, 1);
    StolaValue *r = stola_new_array_cap(n);
    for (int i = 0; i < arr->as.array_val.count; i++)
      stola_push(r, call1(a[0], arr->as.array_val.items[i]));
    return r;
  }
  if (!strcmp(m, "filter")) {
    need_args(m, argc, 1, 1);
    StolaValue *r = stola_new_array();
    for (int i = 0; i < arr->as.array_val.count; i++)
      if (stola_is_truthy(call1(a[0], arr->as.array_val.items[i])))
        stola_push(r, arr->as.array_val.items[i]);
    return r;
  }
  if (!strcmp(m, "reduce")) {
    need_args(m, argc, 1, 2);
    int i = 0;
    StolaValue *acc;
    if (argc == 2)
      acc = a[1];
    else {
      if (n == 0)
        stola_raise("TypeError: reduce() of empty array with no initial value");
      acc = it[i++];
    }
    for (; i < arr->as.array_val.count; i++)
      acc = call2(a[0], acc, arr->as.array_val.items[i]);
    return acc;
  }
  if (!strcmp(m, "each") || !strcmp(m, "for_each")) {
    need_args(m, argc, 1, 1);
    for (int i = 0; i < arr->as.array_val.count; i++)
      call1(a[0], arr->as.array_val.items[i]);
    return &stola_null_value;
  }
  if (!strcmp(m, "any") || !strcmp(m, "all")) {
    int want_all = m[1] == 'l';
    for (int i = 0; i < arr->as.array_val.count; i++) {
      StolaValue *x = arr->as.array_val.items[i];
      int t = stola_is_truthy(argc ? call1(a[0], x) : x);
      if (want_all && !t)
        return &stola_false_value;
      if (!want_all && t)
        return &stola_true_value;
    }
    return stola_bool_value(want_all);
  }
  if (!strcmp(m, "sum"))
    return stola_sum(arr);
  if (!strcmp(m, "min") || !strcmp(m, "max")) {
    StolaValue *argv1[1] = {arr};
    return m[1] == 'i' ? stola_min(1, argv1) : stola_max(1, argv1);
  }
  stola_raise("AttributeError: array has no method '%s'", m);
  return &stola_null_value;
}

static StolaValue *dict_method(StolaValue *d, const char *m, int64_t argc,
                               StolaValue **a) {
  if (!strcmp(m, "keys"))
    return stola_keys(d);
  if (!strcmp(m, "values"))
    return stola_values(d);
  if (!strcmp(m, "items")) {
    StolaValue *r = stola_new_array_cap(d->as.dict_val.count);
    for (int i = 0; i < d->as.dict_val.count; i++) {
      StolaValue *pair = stola_new_array_cap(2);
      stola_push(pair, stola_new_string(d->as.dict_val.entries[i].key));
      stola_push(pair, d->as.dict_val.entries[i].value);
      stola_push(r, pair);
    }
    return r;
  }
  if (!strcmp(m, "has") || !strcmp(m, "has_key") || !strcmp(m, "contains")) {
    need_args(m, argc, 1, 1);
    return stola_bool_value(stola_contains(d, a[0]));
  }
  if (!strcmp(m, "get")) {
    need_args(m, argc, 1, 2);
    if (stola_contains(d, a[0]))
      return stola_dict_get(d, a[0]);
    return arg_or(argc, a, 1, &stola_null_value);
  }
  if (!strcmp(m, "set")) {
    need_args(m, argc, 2, 2);
    stola_dict_set(d, a[0], a[1]);
    return &stola_null_value;
  }
  if (!strcmp(m, "remove") || !strcmp(m, "delete") || !strcmp(m, "pop")) {
    need_args(m, argc, 1, 2);
    StolaValue *v = stola_dict_get(d, a[0]);
    int owned;
    char *k = key_of(a[0], &owned);
    int found = dict_remove(&d->as.dict_val, k);
    if (owned)
      free(k);
    if (!found)
      return arg_or(argc, a, 1, &stola_null_value);
    return v;
  }
  if (!strcmp(m, "len") || !strcmp(m, "length") || !strcmp(m, "size"))
    return stola_new_int(d->as.dict_val.count);
  if (!strcmp(m, "copy"))
    return stola_copy(d);
  if (!strcmp(m, "clear")) {
    for (int i = 0; i < d->as.dict_val.count; i++)
      free(d->as.dict_val.entries[i].key);
    d->as.dict_val.count = 0;
    if (d->as.dict_val.index)
      dict_reindex(&d->as.dict_val);
    return &stola_null_value;
  }
  if (!strcmp(m, "update") || !strcmp(m, "merge")) {
    need_args(m, argc, 1, 1);
    if (a[0]->type != STOLA_DICT)
      stola_raise("TypeError: update() expects a dict");
    for (int i = 0; i < a[0]->as.dict_val.count; i++)
      dict_put(&d->as.dict_val, a[0]->as.dict_val.entries[i].key,
               a[0]->as.dict_val.entries[i].value);
    return &stola_null_value;
  }
  // calling a function stored in the dict: d.handler(x)
  StolaDictEntry *e = dict_find(&d->as.dict_val, m);
  if (e && e->value->type == STOLA_FUNCTION)
    return stola_call_n(e->value, (int)argc, a);
  stola_raise("AttributeError: dict has no method '%s'", m);
  return &stola_null_value;
}

StolaValue *stola_call_method(StolaValue *obj, const char *name, int64_t argc,
                              StolaValue **argv_rev) {
  StolaValue *args[12];
  if (argc > 11)
    stola_raise("TypeError: too many arguments to %s()", name);
  for (int64_t i = 0; i < argc; i++)
    args[i] = argv_rev[argc - 1 - i];

  switch (obj->type) {
  case STOLA_STRUCT: {
    ClassMethod *m = find_method(obj->as.struct_val.type_name, name);
    if (m) {
      if (argc > m->nparams)
        stola_raise("TypeError: %s.%s() takes %d argument%s but %d were given",
                    obj->as.struct_val.type_name, name, m->nparams,
                    m->nparams == 1 ? "" : "s", (int)argc);
      StolaValue *a[12];
      a[0] = obj;
      for (int i = 0; i < m->nparams && i < 11; i++)
        a[i + 1] = i < argc ? args[i] : &stola_missing_value;
      return invoke_raw(m->fn, m->nparams + 1, a);
    }
    StolaDictEntry *e = dict_find(&obj->as.struct_val.fields, name);
    if (e && e->value->type == STOLA_FUNCTION)
      return stola_call_n(e->value, (int)argc, args);
    stola_raise("AttributeError: '%s' object has no method '%s'",
                obj->as.struct_val.type_name, name);
    return &stola_null_value;
  }
  case STOLA_STRING:
    return string_method(obj, name, argc, args);
  case STOLA_ARRAY:
    return array_method(obj, name, argc, args);
  case STOLA_DICT:
    return dict_method(obj, name, argc, args);
  case STOLA_FUNCTION:
    if (!strcmp(name, "call"))
      return stola_call_n(obj, (int)argc, args);
    break;
  default:
    break;
  }
  stola_raise("AttributeError: %s has no method '%s'", stola_type_name(obj),
              name);
  return &stola_null_value;
}

// ============================================================
// FFI
// ============================================================

#ifdef _WIN32
static HMODULE loaded_dlls[32];
#else
#include <dlfcn.h>
static void *loaded_dlls[32];
#endif
static int ndlls = 0;

typedef struct {
  const char *name;
  void *ptr;
} CFuncRegistry;
static CFuncRegistry c_functions[256];
static int c_func_count = 0;

void stola_load_dll(const char *dll_name) {
  if (ndlls >= 32)
    return;
#ifdef _WIN32
  HMODULE h = LoadLibraryA(dll_name);
#else
  void *h = dlopen(dll_name, RTLD_LAZY);
#endif
  if (!h) {
    fprintf(stderr, "Warning: could not load native library '%s'\n", dll_name);
    return;
  }
  loaded_dlls[ndlls++] = h;
}

void stola_bind_c_function(const char *name) {
  void *ptr = NULL;
  for (int i = 0; i < ndlls && !ptr; i++) {
#ifdef _WIN32
    ptr = (void *)GetProcAddress(loaded_dlls[i], name);
#else
    ptr = dlsym(loaded_dlls[i], name);
#endif
  }
#ifndef _WIN32
  if (!ptr)
    ptr = dlsym(RTLD_DEFAULT, name);
#endif
  if (!ptr) {
    fprintf(stderr, "Warning: C function '%s' not found\n", name);
    return;
  }
  if (c_func_count < 256) {
    c_functions[c_func_count].name = name;
    c_functions[c_func_count].ptr = ptr;
    c_func_count++;
  }
}

static int64_t ffi_arg(StolaValue *v) {
  switch (v->type) {
  case STOLA_INT: return v->as.int_val;
  case STOLA_BOOL: return v->as.bool_val;
  case STOLA_STRING: return (int64_t)(uintptr_t)v->as.str_val;
  case STOLA_FLOAT: return (int64_t)v->as.float_val;
  case STOLA_NULL: return 0;
  default: return (int64_t)(uintptr_t)v;
  }
}

typedef int64_t (*CF4)(int64_t, int64_t, int64_t, int64_t);

StolaValue *stola_invoke_c_function(const char *name, int64_t argc,
                                    StolaValue **argv_rev) {
  void *ptr = NULL;
  for (int i = 0; i < c_func_count; i++)
    if (c_functions[i].name == name || strcmp(c_functions[i].name, name) == 0) {
      ptr = c_functions[i].ptr;
      break;
    }
  if (!ptr) {
    stola_bind_c_function(name);
    for (int i = 0; i < c_func_count; i++)
      if (strcmp(c_functions[i].name, name) == 0)
        ptr = c_functions[i].ptr;
  }
  if (!ptr)
    stola_raise("NameError: C function '%s' is not bound", name);
  int64_t a[4] = {0, 0, 0, 0};
  for (int64_t i = 0; i < argc && i < 4; i++)
    a[i] = ffi_arg(argv_rev[argc - 1 - i]);
  return stola_new_int(((CF4)ptr)(a[0], a[1], a[2], a[3]));
}

// ============================================================
// Builtin functions
// ============================================================

StolaValue *stola_to_string(StolaValue *val) {
  if (val->type == STOLA_STRING)
    return val;
  return stola_new_string_owned(stola_value_to_cstr(val));
}

StolaValue *stola_to_number(StolaValue *val) {
  if (val->type == STOLA_INT || val->type == STOLA_FLOAT)
    return val;
  if (val->type == STOLA_BOOL)
    return stola_new_int(val->as.bool_val);
  if (val->type == STOLA_STRING) {
    const char *s = val->as.str_val;
    char *end;
    long long iv = strtoll(s, &end, 10);
    while (*end && isspace((unsigned char)*end))
      end++;
    if (*s && !*end)
      return stola_new_int(iv);
    double d = strtod(s, &end);
    while (*end && isspace((unsigned char)*end))
      end++;
    if (*s && !*end)
      return stola_new_float(d);
    stola_raise("ValueError: cannot convert \"%s\" to a number", s);
  }
  if (val->type == STOLA_NULL)
    return stola_new_int(0);
  stola_raise("TypeError: cannot convert %s to a number", stola_type_name(val));
  return &stola_null_value;
}

int64_t stola_to_int_parse(StolaValue *val) {
  if (val->type == STOLA_STRING) {
    const char *s = val->as.str_val;
    while (isspace((unsigned char)*s))
      s++;
    char *end;
    long long iv = strtoll(s, &end, 0);
    while (*end && isspace((unsigned char)*end))
      end++;
    if (*s && !*end)
      return iv;
    double d = strtod(s, &end);
    while (*end && isspace((unsigned char)*end))
      end++;
    if (*s && !*end)
      return (int64_t)d;
    stola_raise("ValueError: invalid literal for int(): \"%s\"", val->as.str_val);
  }
  if (val->type == STOLA_NULL)
    return 0;
  return stola_to_int64(val);
}

StolaValue *stola_to_float(StolaValue *val) {
  if (val->type == STOLA_FLOAT)
    return val;
  if (val->type == STOLA_STRING) {
    char *end;
    double d = strtod(val->as.str_val, &end);
    if (end == val->as.str_val)
      stola_raise("ValueError: could not convert \"%s\" to float",
                  val->as.str_val);
    return stola_new_float(d);
  }
  return stola_new_float(to_double(val));
}

StolaValue *stola_type_of(StolaValue *val) {
  return stola_new_string(stola_type_name(val));
}

StolaValue *stola_range(int64_t argc, StolaValue **argv_rev) {
  int64_t a = 0, b = 0, s = 1;
  if (argc == 1)
    b = stola_to_int64(argv_rev[0]);
  else {
    a = stola_to_int64(argv_rev[argc - 1]);
    b = stola_to_int64(argv_rev[argc - 2]);
    if (argc == 3)
      s = stola_to_int64(argv_rev[0]);
  }
  if (s == 0)
    stola_raise("ValueError: range() step must not be zero");
  int64_t n = s > 0 ? (b > a ? (b - a + s - 1) / s : 0)
                    : (a > b ? (a - b - s - 1) / (-s) : 0);
  StolaValue *r = stola_new_array_cap(n);
  for (int64_t i = a; s > 0 ? i < b : i > b; i += s)
    r->as.array_val.items[r->as.array_val.count++] = stola_new_int(i);
  return r;
}

static StolaValue *minmax(int64_t argc, StolaValue **argv_rev, int want_max) {
  StolaValue **items;
  int64_t n;
  StolaValue *tmp[64];
  if (argc == 1 && argv_rev[0]->type == STOLA_ARRAY) {
    items = argv_rev[0]->as.array_val.items;
    n = argv_rev[0]->as.array_val.count;
  } else {
    n = argc > 64 ? 64 : argc;
    for (int64_t i = 0; i < n; i++)
      tmp[i] = argv_rev[argc - 1 - i];
    items = tmp;
  }
  if (n == 0)
    stola_raise("ValueError: %s() of empty sequence", want_max ? "max" : "min");
  StolaValue *best = items[0];
  for (int64_t i = 1; i < n; i++) {
    int c = values_order(items[i], best);
    if (want_max ? c > 0 : c < 0)
      best = items[i];
  }
  return best;
}

StolaValue *stola_min(int64_t argc, StolaValue **argv_rev) {
  return minmax(argc, argv_rev, 0);
}

StolaValue *stola_max(int64_t argc, StolaValue **argv_rev) {
  return minmax(argc, argv_rev, 1);
}

StolaValue *stola_abs(StolaValue *v) {
  if (v->type == STOLA_FLOAT)
    return stola_new_float(fabs(v->as.float_val));
  int64_t i = stola_to_int64(v);
  return stola_new_int(i < 0 ? -i : i);
}

StolaValue *stola_sum(StolaValue *v) {
  if (v->type != STOLA_ARRAY)
    stola_raise("TypeError: sum() expects an array");
  StolaValue *acc = stola_new_int(0);
  for (int i = 0; i < v->as.array_val.count; i++)
    acc = stola_add(acc, v->as.array_val.items[i]);
  return acc;
}

StolaValue *stola_copy(StolaValue *v) {
  if (v->type == STOLA_ARRAY) {
    StolaValue *r = stola_new_array_cap(v->as.array_val.count);
    memcpy(r->as.array_val.items, v->as.array_val.items,
           sizeof(StolaValue *) * v->as.array_val.count);
    r->as.array_val.count = v->as.array_val.count;
    return r;
  }
  if (v->type == STOLA_DICT) {
    StolaValue *r = stola_new_dict();
    for (int i = 0; i < v->as.dict_val.count; i++)
      dict_put(&r->as.dict_val, v->as.dict_val.entries[i].key,
               v->as.dict_val.entries[i].value);
    return r;
  }
  return v;
}

StolaValue *stola_sorted(StolaValue *v) {
  if (v->type != STOLA_ARRAY)
    stola_raise("TypeError: sorted() expects an array");
  StolaValue *c = stola_copy(v);
  sort_array(c, NULL);
  return c;
}

StolaValue *stola_reversed(StolaValue *v) {
  if (v->type == STOLA_STRING) {
    size_t n = strlen(v->as.str_val);
    char *r = (char *)xmalloc(n + 1);
    for (size_t i = 0; i < n; i++)
      r[i] = v->as.str_val[n - 1 - i];
    r[n] = 0;
    return stola_new_string_owned(r);
  }
  if (v->type != STOLA_ARRAY)
    stola_raise("TypeError: reversed() expects an array or string");
  int n = v->as.array_val.count;
  StolaValue *r = stola_new_array_cap(n);
  for (int i = n - 1; i >= 0; i--)
    r->as.array_val.items[r->as.array_val.count++] = v->as.array_val.items[i];
  return r;
}

StolaValue *stola_keys(StolaValue *v) {
  StolaDict *d = v->type == STOLA_DICT     ? &v->as.dict_val
                 : v->type == STOLA_STRUCT ? &v->as.struct_val.fields
                                           : NULL;
  if (!d)
    stola_raise("TypeError: keys() expects a dict");
  StolaValue *r = stola_new_array_cap(d->count);
  for (int i = 0; i < d->count; i++)
    stola_push(r, stola_new_string(d->entries[i].key));
  return r;
}

StolaValue *stola_values(StolaValue *v) {
  StolaDict *d = v->type == STOLA_DICT     ? &v->as.dict_val
                 : v->type == STOLA_STRUCT ? &v->as.struct_val.fields
                                           : NULL;
  if (!d)
    stola_raise("TypeError: values() expects a dict");
  StolaValue *r = stola_new_array_cap(d->count);
  for (int i = 0; i < d->count; i++)
    stola_push(r, d->entries[i].value);
  return r;
}

StolaValue *stola_chr(StolaValue *v) {
  int64_t c = stola_to_int64(v);
  char buf[5];
  int n = 0;
  if (c < 0x80) {
    buf[n++] = (char)c;
  } else if (c < 0x800) {
    buf[n++] = (char)(0xC0 | (c >> 6));
    buf[n++] = (char)(0x80 | (c & 0x3F));
  } else if (c < 0x10000) {
    buf[n++] = (char)(0xE0 | (c >> 12));
    buf[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
    buf[n++] = (char)(0x80 | (c & 0x3F));
  } else {
    buf[n++] = (char)(0xF0 | (c >> 18));
    buf[n++] = (char)(0x80 | ((c >> 12) & 0x3F));
    buf[n++] = (char)(0x80 | ((c >> 6) & 0x3F));
    buf[n++] = (char)(0x80 | (c & 0x3F));
  }
  return stola_new_string_len(buf, n);
}

int64_t stola_ord(StolaValue *v) {
  if (v->type != STOLA_STRING || !v->as.str_val[0])
    stola_raise("TypeError: ord() expects a non-empty string");
  const unsigned char *s = (const unsigned char *)v->as.str_val;
  if (s[0] < 0x80)
    return s[0];
  if ((s[0] & 0xE0) == 0xC0 && s[1])
    return ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
  if ((s[0] & 0xF0) == 0xE0 && s[1] && s[2])
    return ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
  if (s[1] && s[2] && s[3])
    return ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) |
           ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
  return s[0];
}

StolaValue *stola_hex(StolaValue *v) {
  char buf[32];
  int64_t x = stola_to_int64(v);
  if (x < 0)
    snprintf(buf, sizeof(buf), "-0x%llx", (unsigned long long)-x);
  else
    snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)x);
  return stola_new_string(buf);
}

StolaValue *stola_join(StolaValue *list, StolaValue *sep) {
  if (list->type == STOLA_STRING && sep->type == STOLA_ARRAY) {
    StolaValue *t = list; // join(sep, list) order also accepted
    list = sep;
    sep = t;
  }
  if (list->type != STOLA_ARRAY)
    stola_raise("TypeError: join() expects an array");
  const char *s = sep->type == STOLA_STRING ? sep->as.str_val : "";
  SBuf b = {0};
  sb_puts(&b, "");
  for (int i = 0; i < list->as.array_val.count; i++) {
    if (i)
      sb_puts(&b, s);
    StolaValue *x = list->as.array_val.items[i];
    if (x->type == STOLA_STRING)
      sb_puts(&b, x->as.str_val);
    else {
      char *t = stola_value_to_cstr(x);
      sb_puts(&b, t);
      free(t);
    }
  }
  return stola_new_string_owned(sb_take(&b));
}

StolaValue *stola_input(int64_t argc, StolaValue **argv_rev) {
  if (argc > 0) {
    char *p = stola_value_to_cstr(argv_rev[0]);
    fputs(p, stdout);
    free(p);
  }
  fflush(stdout);
  SBuf b = {0};
  int c, got = 0;
  while ((c = fgetc(stdin)) != EOF) {
    got = 1;
    if (c == '\n')
      break;
    char ch = (char)c;
    sb_putn(&b, &ch, 1);
  }
  if (!got)
    return &stola_null_value;
  char *s = sb_take(&b);
  size_t n = strlen(s);
  if (n && s[n - 1] == '\r')
    s[n - 1] = 0;
  return stola_new_string_owned(s);
}

void stola_exit(StolaValue *code) {
  fflush(stdout);
  exit((int)stola_to_int64(code));
}

void stola_assert(int64_t argc, StolaValue **argv_rev) {
  StolaValue *cond = argv_rev[argc - 1];
  if (stola_is_truthy(cond))
    return;
  if (argc > 1) {
    char *m = stola_value_to_cstr(argv_rev[0]);
    char buf[1024];
    snprintf(buf, sizeof(buf), "AssertionError: %s", m);
    free(m);
    stola_throw(stola_new_string(buf));
  }
  stola_raise("AssertionError");
}

StolaValue *stola_sqrt(StolaValue *v) { return stola_new_float(sqrt(to_double(v))); }

StolaValue *stola_floor(StolaValue *v) {
  if (v->type == STOLA_INT)
    return v;
  return stola_new_int((int64_t)floor(to_double(v)));
}

StolaValue *stola_ceil(StolaValue *v) {
  if (v->type == STOLA_INT)
    return v;
  return stola_new_int((int64_t)ceil(to_double(v)));
}

StolaValue *stola_round(StolaValue *v) {
  if (v->type == STOLA_INT)
    return v;
  return stola_new_int((int64_t)llround(to_double(v)));
}

StolaValue *stola_random(void) {
  return stola_new_float((double)rand() / ((double)RAND_MAX + 1.0));
}

StolaValue *stola_random_int(StolaValue *a, StolaValue *b) {
  int64_t lo = stola_to_int64(a), hi = stola_to_int64(b);
  if (hi < lo)
    stola_raise("ValueError: random_int(a, b) requires a <= b");
  uint64_t span = (uint64_t)(hi - lo) + 1;
  uint64_t r = ((uint64_t)rand() << 31) ^ (uint64_t)rand();
  return stola_new_int(lo + (int64_t)(r % span));
}

StolaValue *stola_format_number(StolaValue *v, StolaValue *decimals) {
  char buf[128];
  snprintf(buf, sizeof(buf), "%.*f", (int)stola_to_int64(decimals),
           to_double(v));
  return stola_new_string(buf);
}

StolaValue *stola_args(void) {
  StolaValue *r = stola_new_array();
  for (int64_t i = 1; i < g_argc; i++)
    stola_push(r, stola_new_string(g_argv[i]));
  return r;
}

StolaValue *stola_clock(void) {
#ifdef _WIN32
  LARGE_INTEGER f, c;
  QueryPerformanceFrequency(&f);
  QueryPerformanceCounter(&c);
  return stola_new_float((double)c.QuadPart / (double)f.QuadPart);
#else
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return stola_new_float(ts.tv_sec + ts.tv_nsec / 1e9);
#endif
}

// ============================================================
// Strings
// ============================================================

static const char *need_str(StolaValue *v, const char *fn) {
  if (v->type != STOLA_STRING)
    stola_raise("TypeError: %s expects a string, got %s", fn,
                stola_type_name(v));
  return v->as.str_val;
}

StolaValue *stola_string_split(StolaValue *str, StolaValue *delim) {
  const char *s = need_str(str, "split()");
  const char *d = need_str(delim, "split()");
  StolaValue *r = stola_new_array();
  size_t dl = strlen(d);
  if (dl == 0) {
    for (const char *p = s; *p; p++)
      stola_push(r, stola_new_string_len(p, 1));
    return r;
  }
  const char *p = s;
  for (;;) {
    const char *q = strstr(p, d);
    if (!q) {
      stola_push(r, stola_new_string(p));
      break;
    }
    stola_push(r, stola_new_string_len(p, q - p));
    p = q + dl;
  }
  return r;
}

StolaValue *stola_string_starts_with(StolaValue *str, StolaValue *prefix) {
  const char *s = need_str(str, "starts_with()");
  const char *p = need_str(prefix, "starts_with()");
  return stola_bool_value(strncmp(s, p, strlen(p)) == 0);
}

StolaValue *stola_string_ends_with(StolaValue *str, StolaValue *suffix) {
  const char *s = need_str(str, "ends_with()");
  const char *x = need_str(suffix, "ends_with()");
  size_t ls = strlen(s), lx = strlen(x);
  return stola_bool_value(lx <= ls && strcmp(s + ls - lx, x) == 0);
}

StolaValue *stola_string_contains(StolaValue *str, StolaValue *sub) {
  return stola_bool_value(strstr(need_str(str, "contains()"),
                                 need_str(sub, "contains()")) != NULL);
}

StolaValue *stola_string_substring(StolaValue *str, StolaValue *start,
                                   StolaValue *end) {
  return stola_slice(str, start, end);
}

StolaValue *stola_string_index_of(StolaValue *str, StolaValue *sub) {
  const char *s = need_str(str, "index_of()");
  const char *f = strstr(s, need_str(sub, "index_of()"));
  return stola_new_int(f ? (int64_t)(f - s) : -1);
}

StolaValue *stola_string_replace(StolaValue *str, StolaValue *from,
                                 StolaValue *to) {
  const char *s = need_str(str, "replace()");
  const char *f = need_str(from, "replace()");
  const char *t = need_str(to, "replace()");
  size_t fl = strlen(f);
  if (fl == 0)
    return str;
  SBuf b = {0};
  sb_puts(&b, "");
  const char *p = s;
  for (;;) {
    const char *q = strstr(p, f);
    if (!q) {
      sb_puts(&b, p);
      break;
    }
    sb_putn(&b, p, q - p);
    sb_puts(&b, t);
    p = q + fl;
  }
  return stola_new_string_owned(sb_take(&b));
}

StolaValue *stola_string_trim(StolaValue *str) {
  const char *s = need_str(str, "trim()");
  const char *e = s + strlen(s);
  while (*s && isspace((unsigned char)*s))
    s++;
  while (e > s && isspace((unsigned char)e[-1]))
    e--;
  return stola_new_string_len(s, e - s);
}

StolaValue *stola_uppercase(StolaValue *str) {
  StolaValue *r = stola_new_string(need_str(str, "upper()"));
  for (char *p = r->as.str_val; *p; p++)
    *p = (char)toupper((unsigned char)*p);
  return r;
}

StolaValue *stola_lowercase(StolaValue *str) {
  StolaValue *r = stola_new_string(need_str(str, "lower()"));
  for (char *p = r->as.str_val; *p; p++)
    *p = (char)tolower((unsigned char)*p);
  return r;
}

// ============================================================
// JSON
// ============================================================

static void json_enc(SBuf *b, StolaValue *v, int depth) {
  if (depth > 128) {
    sb_puts(b, "null");
    return;
  }
  switch (v->type) {
  case STOLA_STRING:
    repr_string(b, v->as.str_val);
    break;
  case STOLA_ARRAY:
    sb_puts(b, "[");
    for (int i = 0; i < v->as.array_val.count; i++) {
      if (i)
        sb_puts(b, ",");
      json_enc(b, v->as.array_val.items[i], depth + 1);
    }
    sb_puts(b, "]");
    break;
  case STOLA_DICT:
  case STOLA_STRUCT: {
    StolaDict *d = v->type == STOLA_DICT ? &v->as.dict_val
                                         : &v->as.struct_val.fields;
    sb_puts(b, "{");
    for (int i = 0; i < d->count; i++) {
      if (i)
        sb_puts(b, ",");
      repr_string(b, d->entries[i].key);
      sb_puts(b, ":");
      json_enc(b, d->entries[i].value, depth + 1);
    }
    sb_puts(b, "}");
    break;
  }
  case STOLA_FUNCTION:
  case STOLA_MISSING:
    sb_puts(b, "null");
    break;
  default:
    repr_into(b, v, 1, depth);
  }
}

StolaValue *stola_json_encode(StolaValue *val) {
  SBuf b = {0};
  sb_puts(&b, "");
  json_enc(&b, val, 0);
  return stola_new_string_owned(sb_take(&b));
}

static StolaValue *json_parse(const char **p);

static void json_ws(const char **p) {
  while (**p && isspace((unsigned char)**p))
    (*p)++;
}

static StolaValue *json_string(const char **p) {
  (*p)++; // opening quote
  SBuf b = {0};
  sb_puts(&b, "");
  while (**p && **p != '"') {
    char c = **p;
    if (c == '\\') {
      (*p)++;
      switch (**p) {
      case 'n': c = '\n'; break;
      case 't': c = '\t'; break;
      case 'r': c = '\r'; break;
      case 'b': c = '\b'; break;
      case 'f': c = '\f'; break;
      case 'u': {
        unsigned code = 0;
        for (int i = 1; i <= 4 && (*p)[i]; i++) {
          char h = (*p)[i];
          code = code * 16 + (isdigit((unsigned char)h) ? h - '0'
                                                       : (tolower(h) - 'a' + 10));
        }
        *p += 4;
        StolaValue tmp = {STOLA_INT, {.int_val = code}};
        StolaValue *ch = stola_chr(&tmp);
        sb_puts(&b, ch->as.str_val);
        (*p)++;
        continue;
      }
      default: c = **p;
      }
    }
    sb_putn(&b, &c, 1);
    (*p)++;
  }
  if (**p == '"')
    (*p)++;
  return stola_new_string_owned(sb_take(&b));
}

static StolaValue *json_parse(const char **p) {
  json_ws(p);
  switch (**p) {
  case '{': {
    (*p)++;
    StolaValue *d = stola_new_dict();
    json_ws(p);
    if (**p == '}') {
      (*p)++;
      return d;
    }
    for (;;) {
      json_ws(p);
      if (**p != '"')
        stola_raise("ValueError: invalid JSON (expected key)");
      StolaValue *k = json_string(p);
      json_ws(p);
      if (**p != ':')
        stola_raise("ValueError: invalid JSON (expected ':')");
      (*p)++;
      StolaValue *v = json_parse(p);
      dict_put(&d->as.dict_val, k->as.str_val, v);
      json_ws(p);
      if (**p == ',') {
        (*p)++;
        continue;
      }
      if (**p == '}') {
        (*p)++;
        break;
      }
      stola_raise("ValueError: invalid JSON object");
    }
    return d;
  }
  case '[': {
    (*p)++;
    StolaValue *a = stola_new_array();
    json_ws(p);
    if (**p == ']') {
      (*p)++;
      return a;
    }
    for (;;) {
      stola_push(a, json_parse(p));
      json_ws(p);
      if (**p == ',') {
        (*p)++;
        continue;
      }
      if (**p == ']') {
        (*p)++;
        break;
      }
      stola_raise("ValueError: invalid JSON array");
    }
    return a;
  }
  case '"':
    return json_string(p);
  case 't':
    if (!strncmp(*p, "true", 4)) {
      *p += 4;
      return &stola_true_value;
    }
    break;
  case 'f':
    if (!strncmp(*p, "false", 5)) {
      *p += 5;
      return &stola_false_value;
    }
    break;
  case 'n':
    if (!strncmp(*p, "null", 4)) {
      *p += 4;
      return &stola_null_value;
    }
    break;
  default: {
    char *end;
    const char *s = *p;
    int is_float = 0;
    for (const char *q = s; *q && (isdigit((unsigned char)*q) || strchr("+-.eE", *q)); q++)
      if (*q == '.' || *q == 'e' || *q == 'E')
        is_float = 1;
    if (is_float) {
      double d = strtod(s, &end);
      if (end != s) {
        *p = end;
        return stola_new_float(d);
      }
    } else {
      long long v = strtoll(s, &end, 10);
      if (end != s) {
        *p = end;
        return stola_new_int(v);
      }
    }
  }
  }
  stola_raise("ValueError: invalid JSON near '%.10s'", *p);
  return &stola_null_value;
}

StolaValue *stola_json_decode(StolaValue *str) {
  const char *p = need_str(str, "json_decode()");
  return json_parse(&p);
}

// ============================================================
// Time / files
// ============================================================

StolaValue *stola_current_time(void) {
#ifdef _WIN32
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
  return stola_new_int((int64_t)(t / 10000 - 11644473600000ULL));
#else
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return stola_new_int((int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
#endif
}

void stola_sleep(StolaValue *ms) {
  int64_t v = stola_to_int64(ms);
#ifdef _WIN32
  Sleep((DWORD)v);
#else
  struct timespec ts = {v / 1000, (v % 1000) * 1000000};
  nanosleep(&ts, NULL);
#endif
}

StolaValue *stola_read_file(StolaValue *path) {
  FILE *f = fopen(need_str(path, "read_file()"), "rb");
  if (!f)
    return &stola_null_value;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  rewind(f);
  char *buf = (char *)xmalloc((size_t)n + 1);
  size_t r = fread(buf, 1, (size_t)n, f);
  buf[r] = 0;
  fclose(f);
  return stola_new_string_owned(buf);
}

static StolaValue *write_mode(StolaValue *path, StolaValue *content,
                              const char *mode) {
  FILE *f = fopen(need_str(path, "write_file()"), mode);
  if (!f)
    return &stola_false_value;
  char *s = content->type == STOLA_STRING ? content->as.str_val
                                          : stola_value_to_cstr(content);
  fputs(s, f);
  if (content->type != STOLA_STRING)
    free(s);
  fclose(f);
  return &stola_true_value;
}

StolaValue *stola_write_file(StolaValue *path, StolaValue *content) {
  return write_mode(path, content, "wb");
}

StolaValue *stola_append_file(StolaValue *path, StolaValue *content) {
  return write_mode(path, content, "ab");
}

StolaValue *stola_file_exists(StolaValue *path) {
  FILE *f = fopen(need_str(path, "file_exists()"), "rb");
  if (f)
    fclose(f);
  return stola_bool_value(f != NULL);
}

// ============================================================
// Raw memory (hosted)
// ============================================================

int64_t stola_memory_read_i(StolaValue *addr) {
  return *(volatile int64_t *)(uintptr_t)stola_to_int64(addr);
}

int64_t stola_memory_write_i(StolaValue *addr, StolaValue *val) {
  int64_t v = stola_to_int64(val);
  *(volatile int64_t *)(uintptr_t)stola_to_int64(addr) = v;
  return v;
}

int64_t stola_memory_write_byte_i(StolaValue *addr, StolaValue *val) {
  int64_t v = stola_to_int64(val);
  *(volatile uint8_t *)(uintptr_t)stola_to_int64(addr) = (uint8_t)v;
  return v;
}
