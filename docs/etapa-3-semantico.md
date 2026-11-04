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
