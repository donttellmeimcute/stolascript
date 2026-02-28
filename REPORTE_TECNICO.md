# Reporte Técnico — StolasScript Compiler Deep Dive

> Fecha: 2026-02-28  
> Fuentes analizadas: `src/codegen.c`, `src/semantic.c`, `src/runtime.c`, `src/builtins.c`

---

## 1. Asignador de Registros — Estrategia de Spilling

### Qué hace el compilador

El asignador implementa un **Linear Scan first-fit** sobre un conjunto fijo de 5 registros
callee-saved: `r12 → r13 → r14 → r15 → rbx`.

```c
// src/codegen.c – líneas 52-66
#define REGALLOC_MAX_REGS  5
static const char *const callee_saved_regs[REGALLOC_MAX_REGS] = {
  "r12", "r13", "r14", "r15", "rbx"
};
```

Antes de generar el cuerpo de cualquier función, se ejecuta `ra_init()` que hace un
**pre-recorrido del AST** (`ra_collect`) y numera todas las variables que aparecen como
destino de asignación, iteradores de bucle, parámetros o variables de `catch`.

El criterio de asignación o spilling es puramente **posicional (FIFO)**:

| Orden de aparición | Destino |
|---|---|
| 1ª – 5ª variable | `r12`, `r13`, `r14`, `r15`, `rbx` |
| 6ª en adelante | Stack frame con `[rbp - offset]` |

```c
// ra_add — src/codegen.c ~línea 80
if (func_regalloc.regs_used < REGALLOC_MAX_REGS) {
    vl->reg_idx      = func_regalloc.regs_used++;   // ← registro
} else {
    vl->reg_idx      = -1;  // ← SPILL al stack
}
```

### Cálculo del offset de stack para variables spilled

El offset **no es secuencial**; se calcula mediante un hash del nombre:

```c
// src/codegen.c
static int get_var_offset(const char *name) {
  int hash = 0;
  for (int i = 0; name[i]; i++)
    hash = (hash * 31 + name[i]) % 64;
  return (hash + 1) * 8;   // rango: 8..512 bytes
}
```

El frame reserva siempre `sub rsp, 512` → 64 slots × 8 bytes.

### Conclusión

El Linear Scan es robusto: **no hay límite duro de variables** (hasta 64 en tabla) y el
spilling al stack es automático.  
**Limitación documentada**: la prioridad de derramen (spilling) es first-come-first-served,
no basada en frecuencia de uso. Las variables más "calientes" no reciben trato preferencial
si aparecen en sexta posición o posterior en el AST.

---

## 2. Modo Freestanding — Variables Globales y Memoria

### Qué cambia en `--freestanding`

```c
// src/codegen.c ~línea 370
if (!is_freestanding) {
    // emite todos los .extern de la librería de runtime
}
```

En modo freestanding el codegen omite:
- Todos los `.extern` de funciones de runtime (constructores, GC, etc.)
- La llamada a `stola_setup_runtime` (sin SO, sin señales POSIX)
- Soporte a clases y excepciones (rechazado por el analizador semántico)

### Dónde van las variables globales

En modo freestanding **no existe una sección `.bss` explícita** para variables
globales.  Las variables locales de `main` (que actúa como kernel entry) se
sitúan en el mismo frame de pila de 512 bytes (`sub rsp, 512`).

Las cadenas de texto literales **sí se emiten** en una sección `.data` estándar:

```c
// src/codegen.c ~línea 522
if (string_table_count > 0) {
    fprintf(out, "\n.data\n");
    fprintf(out, ".str%d: .asciz \"%s\"\n", ...);
}
```

El enlazador (`ld` / `clang`) puede mapear esa sección `.data` a la dirección
física que indique el linker script del kernel. StolasScript no controla esa
dirección: la responsabilidad recae íntegramente en el linker script de la
plataforma bare-metal.

### Conclusión

| Aspecto | Estado |
|---|---|
| Sección `.data` (strings) | ✅ emitida correctamente |
| Sección `.bss` (zero-init globales) | ❌ no implementada |
| Variables locales de `main` | ✅ en stack frame de 512 bytes |
| Control de dirección física | ❌ delegado al linker script externo |

---

## 3. Funciones de Interrupción (ISR) — Transparencia de Registros

### Código generado para `interrupt function`

