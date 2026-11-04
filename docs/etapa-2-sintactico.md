# Etapa 2 – Análisis Sintáctico: documentación

Compilador **c-tds** para C-TDS. Implementación en C con **flex**, **bison** y **make**.
Checklist de la etapa: [etapa-2-sintactico-checklist.md](etapa-2-sintactico-checklist.md).

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
| 5 | Target por defecto | `parse`, porque es la etapa actual. `-target scan` sigue disponible y genera el `.lex` como antes. |
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
