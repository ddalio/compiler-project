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
| 21 | Enteros del intérprete | Los enteros son de **32 bits con signo** (lo dice la spec) y las operaciones **dan la vuelta** al desbordar, como en la máquina: `2147483647 + 1` vale `-2147483648`. Se hace con aritmética sin signo para no caer en el comportamiento indefinido de C. `INT_MIN / -1` vale `INT_MIN` y `INT_MIN % -1` vale 0. |
| 22 | Recursión del intérprete | El intérprete es iterativo y lleva su propia pila de llamadas, no usa la de C. La profundidad máxima es de **100000** llamadas anidadas; superarla (por ejemplo, una recursión infinita) es el error de ejecución `runtime error: call stack overflow`, con código de salida 1. Una recursión legítima de decenas de miles de niveles funciona. |

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

## 3. Diseño y decisiones clave

### Representación del código intermedio (`ir.h`, `ir.c`)

Implementa las decisiones 1 a 7 y 16. Un `IrProgram` guarda las variables globales y una lista de `IrFunc`, una por método.
Cada método guarda sus parámetros, sus locales, sus instrucciones (cuádruplas: operación, dos operandos y un resultado) y la
cantidad de temporales que usa.

- **Los operandos son valores, no punteros a nodos.** Un `Operand` es una estructura chica que se copia (tipo, valor
  numérico, variable o nombre de método). Una constante, un temporal, una etiqueta y un método se distinguen por el tipo
  del operando, y una instrucción no necesita campos distintos para cada uno.
- **Todas las instrucciones tienen la misma forma** (`op`, `a`, `b`, `result`). La tabla de `ir.h` indica qué campos usa cada
  operación. Es la representación clásica de cuádruplas y hace que recorrer, imprimir y más adelante traducir sea un
  `switch` sobre la operación.
- **Las variables son únicas por método.** `ir_add_param` e `ir_add_local` asignan el nombre: si el método ya tiene una
  variable con ese nombre, la nueva se llama `x.2`, `x.3`, ... (decisión 6). La regla vive en el TAD y no en el generador,
  por lo que no puede olvidarse. Si el fuente tuviera una variable llamada `x.2` no hay conflicto posible (un identificador
  de C-TDS no puede contener un punto), y el nombre `x.2` ya ocupado se resuelve con `x.2.2`.
- **Los temporales y las etiquetas se numeran en el TAD.** `ir_new_temp` numera por método desde `t1` y deja la cuenta en
  `ntemps`; `ir_new_label` numera todo el programa desde `L1`. El generador solo pide uno nuevo, así los números salen
  en el orden en que se crean.
- **El programa es dueño de todo.** Las variables, las instrucciones y los nombres se liberan con `ir_program_free`. Un
  `OPD_VAR` guarda un puntero a la `IrVar`, que vive en el programa; un `OPD_FUNC` guarda el nombre del método, que vive
  en su `IrFunc`.
- **Dos listas por método, no una global.** Cada método tiene su propia lista de instrucciones, como dice la decisión 2;
  el recorrido de la etapa siguiente puede procesar un método a la vez y calcular el tamaño de su marco de pila con `nparams`,
  `nlocals` y `ntemps`.
- **Los arreglos dinámicos** (globales, parámetros, locales, instrucciones) empiezan con capacidad 8 y la duplican, de
  modo que agregar es de costo amortizado constante.
- **`IrFunc.returns_value`** guarda si el método devuelve un valor: lo necesita el intérprete para saber si una llamada
  deja un resultado y la etapa siguiente para saber qué devolver.

### Verificación

