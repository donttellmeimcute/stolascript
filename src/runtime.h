#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdint.h>

// ============================================================
// StolasScript runtime (hosted target)
//
// Dynamic values are pointers to StolaValue. Typed code (int/bool)
// never touches this runtime: it runs as raw machine code.
// ============================================================

typedef enum {
  STOLA_INT,
  STOLA_BOOL,
  STOLA_STRING,
  STOLA_ARRAY,
  STOLA_DICT,
  STOLA_STRUCT,
  STOLA_FUNCTION,
  STOLA_NULL,
  STOLA_FLOAT,
  STOLA_MISSING // marker for omitted optional arguments
} StolaType;

typedef struct StolaValue StolaValue;
typedef struct StolaDict StolaDict;

typedef struct {
  char *key;
  StolaValue *value;
} StolaDictEntry;

// Insertion-ordered dictionary with a hash index
struct StolaDict {
  StolaDictEntry *entries;
  int count;
  int capacity;
  int *index;     // open addressing: entry position + 1, 0 = empty
  int index_cap;  // power of two (0 = no index yet)
};

typedef struct {
  const char *type_name; // class / struct name (interned literal)
  StolaDict fields;
} StolaStruct;

typedef struct {
  void *fn;          // machine code entry point
  int arity;         // user-visible parameters
  int ncap;          // captured values (closures), -1 for plain functions
  StolaValue **env;  // captured values
  const char *name;
} StolaFunction;

struct StolaValue {
  StolaType type;
  union {
    int64_t int_val;
    int bool_val;
    char *str_val;
    double float_val;
    struct {
      StolaValue **items;
      int count;
      int capacity;
    } array_val;
    StolaDict dict_val;
    StolaStruct struct_val;
    StolaFunction fn_val;
  } as;
};

// Immortal singletons (referenced directly by generated code)
extern StolaValue stola_null_value;
extern StolaValue stola_true_value;
extern StolaValue stola_false_value;
extern StolaValue stola_missing_value;

// ---- Lifecycle ----
void stola_runtime_init(int64_t argc, char **argv, void *longjmp_fn);
int stola_runtime_exit(void);
void stola_setup_runtime(void);

// ---- Constructors ----
StolaValue *stola_new_int(int64_t val);
StolaValue *stola_new_float(double val);
StolaValue *stola_new_float_bits(int64_t bits);
StolaValue *stola_new_bool(int val);
StolaValue *stola_bool_value(int64_t val);
StolaValue *stola_new_string(const char *str);
StolaValue *stola_new_string_len(const char *str, int64_t len);
StolaValue *stola_new_string_owned(char *str);
StolaValue *stola_new_null(void);
StolaValue *stola_new_array(void);
StolaValue *stola_new_array_cap(int64_t cap);
StolaValue *stola_new_dict(void);
StolaValue *stola_new_struct(const char *type_name);
StolaValue *stola_new_object(const char *class_name);
StolaValue *stola_new_function(void *fn, int64_t arity, const char *name);
StolaValue *stola_new_closure(void *fn, int64_t arity, int64_t ncap);
void stola_closure_set(StolaValue *clo, int64_t i, StolaValue *v);
StolaValue *stola_closure_get(StolaValue *clo, int64_t i);

// ---- Conversions / inspection ----
int stola_is_truthy(StolaValue *val);
int64_t stola_to_int64(StolaValue *val);
const char *stola_type_name(StolaValue *val);
char *stola_value_to_cstr(StolaValue *val); // malloc'ed, caller frees
void stola_print_value(StolaValue *val);
void stola_print_args(int64_t argc, StolaValue **argv_rev);

// ---- Operators ----
StolaValue *stola_add(StolaValue *a, StolaValue *b);
StolaValue *stola_sub(StolaValue *a, StolaValue *b);
StolaValue *stola_mul(StolaValue *a, StolaValue *b);
StolaValue *stola_div(StolaValue *a, StolaValue *b);
StolaValue *stola_mod(StolaValue *a, StolaValue *b);
StolaValue *stola_pow(StolaValue *a, StolaValue *b);
StolaValue *stola_band(StolaValue *a, StolaValue *b);
StolaValue *stola_bor(StolaValue *a, StolaValue *b);
StolaValue *stola_bxor(StolaValue *a, StolaValue *b);
StolaValue *stola_shl(StolaValue *a, StolaValue *b);
StolaValue *stola_shr(StolaValue *a, StolaValue *b);
StolaValue *stola_neg(StolaValue *a);
StolaValue *stola_bnot(StolaValue *a);
int stola_compare(StolaValue *a, StolaValue *b, int64_t op);
int stola_contains(StolaValue *container, StolaValue *item);
void stola_raise_zero_division(void);

// ---- Collections ----
int64_t stola_len_i(StolaValue *val);
StolaValue *stola_length(StolaValue *val);
void stola_push(StolaValue *arr, StolaValue *val);
StolaValue *stola_pop(StolaValue *arr);
StolaValue *stola_shift(StolaValue *arr);
void stola_unshift(StolaValue *arr, StolaValue *val);
StolaValue *stola_getitem(StolaValue *obj, StolaValue *key);
void stola_setitem(StolaValue *obj, StolaValue *key, StolaValue *val);
StolaValue *stola_slice(StolaValue *obj, StolaValue *start, StolaValue *end);
StolaValue *stola_iter_get(StolaValue *it, int64_t idx);
StolaValue *stola_iter_value(StolaValue *it, int64_t idx);
StolaValue *stola_dict_get(StolaValue *dict, StolaValue *key);
void stola_dict_set(StolaValue *dict, StolaValue *key, StolaValue *val);
StolaValue *stola_struct_get(StolaValue *s, const char *field);
void stola_struct_set(StolaValue *s, const char *field, StolaValue *val);

