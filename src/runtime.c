#include "runtime.h"
#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#define stola_strdup _strdup
#else
#define stola_strdup strdup
#endif

// ============================================================
// Value Constructors
// ============================================================

StolaValue *stola_new_int(int64_t val) {
  StolaValue *v = (StolaValue *)malloc(sizeof(StolaValue));
  v->type = STOLA_INT;
  v->as.int_val = val;
  return v;
}

StolaValue *stola_new_bool(int val) {
  StolaValue *v = (StolaValue *)malloc(sizeof(StolaValue));
  v->type = STOLA_BOOL;
  v->as.bool_val = val ? 1 : 0;
  return v;
}

StolaValue *stola_new_string(const char *str) {
  StolaValue *v = (StolaValue *)malloc(sizeof(StolaValue));
  v->type = STOLA_STRING;
  v->as.str_val = stola_strdup(str);
  return v;
}

StolaValue *stola_new_string_owned(char *str) {
  StolaValue *v = (StolaValue *)malloc(sizeof(StolaValue));
  v->type = STOLA_STRING;
  v->as.str_val = str;
  return v;
}

StolaValue *stola_new_null(void) {
  StolaValue *v = (StolaValue *)malloc(sizeof(StolaValue));
  v->type = STOLA_NULL;
  v->as.int_val = 0;
  return v;
}

StolaValue *stola_new_array(void) {
  StolaValue *v = (StolaValue *)malloc(sizeof(StolaValue));
  v->type = STOLA_ARRAY;
  v->as.array_val.items = NULL;
  v->as.array_val.count = 0;
  v->as.array_val.capacity = 0;
  return v;
}

StolaValue *stola_new_dict(void) {
  StolaValue *v = (StolaValue *)malloc(sizeof(StolaValue));
  v->type = STOLA_DICT;
  v->as.dict_val.entries = NULL;
  v->as.dict_val.count = 0;
  v->as.dict_val.capacity = 0;
  return v;
}

StolaValue *stola_new_struct(const char *type_name) {
  StolaValue *v = (StolaValue *)malloc(sizeof(StolaValue));
  v->type = STOLA_STRUCT;
  v->as.struct_val.type_name = stola_strdup(type_name);
  v->as.struct_val.fields.entries = NULL;
  v->as.struct_val.fields.count = 0;
  v->as.struct_val.fields.capacity = 0;
  return v;
}

// ============================================================
// Type Inspection
// ============================================================

int stola_is_truthy(StolaValue *val) {
  if (!val)
    return 0;
  switch (val->type) {
  case STOLA_NULL:
    return 0;
  case STOLA_BOOL:
    return val->as.bool_val;
  case STOLA_INT:
    return val->as.int_val != 0;
  case STOLA_STRING:
    return val->as.str_val && val->as.str_val[0] != '\0';
  case STOLA_ARRAY:
    return val->as.array_val.count > 0;
  case STOLA_DICT:
    return val->as.dict_val.count > 0;
  case STOLA_STRUCT:
    return 1;
  case STOLA_FUNCTION:
    return 1;
  default:
    return 0;
  }
}

const char *stola_type_name(StolaValue *val) {
  if (!val)
    return "null";
  switch (val->type) {
  case STOLA_INT:
    return "int";
  case STOLA_BOOL:
    return "bool";
  case STOLA_STRING:
    return "string";
  case STOLA_ARRAY:
    return "array";
  case STOLA_DICT:
    return "dict";
  case STOLA_STRUCT:
    return val->as.struct_val.type_name;
  case STOLA_FUNCTION:
    return "function";
  case STOLA_NULL:
    return "null";
  default:
    return "unknown";
  }
}

// ============================================================
// Printing
// ============================================================

static void print_value_internal(StolaValue *val, int nested) {
  if (!val) {
    printf("null");
    return;
  }
  switch (val->type) {
  case STOLA_INT:
    printf("%lld", (long long)val->as.int_val);
    break;
  case STOLA_BOOL:
    printf("%s", val->as.bool_val ? "true" : "false");
    break;
  case STOLA_STRING:
    if (nested)
      printf("\"");
    printf("%s", val->as.str_val ? val->as.str_val : "");
    if (nested)
      printf("\"");
    break;
  case STOLA_ARRAY:
    printf("[");
    for (int i = 0; i < val->as.array_val.count; i++) {
      if (i > 0)
        printf(", ");
      print_value_internal(val->as.array_val.items[i], 1);
    }
    printf("]");
    break;
  case STOLA_DICT:
    printf("{");
    for (int i = 0; i < val->as.dict_val.count; i++) {
      if (i > 0)
        printf(", ");
      printf("%s: ", val->as.dict_val.entries[i].key);
      print_value_internal(val->as.dict_val.entries[i].value, 1);
    }
    printf("}");
    break;
  case STOLA_STRUCT:
    printf("%s{", val->as.struct_val.type_name);
    for (int i = 0; i < val->as.struct_val.fields.count; i++) {
      if (i > 0)
        printf(", ");
      printf("%s: ", val->as.struct_val.fields.entries[i].key);
      print_value_internal(val->as.struct_val.fields.entries[i].value, 1);
    }
    printf("}");
    break;
  case STOLA_NULL:
    printf("null");
    break;
  case STOLA_FUNCTION:
    printf("<function>");
    break;
  }
}

