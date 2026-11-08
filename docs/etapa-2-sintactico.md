# Etapa 2 – Análisis Sintáctico: documentación

Compilador **c-tds** para C-TDS. Implementación en C con **flex**, **bison** y **make**.
Checklist de la etapa: [etapa-2-sintactico-checklist.md](etapa-2-sintactico-checklist.md).
Explicación de cada archivo y función: [guia-del-codigo.md](guia-del-codigo.md).

## 1. División del trabajo

El proyecto lo realiza una sola persona, **Damián Dalio**, que se encargó de todas las
tareas de la etapa.

## 2. Decisiones de diseño, asunciones y aclaraciones

La especificación (`00-TDS-proyecto` y `01-TDS-spec-lenguaje`) deja abiertos varios detalles
del analizador sintáctico. Estas son las decisiones tomadas antes de escribir la gramática.

| # | Tema | Decisión |
|---|------|----------|
| 1 | Alcance | Solo se verifica que la secuencia de tokens cumpla la gramática. Todo lo que dice la sección "Reglas Semánticas" de la spec (existencia de `main`, tipos, declaración antes de uso, tipo de `return`, cantidad y tipos de argumentos) **no** se verifica en esta etapa. |
| 2 | Qué produce el parser | Un **árbol de parsing**, no solo aceptar o rechazar. Es la única forma de comprobar con tests que la precedencia y la asociatividad agrupan bien (por ejemplo, que `a + b * c` es `a + (b * c)`). |
| 3 | Archivo `.sint` | Es la salida de la etapa (la cátedra lo define como la extensión de salida del análisis sintáctico). Contiene el árbol de parsing en texto, un nodo por línea y 2 espacios de sangría por nivel. Los nodos internos muestran el nombre del no terminal; las hojas muestran `NOMBRE_TOKEN lexema`, igual que el `.lex`. Los signos de puntuación (`;`, `,`, paréntesis y llaves) no aparecen porque no aportan información al árbol. |
| 4 | Cuándo se genera | Solo si no hubo errores léxicos ni sintácticos. Nombre por defecto: el del fuente con extensión `.sint`, junto al fuente; `-o <salida>` lo reemplaza. |
| 5 | Target por defecto | `parse`, porque es la etapa actual _(actualización etapas 3 y 4: el target por defecto pasó a ser `sem` y hoy es `codinter`; `-target parse` sigue generando el `.sint` igual que antes)_. `-target scan` sigue disponible y genera el `.lex` como antes. |
| 6 | Errores sintácticos | Formato: `archivo:línea:columna: syntax error, unexpected X, expecting Y`, por `stderr`. La posición es la del token que no pudo aceptarse. Los nombres de los tokens se muestran legibles (`identifier`, `';'`) y el texto "unexpected / expecting" lo arma bison. |
| 7 | Recuperación de errores | **No hay recuperación**: el parser se detiene en el primer error sintáctico. Evita errores en cascada falsos y mantiene simple la gramática. Los errores léxicos siguen informándose todos, igual que en la etapa 1; si hay errores léxicos el parseo continúa para poder informar también un error sintáctico, pero no se genera `.sint`. |
| 8 | Código de salida | `0` correcto, `1` errores léxicos o sintácticos, `2` error de uso o de archivos. |
| 9 | Precedencia | La de la tabla de la spec, de mayor a menor: `-` unario, `!`, `* / %`, `+ -`, `< >`, `==`, `&&`, `\|\|`. Todos los operadores binarios asocian a izquierda, como pide la spec ("los operadores con la misma precedencia son evaluados de izquierda a derecha"). Que `a < b < c` o `a == b == c` sean sintácticamente válidos no es un problema: el chequeo de tipos los rechaza después. |
| 10 | Programa vacío | Es sintácticamente válido, porque la gramática admite cero declaraciones. La obligación de tener `main` es semántica. |
| 11 | Orden de declaraciones | Por la gramática, las variables globales van **antes** de los métodos, y dentro de un bloque las declaraciones de variables van **antes** de las sentencias. Una declaración fuera de lugar es un error sintáctico. |
| 12 | `if` y `while` | Sus cuerpos son siempre un `<block>` con llaves, así que no existe el problema del `else` colgante. |
| 13 | Llamada vs. asignación | `x = e;` y `f(...);` empiezan con un identificador; se distinguen mirando el token siguiente (`=` o `(`), por lo que alcanza con un token de anticipación (LALR(1)). |
| 14 | Programa de ejemplo de la spec | Tiene una llave `}` de más al final; se considera una errata y los tests usan el ejemplo sin ella (igual que en la etapa 1). |
| 15 | `while` sin paréntesis | La gramática de la spec define `while <expr> <block>`, sin paréntesis propios. Se respeta tal cual: `while (x) {...}` es válido porque `(x)` es una `<expr>`, y `while x < 3 {...}` también lo es. |