Se probó el TAD con un programa temporal (que no se versiona) que arma con la API el código del ejemplo de la sección 2
(`fact` y `main` con un `while` y el cortocircuito de `&&`) y lo imprime: el resultado coincide **carácter por carácter**
con el ejemplo del documento (se comparó con un script). También se verificó que las variables repetidas se nombren
`x`, `x.2`, `x.3`, que un nombre que ya tiene un punto se resuelva sin colisiones (`x.2.2`), que los contadores de temporales
y etiquetas sean los esperados y que `ir_program_free(NULL)` sea válido. Sin errores de memoria con AddressSanitizer y UBSan.
Los tests unitarios permanentes se agregan en la sección de testing.

### Generador de código (`irgen.h`, `irgen.c`)

Implementa las decisiones 5 a 14 recorriendo el AST anotado en **un solo paso**, en el orden del fuente. Cada método se
genera de forma independiente: se agrega al programa, se declaran sus parámetros y sus locales y se genera su cuerpo.

- **`gen_expr` devuelve un operando.** En vez de generar siempre un temporal, una constante devuelve la constante y un
  identificador su variable: `x = y + 1` no copia `y` ni `1` a temporales. Solo las operaciones, las llamadas y los
  operadores lógicos producen temporales nuevos.
- **La asignación es una copia del valor ya calculado**: `x = a + b` genera `t = a + b` y después `x = t`. No se intenta
  escribir el resultado directo en `x`; eso es una optimización y corresponde a la última etapa.
- **Orden de creación de temporales y etiquetas** (de ello depende la numeración de los tests): una operación evalúa su
  operando izquierdo, después el derecho y *después* pide el temporal del resultado; `&&` y `||` evalúan el izquierdo,
  piden el temporal `r` y la etiqueta de fin y recién entonces evalúan el derecho; un `while` crea sus dos etiquetas
  **antes** de generar la condición; un `if` las crea **después** de generar la condición.
- **Se protegen las globales** (decisión 9): `protect` y la lógica de `gen_call` copian una global a un temporal cuando una
  llamada posterior podría cambiarla. Por ejemplo, `x = g + h();` genera `t1 = @g` antes de `t2 = call h, 0`.
- **El `ret` implícito se agrega siempre a los métodos `void`**, incluso si ya terminan con un `return;` explícito; el código
  queda con dos `ret` seguidos, sin consecuencia. Simplifica el generador (no hay que analizar el flujo) y los métodos con
  tipo no lo necesitan porque el análisis semántico garantiza que terminan en un `return`.
- **Los `float` se rechazan en un recorrido previo** (`check_floats`), antes de generar nada: así se informan todas las
  apariciones juntas y no se genera un programa a medias.
- **Dependencia con la tabla de símbolos.** El generador usa los símbolos del AST (`sym`) para identificar variables. La tabla
  tiene que estar viva mientras se genera; hoy `run_sem` la libera al terminar, y eso se resuelve en la integración.

### Verificación del generador

Con un programa temporal (que no se versiona) se compilaron 8 programas y se comparó el código intermedio generado con el
escrito a mano, línea por línea: el ejemplo de la sección 2 (que sale **idéntico** al del documento), aritmética con `-`
unario, `%` y precedencia, `||` con negación, `if/else`, llamadas con globales como argumentos y como operando izquierdo
(la protección de la decisión 9), llamadas anidadas como argumento, recursión doble (`fib`), llamada a un método `void`
y a uno con tipo como sentencia, variables con el mismo nombre en bloques distintos (`x`, `x.2`, `x.3`) frente a una
global del mismo nombre (`@x`), y cortocircuito anidado (`true && (false || b) && !b`). El programa con `float` se
rechaza con un error por cada aparición y las posiciones correctas. Sin errores de memoria con AddressSanitizer y UBSan.
Los tests permanentes se agregan en la sección de testing.

## 4. Detalles de implementación

### Integración en el compilador (`main.c`)

El target `codinter` reutiliza el front end del target `sem`. Para no duplicarlo, se extrajo de `run_sem` la función
`run_front`, que hace la lectura (scanner y parser), la conversión a AST y el análisis semántico, y la estructura `Front`,
que guarda el resultado: el **AST**, la **tabla de símbolos** y la cantidad de errores semánticos. `run_sem` y
`run_codinter` son ahora dos usos del mismo front end:

