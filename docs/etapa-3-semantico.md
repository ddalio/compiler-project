# Etapa 3 – Análisis Semántico: documentación

Compilador **c-tds** para C-TDS. Implementación en C con **flex**, **bison** y **make**.
Checklist de la etapa: [etapa-3-semantico-checklist.md](etapa-3-semantico-checklist.md).
Explicación de cada archivo y función: [guia-del-codigo.md](guia-del-codigo.md).

## 1. División del trabajo

El proyecto lo realiza una sola persona, **Damián Dalio**, que se encargó de todas las
tareas de la etapa.

## 2. Decisiones de diseño, asunciones y aclaraciones

La especificación (`00-TDS-proyecto`, `01-TDS-spec-lenguaje` y las clases de AST y tabla de símbolos) deja
abiertos varios detalles. Estas son las decisiones tomadas **antes** de escribir código. Algunas
interpretan puntos ambiguos de la spec y están marcadas con **(interpretación)**: si la cátedra las
entendiera de otro modo, solo habría que cambiar el chequeo correspondiente.

### Estructuras: AST y tabla de símbolos

| # | Tema | Decisión |
|---|------|----------|
| 1 | Cómo se obtiene el AST | Se construye **recorriendo el árbol de parsing de la etapa 2**, en un paso posterior al parseo (`ast_from_tree`), y no en las acciones de `bison.y`. Alternativa descartada: armar el AST en las acciones del parser, como en la teoría. Se prefirió la conversión porque (a) deja intactos el parser, el `.sint` y sus 30 tests, así que la etapa 2 no puede regresar; (b) mantiene `bison.y` libre de preocupaciones semánticas; (c) el árbol de parsing ya está normalizado (sin puntuación ni paréntesis y con las listas aplanadas), por lo que la conversión es un mapeo casi directo y no una reestructuración. El AST sigue la teoría: tipo de nodo, referencia a la tabla de símbolos y hasta tres hijos (el `if/else` tiene tres). |
| 2 | Cuándo se arma la TS | **Durante un único recorrido del AST**, en orden de aparición en el fuente: al encontrar una declaración se inserta el símbolo, al encontrar un bloque se abre y cierra un nivel, y al encontrar un uso se busca el símbolo. El mismo recorrido hace el chequeo de tipos (la teoría de `07-TDS-ASTyTS` hace el chequeo "recorriendo el AST con la TS"). Como las declaraciones siempre anteceden a los usos en el recorrido, "usar antes de declarar" se detecta naturalmente: el símbolo todavía no está en la tabla. |
| 3 | Vida de los símbolos | Cerrar un nivel **solo lo hace invisible**: los símbolos siguen vivos porque los nodos del AST apuntan a ellos (la teoría guarda referencias a la TS en el AST) y las etapas siguientes los van a necesitar. Los símbolos pertenecen a la tabla y se liberan todos juntos al final. |
| 4 | Niveles | Un nivel global (variables y métodos), un nivel por método y un nivel por cada bloque anidado. Los **parámetros y las variables del cuerpo de un método comparten el mismo nivel** (la spec define el scope de la función como parámetros más variables del cuerpo), por lo que redeclarar un parámetro dentro del cuerpo es un error. El bloque del cuerpo del método no abre un nivel propio. |
| 5 | Nombres | Variables, parámetros y métodos comparten un único espacio de nombres por nivel (regla 1: "variables y funciones deben tener distinto nombre en el scope global"). Una variable local puede ocultar una global y también un método. |
| 6 | Recursión | Un método se inserta en la TS **antes** de analizar su cuerpo, así que puede llamarse a sí mismo. La spec lo permite ("una función puede ser invocada únicamente por código ubicado después de su declaración"). No hay declaraciones adelantadas, así que la recursión mutua no es posible. |

### Salida, CLI y errores