```c
// src/codegen.c ~línea 932
if (node->as.function_decl.is_interrupt) {
    fprintf(out, ".global %s\n", node->as.function_decl.name);
    fprintf(out, "%s:\n",        node->as.function_decl.name);
    // Guarda registros caller-saved + rsi/rdi (volátiles en SysV AMD64)
    fprintf(out, "    push rax\n");
    fprintf(out, "    push rcx\n");
    fprintf(out, "    push rdx\n");
    fprintf(out, "    push r8\n");
    fprintf(out, "    push r9\n");
    fprintf(out, "    push r10\n");
    fprintf(out, "    push r11\n");
    fprintf(out, "    push rsi\n");   // SysV: volátil
    fprintf(out, "    push rdi\n");   // SysV: volátil
    // Frame de pila para asm {} dentro del ISR
    fprintf(out, "    push rbp\n");
    fprintf(out, "    mov  rbp, rsp\n");
    fprintf(out, "    sub  rsp, 256\n");
    // ... cuerpo ...
    fprintf(out, "    add  rsp, 256\n");
    fprintf(out, "    pop  rbp\n");
    // Restaura en orden inverso
    fprintf(out, "    pop rdi\n");    // restaura SysV
    fprintf(out, "    pop rsi\n");    // restaura SysV
    fprintf(out, "    pop r11\n");
    ...
    fprintf(out, "    pop rax\n");
    fprintf(out, "    iretq\n");
}
```

### Análisis de cobertura

El ISR guarda y restaura los 7 registros volátiles que el hardware **no** preserva
automáticamente (`rax rcx rdx r8 r9 r10 r11`).

| Registro | Windows x64 (volátil) | SysV AMD64 (volátil) | Guardado |
|---|---|---|---|
| `rax` | ✅ | ✅ | ✅ |
| `rcx` | ✅ | ✅ | ✅ |
| `rdx` | ✅ | ✅ | ✅ |
| `r8`  | ✅ | ✅ | ✅ |
| `r9`  | ✅ | ✅ | ✅ |
| `r10` | ✅ | ✅ | ✅ |
| `r11` | ✅ | ✅ | ✅ |
| `rsi` | ❌ callee-saved | ✅ **volátil** | ✅ **guardado** |
| `rdi` | ❌ callee-saved | ✅ **volátil** | ✅ **guardado** |

### Conclusión

La ISR es ahora **totalmente transparente en ambas ABIs**. `push rsi` / `push rdi`
se añadieron al prólogo (antes del frame) y los correspondientes `pop rdi` /
`pop rsi` al epílogo (después de desmontar el frame, antes de restaurar `r11`–`rax`).
Guardar estos registros en Windows x64 es superfluo pero inofensivo.

---

## 4. FFI y Convenciones de Llamada (ABI)

### Mecanismo de detección

StolasScript selecciona la ABI en **tiempo de compilación del compilador mismo**
mediante macros de preprocesador de C:

```c
// src/codegen.c ~línea 14
#ifdef _WIN32
  #define ARG0 "rcx"
  #define ARG1 "rdx"
  #define ARG2 "r8"
  #define ARG3 "r9"
#else
  #define ARG0 "rdi"
  #define ARG1 "rsi"
  #define ARG2 "rdx"
  #define ARG3 "rcx"
#endif
```

Cada vez que el codegen necesita pasar un argumento a una función externa
(builtin, runtime, o FFI de usuario), usa los macros `ARG0`–`ARG3`.  No
existe detección en runtime: la ABI se "hornea" en el binario del compilador.

### Shadow space y alineación

```c
static void emit_call(FILE *out, const char *func_name) {
  fprintf(out, "    mov r10, rsp\n");
  fprintf(out, "    and rsp, -16\n");     // alineación 16 bytes
#ifdef _WIN32
  fprintf(out, "    sub rsp, 48\n");      // 32 shadow + 16 pad
  fprintf(out, "    mov [rsp + 32], r10\n");
  fprintf(out, "    call %s\n", func_name);
  fprintf(out, "    mov rsp, [rsp + 32]\n");
#else
  fprintf(out, "    push r10\n");         // guarda RSP original
  fprintf(out, "    call %s\n", func_name);
  fprintf(out, "    pop rsp\n");
#endif
}
```

### Conclusión