## 3. Diseño y decisiones clave

### Gramática (`src/bison.y`)

La gramática es la de la spec llevada a bison, con estas adaptaciones que no cambian el lenguaje
aceptado:

- **Repeticiones.** `x*`, `x+` y las listas separadas por coma (`{x}+,`) se escriben como reglas
  recursivas a izquierda, que usan poca pila en un parser LR.
- **Precedencia en vez de niveles.** `<expr>` se deja ambigua (`expr op expr`) y los conflictos se
  resuelven con `%left` / `%precedence`, en el orden de la tabla de la spec. Es más corta y legible
  que una cascada de no terminales (`term`, `factor`, ...) y el árbol no tiene nodos intermedios
  vacíos. El `-` unario usa el nivel `UMINUS` mediante `%prec`.
- **`program` con listas no vacías.** `<var_decl>* <method_decl>*` escrita literalmente produce un
  conflicto shift/reduce: al ver `int` bison no puede saber si debe cerrar la lista de variables
  (vacía) o empezar una declaración. Se resuelve escribiendo `program` como cuatro alternativas
  (vacío, solo variables, variables y métodos, solo métodos) con listas no vacías. Así variables y
  métodos comparten el prefijo `type id` y se distinguen con el token siguiente: `(` es un método;
  `;` o `,` es una variable. El lenguaje aceptado es el mismo.
- **`block`.** Aquí las listas vacías sí alcanzan: tras `{`, una declaración empieza con un tipo y
  una sentencia nunca empieza con un tipo, por lo que no hay ambigüedad.
- **Tokens.** Se llaman igual que en `src/tokens.h` (`TOK_KW_INT`, `TOK_ID`, ...). Cada uno tiene un
  nombre legible para los mensajes de error (`"identifier"`, `"int"`, `"="`).

### Verificación de la gramática

- `bison -Wall -Wcounterexamples` no informa conflictos shift/reduce ni reduce/reduce y no emite
  warnings. El archivo declara `%expect 0`, así que cualquier conflicto futuro hará fallar la
  generación del parser.
- Se probó contra el programa de ejemplo de la spec y contra programas válidos e inválidos
  (variables sin métodos, métodos sin variables, `if`/`else`, `while`, `return` con y sin valor, `;`
  suelto, bloques anidados, llamadas anidadas, `else` sin `if`, declaración después de una sentencia,
  `if` sin llaves, argumentos o parámetros mal formados, variable después de un método, función dentro
  de función, llave sin cerrar).
- La precedencia y la asociatividad se comprobaron con la traza de reducciones de bison. Por
  ejemplo, `a + b * c` reduce primero la multiplicación, `a - b - c` reduce la primera resta antes de
  leer la `c` (asociatividad a izquierda), `a == b < c` reduce primero `<`, y `! - a` reduce primero
  el `-` unario.

### Cambios en la etapa 1 (tokens compartidos con bison)

En la etapa 1 los códigos de los tokens eran un `enum TokenType` escrito a mano en `src/tokens.h`.
Bison necesita los códigos de los tokens para el parser y los define por su cuenta (a partir de los
`%token` de `bison.y`), por lo que mantener también el `enum` habría significado tener dos listas
que deben coincidir. Se eligió que **bison sea la única definición**:

- `src/tokens.h` ya no define el `enum`: incluye `build/bison.tab.h`, el header que genera
  `bison -d`.