| # | Tema | Decisión |
|---|------|----------|
| 7 | Target | Se agrega **`-target sem`** y pasa a ser el **target por defecto** (la etapa actual). La tabla de targets de la cátedra (`scan`, `parse`, `codinter`, `assembly`) no tiene uno para el análisis semántico, aunque sí define la extensión `.sem`; esto es una **extensión** que conviene confirmar con la cátedra. `scan` y `parse` siguen funcionando igual; `codinter` y `assembly` se siguen rechazando con "not available yet". |
| 8 | Archivo `.sem` | Es la salida de la etapa. Contiene el **AST anotado**: un nodo por línea, 2 espacios de sangría por nivel; las expresiones muestran su tipo y los identificadores muestran su tipo y la clase de símbolo (variable, parámetro o método) a la que resolvieron. El formato exacto se fija al implementar la impresión del AST (sección 2 del checklist) y lo verifican los tests. Se genera solo si no hubo ningún error (léxico, sintáctico ni semántico); por defecto es `<fuente>.sem` junto al fuente y `-o` lo reemplaza. |
| 9 | `-debug` | Con `-debug` el AST anotado sale también por `stdout`, y la traza de la tabla de símbolos (abrir y cerrar nivel, insertar) por `stderr`. Sin `-debug`, una compilación exitosa no imprime nada. |
| 10 | Cuándo corre | El análisis semántico corre **solo si el parseo terminó bien y no hubo errores léxicos**. Un error léxico significa que el programa ya es inválido y los símbolos descartados podrían producir errores semánticos engañosos. |
| 11 | Mensajes de error | Por `stderr`, uno por línea, en el mismo estilo que las etapas anteriores: `archivo:línea:columna: semantic error: mensaje`. Mensajes en inglés, con los identificadores entre comillas simples y los tipos como `int`, `float`, `boolean` y `void` (por ejemplo, `'x' undeclared`). Código de salida `1` (error en el programa fuente), igual que los errores léxicos y sintácticos. |
| 12 | Cantidad de errores | Se **informan todos** los errores semánticos de una corrida, en el orden del fuente. A diferencia del parser (que se detiene en el primero porque, tras un error de sintaxis, el estado queda desincronizado), el recorrido semántico siempre puede continuar. Para no generar errores en cascada, una expresión con error recibe un tipo especial `error` que es compatible con todo y no produce errores nuevos en las expresiones que la contienen. |
| 13 | Posiciones | Cada nodo del árbol de parsing y del AST guarda línea y columna. Las hojas toman la posición del token (la fija el scanner) y los nodos internos la posición de su primer símbolo, usando `%locations` de bison. Un error se informa en la posición de su construcción principal: el operador, en las operaciones; el identificador en las asignaciones y llamadas; la palabra `return`, `if` o `while` en esas sentencias. |

### Reglas de la spec

