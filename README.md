# StolasScript

StolasScript es un lenguaje de programación moderno, compilado, dinámico pero con soporte para tipado gradual opcional. Está diseñado para ser rápido y generar código máquina (x64 Assembly) de forma nativa.

Actualmente, el compilador genera un archivo ensamblador `.s` (sintaxis Intel) que luego puede ser ensamblado y enlazado usando `clang` o `gcc` (junto con el runtime escrito en C) para producir un archivo ejecutable `.exe` nativo y optimizado, sin depender de máquinas virtuales pesadas en el entorno final.

---

## Índice de Características

1. [Sintaxis Básica](#sintaxis-básica)
2. [Estructuras de Control](#estructuras-de-control)
3. [Funciones](#funciones)
4. [Estructuras de Datos Complejas](#estructuras-de-datos)
5. [Programación Orientada a Objetos (POO)](#programación-orientada-a-objetos)
6. [Tipado Gradual (Opcional)](#tipado-gradual-opcional)
7. [Paralelismo Real (Hilos Win32)](#paralelismo-real)
8. [FFI (Interoperabilidad con C/DLLs)](#ffi-interoperabilidad-nativa)
9. [Funciones Integradas (Builtins)](#funciones-integradas-builtins)
10. [Manejo de Excepciones (Try/Catch)](#manejo-de-excepciones)
11. [Modo Freestanding (Sin Runtime)](#modo-freestanding-sin-runtime)
12. [Soporte de Interrupciones (ISR)](#soporte-de-interrupciones-isr)
13. [Inline Assembly](#inline-assembly)
14. [Cómo Compilar a `.exe`](#cómo-compilar-a-exe)

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

StolasScript escapa de las limitaciones de concurrencia asíncrona simulada de otros lenguajes al implementar **Hilos del Sistema Operativo nativos (Win32)** y Mutexes.

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
- **Redes (WinSock2)**: `socket_connect`, `socket_send`, `socket_receive`, `socket_close`.
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

## Cómo Compilar a `.exe`

Dado que StolasScript es un lenguaje compilado frontalmente (Front-End) que vomita Assembly (`.s`), el ensamblaje en un binario final se realiza a través de las herramientas LLVM / `clang`.

### Requisitos Previos

- Compilador de StolasScript (`s.exe`).
- `clang` instalado en el path (suele venir con LLVM o Visual Studio build tools).

### Proceso de Compilación en 2 Pasos

#### 1. Traducir StolasScript a x64 Assembly

Ejecuta tu archivo `.stola` a través del compilador (`s.exe`).

```cmd
s.exe mi_programa.stola mi_programa.s
```

Para generar código sin runtime (bare-metal):

```cmd
s.exe --freestanding mi_programa.stola mi_programa.s
```

#### 2. Ensamblar y Enlazar con Clang

Ahora debes pasar el punto de inicio `.s` y compilarlo junto con la librería de runtime nativa de C. Necesitas linkear `ws2_32` y `winhttp` para las operaciones de red.

```cmd
clang mi_programa.s src/runtime.c src/builtins.c -lws2_32 -lwinhttp -o mi_programa.exe
```

Listo, ahora tienes un binario compilado nativo:

```cmd
.\mi_programa.exe
```