- `src/tokens.c` pasó de un arreglo indexado a un `switch` sobre `enum yytokentype`. Bison numera sus
  tokens desde 258, así que el arreglo ya no sirve. El `switch` no tiene `default`, de modo que si se
  agrega un `%token` y se olvida su nombre, `gcc -Wswitch` avisa.
- Los nombres `TOK_*` de `bison.y` son los mismos que ya usaba el scanner, por lo que
  **`src/scanner.l` no necesitó cambios** y el formato del `.lex` es idéntico.
- El `Makefile` genera `build/bison.tab.h` con `bison -d` y hace que dependan de él el scanner, `main.c`
  y `tokens.c`. Cambiar `bison.y` recompila lo que corresponde.
- El target `scan` funciona igual que antes: los 28 tests de la etapa 1 siguen pasando.

## 4. Detalles de implementación

### Árbol de parsing (`src/tree.h`, `src/tree.c`)

El árbol es genérico: cada nodo tiene una etiqueta, un texto opcional (el lexema, solo en las hojas) y una
lista de hijos. No se modelan tipos de nodo distintos porque en esta etapa el árbol solo se escribe en el
`.sint`. Las listas de la gramática (`var_decl*`, `{id}+,`, argumentos) se arman como nodos auxiliares
`list` y se pasan al nodo padre con `node_adopt`, de modo que en el árbol final los elementos son hijos
directos y no hay nodos intermedios vacíos.

| Construcción | Nodo y sus hijos |
|--------------|------------------|
| programa | `program`: declaraciones de variables y luego de métodos |
| variables | `var_decl`: tipo (`KW_INT int`) e identificadores (`ID a`, `ID b`, ...) |
| método | `method_decl`: tipo de retorno (`KW_INT` o `KW_VOID`), `ID`, `params`, `block` |
| parámetros | `params` con un `param` (tipo, `ID`) por parámetro; `params` sin hijos si no hay |
| bloque | `block`: sus `var_decl` y luego sus sentencias |
| asignación | `assign`: `ID`, expresión |
| llamada | `call`: `ID` y los argumentos |
| `if` / `if` con `else` | `if`: condición, bloque; `if_else`: condición, bloque, bloque del `else` |
| `while` | `while`: condición, bloque |
| `return` | `return` (sin hijos) o `return` con la expresión |
| sentencia vacía (`;`) | `empty_stmt` |
| expresión binaria | `binary_expr`: izquierda, operador (`PLUS +`), derecha |
| expresión unaria | `unary_expr`: operador (`MINUS -` o `NOT !`), expresión |
| identificador, literal | hoja: `ID x`, `INT_LITERAL 3`, `FLOAT_LITERAL 1.5`, `KW_TRUE true` |

Los paréntesis de una expresión no generan nodo: el agrupamiento ya está en la forma del árbol. Los signos
de puntuación y las palabras `if`, `else`, `while` y `return` tampoco generan hojas, porque el nombre del
nodo ya los implica.

### Valores semánticos y memoria

- El scanner crea la hoja (`leaf_new`) de los tokens que aportan información al árbol y la entrega en
  `yylval.node`: tipos, identificadores, literales y operadores. Para el resto, `yylval.node` es `NULL`. La
  macro `YY_USER_ACTION` lo reinicia en cada acción, así un token sin hoja nunca arrastra la del anterior.
- El parser es el dueño de las hojas que recibe. Si un error sintáctico lo obliga a descartar símbolos, el
  `%destructor { node_free($$); } <node>` libera el subárbol de cada uno.
- Al aceptar, bison destruye también el valor del símbolo inicial; si `program` guardara la raíz, el árbol se
  liberaría antes de imprimirlo. Por eso la regla `program` guarda la raíz en `parse_root`, devuelve
  `NULL` como valor, y `main.c` libera el árbol después de escribirlo.
- En el target `scan` nadie consume las hojas, así que `main.c` las libera token por token.
- Se verificó con AddressSanitizer y UBSan (ver sección de testing) que no hay pérdidas de memoria ni
  accesos inválidos con programas válidos, con errores léxicos y con errores sintácticos.

### Errores sintácticos