| # | Tema | Decisión |
|---|------|----------|
| 14 | Coerciones entre `int` y `float` **(interpretación)** | Las reglas 4, 7, 11 y 13 piden "el mismo tipo", pero la regla 14 dice que "se permiten coerciones o truncamientos entre `int` y `float`". Se interpreta que la regla 14 **relaja** a las demás: donde se pide el mismo tipo, `int` y `float` son compatibles entre sí (asignación, `return`, argumentos de una llamada, comparación con `==`). `boolean` no es compatible con ningún otro tipo. Operaciones aritméticas con un `int` y un `float` dan `float`; con dos del mismo tipo, ese tipo. Las comparaciones (`< > ==`) y los operadores lógicos dan `boolean`. |
| 15 | Operador `%` | Se aplica la regla 10 tal cual: sus operandos pueden ser `int` o `float`. La prosa de la spec dice que `%` es el resto entre enteros, pero no lo lista entre las reglas semánticas, así que no se agrega una restricción que la spec no exige. |
| 16 | `main` | Se exige un **método** llamado `main` **sin parámetros** (regla 3). No se restringe su tipo de retorno. Un `main` declarado como variable es un error. |
| 17 | Método con tipo sin `return` | La spec dice que un método que devuelve un valor "no puede alcanzar el fin del método". Se **verifica** con un análisis conservador: una sentencia garantiza `return` si es un `return`, un bloque con alguna sentencia que lo garantiza, o un `if/else` cuyas dos ramas lo garantizan. Un `while` no lo garantiza, salvo que su condición sea el literal `true`. Si el cuerpo no lo garantiza, es un error. Los métodos `void` pueden terminar sin `return`. |
| 18 | Clase del símbolo | Se verifica que cada uso corresponda a la clase correcta: solo las variables y parámetros pueden usarse como valor o ubicación (regla 8), y solo los métodos pueden invocarse. Usar un método como variable o invocar una variable son errores. |
| 19 | Métodos `void` | Pueden invocarse como sentencia y no como parte de una expresión (regla 5). Un método con tipo puede invocarse como sentencia: el resultado se ignora. |
| 20 | Declaración múltiple | `int x, x;` es una redeclaración (regla 1), porque ambos identificadores quedan en el mismo nivel. |
| 21 | Etapa 2 | El árbol de parsing y el `.sint` **no cambian**. Solo se agregan posiciones a los nodos, que el `.sint` no imprime, por lo que sus tests siguen valiendo tal cual. |
| 22 | Programa de ejemplo de la spec | El programa de ejemplo de `01-TDS-spec-lenguaje` es **sintácticamente** válido pero **no semánticamente**: su `main` es `void` y ejecuta `return 1;` y `return inc(y);`, lo que viola la regla 6 (en un método `void`, `return` no puede llevar expresión). El compilador informa esos dos errores. Se respeta la regla y no se la relaja para que el ejemplo pase; en los tests semánticos el ejemplo se usa como caso inválido y se agrega una versión corregida como caso válido. |

## 3. Diseño y decisiones clave

### Posiciones en el árbol de parsing (cambio en etapas anteriores)

Los mensajes de error semántico necesitan línea y columna, y hasta ahora las hojas solo guardaban el lexema.
Para no tocar la gramática se agregó lo siguiente (la decisión 13 de la sección 2):

- `Node` (`tree.h`) tiene los campos `line` y `col`, y la función `node_at` para fijarlos.
- El scanner completa `yylloc` en cada token (macro `SET_YYLLOC`) y la macro `LEAF` fija la posición de cada hoja.
- `bison.y` declara `%locations`. En las acciones, los nodos internos toman la posición del primer símbolo de la
  regla (`@$`, mediante la macro `AT`); un `param` toma la de su tipo (`AT_NODE`), porque en una lista el primer
  símbolo de la regla es la lista completa y no el parámetro.
- El `.sint` **no imprime posiciones**, así que no cambió: los 62 tests de las etapas 1 y 2 siguen pasando sin
  modificaciones.

### AST (`ast.h`, `ast.c`)

Sigue la teoría de `05-TDS-AST`: un nodo con tipo de nodo, referencia a la tabla de símbolos, información extra
(tipo, valor, posición) y **hasta tres hijos** (`left`, `mid`, `right`), de los cuales el `if/else` usa los tres.
La teoría resuelve las secuencias con nodos `;`; acá se eligió una **lista enlazada con `next`**: un hijo que es
una secuencia apunta a su primer elemento. Es más simple de recorrer (un `for` en vez de un descenso por nodos `;`)
y no agrega nodos que no representan nada del programa.