void stola_print_value(StolaValue *val) {
  print_value_internal(val, 0);
  printf("\n");
  fflush(stdout);
}

// ============================================================
// Dynamic Arithmetic
// ============================================================

static int64_t val_to_int(StolaValue *v) {
  if (!v)
    return 0;
  if (v->type == STOLA_INT)
    return v->as.int_val;
  if (v->type == STOLA_BOOL)
    return v->as.bool_val;
  return 0;
}

StolaValue *stola_add(StolaValue *a, StolaValue *b) {
  if (!a || !b)
    return stola_new_null();
  // String + anything = string concatenation
  if (a->type == STOLA_STRING || b->type == STOLA_STRING) {
    return stola_string_concat(a, b);
  }
  return stola_new_int(val_to_int(a) + val_to_int(b));
}

StolaValue *stola_sub(StolaValue *a, StolaValue *b) {
  return stola_new_int(val_to_int(a) - val_to_int(b));
}

StolaValue *stola_mul(StolaValue *a, StolaValue *b) {
  return stola_new_int(val_to_int(a) * val_to_int(b));
}

StolaValue *stola_div(StolaValue *a, StolaValue *b) {
  int64_t divisor = val_to_int(b);
  if (divisor == 0) {
    fprintf(stderr, "Runtime error: division by zero\n");
    exit(1);
  }
  return stola_new_int(val_to_int(a) / divisor);
}

StolaValue *stola_mod(StolaValue *a, StolaValue *b) {
  int64_t divisor = val_to_int(b);
  if (divisor == 0) {
    fprintf(stderr, "Runtime error: modulo by zero\n");
    exit(1);
  }
  return stola_new_int(val_to_int(a) % divisor);
}

StolaValue *stola_neg(StolaValue *a) { return stola_new_int(-val_to_int(a)); }

// ============================================================
// Dynamic Comparisons
// ============================================================

StolaValue *stola_eq(StolaValue *a, StolaValue *b) {
  if (!a || !b)
    return stola_new_bool(a == b);
  if (a->type != b->type)
    return stola_new_bool(0);
  switch (a->type) {
  case STOLA_INT:
    return stola_new_bool(a->as.int_val == b->as.int_val);
  case STOLA_BOOL:
    return stola_new_bool(a->as.bool_val == b->as.bool_val);
  case STOLA_STRING:
    return stola_new_bool(strcmp(a->as.str_val, b->as.str_val) == 0);
  case STOLA_NULL:
    return stola_new_bool(1);
  default:
    return stola_new_bool(a == b); // reference equality
  }
}

StolaValue *stola_neq(StolaValue *a, StolaValue *b) {
  StolaValue *eq = stola_eq(a, b);
  eq->as.bool_val = !eq->as.bool_val;
  return eq;
}

StolaValue *stola_lt(StolaValue *a, StolaValue *b) {
  if (a && b && a->type == STOLA_STRING && b->type == STOLA_STRING)
    return stola_new_bool(strcmp(a->as.str_val, b->as.str_val) < 0);
  return stola_new_bool(val_to_int(a) < val_to_int(b));
}

StolaValue *stola_gt(StolaValue *a, StolaValue *b) {
  if (a && b && a->type == STOLA_STRING && b->type == STOLA_STRING)
    return stola_new_bool(strcmp(a->as.str_val, b->as.str_val) > 0);
  return stola_new_bool(val_to_int(a) > val_to_int(b));
}

StolaValue *stola_le(StolaValue *a, StolaValue *b) {
  return stola_new_bool(!stola_is_truthy(stola_gt(a, b)));
}

StolaValue *stola_ge(StolaValue *a, StolaValue *b) {
  return stola_new_bool(!stola_is_truthy(stola_lt(a, b)));
}

StolaValue *stola_and(StolaValue *a, StolaValue *b) {
  return stola_new_bool(stola_is_truthy(a) && stola_is_truthy(b));
}

StolaValue *stola_or(StolaValue *a, StolaValue *b) {
  return stola_new_bool(stola_is_truthy(a) || stola_is_truthy(b));
}

StolaValue *stola_not(StolaValue *a) {
  return stola_new_bool(!stola_is_truthy(a));
}

// ============================================================
// String Operations
// ============================================================