- Se usa `%define parse.error custom` y se implementa `yyreport_syntax_error`. Con el modo `verbose` de
  bison los nombres de tokens salen sin un criterio uniforme (`unexpected ), expecting identifier`, pero
  `unexpected ","`), mientras que con el modo custom los símbolos y palabras reservadas se muestran entre
  comillas simples y las clases de tokens tal cual: `unexpected ')', expecting identifier`.
- La posición es la del último token leído (`tok_line`, `tok_col`), que es el que el parser no pudo aceptar.
- Se lista lo esperado solo si son hasta 4 tokens; con más la lista no ayuda.
- Se declara `%define lr.default-reduction accepting`. Por defecto bison hace "reducciones por defecto":
  reduce aunque el token siguiente no sea válido y recién después detecta el error, por lo que la lista de
  tokens esperados queda incompleta o engañosa. Un caso real que apareció en los tests: un programa que
  empieza con un identificador (`x = 1;`) decía `expecting end of file`, y `g(1 2)` decía `expecting ')'` sin
  mencionar `,` ni los operadores. Con la opción el parser solo reduce con un token válido y el mensaje
  es exacto. El costo es una tabla algo más grande, irrelevante para este lenguaje.
- El scanner tiene una regla de fin de archivo que fija la posición del EOF al final del archivo. Sin ella, un
  error por "fin de archivo inesperado" se informaría en la posición del último token real.
- Los errores léxicos no detienen el parseo: el scanner descarta el símbolo inválido y sigue. Si el
  programa tiene errores léxicos, no se genera el `.sint` aunque el parseo termine bien.

### Interfaz de línea de comandos

- El target por defecto es `parse`. `scan` sigue disponible. `codinter` y `assembly` se rechazan con
  "not available yet".
- Con `-debug` en `parse`: el árbol sale por `stdout` y `yydebug` activa la traza de bison por `stderr`.
- Como el target por defecto cambió, los tests de la etapa 1 pasaron a invocarse con `-target scan`, y el test
  que verificaba que `-target parse` se rechazaba ahora usa `-target codinter`.

### Makefile

`bison -d` genera `build/bison.tab.c` y `build/bison.tab.h` en una sola corrida. El header es el objetivo
de la regla; el `.c` depende de él con una receta vacía para que `make` no corra bison dos veces. Cambiar
`bison.y` recompila el scanner, `main.c`, `tokens.c` y el parser, porque todos incluyen el header generado.

## 5. Testing

Los casos están en `tests/parser/` y los ejecuta `tests/run_tests.sh` (`make test`). Los `.expected` están
escritos a mano a partir de la gramática, no copiados de la salida del compilador, y los casos de errores de
`expecting` se razonaron contra la gramática (ver la nota de `lr.default-reduction` en la sección 4).

**Casos válidos (`tests/parser/valid/`, el `.expected` es el árbol del `.sint`):**

| Caso | Qué cubre |
|------|-----------|
| `empty` | Archivo vacío: el programa vacío es válido. |
| `globals` | Variables globales, lista `int a, b;` y los tres tipos. |
| `methods` | Métodos `void` y con tipo, con y sin parámetros. |
| `vars_and_methods` | Variables globales seguidas de métodos, variable local y asignación. |
| `if_else` | `if` con y sin `else`. |
| `while` | `while` con paréntesis y sin ellos (`while x < 3 {...}`). |
| `return_forms` | `return;` y `return expr;`. |
| `blocks` | Sentencia vacía `;`, bloques anidados, variables locales en un bloque interno. |
| `calls` | Llamada como sentencia, con argumentos, y como parte de una expresión. |
| `literals` | Literales entero, real, `true` y `false`. |
| `precedence` | Precedencia y asociatividad: `a + b * c`, `a * b + c`, `a - b - c`, `-a * b`, `!a && b \|\| c`, `a \|\| b && c`, `a == b < c` y paréntesis. |
| `spec_example` | Programa de ejemplo de la especificación. |
| `comments_layout` | Comentarios, tabs y saltos de línea que no deben cambiar el árbol. |

**Casos inválidos (`tests/parser/errors/`, el `.expected` es el contenido de `stderr`):**