| Nodo | Hijos y datos |
|------|---------------|
| `program` | `left` variables globales, `mid` métodos |
| `var_decl` | nombre y tipo; **una por identificador** |
| `method_decl` | nombre, tipo de retorno, `left` parámetros, `right` cuerpo (raíz del AST de ese método) |
| `param` | nombre y tipo |
| `block` | `left` variables declaradas, `mid` sentencias |
| `assign` | `left` el identificador, `right` la expresión |
| `call` | nombre, `left` argumentos |
| `if` | `left` condición, `mid` bloque *then*, `right` bloque *else* (o nada) |
| `while` | `left` condición, `mid` cuerpo |
| `return` | `left` expresión (o nada) |
| `empty` | — |
| `binary_expr` / `unary_expr` | operador, operandos; posición del operador |
| `id` | nombre |
| `int_literal`, `float_literal`, `bool_literal` | lexema y valor |

Decisiones de este diseño:

- **Un AST por método.** Cada `method_decl` es la raíz del AST de su método (como pide la teoría) y el nodo
  `program` guarda las variables globales y la lista de métodos.
- **Una `var_decl` por identificador.** `int a, b;` genera dos nodos. Cada uno queda en la posición de su
  identificador, de modo que un error de redeclaración apunta al nombre repetido, y el análisis semántico trata
  igual a todos los símbolos.
- **El AST copia lo que necesita.** Nombres y lexemas se copian; después de construirlo se puede liberar el árbol de
  parsing, y el AST no depende de él.
- **El AST no es dueño de los símbolos.** El campo `sym` apunta a una entrada de la tabla de símbolos, que vive en
  la tabla (decisión 3). `ast_free` no los libera.
- **Tipos en el AST.** Las declaraciones (`var_decl`, `param`, `method_decl`) ya traen su tipo desde la construcción.
  Las expresiones empiezan en `TYPE_UNKNOWN` y las completa el análisis semántico. La impresión del AST muestra
  `: tipo` solo cuando el tipo ya está calculado.
- **El árbol de parsing sigue siendo la entrada.** La conversión se apoya en que el parser garantiza la forma del
  árbol; si no coincide (un error del compilador y no del programa) `ast_from_tree` informa un error interno y
  termina con código 2.

### Verificación

La construcción del AST se probó con un programa de prueba temporal (que no se versiona), con un programa que usa
variables globales, parámetros, `if/else`, `while`, llamadas, `-` unario, bloques y sentencia vacía: el AST impreso
y las posiciones de cada nodo (línea y columna) se compararon a mano con el fuente, y la ejecución con
AddressSanitizer y UBSan no mostró errores de memoria, incluso liberando el árbol de parsing antes de usar el AST.

### Tabla de símbolos (`symtab.h`, `symtab.c`)

Sigue la teoría de `06-TDS-TS`: una **pila de niveles**, cada nivel con la lista de sus símbolos, y las
operaciones AbrirNivel, CerrarNivel, InsertarSimbolo y BuscarSimbolo. Un símbolo guarda su clase (variable,
parámetro o método), nombre, tipo, posición de declaración y nivel; los métodos además guardan los tipos de sus
parámetros formales, que se necesitan para verificar las llamadas (regla 4).

Decisiones de este diseño:

- **El nivel global existe desde el principio.** `symtab_new` lo deja abierto, y no se puede cerrar. Así no hay un
  estado "sin niveles" que haya que contemplar al insertar.
- **Insertar detecta la redeclaración.** `symtab_insert` devuelve `NULL` y no inserta si el nombre ya está en el
  nivel actual. Detectar el error y evitar el duplicado es una sola operación, y no puede olvidarse la consulta
  previa. Para informar "previous declaration at ..." se obtiene el símbolo anterior con
  `symtab_lookup_current`.
- **Dos búsquedas distintas.** `symtab_lookup_current` (solo el nivel actual) sirve para la regla 1
  (redeclaración) y `symtab_lookup` (del nivel actual hacia el global) para los usos (regla 2). La segunda
  implementa el ocultamiento: un nombre interno tapa al externo, incluso a un método.
- **Los símbolos sobreviven al cierre del nivel** (decisión 3 de la sección 2). La tabla guarda además una lista de
  todos los símbolos en orden de alta; con ella `symtab_free` libera todo y `symtab_dump` puede volcar el contenido
  completo aunque los niveles ya estén cerrados.