// Helper: convert any value to a C string (caller must free)
static char *value_to_cstr(StolaValue *val) {
  if (!val)
    return stola_strdup("null");
  switch (val->type) {
  case STOLA_STRING:
    return stola_strdup(val->as.str_val ? val->as.str_val : "");
  case STOLA_INT: {
    char buf[64];
    snprintf(buf, sizeof(buf), "%lld", (long long)val->as.int_val);
    return stola_strdup(buf);
  }
  case STOLA_BOOL:
    return stola_strdup(val->as.bool_val ? "true" : "false");
  case STOLA_NULL:
    return stola_strdup("null");
  default:
    return stola_strdup("[object]");
  }
}

StolaValue *stola_string_concat(StolaValue *a, StolaValue *b) {
  char *sa = value_to_cstr(a);
  char *sb = value_to_cstr(b);
  size_t la = strlen(sa), lb = strlen(sb);
  char *result = (char *)malloc(la + lb + 1);
  memcpy(result, sa, la);
  memcpy(result + la, sb, lb);
  result[la + lb] = '\0';
  free(sa);
  free(sb);
  return stola_new_string_owned(result);
}

StolaValue *stola_string_split(StolaValue *str, StolaValue *delim) {
  if (!str || str->type != STOLA_STRING || !delim ||
      delim->type != STOLA_STRING)
    return stola_new_array();
  StolaValue *arr = stola_new_array();
  const char *s = str->as.str_val;
  const char *d = delim->as.str_val;
  size_t dlen = strlen(d);
  if (dlen == 0) {
    stola_push(arr, stola_new_string(s));
    return arr;
  }
  const char *start = s;
  const char *found;
  while ((found = strstr(start, d)) != NULL) {
    size_t len = found - start;
    char *part = (char *)malloc(len + 1);
    memcpy(part, start, len);
    part[len] = '\0';
    stola_push(arr, stola_new_string_owned(part));
    start = found + dlen;
  }
  stola_push(arr, stola_new_string(start));
  return arr;
}

StolaValue *stola_string_starts_with(StolaValue *str, StolaValue *prefix) {
  if (!str || !prefix || str->type != STOLA_STRING ||
      prefix->type != STOLA_STRING)
    return stola_new_bool(0);
  return stola_new_bool(strncmp(str->as.str_val, prefix->as.str_val,
                                strlen(prefix->as.str_val)) == 0);
}

StolaValue *stola_string_ends_with(StolaValue *str, StolaValue *suffix) {
  if (!str || !suffix || str->type != STOLA_STRING ||
      suffix->type != STOLA_STRING)
    return stola_new_bool(0);
  size_t sl = strlen(str->as.str_val), xl = strlen(suffix->as.str_val);
  if (xl > sl)
    return stola_new_bool(0);
  return stola_new_bool(strcmp(str->as.str_val + sl - xl, suffix->as.str_val) ==
                        0);
}

StolaValue *stola_string_contains(StolaValue *str, StolaValue *sub) {
  if (!str || !sub || str->type != STOLA_STRING || sub->type != STOLA_STRING)
    return stola_new_bool(0);
  return stola_new_bool(strstr(str->as.str_val, sub->as.str_val) != NULL);
}

StolaValue *stola_string_substring(StolaValue *str, StolaValue *start,
                                   StolaValue *end) {
  if (!str || str->type != STOLA_STRING)
    return stola_new_string("");
  int64_t s = start ? val_to_int(start) : 0;
  int64_t e = end ? val_to_int(end) : (int64_t)strlen(str->as.str_val);
  int64_t len = (int64_t)strlen(str->as.str_val);
  if (s < 0)
    s = 0;
  if (s > len)
    s = len;
  if (e < s)
    e = s;
  if (e > len)
    e = len;
  int64_t rlen = e - s;
  char *result = (char *)malloc(rlen + 1);
  memcpy(result, str->as.str_val + s, rlen);
  result[rlen] = '\0';
  return stola_new_string_owned(result);
}

StolaValue *stola_string_index_of(StolaValue *str, StolaValue *sub) {
  if (!str || !sub || str->type != STOLA_STRING || sub->type != STOLA_STRING)
    return stola_new_int(-1);
  const char *found = strstr(str->as.str_val, sub->as.str_val);
  if (!found)
    return stola_new_int(-1);
  return stola_new_int((int64_t)(found - str->as.str_val));
}

StolaValue *stola_string_replace(StolaValue *str, StolaValue *from,
                                 StolaValue *to) {
  if (!str || !from || !to || str->type != STOLA_STRING ||
      from->type != STOLA_STRING || to->type != STOLA_STRING)
    return str ? stola_new_string(str->as.str_val) : stola_new_string("");
  const char *s = str->as.str_val;
  const char *f = from->as.str_val;
  const char *t = to->as.str_val;
  size_t fl = strlen(f), tl = strlen(t);
  if (fl == 0)
    return stola_new_string(s);

  // Count occurrences
  int count = 0;
  const char *p = s;
  while ((p = strstr(p, f)) != NULL) {
    count++;
    p += fl;
  }

  size_t new_len = strlen(s) + count * ((int64_t)tl - (int64_t)fl);
  char *result = (char *)malloc(new_len + 1);
  char *dst = result;
  p = s;
  const char *found;
  while ((found = strstr(p, f)) != NULL) {
    size_t chunk = found - p;
    memcpy(dst, p, chunk);
    dst += chunk;
    memcpy(dst, t, tl);
    dst += tl;
    p = found + fl;
  }
  strcpy(dst, p);
  return stola_new_string_owned(result);
}

