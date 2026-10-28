#include "builtin_table.h"
#include <string.h>

#define H(name, cname, mn, mx, ret) {name, cname, mn, mx, 0, ret, 0, 1}
#define HV(name, cname, mn, mx, ret) {name, cname, mn, mx, 1, ret, 0, 1}
#define N(name, mn, mx) {name, NULL, mn, mx, 0, RK_INT, 1, 0}
#define NH(name, cname, mn, mx) {name, cname, mn, mx, 0, RK_INT, 1, 1}

static const BuiltinInfo builtin_table[] = {
    // ---- Core (hosted) ----
    HV("print", "stola_print_args", 0, -1, RK_VOID),
    H("len", "stola_len_i", 1, 1, RK_INT),
    H("length", "stola_len_i", 1, 1, RK_INT),
    H("push", "stola_push", 2, 2, RK_VOID),
    H("append", "stola_push", 2, 2, RK_VOID),
    H("pop", "stola_pop", 1, 1, RK_DYN),
    H("shift", "stola_shift", 1, 1, RK_DYN),
    H("unshift", "stola_unshift", 2, 2, RK_VOID),
    H("str", "stola_to_string", 1, 1, RK_DYN),
    H("to_string", "stola_to_string", 1, 1, RK_DYN),
    H("to_number", "stola_to_number", 1, 1, RK_DYN),
    H("int", "stola_to_int_parse", 1, 1, RK_INT),
    H("float", "stola_to_float", 1, 1, RK_DYN),
    H("bool", "stola_is_truthy", 1, 1, RK_BOOL),
    H("type", "stola_type_of", 1, 1, RK_DYN),
    H("type_of", "stola_type_of", 1, 1, RK_DYN),
    HV("range", "stola_range", 1, 3, RK_DYN),
    HV("min", "stola_min", 1, -1, RK_DYN),
    HV("max", "stola_max", 1, -1, RK_DYN),
    H("abs", "stola_abs", 1, 1, RK_DYN),
    H("sum", "stola_sum", 1, 1, RK_DYN),
    H("sorted", "stola_sorted", 1, 1, RK_DYN),
    H("reversed", "stola_reversed", 1, 1, RK_DYN),
    H("keys", "stola_keys", 1, 1, RK_DYN),
    H("values", "stola_values", 1, 1, RK_DYN),
    H("has_key", "stola_contains", 2, 2, RK_BOOL),
    H("contains", "stola_contains", 2, 2, RK_BOOL),
    H("chr", "stola_chr", 1, 1, RK_DYN),
    H("ord", "stola_ord", 1, 1, RK_INT),
    H("hex", "stola_hex", 1, 1, RK_DYN),
    H("join", "stola_join", 2, 2, RK_DYN),
    HV("input", "stola_input", 0, 1, RK_DYN),
    H("exit", "stola_exit", 1, 1, RK_VOID),
    HV("assert", "stola_assert", 1, 2, RK_VOID),
    H("sqrt", "stola_sqrt", 1, 1, RK_DYN),
    H("pow", "stola_pow", 2, 2, RK_DYN),
    H("floor", "stola_floor", 1, 1, RK_DYN),
    H("ceil", "stola_ceil", 1, 1, RK_DYN),
    H("round", "stola_round", 1, 1, RK_DYN),
    H("random", "stola_random", 0, 0, RK_DYN),
    H("random_int", "stola_random_int", 2, 2, RK_DYN),
    H("copy", "stola_copy", 1, 1, RK_DYN),
    H("format_number", "stola_format_number", 2, 2, RK_DYN),
    H("args", "stola_args", 0, 0, RK_DYN),
    H("clock", "stola_clock", 0, 0, RK_DYN),

    // ---- Strings (legacy names) ----
    H("string_split", "stola_string_split", 2, 2, RK_DYN),
    H("split", "stola_string_split", 2, 2, RK_DYN),
    H("string_starts_with", "stola_string_starts_with", 2, 2, RK_DYN),
    H("string_ends_with", "stola_string_ends_with", 2, 2, RK_DYN),
    H("string_contains", "stola_string_contains", 2, 2, RK_DYN),
    H("string_substring", "stola_string_substring", 3, 3, RK_DYN),
    H("substring", "stola_string_substring", 3, 3, RK_DYN),
    H("string_index_of", "stola_string_index_of", 2, 2, RK_DYN),
    H("string_replace", "stola_string_replace", 3, 3, RK_DYN),
    H("replace", "stola_string_replace", 3, 3, RK_DYN),
    H("string_trim", "stola_string_trim", 1, 1, RK_DYN),
    H("trim", "stola_string_trim", 1, 1, RK_DYN),
    H("uppercase", "stola_uppercase", 1, 1, RK_DYN),
    H("lowercase", "stola_lowercase", 1, 1, RK_DYN),

    // ---- Networking / IO / system ----
    H("socket_connect", "stola_socket_connect", 2, 2, RK_DYN),
    H("socket_send", "stola_socket_send", 2, 2, RK_DYN),
    H("socket_receive", "stola_socket_receive", 1, 1, RK_DYN),
    H("socket_close", "stola_socket_close", 1, 1, RK_VOID),
    H("ws_connect", "stola_ws_connect", 1, 1, RK_DYN),
    H("ws_send", "stola_ws_send", 2, 2, RK_DYN),
    H("ws_receive", "stola_ws_receive", 1, 1, RK_DYN),
    H("ws_close", "stola_ws_close", 1, 1, RK_DYN),
    H("ws_server_create", "stola_ws_server_create", 1, 1, RK_DYN),
    H("ws_server_accept", "stola_ws_server_accept", 1, 1, RK_DYN),
    H("ws_server_close", "stola_ws_server_close", 1, 1, RK_DYN),
    H("ws_select", "stola_ws_select", 2, 2, RK_DYN),
    H("json_encode", "stola_json_encode", 1, 1, RK_DYN),
    H("json_decode", "stola_json_decode", 1, 1, RK_DYN),
    H("current_time", "stola_current_time", 0, 0, RK_DYN),
    H("sleep", "stola_sleep", 1, 1, RK_VOID),
    H("read_file", "stola_read_file", 1, 1, RK_DYN),
    H("write_file", "stola_write_file", 2, 2, RK_DYN),
    H("append_file", "stola_append_file", 2, 2, RK_DYN),
    H("file_exists", "stola_file_exists", 1, 1, RK_DYN),
    H("http_fetch", "stola_http_fetch", 1, 1, RK_DYN),
    H("thread_spawn", "stola_thread_spawn_value", 2, 2, RK_DYN),
    H("thread_join", "stola_thread_join", 1, 1, RK_DYN),
    H("mutex_create", "stola_mutex_create", 0, 0, RK_DYN),
    H("mutex_lock", "stola_mutex_lock", 1, 1, RK_DYN),
    H("mutex_unlock", "stola_mutex_unlock", 1, 1, RK_DYN),

    // ---- Raw memory (hosted wrappers; inline in native targets) ----
    NH("memory_read", "stola_memory_read_i", 1, 1),
    NH("memory_write", "stola_memory_write_i", 2, 2),
    NH("memory_write_byte", "stola_memory_write_byte_i", 2, 2),

    // ---- Native-only (inline machine code) ----
    N("load8", 1, 1),  N("load16", 1, 1), N("load32", 1, 1), N("load64", 1, 1),
    N("store8", 2, 2), N("store16", 2, 2), N("store32", 2, 2),
    N("store64", 2, 2),
    N("outb", 2, 2),   N("outw", 2, 2),   N("outl", 2, 2),
    N("inb", 1, 1),    N("inw", 1, 1),    N("inl", 1, 1),
    N("cli", 0, 0),    N("sti", 0, 0),    N("hlt", 0, 0),
    N("pause", 0, 0),  N("lidt", 1, 1),   N("lgdt", 1, 1),
    N("ltr", 1, 1),    N("invlpg", 1, 1), N("rdtsc", 0, 0),
    N("read_cr0", 0, 0), N("read_cr2", 0, 0), N("read_cr3", 0, 0),
    N("read_cr4", 0, 0), N("write_cr0", 1, 1), N("write_cr3", 1, 1),
    N("write_cr4", 1, 1), N("rdmsr", 1, 1), N("wrmsr", 2, 2),
    N("read_rsp", 0, 0), N("read_rflags", 0, 0),
    N("mem_copy", 3, 3), N("mem_set", 3, 3),
    N("addr_of", 1, 1),  N("syscall", 1, 7),
    N("multiboot_info", 0, 0), N("multiboot_magic", 0, 0),
    N("argc", 0, 0),     N("argv", 1, 1),
    N("call_ptr", 1, 7), N("unreachable", 0, 0),
    N("print", 0, -1),   N("as_str", 1, 1),
    {NULL, NULL, 0, 0, 0, RK_DYN, 0, 0}};

const BuiltinInfo *builtin_lookup(const char *name, int native) {
  for (int i = 0; builtin_table[i].name; i++) {
    const BuiltinInfo *b = &builtin_table[i];
    if (strcmp(b->name, name) != 0)
      continue;
    if (native ? b->native : b->hosted)
      return b;
  }
  return 0;
}
