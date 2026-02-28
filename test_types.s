.intel_syntax noprefix
.global main

.extern stola_print_value
.extern stola_length
.extern stola_length
.extern stola_push
.extern stola_pop
.extern stola_shift
.extern stola_unshift
.extern stola_to_string
.extern stola_to_number
.extern stola_string_split
.extern stola_string_starts_with
.extern stola_string_ends_with
.extern stola_string_contains
.extern stola_string_substring
.extern stola_string_index_of
.extern stola_string_replace
.extern stola_string_trim
.extern stola_uppercase
.extern stola_lowercase
.extern stola_socket_connect
.extern stola_socket_send
.extern stola_socket_receive
.extern stola_socket_close
.extern stola_json_encode
.extern stola_json_decode
.extern stola_current_time
.extern stola_sleep
.extern stola_random
.extern stola_floor
.extern stola_ceil
.extern stola_round
.extern stola_read_file
.extern stola_write_file
.extern stola_append_file
.extern stola_file_exists
.extern stola_http_fetch
.extern stola_thread_join
.extern stola_mutex_create
.extern stola_mutex_lock
.extern stola_mutex_unlock
.extern stola_thread_spawn
.extern stola_register_method
.extern stola_invoke_method
.extern stola_load_dll
.extern stola_bind_c_function
.extern stola_invoke_c_function
.extern stola_new_int
.extern stola_new_bool
.extern stola_new_string
.extern stola_new_null
.extern stola_new_array
.extern stola_new_dict
.extern stola_new_struct
.extern stola_is_truthy
.extern stola_add
.extern stola_sub
.extern stola_mul
.extern stola_div
.extern stola_mod
.extern stola_neg
.extern stola_eq
.extern stola_neq
.extern stola_lt
.extern stola_gt
.extern stola_le
.extern stola_ge
.extern stola_and
.extern stola_or
.extern stola_not
.extern stola_struct_get
.extern stola_struct_set
.extern stola_array_get
.extern stola_array_set
.extern stola_dict_get
.extern stola_dict_set
.extern stola_push

.text
main:
    push rbp
    mov rbp, rsp
    sub rsp, 512
    mov rcx, 25
    mov r10, rsp
    and rsp, -16
    sub rsp, 48
    mov [rsp + 32], r10
    call stola_new_int
    mov rsp, [rsp + 32]
    push rax
    pop rax
    mov [rbp - 24], rax
    mov rax, [rbp - 24]
    push rax
    pop rcx
    mov r10, rsp
    and rsp, -16
    sub rsp, 48
    mov [rsp + 32], r10
    call stola_print_value
    mov rsp, [rsp + 32]
    push rax
    pop rax
    lea rcx, [rip + .str0]
    mov r10, rsp
    and rsp, -16
    sub rsp, 48
    mov [rsp + 32], r10
    call stola_new_string
    mov rsp, [rsp + 32]
    push rax
    pop rax
    mov [rbp - 24], rax
    mov rax, [rbp - 24]
    push rax
    pop rcx
    mov r10, rsp
    and rsp, -16
    sub rsp, 48
    mov [rsp + 32], r10
    call stola_print_value
    mov rsp, [rsp + 32]
    push rax
    pop rax
    lea rcx, [rip + .str1]
    mov r10, rsp
    and rsp, -16
    sub rsp, 48
    mov [rsp + 32], r10
    call stola_new_string
    mov rsp, [rsp + 32]
    push rax
    pop rcx
    mov r10, rsp
    and rsp, -16
    sub rsp, 48
    mov [rsp + 32], r10
    call process_data
    mov rsp, [rsp + 32]
    push rax
    pop rax
    xor eax, eax
    add rsp, 512
    pop rbp
    ret

process_data:
    push rbp
    mov rbp, rsp
    sub rsp, 512
    mov [rbp - 344], rcx
    lea rcx, [rip + .str2]
    mov r10, rsp
    and rsp, -16
    sub rsp, 48
    mov [rsp + 32], r10
    call stola_new_string
    mov rsp, [rsp + 32]
    push rax
    mov rax, [rbp - 344]
    push rax
    pop rdx
    pop rcx
    mov r10, rsp
    and rsp, -16
    sub rsp, 48
    mov [rsp + 32], r10
    call stola_add
    mov rsp, [rsp + 32]
    push rax
    pop rcx
    mov r10, rsp
    and rsp, -16
    sub rsp, 48
    mov [rsp + 32], r10
    call stola_print_value
    mov rsp, [rsp + 32]
    push rax
    pop rax
    mov rcx, 100
    mov r10, rsp
    and rsp, -16
    sub rsp, 48
    mov [rsp + 32], r10
    call stola_new_int
    mov rsp, [rsp + 32]
    push rax
    pop rax
    add rsp, 512
    pop rbp
    ret
    mov r10, rsp
    and rsp, -16
    sub rsp, 48
    mov [rsp + 32], r10
    call stola_new_null
    mov rsp, [rsp + 32]
    add rsp, 512
    pop rbp
    ret

.data
.str0: .asciz "25 años"
.str1: .asciz "Hola"
.str2: .asciz "Procesando datos:"