StolaValue *stola_string_trim(StolaValue *str) {
  if (!str || str->type != STOLA_STRING)
    return stola_new_string("");
  const char *s = str->as.str_val;
  while (*s && isspace((unsigned char)*s))
    s++;
  if (*s == '\0')
    return stola_new_string("");
  const char *e = s + strlen(s) - 1;
  while (e > s && isspace((unsigned char)*e))
    e--;
  size_t len = e - s + 1;
  char *result = (char *)malloc(len + 1);
  memcpy(result, s, len);
  result[len] = '\0';
  return stola_new_string_owned(result);
}

StolaValue *stola_uppercase(StolaValue *str) {
  if (!str || str->type != STOLA_STRING)
    return stola_new_string("");
  char *result = stola_strdup(str->as.str_val);
  for (int i = 0; result[i]; i++)
    result[i] = (char)toupper((unsigned char)result[i]);
  return stola_new_string_owned(result);
}

StolaValue *stola_lowercase(StolaValue *str) {
  if (!str || str->type != STOLA_STRING)
    return stola_new_string("");
  char *result = stola_strdup(str->as.str_val);
  for (int i = 0; result[i]; i++)
    result[i] = (char)tolower((unsigned char)result[i]);
  return stola_new_string_owned(result);
}

StolaValue *stola_to_string(StolaValue *val) {
  char *s = value_to_cstr(val);
  return stola_new_string_owned(s);
}

StolaValue *stola_to_number(StolaValue *val) {
  if (!val)
    return stola_new_int(0);
  if (val->type == STOLA_INT)
    return stola_new_int(val->as.int_val);
  if (val->type == STOLA_BOOL)
    return stola_new_int(val->as.bool_val);
  if (val->type == STOLA_STRING)
    return stola_new_int(atoll(val->as.str_val));
  return stola_new_int(0);
}

// ============================================================
// Array Operations
// ============================================================

StolaValue *stola_length(StolaValue *val) {
  if (!val)
    return stola_new_int(0);
  if (val->type == STOLA_ARRAY)
    return stola_new_int(val->as.array_val.count);
  if (val->type == STOLA_STRING)
    return stola_new_int((int64_t)strlen(val->as.str_val));
  if (val->type == STOLA_DICT)
    return stola_new_int(val->as.dict_val.count);
  return stola_new_int(0);
}

void stola_push(StolaValue *arr, StolaValue *val) {
  if (!arr || arr->type != STOLA_ARRAY)
    return;
  if (arr->as.array_val.count >= arr->as.array_val.capacity) {
    int new_cap =
        arr->as.array_val.capacity == 0 ? 8 : arr->as.array_val.capacity * 2;
    arr->as.array_val.items = (StolaValue **)realloc(
        arr->as.array_val.items, sizeof(StolaValue *) * new_cap);
    arr->as.array_val.capacity = new_cap;
  }
  arr->as.array_val.items[arr->as.array_val.count++] = val;
}

StolaValue *stola_pop(StolaValue *arr) {
  if (!arr || arr->type != STOLA_ARRAY || arr->as.array_val.count == 0)
    return stola_new_null();
  return arr->as.array_val.items[--arr->as.array_val.count];
}

StolaValue *stola_shift(StolaValue *arr) {
  if (!arr || arr->type != STOLA_ARRAY || arr->as.array_val.count == 0)
    return stola_new_null();
  StolaValue *first = arr->as.array_val.items[0];
  for (int i = 0; i < arr->as.array_val.count - 1; i++)
    arr->as.array_val.items[i] = arr->as.array_val.items[i + 1];
  arr->as.array_val.count--;
  return first;
}

void stola_unshift(StolaValue *arr, StolaValue *val) {
  if (!arr || arr->type != STOLA_ARRAY)
    return;
  stola_push(arr, NULL); // grow
  for (int i = arr->as.array_val.count - 1; i > 0; i--)
    arr->as.array_val.items[i] = arr->as.array_val.items[i - 1];
  arr->as.array_val.items[0] = val;
}

StolaValue *stola_array_get(StolaValue *arr, StolaValue *index) {
  if (!arr || arr->type != STOLA_ARRAY)
    return stola_new_null();
  int64_t i = val_to_int(index);
  if (i < 0 || i >= arr->as.array_val.count)
    return stola_new_null();
  return arr->as.array_val.items[i];
}

