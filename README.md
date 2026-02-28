# StolasScript

StolasScript es un lenguaje de programación compilado de tipado dinámico con una sintaxis expresiva y basada en palabras clave en inglés. Su diseño prioriza la legibilidad al reemplazar muchos símbolos tradicionales (como `+`, `-`, `==`, `&&`) por palabras naturales (`plus`, `minus`, `equals`, `and`), creando un código que se lee casi como texto en inglés.

El compilador de StolasScript analiza el código fuente `.stola` y genera código ensamblador `.s` (o interactúa directamente con una máquina virtual/backend).

---

## 🛠️ Cómo Compilar y Ejecutar

Para compilar el compilador de StolasScript desde su código fuente en C (requiere `clang` o `gcc` de Windows con las librerías `ws2_32` y `winhttp`):

```bash
clang -D_CRT_SECURE_NO_WARNINGS -Isrc src/*.c -lws2_32 -lwinhttp -o stolascript.exe
```

### Usando el Compilador

El proceso de creación de un ejecutable consta de dos pasos: primero StolasScript traduce tu código `.stola` a código ensamblador (`.s`), y luego usas un compilador estándar (como `gcc` o `clang`) para generar el `.exe` final.

1. **Generar código ensamblador (.s):**

   ```bash
   .\stolascript.exe ruta/archivo.stola salida.s
   ```

   *Ejemplo:* `.\stolascript.exe tests\programs\test_import.stola out.s`

2. **Compilar a ejecutable (.exe):**
   Usando `gcc` (si tienes MinGW o similar instalado):

   ```bash
   gcc salida.s -o mi_programa.exe
   ```

   Usando `clang`:

   ```bash
   clang salida.s -o mi_programa.exe
   ```

3. **Ejecutar el programa:**

   ```bash
   .\mi_programa.exe
   ```

---

## 📖 Guía del Lenguaje

### 1. Variables y Tipos de Datos

Las variables se declaran implícitamente mediante la asignación. StolasScript soporta números, cadenas (strings), booleanos, nulos, arreglos (arrays) y diccionarios.

```stola
// Asignación básica
nombre = "Stolas"
edad = 25
es_valido = true
vacio = null

// Arreglos
numeros = [1, 2, 3, 4]
numeros_mixtos = [1, "dos", true]

// Diccionarios
usuario = {
  nombre: "Grimoire",
  nivel: 42
}
```

### 2. Operadores Literales

Uno de los principales atractivos de StolasScript es el uso de palabras para los operadores en lugar de símbolos.

**Aritmética:**

* Suma: `plus` (ej. `a plus b`)
* Resta: `minus` (ej. `a minus b`)
* Multiplicación: `times` (ej. `a times b`)
* División: `divided by` (ej. `a divided by b`)
* Módulo: `modulo` (ej. `a modulo 2`)
* Potencia: `power` (ej. `2 power 3`)

**Comparación:**

* Igual: `equals` (ej. `a equals b`)
* Diferente: `not equals` (ej. `a not equals b`)
* Menor que: `less than` (ej. `a less than b`)
* Mayor que: `greater than` (ej. `a greater than b`)
* Menor o igual: `less or equals`
* Mayor o igual: `greater or equals`

**Lógicos:**

* Y lógico: `and`
* O lógico: `or`
* Negación: `not` (ej. `not true`)

### 3. Accesos a Arreglos y Diccionarios

Puedes acceder a elementos usando corchetes tradicionales `[ ]`, el operador especial `at`, o acceso a propiedades usando un punto `.`.

```stola
// Acceso tradicional o con operador 'at'
primer_numero = numeros[0]
segundo_numero = numeros at 1

// Acceso a propiedades de diccionarios
username = usuario.nombre
```

### 4. Estructuras de Control de Flujo

Las estructuras de control usan la palabra clave `end` para cerrar los bloques correspondientes.

**If / Elif / Else:**

```stola
if edad less than 18
  print("Menor de edad")
elif edad equals 18
  print("Apenas 18")
else
  print("Mayor de edad")
end
```

**While Loop:**

```stola
contador = 0
while contador less than 10
  print(contador)
  contador = contador plus 1
end
```

**Loop Ranged (From / To / Step):**

```stola
loop i from 1 to 10 step 1
  print(i)
end
```

**Match / Case (Switch):**

```stola
estado = "activo"
match estado
  case "activo"
    print("El usuario está conectado")
  case "inactivo"
    print("El usuario está desconectado")
  default
    print("Estado desconocido")
end
```

### 5. Funciones

Las funciones se declaran usando la palabra reservada `function` y devuelven valores con `return`.

```stola
function saludar(nombre)
  return "Hola " plus nombre
end

resultado = saludar("Mundo")
print(resultado)
```

También se pueden pasar funciones como parámetros para otras funciones (Higher-order functions):

```stola
function aplicar(valor, func_callback)
  return func_callback(valor)
end
```

### 6. Sistema de Módulos (Imports)

Puedes dividir tu código en varios archivos `.stola` e importarlos fácilmente usando `import`.

```stola
import prelude
import http
import async

// Usa código definido en los otros módulos
content = read_file("datos.txt")
```

---

## 📚 Librerías Estándar y Funciones Integradas (Built-ins)

StolasScript incluye implementaciones nativas y librerías estándares escritas en el propio lenguaje.

**Built-ins Comunes:**

* `print(valor)`: Imprime en consola.
* `length(coleccion)`: Obtiene el tamaño de un string o array.
* `push(arreglo, elemento)`: Añade un elemento a un arreglo.

**Archivos (I/O):**

* `read_file(ruta)`: Devuelve el contenido de un archivo como string.
* `write_file(ruta, contenido)`: Sobrescribe un archivo.
* `append_file(ruta, contenido)`: Añade al final de un archivo.
* `file_exists(ruta)`: Devuelve `true` si el archivo existe.

**Operaciones de String (en Prelude/Standard Library):**

* `string_substring(str, inicio, fin)`
* `string_starts_with(str, prefijo)`
* `string_index_of(str, busqueda)`
* `string_split(str, delimitador)`

**Módulo HTTP (`import http`):**
Contiene herramientas para peticiones de red. (Ej. `parse_url`, `parse_response`).

**Módulo Async (`import async`):**
Estructuras para manejar código asíncrono, si bien el manejo nativo asincrónico se basa en sus propias primitivas dentro del runtime.

---

*Nota: StolasScript es un proyecto en desarrollo. Sus herramientas semánticas y la implementación nativa del Backend Virtual Machine (VM) / Generación de Código pueden estar sujetas a actualizaciones continuas.*