| Aspecto | Estado |
|---|---|
| Registro de argumentos Windows (`rcx/rdx/r8/r9`) | ✅ correcto |
| Registro de argumentos Linux (`rdi/rsi/rdx/rcx`) | ✅ correcto |
| Shadow space Windows (32 bytes) | ✅ implementado |
| Alineación de pila a 16 bytes antes de `call` | ✅ implementado |
| Detección de ABI en runtime / cross-compilation | ❌ solo compile-time |

---

## 5. WebSockets y No-Bloqueo

### Implementación real de `ws_receive`

```c
// src/builtins.c — ws_recv_frame (Windows)
static char *ws_recv_frame(SOCKET sock) {
  ...
  if (recv(sock, (char*)hdr, 2, MSG_WAITALL) != 2) return NULL;
  ...
  while (got < plen) {
    int r = recv(sock, payload+got, (int)(plen-got), 0);
    ...
  }
}
```

La llamada usa `recv()` **sincrónico** (`MSG_WAITALL`).  No hay ni una sola
referencia a `select()`, `poll()`, `WSAEventSelect()`, `epoll()` ni a
`O_NONBLOCK` / `SOCK_NONBLOCK` en todo el código fuente.

### Implicaciones

Si `ws_receive` se llama desde el hilo principal y el servidor tarda en enviar
datos, **el hilo completo se bloquea indefinidamente**.  

El runtime sí expone `stola_thread_spawn`, lo que significa que la única forma
de hacer I/O concurrente con WebSockets es que el programador cree un hilo
manualmente para cada conexión:

```stola
// Patrón de un-hilo-por-conexión (sigue siendo válido)
let handle = thread_spawn(fn() {
    let msg = ws_receive(conn)   // bloquea este hilo, no el principal
    ...
})

// Nuevo: multiplexado con ws_select (un hilo para N conexiones)
let ready = ws_select([conn1, conn2, conn3], 100)  // timeout 100 ms
foreach ready as sock {
    let msg = ws_receive(sock)
    ...
}
```

`stola_ws_select` (`src/builtins.c`) acepta un array de handles enteros y un
timeout en millisegundos; internamente llama a `select()` (Winsock en Windows,
POSIX en Linux) y devuelve únicamente los handles listos para lectura, lo que
permite servir N clientes desde un único hilo de evento sin bloqueo.

### Conclusión

| Aspecto | Estado |
|---|---|
| Implementación RFC 6455 (handshake, framing, mask) | ✅ completa |
| `ws_receive` bloqueante | ⚠️ sí, bloquea el hilo llamante |
| I/O multiplexado (`select`) — `ws_select()` | ✅ implementado |
| Modelo un-hilo-para-N-conexiones | ✅ ahora posible vía `ws_select` |
| I/O multiplexado de alta escala (`epoll`/`kqueue`) | ⚠️ no implementado (mejora futura) |

---

## 6. Manejo de Señales — Calidad del Stack Trace

### Código del handler de SIGSEGV

```c
// src/runtime.c ~línea 1313  (actualizado)
#include <execinfo.h>
#include <unistd.h>

static void stola_sigsegv_handler(int sig, siginfo_t *info, void *ctx) {
  (void)sig; (void)ctx;
  fprintf(stderr, "\n[StolasScript] Segmentation fault (SIGSEGV)\n");
  fprintf(stderr, "[StolasScript] Fault address : %p\n",
          info ? info->si_addr : (void *)0);
  fflush(stderr);
  void *frames[32];
  int n = backtrace(frames, 32);
  fprintf(stderr, "[StolasScript] Stack trace (%d frames):\n", n);
  backtrace_symbols_fd(frames, n, STDERR_FILENO);
  _exit(1);
}
```

```c
void stola_setup_runtime(void) {
#ifndef _WIN32
  signal(SIGINT, stola_sigint_handler);

  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sa.sa_sigaction = stola_sigsegv_handler;
  sa.sa_flags     = SA_SIGINFO;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGSEGV, &sa, NULL);
#endif
}
```

### Análisis

El handler usa ahora `sigaction()` con `SA_SIGINFO`, lo que proporciona acceso
completo a `siginfo_t`. En un fallo real el output es:

```
[StolasScript] Segmentation fault (SIGSEGV)
[StolasScript] Fault address : 0x0000000000000018
[StolasScript] Stack trace (8 frames):
./s_linux(_start+0x…) [0x…]
./s_linux(stola_setup_runtime+0x…) [0x…]
…
```