void stola_array_set(StolaValue *arr, StolaValue *index, StolaValue *val) {
  if (!arr || arr->type != STOLA_ARRAY)
    return;
  int64_t i = val_to_int(index);
  // Grow array if needed
  while (i >= arr->as.array_val.count) {
    stola_push(arr, stola_new_null());
  }
  arr->as.array_val.items[i] = val;
}

// ============================================================
// Dict Operations
// ============================================================

static void dict_ensure_capacity(StolaDict *d) {
  if (d->count >= d->capacity) {
    int new_cap = d->capacity == 0 ? 8 : d->capacity * 2;
    d->entries =
        (StolaDictEntry *)realloc(d->entries, sizeof(StolaDictEntry) * new_cap);
    d->capacity = new_cap;
  }
}

StolaValue *stola_dict_get(StolaValue *dict, StolaValue *key) {
  if (!dict || !key)
    return stola_new_null();
  StolaDict *d = NULL;
  if (dict->type == STOLA_DICT)
    d = &dict->as.dict_val;
  else if (dict->type == STOLA_STRUCT)
    d = &dict->as.struct_val.fields;
  else
    return stola_new_null();

  char *k = value_to_cstr(key);
  for (int i = 0; i < d->count; i++) {
    if (strcmp(d->entries[i].key, k) == 0) {
      free(k);
      return d->entries[i].value;
    }
  }
  free(k);
  return stola_new_null();
}

void stola_dict_set(StolaValue *dict, StolaValue *key, StolaValue *val) {
  if (!dict || !key)
    return;
  StolaDict *d = NULL;
  if (dict->type == STOLA_DICT)
    d = &dict->as.dict_val;
  else if (dict->type == STOLA_STRUCT)
    d = &dict->as.struct_val.fields;
  else
    return;

  char *k = value_to_cstr(key);
  // Check if key exists
  for (int i = 0; i < d->count; i++) {
    if (strcmp(d->entries[i].key, k) == 0) {
      d->entries[i].value = val;
      free(k);
      return;
    }
  }
  // Add new entry
  dict_ensure_capacity(d);
  d->entries[d->count].key = k;
  d->entries[d->count].value = val;
  d->count++;
}

// ============================================================
// Struct Operations
// ============================================================

StolaValue *stola_struct_get(StolaValue *s, const char *field) {
  if (!s)
    return stola_new_null();
  StolaDict *d = NULL;
  if (s->type == STOLA_STRUCT)
    d = &s->as.struct_val.fields;
  else if (s->type == STOLA_DICT)
    d = &s->as.dict_val;
  else
    return stola_new_null();

  for (int i = 0; i < d->count; i++) {
    if (strcmp(d->entries[i].key, field) == 0) {
      return d->entries[i].value;
    }
  }
  return stola_new_null();
}

void stola_struct_set(StolaValue *s, const char *field, StolaValue *val) {
  if (!s)
    return;
  StolaDict *d = NULL;
  if (s->type == STOLA_STRUCT)
    d = &s->as.struct_val.fields;
  else if (s->type == STOLA_DICT)
    d = &s->as.dict_val;
  else
    return;

  // Check if field exists
  for (int i = 0; i < d->count; i++) {
    if (strcmp(d->entries[i].key, field) == 0) {
      d->entries[i].value = val;
      return;
    }
  }
  // Add new field
  dict_ensure_capacity(d);
  d->entries[d->count].key = stola_strdup(field);
  d->entries[d->count].value = val;
  d->count++;
}

// ============================================================
// OOP Method Dispatch Registry
// ============================================================

typedef struct {
  char *class_name;
  char *method_name;
  void *func_ptr;
} StolaMethod;

static StolaMethod method_registry[256];
static int method_count = 0;

void stola_register_method(const char *class_name, const char *method_name,
                           void *func_ptr) {
  if (method_count >= 256)
    return;
  method_registry[method_count].class_name = stola_strdup(class_name);
  method_registry[method_count].method_name = stola_strdup(method_name);
  method_registry[method_count].func_ptr = func_ptr;
  method_count++;
}

StolaValue *stola_invoke_method(StolaValue *obj, const char *method_name,
                                StolaValue *a1, StolaValue *a2) {
  if (!obj || obj->type != STOLA_STRUCT)
    return stola_new_null();
  const char *cname = obj->as.struct_val.type_name;

  for (int i = 0; i < method_count; i++) {
    if (strcmp(method_registry[i].class_name, cname) == 0 &&
        strcmp(method_registry[i].method_name, method_name) == 0) {

      // Method signature: StolaValue* func(StolaValue* this, arg1, arg2...)
      typedef StolaValue *(*MethodFunc)(StolaValue *, StolaValue *,
                                        StolaValue *);
      MethodFunc f = (MethodFunc)method_registry[i].func_ptr;

      return f(obj, a1, a2);
    }
  }

  // Fallback: if there was no method, maybe return null
  return stola_new_null();
}