- **Traza.** `symtab_set_trace` activa una línea por operación (`[symtab] insert x (variable int) at level 2`), que es
  lo que se mostrará con `-debug`.
- **Búsqueda lineal.** Cada nivel es una lista y se busca recorriéndola. Es suficiente para programas de este tamaño;
  una tabla hash por nivel sería la mejora si hiciera falta.
- **Cerrar el nivel global es un error de programación** y se comprueba con `assert`: no puede ocurrir con un
  programa de entrada, solo por un uso incorrecto de la tabla.

La tabla todavía no la usa ningún chequeo: abrir un nivel por método y por bloque, y cargarla con las
declaraciones del AST, es el trabajo del recorrido semántico.

Verificación: se probó con un programa temporal (que no se versiona), con las situaciones de la teoría: un símbolo
global visible desde un método, una variable local que oculta a una global, una local que oculta a un método,
el mismo nombre en niveles distintos (válido), la misma declaración dos veces en un nivel (rechazada), la vuelta
a ver el símbolo exterior al cerrar un nivel, símbolos que siguen vivos tras cerrar su nivel y la liberación con un
nivel todavía abierto. Corre sin errores con AddressSanitizer y UBSan.

### Análisis semántico, parte 1: nombres y alcance (`semantic.h`, `semantic.c`)

Un único recorrido del AST en el orden del fuente (decisión 2), que arma la tabla de símbolos a medida que avanza:

1. Se declaran las variables globales y luego se analiza cada método.
2. Para cada método: se lo inserta en el nivel global **antes** de su cuerpo (recursión, decisión 6), se guardan los tipos
   de sus parámetros, se abre un nivel para los parámetros y el cuerpo (que comparten nivel, decisión 4) y se
   analizan sus sentencias.
3. Cada bloque anidado (cuerpo de `if` / `while` o bloque suelto) abre y cierra un nivel propio.
4. Al terminar, se verifica la regla 3 (`main`).

Los mensajes de esta parte (todos con la posición del identificador o de la declaración):

| Regla | Mensaje |
|-------|---------|
| 1 | `redeclaration of 'x' (previous declaration at L:C)` |
| 2 | `'x' undeclared` |
| 3 | `program has no 'main' method`, `'main' must be a method`, `'main' must not have parameters` |
| 8 / decisión 18 | `'f' is a method, not a variable`, `'x' is not a method` |

Decisiones y detalles:

- **El análisis no se detiene en el primer error** (decisión 12): tras un error se sigue recorriendo. Un identificador con
  error recibe `TYPE_ERROR` y no queda vinculado a ningún símbolo; el tipo `error` es el que evitará los errores en
  cascada cuando se agreguen las reglas de tipos.
- **Una declaración repetida queda sin símbolo**, pero el método repetido igual se analiza (parámetros y cuerpo), para
  informar los demás errores que tenga.
- **Orden de los errores.** Se informan en el orden del recorrido: primero las variables globales y luego cada método
  en orden. Dentro de un método, en orden del fuente. El error de `main` va al final.
- **`program has no 'main' method` usa la posición 1:1**, porque no hay ningún lugar del fuente al que apuntar.
- **`ast_print` muestra la clase del símbolo** de los identificadores y llamadas ya vinculados (`id p : int [param]`,
  `call f : int [method]`), según la decisión 8.
- **Los literales ya tienen tipo** (`int`, `float`, `boolean`) y los identificadores y llamadas toman el de su símbolo. La
  propagación del tipo por las operaciones y las reglas de tipos son la parte siguiente.

