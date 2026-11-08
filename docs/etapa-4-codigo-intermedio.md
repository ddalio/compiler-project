# Etapa 4 – Generador de Código Intermedio: documentación

Compilador **c-tds** para C-TDS. Implementación en C con **flex**, **bison** y **make**.
Checklist de la etapa: [etapa-4-codigo-intermedio-checklist.md](etapa-4-codigo-intermedio-checklist.md).
Explicación de cada archivo y función: [guia-del-codigo.md](guia-del-codigo.md).

## 1. División del trabajo

El proyecto lo realiza una sola persona, **Damián Dalio**, que se encargó de todas las
tareas de la etapa.

## 2. Decisiones de diseño, asunciones y aclaraciones

La especificación (`00-TDS-proyecto`, `01-TDS-spec-lenguaje`, la clase de código intermedio `05-TDS-GenCodInt` y el
tutorial de generación de assembly de la cátedra) deja abiertos varios detalles. Estas son las decisiones tomadas
**antes** de escribir código. Algunas son extensiones o interpretaciones y están marcadas con **(extensión)** o
**(interpretación)**.

### Representación

| # | Tema | Decisión |
|---|------|----------|
| 1 | Código de tres direcciones | Se genera **código de tres direcciones** (lo pide la cátedra) y no código para máquina pila. Cada instrucción tiene a lo sumo dos operandos y un resultado (`t3 = a + b`). Frente a la máquina pila: (a) es la que usa el tutorial de la etapa siguiente, donde cada variable y cada temporal tienen su lugar en el marco de pila y las operaciones cargan dos operandos en registros; (b) queda más cerca del assembly de x86-64, de dos operandos y un resultado; (c) las optimizaciones de la última etapa (propagación de constantes, eliminación de código muerto) son más naturales sobre nombres explícitos que sobre una pila implícita. |
| 2 | Estructura del código | El programa es: la lista de variables globales y **una lista de instrucciones por método**. Cada método tiene su nombre, sus parámetros, sus variables locales, la cantidad de temporales que usa y sus instrucciones, en orden lineal. Los métodos van en el orden del fuente. |
| 3 | Sin tipos en las instrucciones | Esta etapa solo maneja `int` y `boolean` (decisión 12), y los dos son enteros en el código intermedio: un `boolean` es un entero que vale 0 (falso) o 1 (verdadero). Las instrucciones no llevan tipo. |

### Instrucciones y operandos

| # | Tema | Decisión |
|---|------|----------|
| 4 | Conjunto de instrucciones | Ver la tabla de abajo. Son las mínimas para el lenguaje: copia, aritmética, comparación, negación lógica, etiquetas, saltos, parámetros, llamada y retorno. No hay instrucciones `AND` ni `OR` porque `&&` y `||` se implementan con saltos (decisión 8). |
| 5 | Operandos | Un operando es una **constante entera** (`5`; `true` es `1` y `false` es `0`), una **variable** (global, local o parámetro) o un **temporal**. Las constantes pueden usarse directamente como operando (`t1 = x + 1`). El resultado de una instrucción es siempre una variable o un temporal. |
| 6 | Nombres | Los **temporales** se llaman `t1`, `t2`, ... y las **etiquetas** `L1`, `L2`, ... Las variables **globales** se escriben con prefijo `@` (`@g`), por lo que nunca se confunden con una local o parámetro del mismo nombre. Los locales y parámetros llevan su nombre del fuente; si un método declara más de una variable con el mismo nombre (en bloques distintos, o una local que oculta a un parámetro), la primera conserva el nombre y las siguientes se llaman `x.2`, `x.3`, ... Así cada símbolo tiene un nombre único dentro de su método. |
| 7 | Temporales y etiquetas | Los temporales son **ilimitados** (registros virtuales) y no se reutilizan: cada resultado intermedio recibe un temporal nuevo. Se numeran desde `t1` **en cada método** y el método guarda cuántos usa (lo necesitará la etapa siguiente para calcular el tamaño del marco de pila). Las etiquetas se numeran desde `L1` en **todo el programa**, así son únicas sin importar el método. Reusar temporales es una optimización y corresponde a la última etapa. |