### Conclusión

| Aspecto | Estado |
|---|---|
| El programa captura SIGSEGV (no muere silencioso) | ✅ |
| Imprime mensaje descriptivo en stderr | ✅ |
| Dirección de memoria faulted (`si_addr`) | ✅ reportada vía `SA_SIGINFO` |
| Stack trace (`backtrace` + `backtrace_symbols_fd`) | ✅ implementado |
| Nombre de función donde ocurrió el fallo | ✅ visible si se compila con `-rdynamic` |

---

## 7. Hoisting y Recursión Mutua

### El Pre-pase semántico

```c
// src/semantic.c ~línea 179
// ── Pre-pass: hoist all top-level function and class names ──────────────
for (int i = 0; i < program->as.program.statement_count; i++) {
  ASTNode *stmt = program->as.program.statements[i];
  if (stmt->type == AST_FUNCTION_DECL) {
    const char *fname = stmt->as.function_decl.name;
    if (!resolve_symbol(analyzer, fname)) {
      define_symbol(analyzer, fname, SYMBOL_FUNCTION,
                    stmt->as.function_decl.param_count, "any");
    }
  } else if (stmt->type == AST_CLASS_DECL) {
    // ídem para clases
  }
}
// ── Solo después de registrar TODOS los nombres, se analizan los cuerpos ──
for (int i = 0; i < program->as.program.statement_count; i++) {
  analyze_node(analyzer, stmt);
}
```

### Soporte a recursión mutua

El pre-pase recorre el array completo de sentencias **antes** de analizar
ningún cuerpo.  Esto garantiza que cuando el analizador eventualmente visita
el cuerpo de `funcA` y encuentra una llamada a `funcB`, el símbolo `funcB`
**ya existe** en la tabla de símbolos global.

```stola
// Ejemplo — plenamente soportado
fn funcA(n: number) -> number {
    if n <= 0 { return 1 }
    return funcB(n - 1)   // funcB no está definida "arriba" en el texto
}

fn funcB(n: number) -> number {
    return funcA(n - 1)
}
```

En el codegen, las funciones se emiten **después de `main`** como etiquetas
independientes:

```c
// src/codegen.c ~línea 481
if (stmt->type != AST_FUNCTION_DECL && ...) {
    generate_node(stmt, ...);  // emite solo código de main
}
// Después emite todas las funciones definidas por el usuario
for (...) {
    if (stmt->type == AST_FUNCTION_DECL)
        generate_node(stmt, ...);
}
```

El ensamblador GNU As puede resolver referencias hacia adelante en `.text`
en el mismo archivo, por lo que un `call funcB` en el cuerpo de `funcA` se
resuelve correctamente aunque `funcB` aparezca físicamente después.

### Conclusión

| Aspecto | Estado |
|---|---|
| Pre-pase registra prototipos antes de analizar cuerpos | ✅ implementado |
| Recursión directa (`funcA` llama a `funcA`) | ✅ soportada |
| Recursión mutua (`funcA` ↔ `funcB`) | ✅ soportada |
| Hoisting de nombres de clases | ✅ soportado |
| Guard `if (!resolve_symbol(...))` evita re-registros | ✅ implementado |

---

## Resumen Ejecutivo

| # | Tema | Veredicto |
|---|---|---|
| 1 | Asignador de registros / Spilling | ✅ Funcional. Spilling automático por hash. Sin prioridad por frecuencia. |
| 2 | Freestanding / Memoria | ⚠️ `.data` emitida, `.bss` ausente, dirección física depende del linker script. |
| 3 | ISR / Transparencia de registros | ✅ `rsi`/`rdi` ahora guardados. Transparente en Windows x64 y Linux/SysV. |
| 4 | FFI / Convenciones de llamada ABI | ✅ Windows y Linux correctamente diferentes, con shadow space y alineación. |
| 5 | WebSockets / No-bloqueo | ⚠️ `ws_receive` sigue bloqueante. `ws_select()` añadido: multiplexado N→1 con `select()`. |
| 6 | Manejo de señales / Stack Trace | ✅ `sigaction`+`SA_SIGINFO`: dirección faulted + backtrace completo. |
| 7 | Hoisting / Recursión mutua | ✅ Pre-pase de dos fases auténtico. Recursión mutua plenamente soportada. |