Verificación: se probó con un programa temporal (que no se versiona). Un programa válido con globales, parámetros,
recursión, una variable que oculta a una global y bloques anidados no da errores, y el AST y la tabla quedan como se
espera (cada identificador vinculado con su símbolo y nivel). Se probaron además 15 programas con errores, uno por
cada mensaje de la tabla (más casos como declarar dos veces en la misma línea, usar una variable fuera de su bloque,
un parámetro que oculta a un método y llamar a un método antes de su declaración) y los mensajes y posiciones
coinciden con los calculados a mano a partir del fuente. Sin errores de memoria con AddressSanitizer y UBSan.

### Análisis semántico, parte 2: tipos (`semantic.c`, `types.c`)

El mismo recorrido calcula el tipo de cada expresión (campo `type` del AST) y verifica las reglas de tipos. El tipo se
calcula de abajo hacia arriba: un literal tiene su tipo, un identificador el de su símbolo, una llamada el de retorno
del método, y una operación lo deduce de sus operandos.

La compatibilidad de tipos está en `types_compatible(esperado, actual)` y es la decisión 14: iguales, o `int` y `float`
entre sí (en asignación, `return`, argumentos y `==`). `boolean` solo es compatible con `boolean`.

| Regla | Qué verifica | Mensaje (posición) |
|-------|--------------|--------------------|
| 4 | Cantidad de argumentos | `call to 'f' has 1 argument, expected 2` (nombre de la llamada) |
| 4 | Tipo de cada argumento | `argument 2 of call to 'f' has type int, expected boolean` (el argumento) |
| 5 | Un método `void` no se usa como expresión | `'v' returns no value and cannot be used in an expression` (la llamada) |
| 6 | `return` con valor en un método `void`, o sin valor en uno con tipo | `void method 'f' cannot return a value`, `method 'g' must return a value of type int` (el `return`) |
| 7 | Tipo del valor de `return` | `cannot return boolean from method 'h' of type int` (el `return`) |
| 9 | Condición de `if` / `while` | `condition of 'if' must be boolean, got int` (la sentencia) |
| 10 | Operandos de `+ - * / % < >` y de `-` unario | `operands of '+' must be int or float, got boolean and int`, `operand of '-' must be int or float, got boolean` (el operador) |
| 11 | Operandos de `==` | `operands of '==' must have the same type, got int and boolean` (el operador) |
| 12 | Operandos de `&&`, `\|\|` y `!` | `operands of '&&' must be boolean, got int and boolean`, `operand of '!' must be boolean, got int` (el operador) |
| 13 | Asignación | `cannot assign boolean to 'x' of type int` (la ubicación) |
| 14 | Coerciones `int` / `float` | No es un error: es lo que relaja a las reglas 4, 7, 11 y 13 |
| decisión 17 | Método con tipo sin `return` en algún camino | `method 'f' may reach the end without returning a value` (nombre del método) |

Decisiones y detalles:

- **Sin errores en cascada** (decisión 12). Una expresión con error tiene `TYPE_ERROR`, que es compatible con todo. Si un
  operando ya tiene error, la operación no informa nada y su resultado también es `TYPE_ERROR`; por eso un identificador
  sin declarar se informa una sola vez aunque se use dentro de varias operaciones, condiciones o asignaciones.
- **Resultado de las operaciones:** aritméticas, `float` si algún operando es `float` y `int` en otro caso (la división de
  dos `int` es `int`); relacionales, `==`, `&&`, `||` y `!`, `boolean`; `-` unario, el tipo del operando.
- **Un solo mensaje por operador**, con los tipos de los dos operandos, en la posición del operador (decisión 13).
- **`return` sin valor en un método con tipo es un error**, porque la spec dice que esos métodos "únicamente retornan con
  una sentencia `return` (que debe tener asociado una expresión)".
- **Análisis de `return` en todos los caminos** (decisión 17, `always_returns`): un `return` garantiza el retorno; un bloque,
  si alguna de sus sentencias lo garantiza; un `if` con `else`, si lo garantizan las dos ramas; un `while`, solo si su
  condición es el literal `true`. Es conservador: un `while (x) { return 1; }` no se acepta como garantía. El error se
  informa en el nombre del método, porque el AST no guarda la posición de la llave de cierre, y después de los errores del
  cuerpo del método.