Instrucciones y su texto en el archivo `.ci`:

| Instrucción | Texto | Significado |
|-------------|-------|-------------|
| Copia | `x = a` | `x` toma el valor de `a` (variable, temporal o constante) |
| Aritmética | `t = a + b`, `-`, `*`, `/`, `%` | operación entera; la división y el resto se truncan hacia cero, como en C |
| Negación aritmética | `t = - a` | `t = -a` |
| Comparación | `t = a < b`, `>`, `==` | `t` vale 1 si se cumple y 0 si no |
| Negación lógica | `t = ! a` | `t` vale 1 si `a` es 0, y 0 en otro caso |
| Etiqueta | `L1:` | marca un punto del código |
| Salto | `jmp L1` | salto incondicional |
| Salto si falso | `jz a, L1` | salta a `L1` si `a` vale 0 |
| Salto si verdadero | `jnz a, L1` | salta a `L1` si `a` no vale 0 |
| Parámetro | `param a` | agrega `a` a los argumentos de la próxima llamada |
| Llamada | `t = call f, n` / `call f, n` | llama a `f` con los últimos `n` argumentos cargados; el resultado queda en `t` (solo si `f` devuelve valor) |
| Retorno | `ret a` / `ret` | termina el método devolviendo `a` (o nada) |

### Generación de condiciones y expresiones

| # | Tema | Decisión |
|---|------|----------|
| 8 | Booleanos y cortocircuito | Un `boolean` siempre se **materializa** en una variable o temporal con 0 o 1; una condición es un valor que se prueba con `jz` o `jnz`. Es lo que hace el tutorial de la cátedra (`cmp` guarda un 0 o un 1 y después se compara con 1). El **cortocircuito** de la spec se implementa con saltos: para `a && b` se evalúa `a` en un temporal `r`, `jz r, Lfin`, se evalúa `b` y se copia a `r`, y `Lfin:`; para `a \|\| b` es igual pero con `jnz`. Así `b` solo se ejecuta si `a` no decide el resultado. |
| 9 | Orden de evaluación | De izquierda a derecha (lo exige la spec). Hay un caso delicado: si el operando derecho contiene una **llamada** y el izquierdo es una **variable global**, la llamada podría modificar la global antes de que se lea el izquierdo. En ese caso el valor del izquierdo se **copia a un temporal antes** de evaluar el derecho. Solo las globales pueden ser modificadas por un método llamado (los parámetros se pasan por valor y no hay punteros ni asignaciones dentro de expresiones). La misma regla vale entre los argumentos de una llamada. |
| 10 | Llamadas | Primero se evalúan **todos** los argumentos, de izquierda a derecha, y recién después se emiten los `param` consecutivos y el `call`. Así los `param` de una llamada anidada (`f(g(1), 2)`) no se intercalan con los de la externa, y cada argumento queda listo para pasar en un registro en la etapa siguiente. El resultado de una llamada a un método con tipo se guarda siempre en un temporal, también cuando se usa como sentencia y el valor se descarta (como hace el tutorial). Una llamada a un método `void` no tiene resultado. |
| 11 | Retorno | `ret a` devuelve un valor y `ret` no devuelve nada. Un método `void` termina siempre con un `ret` (el implícito, cuando el cuerpo no termina en un `return`). Un método con tipo no lleva `ret` implícito: el análisis semántico ya garantiza que no puede alcanzar el final. |

### Programa completo