1. **Front end** (`run_front`): si hubo errores léxicos o sintácticos, termina con código 1 sin correr el análisis
   semántico (decisión 10 de la etapa 3).
2. **Si hubo errores semánticos**, termina con código 1 y no genera código (decisión 17).
3. **Generación** (`irgen_generate`): si el programa usa `float`, informa un error por cada aparición y devuelve `NULL`; el
   compilador termina con código 1 y no genera `.ci` (decisión 12).
4. **Salida**: el código se escribe en `<fuente>.ci` (o en el archivo de `-o`) con `ir_print`.
5. **Liberación**: la tabla de símbolos y el AST se liberan **después** de generar el código, porque el generador usa los
   símbolos del AST. Antes, `run_sem` los liberaba al terminar el análisis.

Con `-debug`, `codinter` imprime el código también por `stdout` (decisión 17). Sin `-debug`, una compilación exitosa no
imprime nada. La traza de la tabla de símbolos solo se activa con `-target sem`, para no mezclar ruido de otra etapa.

La escritura de la salida se factorizó en `write_output`, que recibe la función que produce el contenido (`ast_print` o
`ir_print`, adaptadas con dos funciones chicas porque tienen distinto tipo de argumento). Así `.sem` y `.ci` se escriben
con el mismo código.

### Cambios en las etapas anteriores

- **Target por defecto:** pasó de `sem` a `codinter`. Los targets `scan`, `parse` y `sem` se comportan igual que antes.
- **`-target codinter` ya no se rechaza.** El test de la línea de comandos que verificaba el mensaje "not available yet"
  ahora usa `-target assembly`, que sigue sin estar disponible.
- **Tests de la etapa 3:** los que dependían del target por defecto (el nombre de salida por defecto del `.sem` y la prueba de
  `-debug`) ahora pasan `-target sem` explícito. Los 119 casos siguen pasando.

### Verificación de la integración

Se probó el compilador completo con el ejemplo de la sección 2: el `.ci` generado con el target por defecto es **idéntico** al
código esperado. También con `-o`, con `-debug` (el código sale por `stdout`), con un programa con `float` (dos errores,
sin `.ci`, mientras que con `-target sem` el mismo programa es válido), con un error semántico y con uno sintáctico (no se
genera código), con `scan`, `parse` y `sem`, y con `assembly` (rechazado). Además se corrió el compilador con
AddressSanitizer y UBSan sobre todos los programas de prueba de las etapas 1 a 3, con y sin `-debug`: sin errores de memoria,
lo que confirma que la tabla de símbolos se libera después de usar el AST. Los tests permanentes se agregan en la sección
de testing.

### Intérprete (`interp.h`, `interp.c`) y opción `-run`

Implementa las decisiones 18 a 22. Es una **interpretación iterativa** sobre el código intermedio, la que describe la teoría
(frente a la interpretación recursiva sobre el AST): mejor verificación de errores y más eficiente.

- **Pila de marcos propia.** En vez de que `exec` se llame a sí misma por cada llamada del programa, el intérprete tiene un
  bucle y una pila de marcos en el heap. Un `call` apila un marco con los argumentos y un `ret` lo desapila y deja el
  valor en el resultado del `call` pendiente del marco de abajo. Así la recursión del programa interpretado no desborda la
  pila de C y el límite es una decisión explícita (decisión 22).
- **Pila de argumentos.** Los `param` apilan valores y el `call` consume los `n` últimos. Como el generador emite todos los
  `param` de una llamada seguidos antes del `call` (decisión 10), los argumentos de llamadas anidadas nunca se mezclan.
- **Cada variable se ubica sin buscarla por nombre.** `IrVar` guarda `is_param` e `index` (se agregaron para esto), y los
  temporales se numeran desde 1, así que la posición de un operando en el marco es un cálculo directo.