- **El programa de ejemplo de la spec no pasa el análisis semántico** (decisión 22): `main` es `void` y retorna valores.

Verificación: con un programa temporal (que no se versiona) se probaron 18 programas, uno o más por cada mensaje de la
tabla, más los casos que **no** deben dar error (coerciones `int`/`float` en asignación y `return`, argumentos `float` donde
se espera `int`, `while (true)` sin `return` posterior, `if/else` con `return` en ambas ramas). Los mensajes y posiciones
coinciden con los calculados a mano. Se comprobó también que los tipos se propagan en el AST (`1 + 2.5` es `float`,
`f < 3 && true` es `boolean`) y que no hay errores de memoria con AddressSanitizer y UBSan.

## 4. Detalles de implementación

### Integración en el compilador (`main.c`)

El análisis semántico se conecta al resto del compilador en `run_sem`, que sigue estos pasos:

1. **Lectura:** `parse_source` corre el scanner y el parser (es la parte común con el target `parse`).
2. **Corte por errores previos:** si hubo errores léxicos o sintácticos, el compilador termina con código 1 y **no**
   corre el análisis semántico (decisión 10).
3. **AST:** `ast_from_tree` convierte el árbol de parsing en AST, que no depende de él, y el árbol de parsing se libera.
4. **Análisis:** se crea la tabla de símbolos y `semantic_analyze` recorre el AST; informa todos los errores.
5. **Salida:** si no hubo errores, el AST anotado se escribe en `<fuente>.sem` (o en el archivo de `-o`). Con errores no se
   genera archivo y el código de salida es 1.
6. **Liberación:** se liberan la tabla y el AST.

Con `-debug`, el AST anotado sale por `stdout` y la traza de la tabla de símbolos por `stderr`
(`[symtab] insert x (variable int) at level 2`). La traza del parser de bison solo se activa con `-target parse`, para no
mezclar ruido de otra etapa. Sin `-debug`, una compilación exitosa no imprime nada.

### Cambios en las etapas anteriores

- **Target por defecto:** pasó de `parse` a `sem`. `-target scan` y `-target parse` se comportan igual que antes.
- **Tests:** los casos de las etapas 1 y 2 ya pasaban `-target` explícito, salvo el que probaba el nombre de salida
  por defecto del parser (`.sint`); ahora usa `-target parse`. Los 62 casos siguen pasando.
- **`parse_source`:** se extrajo de `run_parse` la lectura con scanner y parser para compartirla con `run_sem`. El
  comportamiento del target `parse` no cambió.
- **Ejemplo de la spec:** con el target por defecto da dos errores semánticos (decisión 22). Compilado con `-target parse`
  sigue generando su `.sint`.

### Formato del `.sem`

Es el AST impreso con `ast_print` (un nodo por línea, 2 espacios de sangría por nivel): las expresiones muestran su tipo
y los identificadores y llamadas muestran además la clase de símbolo. Por ejemplo, para `y = inc(y);` con `inc` un método
y `y` una variable:

```
assign
  id y : int [variable]
  call inc : int [method]
    id y : int [variable]
```

### Verificación

Se probó el compilador completo con un programa válido (cuyo `.sem` coincide con lo esperado a mano), con el ejemplo de la
spec (dos errores, sin `.sem`), con un error léxico y con un error sintáctico (en ambos no corre el análisis semántico),
con `-o`, con `-debug` y con los otros targets (`scan`, `parse`, y el rechazo de `codinter`). Además se corrió el
compilador compilado con AddressSanitizer y UBSan sobre todos los programas de prueba de las etapas 1 y 2, con y sin
`-debug`, sin errores de memoria. Los tests permanentes de esta etapa se agregan en la sección de testing.