| # | Tema | Decisión |
|---|------|----------|
| 12 | Números reales **(interpretación)** | La cátedra pide código de tres direcciones para las operaciones con tipos **enteros y lógicos** y para el control de flujo, y la etapa siguiente entrega primero los enteros. Por eso el código intermedio **no soporta `float`**: un programa que declare una variable, parámetro o método `float`, o use un literal real, **pasa el análisis semántico pero se rechaza al generar código** con `archivo:línea:columna: codegen error: type 'float' is not supported by the code generator` (un error por cada aparición, en orden del fuente) y no se genera `.ci`. Con `-target sem` esos programas siguen siendo válidos. |
| 13 | Variables globales | Se declaran con `global @x` al principio del archivo, en orden de declaración. Como la gramática no tiene inicializadores, valen **0** (o falso) al empezar el programa. |
| 14 | Variables locales | Se declaran todas con `local x` al principio de su método, en orden de declaración (incluidas las de bloques anidados, con nombres únicos según la decisión 6). Los parámetros se declaran en el encabezado del método. El valor inicial de una local no está definido por la spec; **el intérprete las inicializa en 0** para que la ejecución sea determinista. |
| 15 | Punto de entrada | La ejecución empieza en el método llamado `main` (regla 3 de la spec) y termina cuando éste retorna. |
| 16 | Formato del `.ci` | Un archivo de texto: primero las líneas `global`, y después cada método como `func nombre(p1, p2)`, sus líneas `local`, las instrucciones con 2 espacios de sangría (las etiquetas sin sangría) y `end`. Una línea en blanco separa las secciones. Abajo se muestra un ejemplo completo. |
| 17 | Target | **`-target codinter`** pasa a ser el **target por defecto** (la etapa actual). Corre antes el análisis semántico, y el código solo se genera si no hubo ningún error. La extensión de salida es `.ci` (la define la cátedra). `scan`, `parse` y `sem` siguen funcionando igual; `assembly` sigue sin estar disponible. Con `-debug` el código sale también por `stdout`. |

### Intérprete y programas con entrada y salida

| # | Tema | Decisión |
|---|------|----------|
| 18 | Intérprete **(extensión)** | La calificación de la cátedra incluye un intérprete y la teoría habla de **interpretación iterativa sobre código intermedio**: se implementa un intérprete del código de tres direcciones, que además permite verificar los programas ejecutándolos. La cátedra no define cómo se lo invoca; se agrega la opción **`-run`**, válida solo con `-target codinter`: después de generar el código, lo ejecuta desde `main`. Conviene confirmarlo con la cátedra. |
| 19 | Entrada y salida | La gramática de C-TDS **no tiene sentencias de entrada ni de salida**. El tutorial de assembly de la cátedra muestra un programa con `extern int get_int();` y `extern void printInt(int x);`, pero las declaraciones `extern` no están en la gramática de la spec y este compilador no las acepta (el analizador sintáctico las rechaza). Por eso el único resultado observable de un programa es el **valor que devuelve `main`**: con `-run`, si `main` devuelve un valor (por ejemplo, `int main()`), se imprime en una línea por `stdout`; si `main` es `void` no se imprime nada. Si más adelante la cátedra confirmara las funciones externas, habría que agregarlas a la gramática y al intérprete. |
| 20 | Errores de ejecución | Si un programa divide o calcula el resto por cero, el intérprete informa `runtime error: division by zero` por `stderr` y termina con código de salida 1. Es el único error posible en tiempo de ejecución de un programa que pasó el análisis semántico (no hay punteros, arreglos ni entrada de datos). |

Ejemplo completo del formato (decisiones 6 a 16). El programa:

```
int g;
int fact(int n) {
  if (n < 2) { return 1; }
  return n * fact(n - 1);
}
void main() {
  int r;
  r = fact(5);
  while (r > 0 && g == 0) { r = r - 1; }
}
```

genera:

```
global @g

func fact(n)
  t1 = n < 2
  jz t1, L1
  ret 1
L1:
  t2 = n - 1
  param t2
  t3 = call fact, 1
  t4 = n * t3
  ret t4
end

func main()
  local r
  param 5
  t1 = call fact, 1
  r = t1
L2:
  t2 = r > 0
  t3 = t2
  jz t3, L4
  t4 = @g == 0
  t3 = t4
L4:
  jz t3, L3
  t5 = r - 1
  r = t5
  jmp L2
L3:
  ret
end
```

Cada `jz`/`jmp` sigue las decisiones 7 y 8: `L1` es la etiqueta del `if` sin `else`; `L2` y `L3` son el inicio y el fin del `while`, creadas antes que `L4`, la etiqueta del cortocircuito de `&&`.