// ---- Functions, classes, exceptions ----
void stola_register_class(const char *name, const char *parent);
void stola_register_method(const char *class_name, const char *method_name,
                           void *func_ptr, int64_t nparams);
StolaValue *stola_call_method(StolaValue *obj, const char *name, int64_t argc,
                              StolaValue **argv_rev);
StolaValue *stola_call_value(StolaValue *fn, int64_t argc,
                             StolaValue **argv_rev);
StolaValue *stola_call_n(StolaValue *fn, int argc, StolaValue **args);
int64_t *stola_push_try(void);
void stola_pop_try(void);
void stola_throw(StolaValue *err);
void stola_raise(const char *fmt, ...);
StolaValue *stola_get_error(void);

// ---- FFI ----
void stola_load_dll(const char *dll_name);
void stola_bind_c_function(const char *name);
StolaValue *stola_invoke_c_function(const char *name, int64_t argc,
                                    StolaValue **argv_rev);

// ---- Builtin functions ----
StolaValue *stola_to_string(StolaValue *val);
StolaValue *stola_to_number(StolaValue *val);
int64_t stola_to_int_parse(StolaValue *val);
StolaValue *stola_to_float(StolaValue *val);
StolaValue *stola_type_of(StolaValue *val);
StolaValue *stola_range(int64_t argc, StolaValue **argv_rev);
StolaValue *stola_min(int64_t argc, StolaValue **argv_rev);
StolaValue *stola_max(int64_t argc, StolaValue **argv_rev);
StolaValue *stola_abs(StolaValue *v);
StolaValue *stola_sum(StolaValue *v);
StolaValue *stola_sorted(StolaValue *v);
StolaValue *stola_reversed(StolaValue *v);
StolaValue *stola_keys(StolaValue *v);
StolaValue *stola_values(StolaValue *v);
StolaValue *stola_chr(StolaValue *v);
int64_t stola_ord(StolaValue *v);
StolaValue *stola_hex(StolaValue *v);
StolaValue *stola_join(StolaValue *list, StolaValue *sep);
StolaValue *stola_input(int64_t argc, StolaValue **argv_rev);
void stola_exit(StolaValue *code);
void stola_assert(int64_t argc, StolaValue **argv_rev);
StolaValue *stola_sqrt(StolaValue *v);
StolaValue *stola_floor(StolaValue *val);
StolaValue *stola_ceil(StolaValue *val);
StolaValue *stola_round(StolaValue *val);
StolaValue *stola_random(void);
StolaValue *stola_random_int(StolaValue *a, StolaValue *b);
StolaValue *stola_copy(StolaValue *v);
StolaValue *stola_format_number(StolaValue *v, StolaValue *decimals);
StolaValue *stola_args(void);
StolaValue *stola_clock(void);

// ---- Strings ----
StolaValue *stola_string_concat(StolaValue *a, StolaValue *b);
StolaValue *stola_string_split(StolaValue *str, StolaValue *delim);
StolaValue *stola_string_starts_with(StolaValue *str, StolaValue *prefix);
StolaValue *stola_string_ends_with(StolaValue *str, StolaValue *suffix);
StolaValue *stola_string_contains(StolaValue *str, StolaValue *sub);
StolaValue *stola_string_substring(StolaValue *str, StolaValue *start,
                                   StolaValue *end);
StolaValue *stola_string_index_of(StolaValue *str, StolaValue *sub);
StolaValue *stola_string_replace(StolaValue *str, StolaValue *from,
                                 StolaValue *to);
StolaValue *stola_string_trim(StolaValue *str);
StolaValue *stola_uppercase(StolaValue *str);
StolaValue *stola_lowercase(StolaValue *str);

// ---- JSON / time / files ----
StolaValue *stola_json_encode(StolaValue *val);
StolaValue *stola_json_decode(StolaValue *str);
StolaValue *stola_current_time(void);
void stola_sleep(StolaValue *ms);
StolaValue *stola_read_file(StolaValue *path);
StolaValue *stola_write_file(StolaValue *path, StolaValue *content);
StolaValue *stola_append_file(StolaValue *path, StolaValue *content);
StolaValue *stola_file_exists(StolaValue *path);

// ---- Raw memory (hosted) ----
int64_t stola_memory_read_i(StolaValue *addr);
int64_t stola_memory_write_i(StolaValue *addr, StolaValue *val);
int64_t stola_memory_write_byte_i(StolaValue *addr, StolaValue *val);

// ---- Networking / threads (builtins.c) ----
StolaValue *stola_socket_connect(StolaValue *host, StolaValue *port);
StolaValue *stola_socket_send(StolaValue *fd, StolaValue *data);
StolaValue *stola_socket_receive(StolaValue *fd);
void stola_socket_close(StolaValue *fd);
StolaValue *stola_http_fetch(StolaValue *url);
StolaValue *stola_thread_spawn_value(StolaValue *fn, StolaValue *arg);
StolaValue *stola_thread_join(StolaValue *handle);
StolaValue *stola_mutex_create(void);
StolaValue *stola_mutex_lock(StolaValue *m);
StolaValue *stola_mutex_unlock(StolaValue *m);
StolaValue *stola_ws_connect(StolaValue *url);
StolaValue *stola_ws_send(StolaValue *handle, StolaValue *msg);
StolaValue *stola_ws_receive(StolaValue *handle);
StolaValue *stola_ws_close(StolaValue *handle);
StolaValue *stola_ws_server_create(StolaValue *port);
StolaValue *stola_ws_server_accept(StolaValue *server);
StolaValue *stola_ws_server_close(StolaValue *server);
StolaValue *stola_ws_select(StolaValue *handles, StolaValue *timeout_ms);

#endif // RUNTIME_H
