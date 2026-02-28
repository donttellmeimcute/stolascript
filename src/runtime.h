#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdint.h>

// ============================================================
// StolasScript Tagged Value Runtime
// Every value in StolasScript is a pointer to a StolaValue on the heap.
// Assembly passes StolaValue* in registers (RCX, RDX, R8, R9).
// ============================================================

typedef enum {
  STOLA_INT,
  STOLA_BOOL,
  STOLA_STRING,
  STOLA_ARRAY,
  STOLA_DICT,
  STOLA_STRUCT,
  STOLA_FUNCTION,
  STOLA_NULL
} StolaType;

// Forward declarations
typedef struct StolaValue StolaValue;
typedef struct StolaDict StolaDict;

// Dictionary entry (key-value pair)
typedef struct {
  char *key;
  StolaValue *value;
} StolaDictEntry;

// Dictionary: simple linear array of key-value pairs
struct StolaDict {
  StolaDictEntry *entries;
  int count;
  int capacity;
};

// Struct instance (same as dict but with a type name)
typedef struct {
  char *type_name;
  StolaDict fields;
} StolaStruct;

// The universal value type
struct StolaValue {
  StolaType type;
  union {
    int64_t int_val;
    int bool_val;
    char *str_val;
    struct {
      StolaValue **items;
      int count;
      int capacity;
    } array_val;
    StolaDict dict_val;
    StolaStruct struct_val;
    void *fn_ptr; // function pointer (for closures/callbacks)
  } as;
};

// ============================================================
// Value Constructors — return heap-allocated StolaValue*
// ============================================================
StolaValue *stola_new_int(int64_t val);
StolaValue *stola_new_bool(int val);
StolaValue *stola_new_string(const char *str);
StolaValue *stola_new_string_owned(char *str); // takes ownership, no copy
StolaValue *stola_new_null(void);
StolaValue *stola_new_array(void);
StolaValue *stola_new_dict(void);
StolaValue *stola_new_struct(const char *type_name);

// ============================================================
// Type Inspection
// ============================================================
int stola_is_truthy(StolaValue *val);
const char *stola_type_name(StolaValue *val);

// ============================================================
// Printing
// ============================================================
void stola_print_value(StolaValue *val);

// ============================================================
// Dynamic Arithmetic — dispatches on types
// "plus" on two strings = concatenation; on ints = addition
// ============================================================
StolaValue *stola_add(StolaValue *a, StolaValue *b);
StolaValue *stola_sub(StolaValue *a, StolaValue *b);
StolaValue *stola_mul(StolaValue *a, StolaValue *b);
StolaValue *stola_div(StolaValue *a, StolaValue *b);
StolaValue *stola_mod(StolaValue *a, StolaValue *b);
StolaValue *stola_neg(StolaValue *a);

// Method Dispatching for OOP
void stola_register_method(const char *class_name, const char *method_name,
                           void *func_ptr);
StolaValue *stola_invoke_method(StolaValue *obj, const char *method_name,
                                StolaValue *a1, StolaValue *a2);

// FFI (Foreign Function Interface)
void stola_load_dll(const char *dll_name);
void stola_bind_c_function(const char *name);
StolaValue *stola_invoke_c_function(const char *name, StolaValue *a1,
                                    StolaValue *a2, StolaValue *a3,
                                    StolaValue *a4);

// Try / Catch / Throw Exceptions
int64_t *stola_push_try();
void stola_pop_try();
void stola_throw(StolaValue *err);
StolaValue *stola_get_error();

// ============================================================
// Dynamic Comparisons — return StolaValue* bool
// ============================================================
StolaValue *stola_eq(StolaValue *a, StolaValue *b);
StolaValue *stola_neq(StolaValue *a, StolaValue *b);
StolaValue *stola_lt(StolaValue *a, StolaValue *b);
StolaValue *stola_gt(StolaValue *a, StolaValue *b);
StolaValue *stola_le(StolaValue *a, StolaValue *b);
StolaValue *stola_ge(StolaValue *a, StolaValue *b);
StolaValue *stola_and(StolaValue *a, StolaValue *b);
StolaValue *stola_or(StolaValue *a, StolaValue *b);
StolaValue *stola_not(StolaValue *a);

// ============================================================
// String Operations
// ============================================================
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
StolaValue *stola_to_string(StolaValue *val);
StolaValue *stola_to_number(StolaValue *val);

// ============================================================
// Array Operations
// ============================================================
StolaValue *stola_length(StolaValue *val);
void stola_push(StolaValue *arr, StolaValue *val);
StolaValue *stola_pop(StolaValue *arr);
StolaValue *stola_shift(StolaValue *arr);
void stola_unshift(StolaValue *arr, StolaValue *val);
StolaValue *stola_array_get(StolaValue *arr, StolaValue *index);
void stola_array_set(StolaValue *arr, StolaValue *index, StolaValue *val);StolaValue *stola_getitem(StolaValue *obj, StolaValue *key);
void        stola_setitem(StolaValue *obj, StolaValue *key, StolaValue *val);
// ============================================================
// Dict Operations
// ============================================================
StolaValue *stola_dict_get(StolaValue *dict, StolaValue *key);
void stola_dict_set(StolaValue *dict, StolaValue *key, StolaValue *val);

// ============================================================
// Struct Operations
// ============================================================
StolaValue *stola_struct_get(StolaValue *s, const char *field);
void stola_struct_set(StolaValue *s, const char *field, StolaValue *val);

// ============================================================
// Socket Operations (WinSock2)
// ============================================================
StolaValue *stola_socket_connect(StolaValue *host, StolaValue *port);
StolaValue *stola_socket_send(StolaValue *fd, StolaValue *data);
StolaValue *stola_socket_receive(StolaValue *fd);
void stola_socket_close(StolaValue *fd);

// ============================================================
// JSON
// ============================================================
StolaValue *stola_json_encode(StolaValue *val);
StolaValue *stola_json_decode(StolaValue *str);

// ============================================================
// Time / System
// ============================================================
StolaValue *stola_current_time(void);
void stola_sleep(StolaValue *seconds);

// ============================================================
// Math
// ============================================================
StolaValue *stola_random(void);
StolaValue *stola_floor(StolaValue *val);
StolaValue *stola_ceil(StolaValue *val);
StolaValue *stola_round(StolaValue *val);

// ============================================================
// File I/O
// ============================================================
StolaValue *stola_read_file(StolaValue *path);
StolaValue *stola_write_file(StolaValue *path, StolaValue *content);
StolaValue *stola_append_file(StolaValue *path, StolaValue *content);
StolaValue *stola_file_exists(StolaValue *path);

// ============================================================
// Raw Memory Access — useful for freestanding / bare-metal mode.
// In hosted mode these call C wrappers; freestanding emits inline asm.
// ============================================================
StolaValue *stola_memory_read(StolaValue *addr);
StolaValue *stola_memory_write(StolaValue *addr, StolaValue *val);
StolaValue *stola_memory_write_byte(StolaValue *addr, StolaValue *val);

// Runtime initialization — installs SIGINT/SIGSEGV handlers on Linux.
void stola_setup_runtime(void);

// ============================================================
// Native HTTP (WinHTTP, supports HTTPS)
// ============================================================
StolaValue *stola_http_fetch(StolaValue *url);

#endif // RUNTIME_H