// ============================================================
// FFI (Foreign Function Interface) Loader
// ============================================================

#ifdef _WIN32
static HMODULE loaded_dlls[32];
#else
#include <dlfcn.h>
static void *loaded_dlls[32];
#endif
static int ddl_count = 0;

typedef struct {
  char *name;
  void *ptr;
} CFuncRegistry;

static CFuncRegistry c_functions[128];
static int c_func_count = 0;

#ifndef _WIN32
void stola_load_dll(const char *dll_name) {
  if (ddl_count >= 32) return;
  void *handle = dlopen(dll_name, RTLD_LAZY);
  if (handle) {
    loaded_dlls[ddl_count++] = handle;
  } else {
    printf("Runtime Warning: Could not load lib '%s': %s\n", dll_name, dlerror());
  }
}

void stola_bind_c_function(const char *name) {
  if (c_func_count >= 128) return;
  void *ptr = dlsym(RTLD_DEFAULT, name);
  for (int i = 0; i < ddl_count && !ptr; i++)
    ptr = dlsym(loaded_dlls[i], name);
  if (ptr) {
    c_functions[c_func_count].name = stola_strdup(name);
    c_functions[c_func_count].ptr = ptr;
    c_func_count++;
  } else {
    printf("Runtime Warning: C function '%s' not found.\n", name);
  }
}

#endif

// --- Exceptions (Try/Catch/Throw) --- //

typedef struct TryCatchNode {
  int64_t env[10]; // Room for registers: rbx, rbp, r12, r13, r14, r15, rsi,
                   // rdi, rsp, rip
  struct TryCatchNode *prev;
} TryCatchNode;

#ifdef _WIN32
__declspec(thread) TryCatchNode *stola_try_stack = NULL;
__declspec(thread) StolaValue *stola_current_error = NULL;
#else
__thread TryCatchNode *stola_try_stack = NULL;
__thread StolaValue *stola_current_error = NULL;
#endif

// Forward declare the assembly longjmp we will emit in codegen via a function
// pointer
static void (*stola_longjmp_ptr)(int64_t *env) = NULL;

void stola_register_longjmp(void *ptr) { stola_longjmp_ptr = ptr; }

int64_t *stola_push_try() {
  TryCatchNode *node = malloc(sizeof(TryCatchNode));
  node->prev = stola_try_stack;
  stola_try_stack = node;
  return node->env;
}

void stola_pop_try() {
  if (stola_try_stack) {
    TryCatchNode *node = stola_try_stack;
    stola_try_stack = node->prev;
    free(node);
  }
}

void stola_throw(StolaValue *err) {
  if (!stola_try_stack) {
    printf("\n[StolasScript FATAL] Unhandled Exception Thrown: ");
    if (err) {
      StolaValue *str = stola_to_string(err);
      printf("%s\n", str->as.str_val);
    } else {
      printf("null\n");
    }
    exit(1);
  }
  stola_current_error = err;
  if (stola_longjmp_ptr) {
    stola_longjmp_ptr(stola_try_stack->env);
  } else {
    printf("\n[StolasScript FATAL] Exception mechanism not initialized!\n");
    exit(1);
  }
}

StolaValue *stola_get_error() {
  return stola_current_error ? stola_current_error : stola_new_null();
}

#ifdef _WIN32
void stola_load_dll(const char *dll_name) {
  if (ddl_count >= 32)
    return;
  HMODULE mod = LoadLibraryA(dll_name);
  if (mod) {
    loaded_dlls[ddl_count++] = mod;
  } else {
    printf("Runtime Warning: Could not load DLL '%s'\n", dll_name);
  }
}

void stola_bind_c_function(const char *name) {
  if (c_func_count >= 128)
    return;

  void *ptr = NULL;
  // Check main exe module first
  ptr = GetProcAddress(GetModuleHandleA(NULL), name);

  // Check loaded DLLs
  for (int i = 0; i < ddl_count && !ptr; i++) {
    ptr = GetProcAddress(loaded_dlls[i], name);
  }

  if (ptr) {
    c_functions[c_func_count].name = stola_strdup(name);
    c_functions[c_func_count].ptr = ptr;
    c_func_count++;
  } else {
    printf("Runtime Warning: C function '%s' not found in loaded memory.\n",
           name);
  }
}
#endif

static int64_t val_to_int_or_ptr(StolaValue *v) {
  if (!v)
    return 0;
  if (v->type == STOLA_INT)
    return v->as.int_val;
  if (v->type == STOLA_STRING)
    return (int64_t)v->as.str_val;
  if (v->type == STOLA_BOOL)
    return v->as.bool_val;
  return 0; // fallback or fail
}

