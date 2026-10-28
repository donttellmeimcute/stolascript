# Reporte Técnico — Compilador StolasScript 2.0

Este documento describe la arquitectura del compilador reescrito en la
versión 2.0. (El análisis del compilador 1.x está en el historial de git.)

---

## 1. Pipeline

```
.stola ──► lexer ──► parser (Pratt) ──► AST ──► semantic ──► codegen ──► .s
                                         ▲        │                       │
                     imports (main.c) ───┘        │ tipos, símbolos,      ▼
                                                  │ closures, escapes   as/ld o gcc/clang
```

| Archivo | Responsabilidad |
|---|---|
| `src/lexer.c` | tokens; operadores en palabras y símbolos; strings con escapes, `"""`, f-strings; números `0x 0b 0o 1_000 1.5e3`; comentarios `// /* */ #` |
| `src/parser.c` | Pratt para expresiones (ternario, `in`, bits, slices, lambdas, f-strings), descenso recursivo para sentencias; las líneas se unen dentro de `() [] {}` |
| `src/semantic.c` | declaraciones, ámbitos (reglas de Python + `global`), capturas de closures, análisis de escape, inferencia de tipos |
| `src/codegen.c` | ensamblador x86-64 (sintaxis Intel de GNU as), ABI System V y Windows x64 |
| `src/main.c` | CLI, resolución de `import`, invocación de `as`/`ld`/`cc`, script de enlazado del kernel |
| `src/runtime.c`, `src/builtins.c` | runtime del objetivo *hosted* (valores dinámicos, colecciones, clases, excepciones, hilos, red) |
| `stdlib/linux.stola` | runtime del objetivo `linux`, escrito en StolasScript sobre `syscall` |

---

## 2. Representación de valores

* **Tipos nativos** (`TY_INT`, `TY_BOOL`, `TY_STR` en modo nativo): una palabra
  de 64 bits en un registro o en la pila. Sin cajas ni llamadas.
* **Tipo dinámico** (`TY_DYN`): puntero a `StolaValue` (entero, flotante,
  string, lista, dict, objeto, función/closure, null). `null`, `true` y
  `false` son singletons inmortales; los enteros −256..1024 están
  precalculados; los literales string/int/float usados dinámicamente se crean
  una sola vez al iniciar el programa.

La conversión entre ambos mundos es explícita en el código generado:
`stola_new_int` / `stola_bool_value` para empaquetar, `stola_to_int64`
(lanza `TypeError`) y `stola_is_truthy` para desempaquetar.

---

## 3. Inferencia de tipos

Retícula: `NONE < {BOOL, INT} < DYN` (en modo nativo `{BOOL, STR} < INT`).

1. Las anotaciones (`x: int`, `-> int`) fijan tipos.
2. Las funciones que **escapan** (usadas como valor, métodos, lambdas,
   `thread_spawn`) tienen parámetros dinámicos salvo anotación.
3. Para las demás, el tipo de cada parámetro es la unión de los tipos de los
   argumentos en todas las llamadas directas (y de los valores por defecto).
4. Variables, parámetros y retornos se recalculan hasta un punto fijo
   (las uniones solo suben en la retícula ⇒ termina).
5. Lo que queda en `NONE` pasa a `DYN` (o `INT` en modo nativo).

Ejemplo: en `fib(n)` sin anotaciones, `fib(35)` aporta `INT` a `n`; `n - 1`
es `INT`; los `return` son `INT` ⇒ `fib` se compila como en C.

Si una función tipada se usa como valor, se genera un *wrapper* `W<label>` con
ABI totalmente dinámica que convierte argumentos, rellena valores por defecto
(marcador `stola_missing_value`) y empaqueta el resultado.

---

## 4. Generación de código

### Marco de pila

```
[rbp+16..]   argumentos 7+ (SysV) / 5+ (Win64, tras 32 bytes de shadow space)
[rbp+8]      dirección de retorno
[rbp]        rbp anterior
[rbp-8..]    registros callee-saved usados (rbx, r12..r15)
[rbp-8k-..]  ranuras de variables y temporales
```

El tamaño del marco se ajusta para que `rsp` quede alineado a 16 bytes; el
generador lleva la cuenta exacta de los `push` pendientes (`depth`) y en cada
llamada añade, solo si hace falta, 8 bytes de relleno (+32 de shadow space en
Windows). No hay realineación dinámica de la pila.

### Asignación de registros