- **Las etiquetas se resuelven una sola vez** al preparar los métodos (`label_pos`), de modo que un salto es una asignación.
- **Todo empieza en 0**: los slots de cada marco y las globales (decisiones 13 y 14).
- **`-run`** es válido solo con `-target codinter`; con otro target el compilador informa `option '-run' requires -target
  codinter` y termina con código 2. El código se genera y se escribe en el `.ci` como siempre, y después se ejecuta. Si
  `main` devuelve un valor, se imprime en una línea por `stdout` (un `boolean` sale como 1 o 0). Un error de ejecución sale
  por `stderr` y el compilador termina con código 1. Con `-debug`, el código intermedio se imprime antes del resultado.
- **No hay límite de instrucciones**: un programa con un bucle infinito no termina, igual que en cualquier otro intérprete.

### Verificación del intérprete

Se ejecutaron 21 programas con `-run` y se compararon con el resultado calculado a mano: factorial de 10 (3628800), Fibonacci
de 20 (6765), suma 1..100 (5050), máximo común divisor, recursión de 50000 niveles (1250025000), `-7 / 2 * 100 + -7 % 2`
(-301, división y resto truncados hacia cero), `2147483647 + 1` (-2147483648), `INT_MIN / -1` y `INT_MIN % -1`, globales y locales
inicializadas en 0, un `boolean main`, un `void main` (no imprime nada) y llamadas con argumentos anidados. Dos casos
verifican la semántica del **cortocircuito** (`false && inc()` y `true || inc()` no llaman a `inc`; las otras dos
combinaciones sí) y otros dos verifican el **orden de evaluación** de la decisión 9 (`g + h()` y `two(g, h())`, donde `h`
modifica `g`, dan el resultado de leer `g` antes de la llamada). La división y el resto por cero informan `runtime error:
division by zero` con código 1, y una recursión infinita informa `runtime error: call stack overflow`. Además se comprobó
que `-run` con otro target, con un error semántico y con un programa con `float` se comporta como corresponde. Sin errores
de memoria con AddressSanitizer y UBSan. Los tests permanentes se agregan en la sección de testing.

## 5. Testing

Los casos están en `tests/codinter/` y `tests/unit/` y los ejecuta `tests/run_tests.sh` (`make test`). Los `.expected` están
escritos a mano: el código intermedio de cada caso se derivó a mano aplicando las decisiones de la sección 2 (incluidos el
orden de creación de temporales y etiquetas) y los resultados de los programas se calcularon a mano, antes de correr
nada. No se copió la salida del compilador.

**Tests unitarios (`tests/unit/`).** `test_ir` prueba el TAD del código intermedio: operandos, numeración de temporales (por
método) y etiquetas (por programa), nombres únicos de variables, crecimiento de los arreglos y el **formato de texto de
todas las instrucciones**. `test_interp` prueba el intérprete armando los programas directamente con la API, sin el
generador: cada operación, la aritmética de 32 bits que da la vuelta, la división por cero, los saltos, las globales, el
paso de argumentos, los marcos independientes en la recursión y el desborde de la pila de llamadas. La explicación de cada
función está en `guia-del-codigo.md`, sección 17.

**Código generado (`tests/codinter/valid/`, el `.expected` es el `.ci`):**