| Caso | Qué cubre |
|------|-----------|
| `missing_semicolon` | Falta `;` antes de `}`. |
| `unclosed_brace`, `error_at_eof` | Error al final del archivo (posición del EOF). |
| `error_first_line` | Error en la primera línea. |
| `else_without_if` | `else` sin `if`. |
| `decl_after_statement` | Declaración de variable después de una sentencia en un bloque. |
| `if_without_braces` | Cuerpo del `if` sin llaves. |
| `incomplete_expr` | Expresión incompleta (`1 + ;`). |
| `param_without_name`, `trailing_comma_params` | Parámetros mal formados. |
| `var_after_method` | Variable global después de un método. |
| `call_missing_comma` | Argumentos sin coma. |
| `nested_method` | Método declarado dentro de otro. |
| `while_extra_paren` | Paréntesis de cierre de más. |
| `statement_at_top_level` | Sentencia fuera de un método. |
| `lexical_then_syntax` | Un error léxico y un error sintáctico en el mismo programa: salen los dos, en orden. |
| `only_lexical_error` | Programa sintácticamente correcto con un carácter ilegal: no se genera `.sint`. |

**Línea de comandos (en el script):** el target por defecto genera `<fuente>.sint` y no `.lex`; `-o` con
`-target parse`; `-target scan` no ejecuta el parser (un programa con error sintáctico igual genera tokens);
archivo inexistente con el target por defecto.

**Regresión y memoria:** los tests de la etapa 1 siguen pasando (con `-target scan`). `make test-asan` corre los
62 casos con un ejecutable compilado con AddressSanitizer y UBSan: no hay pérdidas de memoria ni accesos
inválidos.

**Hallazgos del testing:** al escribir los casos de errores se detectó que los mensajes `expecting` eran
engañosos por las reducciones por defecto de bison. Se corrigió con `%define lr.default-reduction accepting`
(ver sección 4).

## 6. Problemas conocidos

- **Anidamiento muy profundo.** La pila del parser de bison tiene un tope de 10000 niveles. Un programa con
  unos 10000 paréntesis anidados (`((((...a...))))`) o 10000 operadores unarios seguidos (`-----a`) termina
  con `error: memory exhausted` y código de salida 2, sin generar `.sint`. Se detectó generando esos
  programas con un script. No es un problema para programas reales; subir el tope requeriría
  `#define YYMAXDEPTH` en `bison.y`.
- **Expresiones larguísimas.** `tree_print` y `node_free` son recursivas y la profundidad del árbol de una
  cadena `a + a + ... + a` es la cantidad de términos. Con 100000 términos funciona; con 1000000 el
  programa se cae por desborde de la pila de C (segmentation fault). Solucionarlo implicaría reescribir
  ambas funciones con una pila propia; se dejó así por ser una entrada irreal para este lenguaje.
- **Un solo error sintáctico por corrida.** El parser se detiene en el primero (decisión de diseño, ver
  sección 2). Los errores léxicos sí se informan todos.
- **Mensajes `expecting`.** Se omiten cuando son más de 4 tokens esperados, así que en muchos errores dentro
  de expresiones el mensaje solo dice qué token fue inesperado.
- **Formato del `.sint`.** La cátedra no define el formato del árbol de parsing; es el que se eligió en la
  decisión 3. Si más adelante se pidiera otro formato, solo habría que cambiar `print_node` en `tree.c`.
- **El árbol no guarda los paréntesis ni la puntuación.** Es intencional (decisión de diseño), pero
  significa que no se puede reconstruir el texto original a partir del `.sint`.
- **Heredados de la etapa 1:** `-2147483648` no se puede escribir como literal; los literales reales solo se
  validan contra el rango de `double`; los caracteres no ASCII se informan byte por byte; los comentarios
  de bloque no se anidan; el ejemplo de la spec trae una llave `}` de más.

**Resuelto en esta etapa:** `123abc` se reconocía en la etapa 1 como `INT_LITERAL 123` seguido de `ID abc`
sin avisar. Ahora el parser lo rechaza (`unexpected identifier`), que era lo previsto.