StolaValue *stola_invoke_c_function(const char *name, StolaValue *a1,
                                    StolaValue *a2, StolaValue *a3,
                                    StolaValue *a4) {
  void *ptr = NULL;
  for (int i = 0; i < c_func_count; i++) {
    if (strcmp(c_functions[i].name, name) == 0) {
      ptr = c_functions[i].ptr;
      break;
    }
  }

  if (!ptr) {
    printf("Runtime Error: C function '%s' was called but not bound.\n", name);
    return stola_new_null();
  }

  typedef int64_t (*CFunc4)(int64_t, int64_t, int64_t, int64_t);
  CFunc4 func = (CFunc4)ptr;

  int64_t v1 = val_to_int_or_ptr(a1);
  int64_t v2 = val_to_int_or_ptr(a2);
  int64_t v3 = val_to_int_or_ptr(a3);
  int64_t v4 = val_to_int_or_ptr(a4);

  int64_t ret = func(v1, v2, v3, v4);
  return stola_new_int(ret); // Box result dynamically
}

// ============================================================
// JSON (simple recursive implementation)
// ============================================================

static void json_encode_internal(StolaValue *val, char **buf, size_t *len,
                                 size_t *cap) {
#define JAPPEND(s)                                                             \
  do {                                                                         \
    size_t sl = strlen(s);                                                     \
    while (*len + sl >= *cap) {                                                \
      *cap *= 2;                                                               \
      *buf = (char *)realloc(*buf, *cap);                                      \
    }                                                                          \
    memcpy(*buf + *len, s, sl);                                                \
    *len += sl;                                                                \
  } while (0)

  if (!val) {
    JAPPEND("null");
    return;
  }
  switch (val->type) {
  case STOLA_NULL:
    JAPPEND("null");
    break;
  case STOLA_BOOL:
    JAPPEND(val->as.bool_val ? "true" : "false");
    break;
  case STOLA_INT: {
    char num[64];
    snprintf(num, sizeof(num), "%lld", (long long)val->as.int_val);
    JAPPEND(num);
    break;
  }
  case STOLA_STRING: {
    JAPPEND("\"");
    // Escape special chars
    const char *s = val->as.str_val;
    for (; *s; s++) {
      char esc[3] = {0};
      if (*s == '"') {
        JAPPEND("\\\"");
      } else if (*s == '\\') {
        JAPPEND("\\\\");
      } else if (*s == '\n') {
        JAPPEND("\\n");
      } else if (*s == '\r') {
        JAPPEND("\\r");
      } else if (*s == '\t') {
        JAPPEND("\\t");
      } else {
        esc[0] = *s;
        JAPPEND(esc);
      }
    }
    JAPPEND("\"");
    break;
  }
  case STOLA_ARRAY: {
    JAPPEND("[");
    for (int i = 0; i < val->as.array_val.count; i++) {
      if (i > 0)
        JAPPEND(",");
      json_encode_internal(val->as.array_val.items[i], buf, len, cap);
    }
    JAPPEND("]");
    break;
  }
  case STOLA_DICT:
  case STOLA_STRUCT: {
    StolaDict *d = (val->type == STOLA_DICT) ? &val->as.dict_val
                                             : &val->as.struct_val.fields;
    JAPPEND("{");
    for (int i = 0; i < d->count; i++) {
      if (i > 0)
        JAPPEND(",");
      JAPPEND("\"");
      JAPPEND(d->entries[i].key);
      JAPPEND("\":");
      json_encode_internal(d->entries[i].value, buf, len, cap);
    }
    JAPPEND("}");
    break;
  }
  default:
    JAPPEND("null");
    break;
  }
#undef JAPPEND
}

StolaValue *stola_json_encode(StolaValue *val) {
  size_t cap = 256, len = 0;
  char *buf = (char *)malloc(cap);
  json_encode_internal(val, &buf, &len, &cap);
  buf[len] = '\0';
  return stola_new_string_owned(buf);
}

// Simple JSON decoder (handles objects, arrays, strings, numbers, bools,
// null)
static StolaValue *json_parse_value(const char **p);

static void json_skip_ws(const char **p) {
  while (**p && isspace((unsigned char)**p))
    (*p)++;
}

static StolaValue *json_parse_string(const char **p) {
  (*p)++; // skip "
  const char *start = *p;
  // For simplicity, no escape handling in decode
  while (**p && **p != '"')
    (*p)++;
  size_t len = *p - start;
  char *s = (char *)malloc(len + 1);
  memcpy(s, start, len);
  s[len] = '\0';
  if (**p == '"')
    (*p)++;
  return stola_new_string_owned(s);
}

static StolaValue *json_parse_number(const char **p) {
  int64_t val = 0;
  int neg = 0;
  if (**p == '-') {
    neg = 1;
    (*p)++;
  }
  while (**p >= '0' && **p <= '9') {
    val = val * 10 + (**p - '0');
    (*p)++;
  }
  // Skip decimal part (we only support ints)
  if (**p == '.') {
    (*p)++;
    while (**p >= '0' && **p <= '9')
      (*p)++;
  }
  return stola_new_int(neg ? -val : val);
}

