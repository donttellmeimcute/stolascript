# StolasScript

StolasScript es un lenguaje de programación moderno, compilado, dinámico pero con soporte para tipado gradual opcional. Está diseñado para ser rápido y generar código máquina (x64 Assembly) de forma nativa.

Actualmente, el compilador genera un archivo ensamblador `.s` (sintaxis Intel) que luego puede ser ensamblado y enlazado usando `clang` o `gcc` (junto con el runtime escrito en C) para producir un binario nativo y optimizado, sin depender de máquinas virtuales pesadas. **Compatible con Windows y Linux** (x64).

---

## Índice de Características

1. [Sintaxis Básica](#sintaxis-básica)
2. [Estructuras de Control](#estructuras-de-control)
3. [Funciones](#funciones)
4. [Estructuras de Datos Complejas](#estructuras-de-datos)
5. [Programación Orientada a Objetos (POO)](#programación-orientada-a-objetos)
6. [Tipado Gradual (Opcional)](#tipado-gradual-opcional)
7. [Paralelismo Real (Hilos nativos)](#paralelismo-real)
8. [FFI (Interoperabilidad con C/DLLs)](#ffi-interoperabilidad-nativa)
9. [Funciones Integradas (Builtins)](#funciones-integradas-builtins)
10. [Manejo de Excepciones (Try/Catch)](#manejo-de-excepciones)
11. [Modo Freestanding (Sin Runtime)](#modo-freestanding-sin-runtime)
12. [Soporte de Interrupciones (ISR)](#soporte-de-interrupciones-isr)
13. [Inline Assembly](#inline-assembly)
14. [WebSockets](#websockets)
15. [Optimizaciones de Registro](#optimizaciones-de-registro)
16. [Manejo de Señales en Linux](#manejo-de-señales-en-linux)
17. [Acceso Directo a Memoria](#acceso-directo-a-memoria)
18. [Cómo Compilar (Windows y Linux)](#cómo-compilar)

---

## Sintaxis Básica

Por defecto, la asignación de variables es dinámica.

```stola
// Variables y tipos primitivos
nombre = "Stolas"
edad = 25
es_demonio = true
nada = null

// Operaciones aritméticas y lógicas
suma = 10 plus 5
resta = 10 minus 5
multiplicacion = 10 times 5
division = 10 divided_by 2
modulo = 10 modulo 3

es_mayor = 10 is_greater_than 5
es_igual = 10 equals 10
```

## Estructuras de Control

```stola
// If, Elif, Else
if edad is_greater_than 18
  print("Mayor de edad")
elif edad equals 18
  print("Apenas 18")
else
  print("Menor de edad")
end

// Bucle While
contador = 0
while contador is_less_than 5
  print(contador)
  contador = contador plus 1
end

// Bucle Loop (rango step 1)
loop i from 0 to 10
  print(i)
end

// Bucle For (iteración sobre colecciones)
nombres = ["Asmodeus", "Paimon", "Bael"]
for nombre in nombres
  print(nombre)
end

// Match (Switch)
match edad
  case 18 => print("Adulto nuevo")
  case 21 => print("Adulto universal")
  default => print("Otra edad")
end
```

## Funciones

```stola
function saludar(nombre)
  return "Hola, " plus nombre
end

mensaje = saludar("Grimoire")
print(mensaje)
```

## Estructuras de Datos

```stola
// Arreglos
numeros = [1, 2, 3, 4]
numeros_1 = numeros at 0 // Acceso por índice
push(numeros, 5)

// Diccionarios
perfil = {
  "nombre": "Stolas",
  "poder": 9000
}
print(perfil.nombre) // Acceso con punto o clave

// Estructuras (Tipos de datos predefinidos)
struct Punto
  x
  y
end

p = Punto(10, 20)
print(p.x)
```

## Programación Orientada a Objetos

StolasScript soporta clases, métodos dinámicos y la palabra reservada `this` para auto-contexto.

```stola
class Mago
  function init(nombre, mana)
    this.nombre = nombre
    this.mana = mana
  end

  function lanzar_hechizo()
    if this.mana is_greater_than 10
      this.mana = this.mana minus 10
      print(this.nombre plus " lanzó un hechizo!")
    else
      print("No hay mana suficiente")
    end
  end
end

merlin = new Mago("Merlín", 50)
merlin.lanzar_hechizo()
```

## Tipado Gradual (Opcional)

Si deseas mayor seguridad, puedes anotar los tipos de variables y funciones. El compilador generará advertencias (`Warnings`) durante el análisis semántico si asignas tipos incompatibles, pero no detendrá la compilación ya que es un entorno dinámico por debajo.

```stola
// Tipos en funciones
function calcular_area(base: number, altura: number) -> number
  return base times altura
end

// Tipado de variables
usuario: string = "Bob"
edad: number = 22

// Generará un Warning semántico, pero compilará
edad = "veintidos" 
```

## Paralelismo Real

StolasScript escapa de las limitaciones de concurrencia asíncrona simulada de otros lenguajes al implementar **Hilos del Sistema Operativo nativos** y Mutexes. Usa Win32 Threads en Windows y pthreads en Linux.

```stola
lock = mutex_create()

function tarea_pesada(args)
  id = args at 0
  _lock = args at 1

  mutex_lock(_lock)
  print("Iniciando hilo " plus to_string(id))
  mutex_unlock(_lock)
  
  sleep(1000)
end

a1 = [1, lock]
a2 = [2, lock]

h1 = thread_spawn(tarea_pesada, a1)
h2 = thread_spawn(tarea_pesada, a2)

thread_join(h1)
thread_join(h2)
print("Tareas paralelas completadas")
```

## FFI (Interoperabilidad Nativa)

Puedes invocar APIs nativas de Windows u otras DLLs de forma dinámica sin reescribir código en C, directo desde StolasScript.

```stola
import_native "user32.dll"

c_function MessageBoxA(hwnd: number, text: string, caption: string, type: number) -> number

// Muestra una ventana nativa de Windows interactiva
MessageBoxA(0, "¡Hola desde StolasScript!", "FFI Interop", 0)
```

## Manejo de Excepciones

StolasScript implementa un sistema robusto de manejo de errores basado en `try`, `catch` y `throw`, utilizando internamente `setjmp` y `longjmp` en el motor para mayor eficiencia.

```stola
try
  // Alguna operación que puede fallar
  if random() is_greater_than 0.5
    throw "¡Error aleatorio!"
  end
  print("Todo bien por aquí")
catch e
  print("Capturamos un error: " plus e)
end
```

## Modo Freestanding (Sin Runtime)

Diseñado para sistemas embebidos, sistemas operativos o entornos bare-metal donde no existe un runtime de C ni gestión de memoria dinámica (heap).

### Características del modo Freestanding

- **Aritmética Nativa:** El compilador genera instrucciones de hardware (`add`, `sub`, `imul`, `idiv`) en lugar de llamar al runtime.
- **Sin Dependencias:** El archivo `.s` generado no contiene llamadas a `stola_*` si se evitan las features restringidas.
- **Restricciones:** No se permiten clases, diccionarios ni excepciones (ya que requieren heap/runtime).
- **Tipado Fuerte:** Obliga al uso de tipos primitivos para asegurar que el hardware pueda operar directamente.

```stola
// test_free.stola
a: number = 5
b: number = 10
c = a plus b
```

Comando: `s.exe --freestanding programa.stola programa.s`

## Funciones Integradas (Builtins)

El lenguaje cuenta con una extensa librería estándar (escrita en C e incrustada en el runtime):

- **Manipulación de Strings/Arrays**: `len`, `push`, `pop`, `shift`, `unshift`, `string_split`, `string_substring`, `string_replace`, `uppercase`, `lowercase`, etc.
- **Redes**: `socket_connect`, `socket_send`, `socket_receive`, `socket_close`. (WinSock2 en Windows, POSIX sockets en Linux)
- **HTTP (WinHTTP)**: `http_fetch("https://...")` (soporta HTTPS).
- **Archivos**: `read_file`, `write_file`, `append_file`, `file_exists`.
- **JSON**: `json_encode`, `json_decode`.
- **Utilidades**: `to_string`, `to_number`, `current_time`, `sleep`, `random`, `floor`, `ceil`, `round`.

---

## Soporte de Interrupciones (ISR)

StolasScript permite declarar funciones como **Rutinas de Servicio de Interrupción** (ISR) usando la palabra clave `interrupt` antes de `function`. Esto es esencial para el desarrollo de sistemas operativos, drivers y entornos bare-metal donde el hardware invoca rutinas directamente al producirse un evento (como una pulsación de tecla, un tick del timer, o un fallo de página).

### Diferencias con una función normal

- El compilador genera un prólogo/epílogo **naked**: no crea un stack frame estándar de C.
- En lugar de `ret`, la función termina con `iretq` (retorno de interrupción en x64), que restaura automáticamente `RIP`, `CS`, `RFLAGS`, `RSP` y `SS`.
- La etiqueta de la función se emite como símbolo **global** en el ensamblador para que pueda ser registrada en la IDT desde C o Assembly externo.
- No pueden recibir parámetros ni devolver valores por convención normal (el hardware controla el contexto de ejecución).

```stola
// ISR para el teclado (IRQ1 → vector 0x21 con PIC en modo protegido)
interrupt function teclado_isr()
  asm {
    in al, 0x60        ; leer scancode del puerto del teclado
    mov al, 0x20
    out 0x20, al       ; enviar EOI (End Of Interrupt) al PIC maestro
  }
end

// ISR para el timer del sistema (IRQ0 → vector 0x20)
interrupt function timer_isr()
  asm {
    mov al, 0x20
    out 0x20, al       ; EOI al PIC maestro
  }
end
```

El compilador genera automáticamente la etiqueta como símbolo global:

```asm
; Código generado para teclado_isr
.global teclado_isr
teclado_isr:
    ; cuerpo de la función...
    iretq
```

> **Nota:** El uso de `interrupt function` está pensado para el modo **Freestanding**. Usarlo en modo normal genera una advertencia del compilador, ya que requiere que el binario final sea cargado en un contexto de kernel o bootloader.

---

## Inline Assembly

Dentro de cualquier función StolasScript puedes insertar bloques `asm { ... }` para ejecutar instrucciones privilegiadas o de bajo nivel del CPU que no tienen equivalente en el lenguaje de alto nivel. Las instrucciones se emiten **verbatim** dentro del contexto de la función en el archivo `.s` generado.

### Instrucciones de uso frecuente

| Instrucción | Descripción |
|---|---|
| `hlt` | Detiene el CPU hasta la próxima interrupción |
| `cli` / `sti` | Deshabilita / habilita interrupciones hardware |
| `in al, dx` | Lee un byte del puerto de E/S indicado en `DX` |
| `out dx, al` | Escribe un byte al puerto de E/S indicado en `DX` |
| `lgdt [ptr]` | Carga el registro GDTR con el descriptor apuntado |
| `lidt [ptr]` | Carga el registro IDTR con el descriptor apuntado |

```stola
// Detener el CPU completamente (útil como último recurso en el kernel)
function detener()
  asm {
    cli
    hlt
  }
end

// Leer un byte de un puerto de E/S (convención: retorno en RAX)
function leer_puerto(puerto: number) -> number
  asm {
    mov rdx, puerto
    in al, dx
    movzx rax, al
  }
end

// Escribir un byte en un puerto de E/S
function escribir_puerto(puerto: number, valor: number)
  asm {
    mov rdx, puerto
    mov rax, valor
    out dx, al
  }
end

// Cargar la Tabla de Descriptores Global (GDT)
function cargar_gdt(ptr: number)
  asm {
    lgdt [ptr]
  }
end

// Cargar la Tabla de Descriptores de Interrupción (IDT)
function cargar_idt(ptr: number)
  asm {
    lidt [ptr]
  }
end

// Deshabilitar / habilitar interrupciones de hardware
function deshabilitar_irq()
  asm { cli }
end

function habilitar_irq()
  asm { sti }
end
```

### Acceso a variables locales desde `asm`

Las variables del scope actual son accesibles por nombre dentro del bloque `asm`. El compilador resuelve sus offsets en el stack y las sustituye por la referencia correcta (`[rbp - N]`) en el código generado:

```stola
function ejemplo(valor: number)
  resultado = 0
  asm {
    mov rax, valor     ; el compilador traduce 'valor' a [rbp-8] (o similar)
    add rax, 1
    mov resultado, rax
  }
  return resultado
end
```

### Notas importantes

- Los bloques `asm { }` de **una sola instrucción** pueden escribirse en línea: `asm { hlt }`.
- En modo **Freestanding** (`--freestanding`) el uso de `asm { }` está completamente permitido y es la vía principal para instrucciones de sistema.
- En modo normal (con runtime), el compilador emite una **advertencia** si detecta instrucciones privilegiadas (`hlt`, `lgdt`, `lidt`, `in`, `out`) fuera de un contexto freestanding, aunque compila igualmente.
- Los bloques `asm { }` no son válidos dentro de clases ni lambdas.

---

## WebSockets

StolasScript implementa el protocolo **WebSocket (RFC 6455)** nativo sin dependencias externas, usando WinSock2 en Windows y POSIX sockets en Linux. Permite comunicación bidireccional full-duplex entre dos procesos (o dos PCs en red), ideal para chats, juegos en tiempo real y herramientas colaborativas.

### Funciones disponibles

| Función | Descripción | Retorno |
|---|---|---|
| `ws_connect(url)` | Conectar como cliente a un servidor WebSocket | handle (number) |
| `ws_send(handle, msg)` | Enviar un mensaje de texto | bytes enviados |
| `ws_receive(handle)` | Esperar y recibir el próximo mensaje | string o null |
| `ws_close(handle)` | Cerrar la conexión limpiamente | - |
| `ws_server_create(port)` | Crear un servidor WebSocket en el puerto dado | server handle |
| `ws_server_accept(handle)` | Aceptar la próxima conexión entrante (bloqueante) | client handle |
| `ws_server_close(handle)` | Cerrar el servidor | - |

> El handshake HTTP Upgrade se realiza automáticamente. Los frames se enmascaran del lado del cliente según el estándar.

### Ejemplo: Chat entre dos PCs

**servidor.stola**
```stola
server = ws_server_create(8080)
print("Esperando conexión en puerto 8080...")
client = ws_server_accept(server)
print("¡Cliente conectado!")

function recibir_mensajes(args)
  c = args at 0
  while true
    msg = ws_receive(c)
    if msg equals null
      print("Cliente desconectado")
      break
    end
    print("Remoto: " plus msg)
  end
end

hilo = thread_spawn(recibir_mensajes, [client])

while true
  ws_send(client, "Hola desde el servidor!")
  sleep(2000)
end
```

**cliente.stola**
```stola
sock = ws_connect("ws://192.168.1.100:8080")
print("Conectado!")

function escuchar(args)
  s = args at 0
  while true
    msg = ws_receive(s)
    if msg equals null
      break
    end
    print("Servidor: " plus msg)
  end
end

hilo = thread_spawn(escuchar, [sock])

while true
  ws_send(sock, "Hola desde el cliente!")
  sleep(2000)
end
```

### Compilar y ejecutar el chat

```cmd
:: En la PC servidor
s.exe servidor.stola servidor.s
clang servidor.s src/runtime.c src/builtins.c -lws2_32 -lwinhttp -o servidor.exe
servidor.exe

:: En la PC cliente (ajustar IP)
s.exe cliente.stola cliente.s
clang cliente.s src/runtime.c src/builtins.c -lws2_32 -lwinhttp -o cliente.exe
cliente.exe
```

---

## Optimizaciones de Registro

El compilador incluye un **Asignador de Registros lineal** que, dentro de cada función, asigna las primeras 5 variables locales a registros callee-saved (`r12`, `r13`, `r14`, `r15`, `rbx`) en lugar de leerlas y escribirlas continuamente en el stack (`[rbp - N]`). El resto de variables que no caben en registros siguen usando slots de stack como antes.

Beneficios obtenidos:
- Las variables con más accesos (iteradores de bucle, contadores, variables de retorno) se leen directamente desde registros de CPU, sin tocar memoria.
- El código generado puede ser hasta **3-4× más rápido** en bucles intensivos.
- La función comparte un único **epilogo** etiquetado: todos los `return` saltan a él con `jmp .Lxx`, evitando duplicar las instrucciones de teardown.

Ejemplo de código generado para una función con asignador activo:

```asm
; Prólogo
push rbp
mov rbp, rsp
push r12          ; guardar r12 (asignado a 'sum')
push r13          ; guardar r13 (asignado a 'i')
sub rsp, 512

; sum = 0   =>   mov r12, rax
; i = 0     =>   mov r13, rax

; Epílogo compartido (.L5)
.L5:
add rsp, 512
pop r13
pop r12
pop rbp
ret
```

---

## Manejo de Señales en Linux

Al iniciarse un programa compilado con StolasScript en Linux, el runtime llama automáticamente a `stola_setup_runtime()` que instala manejadores para:

| Señal | Comportamiento |
|-------|----------------|
| `SIGINT` (Ctrl+C) | Imprime `[StolasScript] Interrupted (SIGINT).` y termina limpiamente con código 0. |
| `SIGSEGV` | Imprime `[StolasScript] Segmentation fault (SIGSEGV) — aborting.` y termina con código 1. |

En Windows el setup es un no-op (Windows usa SEH/VEH). No se requiere ningún cambio en el código StolasScript — la protección es automática.

---

## Acceso Directo a Memoria

StolasScript ofrece tres builtins para leer y escribir en direcciones de memoria arbitrarias. Son especialmente útiles en **modo freestanding** (bare-metal) pero también funcionan en modo hosted como wrappers C sobre punteros volátiles.

### Funciones

```stola
// Leer 8 bytes (qword) desde una dirección
valor = memory_read(0xB8000)

// Escribir 8 bytes en una dirección
memory_write(0xB8000, 65)

// Escribir 1 byte (útil para VGA texto, puertos MMIO, etc.)
memory_write_byte(0xB8000, 65)   // Escribe 'A' en el buffer VGA
```

### Ejemplo bare-metal: hola mundo en VGA

```stola
// Escribe 'H' con atributo 0x0F (blanco sobre negro) en VGA text mode
VGA = 0xB8000
memory_write_byte(VGA, 72)        // 'H'
memory_write_byte(VGA plus 1, 15) // atributo
memory_write_byte(VGA plus 2, 105)// 'i'
memory_write_byte(VGA plus 3, 15)
```

En modo freestanding el compilador emite **instrucciones inline directas** (`mov [rax], cl`, `mov rax, [rax]`) sin llamadas a función, lo que es adecuado para entornos donde no hay libc disponible.

---

## Cómo Compilar

StolasScript es un compilador de front-end que genera Assembly `.s` (sintaxis Intel). El ensamblaje en un binario final se realiza con `clang` o `gcc`. **Soporta Windows y Linux.**

---

### Windows

#### Requisitos
- `clang` (viene con LLVM o Visual Studio Build Tools)

#### 1. Compilar el compilador `s.exe`

```cmd
clang src/main.c src/lexer.c src/parser.c src/ast.c src/semantic.c src/codegen.c -o s.exe
```

#### 2. Traducir `.stola` a Assembly

```cmd
s.exe mi_programa.stola mi_programa.s
```

#### 3. Ensamblar y enlazar

```cmd
clang mi_programa.s src/runtime.c src/builtins.c -lws2_32 -lwinhttp -o mi_programa.exe
```

```cmd
.\mi_programa.exe
```

---

### Linux / WSL

#### Requisitos
- `gcc`

#### 1. Compilar el compilador `s`

```bash
gcc src/main.c src/lexer.c src/parser.c src/ast.c src/semantic.c src/codegen.c -o s
```

#### 2. Traducir `.stola` a Assembly

```bash
./s mi_programa.stola mi_programa.s
```

#### 3. Ensamblar y enlazar

```bash
gcc mi_programa.s src/runtime.c src/builtins.c -lpthread -ldl -o mi_programa
```

```bash
./mi_programa
```

---

### Modo Freestanding (bare-metal, sin runtime)

```bash
# Windows
s.exe --freestanding mi_programa.stola mi_programa.s

# Linux
./s --freestanding mi_programa.stola mi_programa.s
```