Cada variable local acumula un peso `8^profundidad_de_bucle` por uso; las 5
más pesadas van a `rbx, r12–r15` (callee-saved, sobreviven a las llamadas al
runtime). Se desactiva en funciones con `try` (por `setjmp`), con `asm { }`
y en rutinas de interrupción.

### Expresiones y condiciones

* Operandos simples (constantes de 32 bits, variables) se usan directamente
  como operandos de instrucción (`add rax, r12`, `cmp rbx, 2`).
* Las condiciones generan saltos directos (`cmp` + `jl`), incluidos `and`/`or`
  con cortocircuito y `not` sin materializar booleanos.
* `x % 2^k == 0` se convierte en `test`; `* / %` por potencias de dos usan
  desplazamientos con la corrección de signo de la división truncada.
* Llamadas: los argumentos complejos se evalúan primero y los simples se
  cargan directamente en `rdi, rsi, ...`; con más de 6 (o 4) argumentos se
  copian al área de argumentos de pila.

### Excepciones

`try` usa un `setjmp`/`longjmp` propio emitido en el `.s` (guarda
`rbx, rbp, r12–r15, rsp` y la dirección de retorno). La pila de manejadores es
*thread-local*. `return`, `break` y `continue` dentro de `try` desapilan los
manejadores necesarios antes de saltar.

### Closures y objetos

* Una lambda se compila como función con un primer parámetro oculto `__env`;
  al crearla se copian (por valor) las variables capturadas a la closure. Las
  capturas transitivas (lambda dentro de lambda) se resuelven creando capturas
  en cada nivel intermedio.
* Las clases registran sus métodos al inicio; `obj.metodo(...)` despacha en el
  runtime buscando en la cadena de herencia (comparación de punteros de los
  nombres internados, con `strcmp` como respaldo). `super.m()` y `init` se
  resuelven estáticamente como llamadas directas.

---

## 5. Objetivos nativos

### `--target linux`

`_start` guarda `argc/argv`, alinea la pila, llama a `S___main` y termina con
`syscall(60)`. Se enlaza con `ld -static -nostdlib`. La biblioteca
`stdlib/linux.stola` implementa `print_str/print_int/print_char` (que usa
`print`), un asignador sobre `mmap`, archivos y strings.

### `--target kernel`

El compilador emite:

1. Cabecera **multiboot v1** con *a.out kludge* (direcciones de carga
   explícitas ⇒ imagen binaria plana generada con `objcopy -O binary`).
2. Código de 32 bits: guarda `eax/ebx` del cargador, limpia `.bss`, crea
   tablas de páginas que mapean 4 GiB 1:1 con páginas de 2 MiB, activa PAE y
   `EFER.LME`, activa paginación, carga una GDT de 64 bits y hace el salto
   lejano a modo largo.
3. Código de 64 bits: segmentos, pila de 64 KiB y `call S___main`.

`main.c` genera el script de enlazado (carga en 1 MiB).

### `interrupt function`

```
push rax rcx rdx rsi rdi r8 r9 r10 r11
push rbp ; mov rbp, rsp ; sub rsp, N ; and rsp, -16 ; cld
...cuerpo (puede llamar funciones normales)...
mov rsp, rbp ; pop rbp ; pop r11..rax ; [add rsp, 8 si hay código de error] ; iretq
```

Los registros callee-saved los preservan las funciones llamadas, y el
cambio de contexto de StolaOS (`switch_to`) guarda `rbx, r12–r15, rbp` en la
pila de la tarea. Una tarea nueva arranca con un marco falso que "retorna" a
`task_trampoline`, que habilita interrupciones y llama a la función de la
tarea. El planificador se invoca desde la interrupción del timer (multitarea
apropiativa).

---

## 6. Verificación

* `tests/run_tests.sh`: programas hosted con salida esperada, binarios
  `--target linux` (comprobando que son estáticos y sin libc), errores de
  compilación, y arranque de StolaOS en QEMU manejando la shell por el puerto
  serie (archivos, multitarea, apagado).
* La ruta de la ABI Windows x64 se verificó compilando el compilador con
  MinGW, generando los programas de prueba con él, enlazándolos con el runtime
  para Windows y ejecutándolos con Wine (salida idéntica a Linux). La CI lo
  repite en `windows-latest`.

---

## 7. Trabajo futuro

* Recolector de basura para los valores dinámicos.
* Flotantes nativos (SSE) en código tipado.
* Ensamblador/enlazador propios y compilador autoalojado (escrito en
  StolasScript), para eliminar también la dependencia de binutils.
* Modo usuario, llamadas al sistema y sistema de archivos en disco para
  StolaOS.