static StolaValue *json_parse_value(const char **p) {
  json_skip_ws(p);
  if (**p == '"')
    return json_parse_string(p);
  if (**p == '{') {
    (*p)++;
    StolaValue *d = stola_new_dict();
    json_skip_ws(p);
    if (**p == '}') {
      (*p)++;
      return d;
    }
    while (1) {
      json_skip_ws(p);
      StolaValue *key = json_parse_string(p);
      json_skip_ws(p);
      if (**p == ':')
        (*p)++;
      StolaValue *val = json_parse_value(p);
      stola_dict_set(d, key, val);
      json_skip_ws(p);
      if (**p == ',') {
        (*p)++;
        continue;
      }
      if (**p == '}') {
        (*p)++;
        break;
      }
      break;
    }
    return d;
  }
  if (**p == '[') {
    (*p)++;
    StolaValue *a = stola_new_array();
    json_skip_ws(p);
    if (**p == ']') {
      (*p)++;
      return a;
    }
    while (1) {
      StolaValue *val = json_parse_value(p);
      stola_push(a, val);
      json_skip_ws(p);
      if (**p == ',') {
        (*p)++;
        continue;
      }
      if (**p == ']') {
        (*p)++;
        break;
      }
      break;
    }
    return a;
  }
  if (strncmp(*p, "true", 4) == 0) {
    *p += 4;
    return stola_new_bool(1);
  }
  if (strncmp(*p, "false", 5) == 0) {
    *p += 5;
    return stola_new_bool(0);
  }
  if (strncmp(*p, "null", 4) == 0) {
    *p += 4;
    return stola_new_null();
  }
  if (**p == '-' || (**p >= '0' && **p <= '9'))
    return json_parse_number(p);
  return stola_new_null();
}

StolaValue *stola_json_decode(StolaValue *str) {
  if (!str || str->type != STOLA_STRING)
    return stola_new_null();
  const char *p = str->as.str_val;
  return json_parse_value(&p);
}

// ============================================================
// Time / System
// ============================================================

StolaValue *stola_current_time(void) {
  return stola_new_int((int64_t)time(NULL));
}

void stola_sleep(StolaValue *seconds) {
  if (!seconds)
    return;
  int64_t ms = val_to_int(seconds) * 1000;
  if (ms <= 0)
    return;
#ifdef _WIN32
  extern void __stdcall Sleep(unsigned long dwMilliseconds);
  Sleep((unsigned long)ms);
#else
  struct timespec ts;
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = (ms % 1000) * 1000000;
  nanosleep(&ts, NULL);
#endif
}

// ============================================================
// Math
// ============================================================

static int rand_seeded = 0;

StolaValue *stola_random(void) {
  if (!rand_seeded) {
    srand((unsigned)time(NULL));
    rand_seeded = 1;
  }
  return stola_new_int(rand());
}

StolaValue *stola_floor(StolaValue *val) {
  return stola_new_int(val_to_int(val)); // ints are already floored
}

StolaValue *stola_ceil(StolaValue *val) {
  return stola_new_int(val_to_int(val));
}

StolaValue *stola_round(StolaValue *val) {
  return stola_new_int(val_to_int(val));
}

// ============================================================
// File I/O
// ============================================================

StolaValue *stola_read_file(StolaValue *path) {
  if (!path || path->type != STOLA_STRING)
    return stola_new_null();
  FILE *f = fopen(path->as.str_val, "rb");
  if (!f)
    return stola_new_null();
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  rewind(f);
  char *buf = (char *)malloc(size + 1);
  size_t rd = fread(buf, 1, size, f);
  buf[rd] = '\0';
  fclose(f);
  return stola_new_string_owned(buf);
}

StolaValue *stola_write_file(StolaValue *path, StolaValue *content) {
  if (!path || path->type != STOLA_STRING || !content ||
      content->type != STOLA_STRING)
    return stola_new_bool(0);
  FILE *f = fopen(path->as.str_val, "w");
  if (!f)
    return stola_new_bool(0);
  fputs(content->as.str_val, f);
  fclose(f);
  return stola_new_bool(1);
}

StolaValue *stola_append_file(StolaValue *path, StolaValue *content) {
  if (!path || path->type != STOLA_STRING || !content ||
      content->type != STOLA_STRING)
    return stola_new_bool(0);
  FILE *f = fopen(path->as.str_val, "a");
  if (!f)
    return stola_new_bool(0);
  fputs(content->as.str_val, f);
  fclose(f);
  return stola_new_bool(1);
}

StolaValue *stola_file_exists(StolaValue *path) {
  if (!path || path->type != STOLA_STRING)
    return stola_new_bool(0);
  FILE *f = fopen(path->as.str_val, "r");
  if (f) {
    fclose(f);
    return stola_new_bool(1);
  }
  return stola_new_bool(0);
}