| Caso | Qué cubre |
|------|-----------|
| `empty_main` | El código más chico: un método vacío con su `ret` implícito. |
| `globals` | Declaración de globales (`global @g`) y su asignación; `true` como constante 1. |
| `arithmetic` | Precedencia y asociatividad de `+ - * / %`, el `-` unario y los paréntesis. |
| `comparisons_logic` | `< > ==`, `!` y `\|\|` con cortocircuito. |
| `if_else` | `if` con y sin `else`: etiquetas y saltos, con una variable `boolean` como condición. |
| `while_nested` | Dos `while` anidados y la numeración de sus etiquetas. |
| `calls_nested_args` | Argumentos que son llamadas (`f(g(1), 2)`) y `param` consecutivos. |
| `calls_statements` | Recursión doble (`fib`), llamada como sentencia a un método con tipo y a uno `void`, y el `ret` implícito duplicado. |
| `short_circuit` | `&&` y `\|\|` anidados con negación: etiquetas y temporales del cortocircuito. |
| `global_protection` | La copia de una global antes de una llamada (decisión 9), como operando izquierdo y como argumento. |
| `scopes` | Variables con el mismo nombre en bloques distintos (`x`, `x.2`, `x.3`) frente a la global `@x`. |
| `return_forms` | `return` con y sin valor en medio de un método y retorno implícito. |
| `unary_operators` | `- - y`, `-(y + 1)` y `!!b`. |
| `long_expression` | Una cadena de cinco sumas (muchos temporales) y una llamada con cinco argumentos. |
| `spec_example_fixed` | El ejemplo de la spec con `main` corregido (`int main`): sin `ret` implícito y una etiqueta final. |
| `nested_blocks` | Sentencias vacías y bloques anidados. |
| `factorial_and_loop` | El ejemplo completo de la sección 2 (con `while` y `&&`). |

**Programas ejecutados con `-run` (`tests/codinter/run/`, el `.expected` es el `stdout`):** 25 programas que verifican la
semántica y no solo la forma del código: factorial de 10, Fibonacci de 20, suma 1..100, máximo común divisor, Collatz de 27
(111 pasos), Ackermann(2, 3), cantidad de primos menores que 30, potencias, una recursión de 50000 niveles, bucles
anidados, el **cortocircuito** (`short_circuit`: `inc()` solo se llama en dos de las cuatro combinaciones), el **orden de
evaluación** (`eval_order_binary` y `eval_order_args`, donde un método modifica una global), la división y el resto
truncados hacia cero (`-301`), la **vuelta de 32 bits** (`2147483647 + 1`, `INT_MIN / -1`, `INT_MIN % -1`), inicialización en
0, variables que ocultan a otras, un `boolean main` y un `void main` (que no imprime nada).

**Errores de ejecución (`tests/codinter/run_errors/`, el `.expected` es el `stderr`):** `division_by_zero`,
`remainder_by_zero`, `division_by_zero_in_call` y `call_stack_overflow`. Cada uno verifica el mensaje, el código de salida 1 y
que no se imprima ningún resultado.

**Errores de generación (`tests/codinter/errors/`):** un caso por cada lugar donde puede aparecer `float` (variable global,
parámetro, tipo de retorno, literal), uno con cinco apariciones que verifica el orden y las posiciones, y uno donde un error
semántico se informa y no se generan los errores de `float` (el análisis semántico corta antes).

**Línea de comandos (en el script):** el target por defecto genera `<fuente>.ci` y no los archivos de las otras etapas; `-o`
con `-target codinter`; `-run` escribe el `.ci` y ejecuta; `-run` con `-target sem` o `parse` se rechaza con código 2;
`-run -debug` imprime primero el código y después el resultado; `-run` con un error semántico no genera ni ejecuta; y un
programa con `float` sigue siendo válido con `-target sem`.

**Regresión y memoria:** los 119 casos de las etapas 1 a 3 siguen pasando. En total `make test` ejecuta 181 casos y
`make test-asan` los mismos con AddressSanitizer y UBSan, sin errores de memoria. Se comprobó además que los tests detectan
fallas: al cambiar a propósito una etiqueta en un `.expected` de código, un resultado en uno de `-run` y un mensaje en
uno de error de ejecución, los tres casos fallaron.

**Hallazgos del testing:** ningún `.expected` calculado a mano necesitó correcciones y no se encontraron errores en el
generador ni en el intérprete. El caso del `ret` duplicado en un método `void` que termina con `return;`
(`calls_statements`) deja documentado ese comportamiento (decisión 11).

