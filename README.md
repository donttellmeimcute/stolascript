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
10. [Cómo Compilar a `.exe`](#cómo-compilar-a-exe)

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

## Funciones Integradas (Builtins)

El lenguaje cuenta con una extensa librería estándar (escrita en C e incrustada en el runtime):

- **Manipulación de Strings/Arrays**: `len`, `push`, `pop`, `shift`, `unshift`, `string_split`, `string_substring`, `string_replace`, `uppercase`, `lowercase`, etc.
- **Redes (WinSock2)**: `socket_connect`, `socket_send`, `socket_receive`, `socket_close`.
- **HTTP (WinHTTP)**: `http_fetch("https://...")` (soporta HTTPS).
- **Archivos**: `read_file`, `write_file`, `append_file`, `file_exists`.
- **JSON**: `json_encode`, `json_decode`.
- **Utilidades**: `to_string`, `to_number`, `current_time`, `sleep`, `random`, `floor`, `ceil`, `round`.

---

## Cómo Compilar a `.exe`

Dado que StolasScript es un lenguaje compilado frontalmente (Front-End) que vomita Assembly (`.s`), el ensamblaje en un binario final se realiza a través de las herramientas LLVM / `clang`.

### Requisitos Previos

- Compilador de StolasScript (`stolascript.exe`).
- `clang` instalado en el path (suele venir con LLVM o Visual Studio build tools).

### Proceso de Compilación en 2 Pasos

#### 1. Traducir StolasScript a x64 Assembly

Ejecuta tu archivo `.stola` a través del compilador para generar el archivo ensamblador nativo.

```cmd
stolascript.exe mi_programa.stola mi_programa.s
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
