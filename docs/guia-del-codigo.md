# Guía del código

Para estudiar el proyecto: qué hace cada archivo y cada función. Describe el código **tal como está hoy**
(etapas 1 y 2). Se actualiza en cada avance.

## 1. Vista general

```
 archivo.ctds
      │
      ▼
  scanner.l ──(flex)──► yylex(): devuelve el siguiente token
      │                         + yylval.node = hoja del árbol (si el token la tiene)
      ▼
  bison.y   ──(bison)─► yyparse(): pide tokens a yylex() y arma el árbol de parsing
      │
      ▼
  tree.c: tree_print() ─► archivo.sint            (target parse)
  main.c: run_scan()   ─► archivo.lex             (target scan)

  Etapa 3: árbol de parsing ──► ast.c: ast_from_tree() ──► AST
           semantic.c: semantic_analyze(AST, tabla de símbolos) ──► main.c: run_sem() ──► archivo.sem  (target sem)
  Etapa 4: AST anotado + tabla de símbolos ──► irgen.c: irgen_generate() ──► ir.c: ir_print() ──► archivo.ci  (target codinter)
```

| Archivo | Para qué sirve |
|---------|----------------|
| `src/main.c` | Programa principal: interpreta la línea de comandos y ejecuta la etapa pedida. |
| `src/scanner.l` | Analizador léxico (flex): convierte caracteres en tokens. |
| `src/scanner.h` | Interfaz del scanner para el resto del código. |
| `src/bison.y` | Analizador sintáctico (bison): gramática, precedencia y armado del árbol. |
| `src/parser.h` | Interfaz del parser para el resto del código. |
| `src/tokens.h`, `src/tokens.c` | Códigos de los tokens y sus nombres imprimibles. |
| `src/tree.h`, `src/tree.c` | Árbol de parsing: estructura, armado, impresión y liberación. |
| `src/types.h`, `src/types.c` | Los tipos del lenguaje (`int`, `float`, `boolean`, `void`) y sus nombres. |
| `src/ast.h`, `src/ast.c` | Árbol Sintáctico Abstracto (AST): estructura, conversión desde el árbol de parsing, impresión y liberación. |
| `src/symtab.h`, `src/symtab.c` | Tabla de símbolos: pila de niveles con los símbolos (variables, parámetros y métodos) de cada scope. |
| `src/semantic.h`, `src/semantic.c` | Análisis semántico: recorre el AST con la tabla de símbolos y verifica las reglas de la spec (hoy: nombres y alcance). |
| `src/ir.h`, `src/ir.c` | Código intermedio (código de tres direcciones): operandos, instrucciones, métodos, impresión y liberación. |
| `src/irgen.h`, `src/irgen.c` | Generador de código intermedio: recorre el AST anotado y produce el código de tres direcciones. |
| `Makefile` | Compila todo y corre los tests. |
| `tests/run_tests.sh` | Corre todos los casos de test. |

Archivos generados (carpeta `build/`, no se versionan): `lex.yy.c` (lo genera flex desde `scanner.l`),
`bison.tab.c` y `bison.tab.h` (los genera bison desde `bison.y`).

## 2. `src/tokens.h` y `src/tokens.c`

`tokens.h` incluye `bison.tab.h`, donde bison define una constante por cada `%token` de `bison.y`
(`TOK_EOF = 0`, `TOK_KW_BOOLEAN = 258`, ...). Así scanner y parser comparten una sola definición.

| Función | Qué hace |
|---------|----------|
| `const char *token_name(int token)` | Recibe el código de un token y devuelve su nombre para el `.lex` y el `.sint` (`TOK_KW_IF` → `"KW_IF"`). Es un `switch` sin `default` sobre `enum yytokentype`: si se agrega un `%token` y se olvida acá, `gcc -Wswitch` avisa. Los valores internos de bison (`YYEMPTY`, `YYerror`, `YYUNDEF`) y el pseudo-token `UMINUS` están listados porque el scanner nunca los devuelve. |

## 3. `src/scanner.h` y `src/scanner.l`

Un archivo `.l` de flex tiene tres partes separadas por `%%`: definiciones (código C entre `%{ %}`, macros
de expresiones regulares), **reglas** (`expresión regular  { acción en C }`) y código C final.
Cómo decide flex: toma el **lexema más largo** que matchee y, ante empate, la **regla que aparece primero**.
Por eso las palabras reservadas van antes que `{ID}` (`while` empata y gana la primera) y `whiletrue` es un
`ID` (es más largo que cualquier palabra reservada).

### Variables y macros

| Nombre | Qué es |
|--------|--------|
| `tok_line`, `tok_col` | Posición (desde 1) donde empieza el último token. Las usan `main.c` y los mensajes de error. |
| `lex_errors` | Cantidad de errores léxicos desde `scanner_init`. |
| `cur_line`, `cur_col` | Posición del *próximo* carácter a leer. |
| `src_name` | Nombre del archivo, solo para los mensajes de error. |
| `comment_line`, `comment_col` | Dónde empezó el comentario `/* */` en curso, para informar "unterminated comment". |
| `SET_YYLLOC()` | Macro que copia la posición del token a `yylloc` (inicio: `tok_line`/`tok_col`; fin: `cur_line`/`cur_col`). Bison la usa, con `%locations`, para darle ubicación a cada símbolo (`@1`, `@$`). |
| `YY_USER_ACTION` | Macro que flex ejecuta **antes de cada acción**: guarda `tok_line`/`tok_col`, avanza la posición con `advance`, llama a `SET_YYLLOC()` y pone `yylval.node = NULL`. |
| `LEAF(tok)` | Macro para los tokens que van al árbol: crea una hoja con el lexema y **su posición** (`leaf_new` + `node_at`), la deja en `yylval.node` y devuelve el token. |
| `yylloc` | Variable de bison: la ubicación (`first_line`, `first_column`, `last_line`, `last_column`) del token que acaba de devolver el scanner. |
| `yytext`, `yyleng` | Variables de flex: texto y largo del lexema reconocido. |
| `yylval` | Variable de bison: el "valor semántico" del token que acaba de devolver el scanner. |

### Funciones

| Función | Qué hace |
|---------|----------|
| `static void advance(const char *text, int len)` | Recorre el lexema y actualiza `cur_line`/`cur_col`: un `\n` pasa a la línea siguiente y columna 1; cualquier otro carácter suma una columna (un tab cuenta 1). |
| `static void lex_error(int line, int col, const char *fmt, ...)` | Suma uno a `lex_errors` y escribe en `stderr` `archivo:línea:columna: lexical error: mensaje`. Es variádica como `printf`. |
| `void scanner_init(const char *filename, FILE *in)` | Prepara una lectura nueva: guarda el nombre, reinicia posición y contador de errores, apunta flex a `in` (`yyrestart`) y vuelve al estado inicial. Está al final del archivo porque `INITIAL` se define en el código que genera flex. |
| `const char *scanner_source_name(void)` | Devuelve el nombre del archivo; lo usa `bison.y` en sus mensajes de error. |
| `int yylex(void)` | La genera flex a partir de las reglas. Devuelve el código del próximo token (`TOK_*`) y `TOK_EOF` (0) al final. |

### Reglas, de arriba abajo

- **Espacios**: `[ \t\r\n]+` se descarta. El `\r` permite archivos con fin de línea de Windows.
- **Comentario de línea**: `"//".*` (el `.` no incluye `\n`, que lo consume la regla de espacios).
- **Comentario de bloque**: al ver `/*` se entra en el *start condition* exclusivo `BLOCK_COMMENT`, donde
  solo valen las reglas que empiezan con `<BLOCK_COMMENT>`: `*/` vuelve al estado normal y el resto
  (`[^*\n]+`, `*`, `\n`) se descarta. Si llega el fin de archivo dentro del comentario se informa
  `unterminated comment` en la posición donde empezó.
- **Palabras reservadas** (`boolean`, `else`, ... `while`): `boolean`, `false`, `float`, `int`, `true` y
  `void` usan `LEAF` (llevan hoja); `else`, `if`, `return` y `while` devuelven solo el token.
- **`{ID}`**: `[A-Za-z][A-Za-z0-9_]*`, con `LEAF`.
- **Entero** `{DIGIT}+`: se convierte con `strtoll` y si no cabe en 32 bits con signo (≤ 2147483647) se
  informa error léxico y **no se devuelve token**.
- **Real** `{DIGIT}+"."{DIGIT}+`: se convierte con `strtod`; si desborda `double` es error léxico.
- **Operadores**: `+ - * / % < > == && || !` con `LEAF`; `=` solo el token.
- **Delimitadores**: `; , ( ) { }`, solo el token.
- **Fin de archivo** (`<<EOF>>`): fija la posición del EOF al final del archivo y devuelve `TOK_EOF`.
- **Errores**: `[&|]` (un `&` o `|` suelto, sugiere `&&` / `||`) y `.` (cualquier otro carácter; si no es
  imprimible se informa su byte en hexadecimal).

Una acción que informa un error y **no hace `return`** hace que flex siga leyendo: así se reportan todos
los errores léxicos de una corrida.

## 4. `src/tree.h` y `src/tree.c`

`Node` es un nodo del árbol: `label` (nombre del no terminal o del token, un literal de C que no se
libera), `text` (lexema; solo las hojas), `kids` (arreglo dinámico de hijos), `nkids` (cuántos hay), `cap`
(capacidad reservada) y `line`/`col` (posición en el fuente; 0 si no se conoce). La posición no se imprime en
el `.sint`: la usan las etapas siguientes para informar errores.

| Función | Qué hace |
|---------|----------|
| `static void *xmalloc(size_t n)` | `malloc` que, si falla, informa "out of memory" y termina el programa con código 2. |
| `Node *node_new(const char *label)` | Crea un nodo interno sin hijos. |
| `Node *leaf_new(const char *label, const char *text)` | Crea una hoja: nodo con el lexema **copiado**. |
| `Node *node_at(Node *n, int line, int col)` | Fija la posición de un nodo y devuelve el mismo nodo (para poder escribir `node_at(node2(...), l, c)`). |
| `Node *node_add(Node *parent, Node *child)` | Agrega un hijo al final. Cuando el arreglo se llena duplica la capacidad (empieza en 4). Devuelve `parent` para poder encadenar. |
| `Node *node1(label, a)` … `node4(label, a, b, c, d)` | Atajos: nodo con 1 a 4 hijos. Cada uno llama al anterior y agrega un hijo. |
| `Node *node_adopt(Node *dst, Node *list)` | Mueve los hijos de `list` al final de los de `dst` y libera la cáscara `list`. Sirve para convertir las listas auxiliares del parser en hijos directos del nodo padre. |
| `static void print_node(FILE *out, const Node *n, int depth)` | Imprime un nodo con `2 * depth` espacios de sangría, su etiqueta y, si es hoja, el lexema; después imprime los hijos con `depth + 1` (recursiva). |
| `void tree_print(FILE *out, const Node *root)` | Imprime el árbol completo; acepta `NULL`. |
| `void node_free(Node *n)` | Libera el nodo y todo su subárbol (hijos, arreglo, texto y nodo). Acepta `NULL`. |

## 5. `src/parser.h` y `src/bison.y`

Un archivo `.y` de bison también tiene tres partes: declaraciones (tokens, tipos, precedencia), **reglas de
gramática** (`no_terminal : símbolos { acción } | ... ;`) y código C final. Bison genera un parser **LALR(1)**:
una pila de símbolos; en cada paso *desplaza* (lee el próximo token) o *reduce* (reemplaza los símbolos
de la cima por el no terminal de una regla y ejecuta su acción). En una acción, `$$` es el valor del
no terminal que se arma y `$1`, `$2`, ... son los valores de los símbolos de la regla.

### Declaraciones de `bison.y`

| Declaración | Para qué |
|-------------|----------|
| `%define parse.error custom` | Los errores sintácticos los informa nuestra función `yyreport_syntax_error`. |
| `%define parse.trace` | Habilita la traza del parser (se activa con `yydebug = 1`, o sea `-debug`). |
| `%define lr.default-reduction accepting` | El parser reduce solo si el token siguiente es válido; así la lista "expecting" de los errores es exacta. |
| `%expect 0` | Falla la generación si hay algún conflicto shift/reduce o reduce/reduce. |
| `%code requires { ... }` | Código que bison copia también en `bison.tab.h` (acá, el `#include "tree.h"`). |
| `%code { ... }` | Código que va al principio de `bison.tab.c`: includes y las macros `AT` y `AT_NODE`. |
| `%locations` | Cada símbolo tiene una ubicación: `@1`, `@2`, ... y `@$` para la regla completa (la de su primer símbolo). El scanner las completa en `yylloc`. |
| `AT(n, loc)` | Macro de `bison.y`: `node_at(n, loc.first_line, loc.first_column)`. Se usa como `AT(node2(...), @$)` para dar al nodo la posición del primer símbolo de la regla. |
| `AT_NODE(n, src)` | Macro de `bison.y`: da al nodo la posición de otro nodo (se usa en `param`, cuya posición es la de su tipo). |
| `%union { Node *node; }` | Todo símbolo con valor guarda un `Node *`. |
| `%destructor { node_free($$); } <node>` | Si el parser descarta un símbolo (por un error), libera su subárbol. |
| `%token <node> TOK_ID "identifier"` | Declara un token con valor `Node *` y el nombre legible para los mensajes. Los tokens sin `<node>` no llevan valor. |
| `%left`, `%precedence` | Precedencia, de **menor a mayor** según se declaran; resuelve la ambigüedad de `expr op expr`. `%left` = asocia a izquierda. |
| `%type <node> ...` | Declara que esos no terminales tienen valor `Node *`. |
| `%start program` | Símbolo inicial. |

### Reglas y forma del árbol

| No terminal | Qué reconoce | Árbol que arma |
|-------------|--------------|----------------|
| `program` | `var_decl* method_decl*` (escrita con 4 alternativas y listas no vacías para evitar un conflicto) | `program` con las declaraciones como hijos. Guarda la raíz en `parse_root` y devuelve `NULL` como valor. |
| `var_decl_list`, `method_decl_list`, `id_list`, `block_var_decls`, `statement_list`, `arg_list` | Repeticiones | Nodo auxiliar `list` que luego se adopta con `node_adopt`. |
| `var_decl` | `tipo id, id ... ;` | `var_decl(tipo, id, ...)` |
| `method_decl` | `(tipo \| void) id ( params ) block` | `method_decl(tipo, id, params, block)` |
| `param_list_opt`, `param_list` | Parámetros formales | `params(param(tipo, id), ...)`; `params` vacío si no hay |
| `block` | `{ var_decl* statement* }` | `block(var_decl..., sentencias...)` |
| `type` | `int \| boolean \| float` | La hoja del token |
| `statement` | asignación, llamada, `if`, `if/else`, `while`, `return`, `;`, bloque | `assign`, `call`, `if`, `if_else`, `while`, `return`, `empty_stmt`, `block` |
| `method_call` | `id ( args )` | `call(id, args...)` |
| `expr` | identificador, llamada, literal, operaciones, paréntesis | hoja, `call`, `binary_expr(izq, op, der)`, `unary_expr(op, expr)`; los paréntesis no generan nodo |
| `literal` | entero, `true`, `false`, real | La hoja del token |

### Funciones del código final

| Función | Qué hace |
|---------|----------|
| `Node *parse_root` | Variable global donde `program` deja la raíz del árbol. Quien llama a `yyparse` la libera con `node_free`. |
| `static void print_symbol(FILE *out, yysymbol_kind_t kind)` | Escribe el nombre de un símbolo para un mensaje de error: las clases de tokens (`identifier`, `integer literal`, `float literal`, `end of file`) tal cual; los demás (`')'`, `'while'`) entre comillas simples. |
| `static int yyreport_syntax_error(const yypcontext_t *ctx)` | Bison la llama ante un error sintáctico. Arma `archivo:línea:col: syntax error, unexpected X[, expecting A or B]`. La posición es `tok_line`/`tok_col` (el último token leído, o sea el que no se pudo aceptar). Pide a bison los tokens esperados (`yypcontext_expected_tokens`) y los lista solo si son entre 1 y 4. Devuelve 0, o un valor negativo si se agotó la memoria. |
| `void yyerror(const char *msg)` | Informa otros errores del parser (por ejemplo memoria agotada) con la posición actual. |

`parser.h` declara lo que `main.c` necesita: `yyparse` (devuelve 0 correcto, 1 error sintáctico, 2 sin
memoria), `parse_root`, `yyerror` y, vía `bison.tab.h`, `yydebug`.

## 6. `src/main.c`

| Función | Qué hace |
|---------|----------|
| `static void usage(FILE *out)` | Imprime la ayuda de uso. |
| `static int has_extension(const char *name, const char *ext)` | Indica si `name` termina en `ext` y tiene algo antes (por eso `.ctds` solo no vale). |
| `static int check_target(const char *target)` | Acepta `scan`, `parse`, `sem` y `codinter`; para `assembly` informa "not available yet" y para cualquier otro "unknown target". (`sem` es el análisis semántico: la cátedra lo ejecuta como parte de la compilación y le asigna la extensión `.sem`, pero no lo lista entre los targets.) |
| `static int parse_args(int argc, char **argv, Options *opt)` | Recorre los argumentos y completa `Options`: `-o` y `-target` toman el argumento siguiente, `-debug` es una bandera y cualquier otro argumento que empiece con `-` es una opción desconocida (por eso un archivo no puede empezar con `-`). Valida que haya un solo archivo de entrada, que termine en `.ctds` y que el target sea válido. El target por defecto es `codinter`, la etapa actual. |
| `static char *output_path(const Options *opt, const char *stage_ext)` | Calcula el archivo de salida: el de `-o` si se dio; si no, el del fuente cambiando `.ctds` por la extensión de la etapa (`.lex`, `.sint`). Devuelve memoria que hay que liberar con `free`. |
| `static FILE *open_input(const Options *opt)` | Abre el fuente; si falla informa el motivo. |
| `static int write_failed(const char *path)` | Informa que no se pudo escribir la salida y devuelve el código 2. |
| `static int run_scan(const Options *opt)` | Etapa scan: llama a `yylex` en un bucle, acumula `línea:columna NOMBRE lexema` en memoria (`open_memstream`) y libera la hoja de cada token. Si no hubo errores léxicos escribe el `.lex`; con `-debug` imprime las líneas por `stdout`. |
| `static int parse_source(const Options *opt, int trace)` | Lee el fuente con el scanner y el parser (lo usan `run_parse` y `run_sem`). Devuelve el resultado de `yyparse` (0 correcto, 1 error sintáctico, 2 sin memoria) o -1 si no se pudo abrir el archivo. Si devuelve 0, `parse_root` es el árbol de parsing y el que llama debe liberarlo; con errores léxicos también devuelve 0, así que hay que mirar `lex_errors`. `trace` activa la traza de bison. |
| `static int run_parse(const Options *opt)` | Etapa parse: llama a `parse_source` (con la traza de bison si hay `-debug`) y, si no hubo errores ni sintácticos ni léxicos, escribe el árbol en el `.sint`. Con `-debug` también lo imprime por `stdout`. Libera el árbol al final. |
| `Front`, `static void front_free(Front *f)` | `Front` agrupa el resultado del front end: el AST analizado, **la tabla de símbolos con la que se lo analizó** y la cantidad de errores semánticos. La tabla tiene que seguir viva mientras se use el AST (sus nodos referencian símbolos); `front_free` libera las dos cosas. |
| `static int run_front(const Options *opt, int trace, Front *f)` | Front end completo, común a los targets `sem` y `codinter`: llama a `parse_source`; si hubo errores léxicos o sintácticos devuelve el código 1 **sin correr el análisis semántico**; si no, convierte el árbol de parsing en AST (`ast_from_tree`) y lo libera, crea la tabla de símbolos (con traza por `stderr` si `trace`) y corre `semantic_analyze`. Devuelve `EXIT_OK` si llegó a correr el análisis semántico: entonces `f` tiene el AST, la tabla y la cantidad de errores y el que llama debe liberarlos con `front_free`. |
| `static int write_output(opt, stage_ext, print, what)` | Abre el archivo de salida (el de `-o` o el del fuente con la extensión de la etapa), escribe ahí lo que produce la función `print` y lo cierra, informando si no se pudo. Lo usan `run_sem` y `run_codinter`. |
| `print_ast_cb`, `print_ir_cb` | Adaptan `ast_print` e `ir_print` a la firma que espera `write_output`. |
| `static int run_sem(const Options *opt)` | Etapa sem: corre el front end y, si no hubo errores semánticos, escribe el AST anotado en el `.sem`. Con `-debug` imprime el AST por `stdout` y la traza de la tabla por `stderr`. |
| `static int run_codinter(const Options *opt)` | Etapa codinter: corre el front end y, solo si no hubo ningún error, llama a `irgen_generate` (que devuelve `NULL` si el programa usa `float`) y escribe el código de tres direcciones en el `.ci`. Con `-debug` lo imprime también por `stdout`. La tabla de símbolos y el AST se liberan recién **después** de generar el código. |
| `int main(int argc, char **argv)` | Interpreta los argumentos y llama a `run_scan`, `run_parse`, `run_sem` o `run_codinter` según el target. |

Códigos de salida: 0 correcto, 1 errores en el programa fuente (léxicos o sintácticos), 2 error de uso o de
archivos.

## 7. `Makefile`

| Objetivo | Qué hace |
|----------|----------|
| `make` / `all` | Construye `./c-tds`. |
| `build/bison.tab.h` | Corre `bison -d` sobre `bison.y`: genera `bison.tab.c` y `bison.tab.h` de una vez. |
| `build/bison.tab.c` | Depende del header (receta vacía) para que bison no corra dos veces. |
| `build/lex.yy.c` | Corre `flex` sobre `scanner.l`. |
| `build/*.o` | Compila cada archivo; todos dependen de `bison.tab.h` porque incluyen `tokens.h`. |
| `build/test_symtab`, `build/test_ast` | Ejecutables de los tests unitarios (`tests/unit/`). Usan los objetos del compilador menos `main.o` (la variable `LIBOBJS`). |
| `make test` | Compila el compilador y los tests unitarios y corre `tests/run_tests.sh` con `UNIT_DIR=build` (el script corre además los unitarios). |
| `make test-asan` | Corre los mismos tests (también los unitarios) con todo compilado con AddressSanitizer y UBSan (detectan pérdidas de memoria y accesos inválidos). |
| `make clean` | Borra el ejecutable y las carpetas generadas. |

## 8. `tests/run_tests.sh`

Cada caso es un `.ctds` con un `.expected` escrito a mano. Se ejecuta con `c-tds` parado en la carpeta del caso,
para que los mensajes lleven solo el nombre del archivo.

| Función | Qué hace |
|---------|----------|
| `ok`, `bad` | Cuentan y muestran un caso aprobado / fallido (con el detalle, por ejemplo el `diff`). |
| `run <dir> <args...>` | Ejecuta `c-tds`, guarda `stdout` en `$TMP/stdout`, `stderr` en `$TMP/stderr` y el código de salida en `$status`. |
| `cli <desc> <exit> <texto> -- <args>` | Verifica un error de uso: código de salida y que `stderr` contenga el texto. |

Grupos de pruebas:

| Grupo | Carpeta | Qué verifica |
|-------|---------|--------------|
| Scanner válidos | `tests/scanner/valid/` | Código 0, nada por pantalla, `.lex` igual al esperado y mismo contenido por `stdout` con `-debug`. |
| Scanner errores | `tests/scanner/errors/` | Código 1, no se genera salida, `stderr` igual al esperado. |
| Parser válidos | `tests/parser/valid/` | Código 0, nada por pantalla, `.sint` igual al esperado y mismo árbol por `stdout` con `-debug`. |
| Parser errores | `tests/parser/errors/` | Código 1, no se genera salida, `stderr` igual al esperado. |
| Unitarios | `tests/unit/` | Corre `test_symtab` y `test_ast` (si `UNIT_DIR` está definido, como hace `make test`); cada uno debe terminar con código 0. |
| Semántico válidos | `tests/semantic/valid/` | Código 0, nada por pantalla, `.sem` (AST anotado) igual al esperado y mismo AST por `stdout` con `-debug`. |
| Semántico errores | `tests/semantic/errors/` | Código 1, no se genera salida, `stderr` igual al esperado (mensaje y posición exactos). |
| Línea de comandos | (en el script) | Errores de uso, nombres de salida por defecto y con `-o`, y cada target: `-target sem` genera `.sem`, `-target parse` y `-target scan` siguen funcionando, `-debug` muestra el AST y la traza de la tabla, y `-target parse` no corre el análisis semántico. |

## 9. `src/types.h` y `src/types.c`

`Type` es un `enum` con los tipos del lenguaje: `TYPE_INT`, `TYPE_FLOAT`, `TYPE_BOOLEAN`, `TYPE_VOID` (retorno de un
método sin valor), `TYPE_UNKNOWN` (todavía no calculado) y `TYPE_ERROR` (una expresión con error: se la trata como
compatible con todo para no encadenar errores).

| Función | Qué hace |
|---------|----------|
| `const char *type_name(Type t)` | Nombre del tipo para mensajes y salidas: `"int"`, `"float"`, `"boolean"`, `"void"`, `"error"`, `"unknown"`. |
| `int type_is_numeric(Type t)` | Verdadero para `int` y `float`. |
| `int types_compatible(Type expected, Type actual)` | Indica si un valor de tipo `actual` sirve donde se espera `expected`: tipos iguales, o `int` y `float` entre sí en cualquier sentido (regla 14: coerciones y truncamientos). `boolean` no es compatible con ningún otro tipo. `TYPE_ERROR` es compatible con todo, para no encadenar errores. |

## 10. `src/ast.h` y `src/ast.c`

El **AST** (Árbol Sintáctico Abstracto) es el árbol que recorre el análisis semántico. Se arma a partir del árbol de
parsing y, a diferencia de éste, cada nodo tiene un **tipo de nodo** (`AstKind`), el **tipo** de la expresión
(`Type`), una **referencia a la tabla de símbolos** (`sym`) y su posición. Tiene hasta tres hijos (`left`, `mid`,
`right`; el `if/else` usa los tres) y las secuencias (declaraciones, sentencias, parámetros, argumentos) son listas
enlazadas con `next`: un hijo que es una secuencia apunta a su primer elemento.

| `AstKind` | Hijos y datos |
|-----------|---------------|
| `AST_PROGRAM` | `left` = variables globales, `mid` = métodos |
| `AST_VAR_DECL` | `name`, `type` (una por cada identificador declarado) |
| `AST_METHOD_DECL` | `name`, `type` = retorno, `left` = parámetros, `right` = cuerpo. Es la raíz del AST del método |
| `AST_PARAM` | `name`, `type` |
| `AST_BLOCK` | `left` = variables declaradas, `mid` = sentencias |
| `AST_ASSIGN` | `left` = `AST_ID` (ubicación), `right` = expresión |
| `AST_CALL` | `name`, `left` = argumentos |
| `AST_IF` | `left` = condición, `mid` = bloque *then*, `right` = bloque *else* (o `NULL`) |
| `AST_WHILE` | `left` = condición, `mid` = cuerpo |
| `AST_RETURN` | `left` = expresión (o `NULL`) |
| `AST_EMPTY` | sentencia vacía `;` |
| `AST_BINARY` | `op`, `left`, `right`; su posición es la del **operador** |
| `AST_UNARY` | `op`, `left`; su posición es la del operador |
| `AST_ID` | `name` |
| `AST_INT_LIT`, `AST_FLOAT_LIT`, `AST_BOOL_LIT` | `name` = lexema, `value` (`i`, `f` o `b`) |

`OpKind` enumera los operadores (`OP_ADD`, `OP_SUB`, ..., `OP_NOT`, `OP_NEG`); el `-` unario es `OP_NEG` y el binario
`OP_SUB`.

| Función | Qué hace |
|---------|----------|
| `Ast *ast_node(kind, line, col, left, mid, right)` | Crea un nodo con los tres hijos dados (pueden ser `NULL`); tipo `TYPE_UNKNOWN`, sin nombre ni símbolo. |
| `Ast *ast_leaf(kind, line, col)` | Hoja: nodo sin hijos. |
| `Ast *ast_from_tree(const Node *program)` | Convierte el árbol de parsing (la raíz `program` que deja el parser) en el AST. **Copia** todo lo que necesita, así que después se puede liberar el árbol de parsing. |
| `const char *ast_kind_name(AstKind)` | Nombre de un nodo para imprimirlo (`"method_decl"`, `"binary_expr"`, ...). |
| `const char *op_text(OpKind)` | Texto de un operador (`"+"`, `"&&"`, `"!"`, ...). |
| `void ast_print(FILE *out, const Ast *root)` | Escribe el AST: un nodo por línea con 2 espacios de sangría por nivel. La línea es `<nodo> [nombre u operador] [: tipo] [[clase]]`; el tipo aparece solo si ya se calculó (las declaraciones lo traen desde la construcción) y la clase del símbolo (`variable`, `param`, `method`) en los identificadores y llamadas que el análisis semántico ya vinculó con la tabla. Por ejemplo: `id p : int [param]`. |
| `void ast_free(Ast *n)` | Libera el nodo, sus hijos y toda la lista de `next`. No libera los símbolos (no son del AST). |

Funciones internas de `ast.c` (todas `static`):

| Función | Qué hace |
|---------|----------|
| `xmalloc`, `xstrdup` | `malloc` y copia de cadena que terminan el programa si no hay memoria. |
| `Chain`, `chain_add` | Lista en construcción con puntero al primer y al último elemento, para agregar al final sin recorrerla. |
| `internal_error` | El árbol de parsing no tiene la forma esperada: es un error del compilador (no del programa de entrada); informa y termina con código 2. |
| `is(n, label)` | Indica si un nodo del árbol de parsing tiene esa etiqueta. |
| `type_of_leaf(leaf)` | De una hoja `KW_INT`/`KW_FLOAT`/`KW_BOOLEAN`/`KW_VOID` al `Type`. |
| `op_of_leaf(leaf, unary)` | De una hoja de operador (`PLUS`, `MINUS`, ...) al `OpKind`; `unary` distingue el `-` unario del binario. |
| `named(kind, id)` | Crea un nodo con el nombre y la posición de una hoja. |
| `convert_expr`, `convert_stmt`, `convert_call`, `convert_block`, `convert_var_decl`, `convert_method` | Convierten cada construcción del árbol de parsing. `convert_var_decl` genera **una `AST_VAR_DECL` por identificador** (`int a, b;` son dos nodos), cada una en la posición de su identificador; así un error de "redeclarado" apunta al nombre. Los paréntesis ya no existen en el árbol de parsing, y `AST_BINARY` toma la posición del operador. |
| `print_node`, `print_list` | Impresión recursiva: `print_list` imprime una lista de hermanos (`next`) al mismo nivel. `print_node` agrega la clase del símbolo (`[variable]`, `[param]`, `[method]`) si el nodo es un `id` o un `call` con `sym`. |

## 11. `src/symtab.h` y `src/symtab.c`

La **tabla de símbolos** (TS) guarda qué significa cada nombre del programa. Es una **pila de niveles**: cada nivel
es la lista de símbolos de un scope. El nivel 1 es el global (variables y métodos); al entrar a un método o a un
bloque se abre un nivel y al salir se cierra. Para resolver un nombre se busca desde el nivel actual hacia el global,
de modo que un nombre interno **oculta** al externo con el mismo nombre.

`Symbol` es una entrada de la tabla:

| Campo | Qué guarda |
|-------|------------|
| `cls` | Clase: `SYM_VARIABLE`, `SYM_PARAM` o `SYM_METHOD`. |
| `name` | Nombre (copiado). |
| `type` | Tipo de la variable o parámetro; tipo de retorno en un método. |
| `line`, `col` | Dónde se declaró (para mensajes como "redeclared, previous declaration at ..."). |
| `level` | Nivel en el que se declaró (1 = global). |
| `param_types`, `nparams` | Solo métodos: los tipos de los parámetros formales, en orden (para verificar las llamadas). |
| `next_in_level`, `next_all` | Enlaces internos: lista de los símbolos del nivel y lista de todos los símbolos. No usarlos desde afuera. |

Internamente, `Level` es un nivel (primer y último símbolo y el nivel que lo contiene) y `SymTab` guarda el nivel
actual, la cantidad de niveles abiertos, la lista de **todos** los símbolos y el archivo de traza.

| Función | Qué hace |
|---------|----------|
| `SymTab *symtab_new(void)` | Crea una tabla con el nivel global ya abierto (profundidad 1). |
| `void symtab_free(SymTab *tab)` | Libera todos los símbolos (también los de niveles ya cerrados) y los niveles que hayan quedado abiertos. |
| `void symtab_set_trace(SymTab *tab, FILE *out)` | Si `out` no es `NULL`, cada operación (abrir, cerrar, insertar) escribe una línea `[symtab] ...` ahí. Es la traza de `-debug`. |
| `void symtab_open_level(SymTab *tab)` | AbrirNivel: apila un nivel nuevo. |
| `void symtab_close_level(SymTab *tab)` | CerrarNivel: desapila el nivel actual. Sus símbolos **dejan de ser visibles pero siguen existiendo**, porque los nodos del AST los referencian. El nivel global no se puede cerrar (`assert`). |
| `int symtab_depth(const SymTab *tab)` | Cantidad de niveles abiertos. |
| `Symbol *symtab_insert(tab, cls, name, type, line, col)` | InsertarSimbolo: agrega el símbolo al nivel actual y lo devuelve. Si ya hay uno con ese nombre **en el nivel actual** no lo agrega y devuelve `NULL`: así la detección de una redeclaración y la inserción son una sola operación. |
| `void symtab_set_params(Symbol *method, const Type *types, int n)` | Guarda (copiando) los tipos de los parámetros formales de un método. |
| `Symbol *symtab_lookup_current(tab, name)` | Busca solo en el nivel actual (por ejemplo, para obtener el símbolo anterior al informar una redeclaración). |
| `Symbol *symtab_lookup(tab, name)` | BuscarSimbolo: busca desde el nivel actual hasta el global y devuelve el primero que encuentra, el más interno. |
| `const char *sym_class_name(SymClass)` | `"variable"`, `"param"` o `"method"`. |
| `void symtab_dump(FILE *out, const SymTab *tab)` | Escribe todos los símbolos insertados, en orden de declaración, una línea cada uno: `level 1 method f : int (int, float) @2:5`. |

Funciones internas (`static`): `xmalloc` (con error si no hay memoria), `level_new` (crea un nivel) y
`find_in_level` (búsqueda lineal de un nombre en la lista de un nivel).

## 12. `src/semantic.h` y `src/semantic.c`

El **análisis semántico** verifica lo que la gramática no puede expresar. Hace un único recorrido del AST **en el orden
del fuente** y arma la tabla de símbolos a medida que avanza. Como las declaraciones siempre se recorren antes que sus
usos, un nombre que todavía no está en la tabla es un uso antes de la declaración (regla 2).

Qué niveles abre el recorrido: el global (ya abierto), uno por cada **método** (con sus parámetros y las variables de
su cuerpo, que comparten nivel) y uno por cada **bloque anidado** (el cuerpo de un `if`/`while` o un bloque suelto).

El análisis verifica **nombres y alcance**: la regla 1 (redeclaración), la 2 (uso antes de declarar), la 3 (`main`) y la 8
(la ubicación de una asignación), más que un método no se use como variable ni una variable como método. Y **tipos**: la
regla 4 (argumentos de una llamada), la 5 (un método `void` no es una expresión), las 6 y 7 (`return`), la 9
(condiciones), la 10 (operandos aritméticos y relacionales), la 11 (`==`), la 12 (operadores lógicos), la 13
(asignación) y la 14 (coerciones `int`/`float`), más que un método con tipo no pueda terminar sin `return`.

**Cómo se calcula el tipo de una expresión** (campo `type` del AST): un literal tiene su tipo; un identificador, el de su
símbolo; una llamada, el tipo de retorno del método. Una operación aritmética da `float` si algún operando es `float` y
`int` en otro caso; las relaciones (`< > ==`) y los operadores lógicos dan `boolean`; el `-` unario conserva el tipo
del operando. Si una operación tiene un error, su tipo es `TYPE_ERROR`, que es compatible con todo: así un error no
produce otros en las expresiones que lo contienen (por eso `y + 1` con `y` sin declarar informa un solo error).

`semantic.h` expone una sola función:

| Función | Qué hace |
|---------|----------|
| `int semantic_analyze(Ast *program, const char *filename, SymTab *tab)` | Analiza el programa (la raíz `AST_PROGRAM`). `tab` debe ser una tabla recién creada. Informa cada error por `stderr` (`archivo:línea:col: semantic error: mensaje`), sigue después de cada uno y devuelve la cantidad de errores. Deja en el AST los símbolos (`sym`) y los tipos de identificadores, llamadas y literales. |

Funciones internas de `semantic.c` (todas `static`). `Ctx` agrupa el estado: la tabla, el nombre del archivo, la cantidad
de errores y el método que se está analizando (lo necesita `return`).

| Función | Qué hace |
|---------|----------|
| `sem_error(c, line, col, fmt, ...)` | Cuenta e imprime un error semántico en esa posición. Es variádica como `printf`. |
| `declare(c, decl, cls)` | Inserta una declaración (variable, parámetro o método) en el nivel actual y guarda el símbolo en `decl->sym`. Si el nombre ya está en ese nivel informa `redeclaration of 'x' (previous declaration at L:C)` (regla 1) y la declaración queda sin símbolo. |
| `resolve_variable(c, id)` | Resuelve un identificador usado como valor o como ubicación: busca desde el nivel actual al global. Si no existe: `'x' undeclared` (reglas 2 y 8); si es un método: `'x' is a method, not a variable`. Si está bien, `id->sym` es el símbolo y `id->type` su tipo; si no, el tipo es `TYPE_ERROR`. |
| `resolve_call(c, call)` | Igual para el nombre de una llamada: tiene que existir (`'f' undeclared`) y ser un método (`'x' is not a method`). El tipo de la llamada es el tipo de retorno del método. |
| `analyze_expr(c, e)` | Calcula el tipo de una expresión y verifica sus reglas: resuelve identificadores, da tipo a los literales, delega en `analyze_call`, `analyze_binary` o `analyze_unary`. Una llamada usada como expresión no puede ser de un método `void` (regla 5: `'v' returns no value and cannot be used in an expression`). |
| `analyze_call(c, call)` | Analiza una llamada (como sentencia o como expresión): la resuelve, analiza los argumentos y verifica la regla 4. Si la cantidad no coincide: `call to 'f' has 1 argument, expected 2`; si coincide, compara el tipo de cada argumento con el del parámetro (con coerciones): `argument 2 of call to 'f' has type int, expected boolean`. |
| `analyze_binary(c, e)` | Analiza una operación binaria. Si un operando ya tiene error, no informa nada y el resultado es `TYPE_ERROR`. Para `==` verifica la regla 11 (`operands of '==' must have the same type, got int and boolean`); para `&&` y `||`, la 12 (`operands of '&&' must be boolean, got ...`); para el resto llama a `check_numeric_op`. |
| `check_numeric_op(c, e, l, r)` | Regla 10 (`+ - * / % < >`): los dos operandos deben ser `int` o `float` (`operands of '+' must be int or float, got boolean and int`). Calcula el tipo del resultado: `boolean` para `<` y `>`; `float` si algún operando es `float`, si no `int`. |
| `analyze_unary(c, e)` | `!` exige `boolean` (regla 12: `operand of '!' must be boolean, got int`) y `-` exige `int` o `float` (regla 10). |
| `analyze_stmt(c, s)` | Recorre una sentencia: en una asignación resuelve la ubicación, analiza la expresión y verifica la regla 13 (`cannot assign boolean to 'x' of type int`); en `if`/`while` verifica la condición y analiza los bloques; en `return` llama a `check_return`; los bloques sueltos abren un nivel. |
| `check_condition(c, stmt, what)` | Regla 9: la condición de `if` o `while` debe ser `boolean` (`condition of 'if' must be boolean, got int`). Se informa en la posición de la sentencia. |
| `check_return(c, ret)` | Reglas 6 y 7: en un método `void` el `return` no lleva expresión (`void method 'f' cannot return a value`); en uno con tipo debe llevarla (`method 'g' must return a value of type int`) y su tipo debe ser compatible con el de retorno (`cannot return boolean from method 'h' of type int`). |
| `analyze_block(c, block, open_level)` | Declara las variables del bloque y analiza sus sentencias. Abre un nivel propio si `open_level` es 1 (bloques anidados); el cuerpo de un método usa 0 porque comparte nivel con los parámetros. |
| `always_returns(s)` | Análisis conservador de flujo: indica si una sentencia nunca termina "normalmente". Un `return` sí; un bloque, si alguna de sus sentencias sí; un `if` con `else`, si lo hacen las dos ramas (sin `else`, no); un `while`, solo si su condición es el literal `true`; cualquier otra, no. |
| `analyze_method(c, m)` | Inserta el método en el nivel global **antes** de su cuerpo (así puede llamarse a sí mismo), guarda los tipos de sus parámetros con `symtab_set_params`, abre un nivel, declara los parámetros, analiza el cuerpo y cierra el nivel. Si el método tiene tipo y `always_returns` dice que el cuerpo puede terminar sin `return`, informa `method 'f' may reach the end without returning a value` (en la posición del nombre del método, después de los errores del cuerpo). |
| `check_main(c)` | Regla 3: tiene que existir un método `main` sin parámetros. Los errores son `program has no 'main' method` (en la posición 1:1, porque no hay un lugar donde apuntar), `'main' must be a method` y `'main' must not have parameters`. |

## 13. `tests/unit/test_symtab.c` y `tests/unit/test_ast.c`

Son programas en C que prueban directamente funciones del compilador, sin pasar por la línea de comandos. Los arma
`make test` y los corre `tests/run_tests.sh`. Cada uno cuenta sus comprobaciones con la macro `CHECK(cond)` (si no se
cumple, imprime la línea) y termina con código 1 si alguna falló.

### `test_symtab.c`: tabla de símbolos y tipos

| Función | Qué prueba |
|---------|------------|
| `dump_equals(fn, tab, expected)` | Auxiliar: ejecuta `symtab_dump` en memoria (`open_memstream`) y compara con el texto esperado. |
| `test_new_table` | Una tabla nueva tiene solo el nivel global, no encuentra nada y su volcado es vacío. |
| `test_insert_and_duplicates` | `symtab_insert` guarda todos los datos del símbolo; un nombre repetido en el mismo nivel se rechaza (`NULL`) sin cambiar el original; variables y métodos comparten el espacio de nombres. |
| `test_method_params` | Los tipos de los parámetros se **copian** y se pueden redefinir. |
| `test_levels_and_shadowing` | Niveles anidados: el global se ve desde adentro, un nombre interno oculta al externo (incluso a un método), el mismo nombre en niveles distintos es válido y al cerrar un nivel vuelve a verse el exterior. |
| `test_symbols_survive_close` | Al cerrar un nivel sus símbolos dejan de verse pero siguen siendo válidos (el AST los referencia). |
| `test_dump` | El volcado incluye los símbolos de niveles cerrados, en orden de declaración y con la firma de los métodos. |
| `test_trace` | La traza escribe una línea por operación, no escribe nada por una inserción rechazada y deja de escribir al desactivarla. |
| `test_free_with_open_levels` | `symtab_free` con niveles abiertos (lo valida AddressSanitizer en `make test-asan`). |
| `test_types` | `type_name`, `type_is_numeric` y `types_compatible`: iguales, `int`/`float` en los dos sentidos, `boolean` aislado, `void` y `TYPE_ERROR` compatible con todo. |

### `test_ast.c`: AST

`build(src)` parsea un programa desde una cadena con `fmemopen`, lo convierte con `ast_from_tree` y **libera el árbol de
parsing enseguida**, para comprobar que el AST no depende de él. `at(n, line, col)` compara la posición de un nodo.

| Función | Qué prueba |
|---------|------------|
| `test_declarations` | `int a, b;` genera una `var_decl` por identificador, en orden y con la posición de cada uno. |
| `test_method` | Tipo de retorno, parámetros en orden con su posición y el cuerpo (un `block` con su `return`). |
| `test_method_without_params` | Un método sin parámetros no tiene lista de parámetros. |
| `test_if` | `if` con y sin `else`: condición, bloques y posición del `if`; los paréntesis no son un nodo. |
| `test_other_statements` | `while`, sentencia vacía, bloque suelto y `return` con y sin valor. |
| `test_operators` | Una operación toma la posición de su operador y el `-` unario (`OP_NEG`) se distingue del binario (`OP_SUB`). |
| `test_all_operators` | Los diez operadores binarios y el `!` se convierten al `OpKind` correcto. |
| `test_literals` | Valor y lexema de los literales entero, real y booleano; sin tipo hasta el análisis semántico. |
| `test_call_arguments` | Los argumentos de una llamada quedan en orden, cada uno con su posición. |
| `test_block_lists` | Las variables y las sentencias de un bloque van en listas separadas. |
| `test_print` | Formato exacto de `ast_print`, con y sin tipo calculado. |
| `test_constructors` | `ast_node`, `ast_leaf`, `ast_kind_name` y `ast_free` (que acepta `NULL`). |

## 14. `src/ir.h` y `src/ir.c`

El **código intermedio** (IR) es una representación lineal del programa, independiente del lenguaje fuente y fácil de
traducir a assembly. Se usa **código de tres direcciones**: cada instrucción tiene una operación, hasta dos operandos y
un resultado (`t3 = a + b`). Un programa es la lista de sus variables globales y, por cada método, una lista de
instrucciones; no hay `if` ni `while`, solo etiquetas y saltos. Un `boolean` es un entero (0 falso, 1 verdadero). Las
decisiones y el formato de texto están en `etapa-4-codigo-intermedio.md`.

**Operandos.** `Operand` tiene un tipo (`OperandKind`): `OPD_NONE` (sin operando), `OPD_CONST` (constante entera),
`OPD_VAR` (una variable), `OPD_TEMP` (temporal `t1`, `t2`, ...), `OPD_LABEL` (etiqueta `L1`, ...) y `OPD_FUNC` (nombre de
un método, solo en las llamadas). `value` guarda la constante o el número del temporal o de la etiqueta; `var`, la variable;
`name`, el nombre del método.

**Variables.** `IrVar` tiene `name` y `is_global`; las globales se imprimen con prefijo `@` (`@g`) para no confundirlas con
una local del mismo nombre.

**Instrucciones.** `Instr` tiene `op` (un `IrOp`), `a`, `b` y `result`. Qué campos usa cada operación:

| `IrOp` | Texto en el `.ci` | Campos |
|--------|-------------------|--------|
| `IR_MOVE` | `x = a` | `result = a` |
| `IR_ADD`, `IR_SUB`, `IR_MUL`, `IR_DIV`, `IR_MOD` | `t = a + b` (y `-`, `*`, `/`, `%`) | `result = a (op) b` |
| `IR_LT`, `IR_GT`, `IR_EQ` | `t = a < b` (y `>`, `==`) | `result` vale 1 o 0 |
| `IR_NEG`, `IR_NOT` | `t = - a`, `t = ! a` | `result = (op) a` |
| `IR_LABEL` | `L1:` | `a` = la etiqueta que se define |
| `IR_JMP` | `jmp L1` | `a` = destino |
| `IR_JZ`, `IR_JNZ` | `jz a, L1` / `jnz a, L1` | `a` = condición, `b` = destino |
| `IR_PARAM` | `param a` | `a` = argumento |
| `IR_CALL` | `t = call f, n` / `call f, n` | `a` = método (`OPD_FUNC`), `b` = cantidad de argumentos, `result` = temporal con el valor devuelto (o `OPD_NONE`) |
| `IR_RET` | `ret a` / `ret` | `a` = valor devuelto (o `OPD_NONE`) |

**Métodos y programa.** `IrFunc` guarda el nombre, si devuelve valor (`returns_value`), los parámetros y las locales
(arreglos dinámicos, en orden de declaración), las instrucciones (`code`, `ncode`), la cantidad de temporales usados
(`ntemps`) y el enlace al método siguiente. `IrProgram` guarda las globales, la lista de métodos y la cantidad de
etiquetas creadas (`nlabels`). Los arreglos dinámicos empiezan con capacidad 8 y la duplican al llenarse.

| Función | Qué hace |
|---------|----------|
| `Operand ir_none(void)`, `ir_const(long long)`, `ir_var(const IrVar *)`, `ir_func(const char *)` | Constructores de operandos. Las etiquetas y los temporales se crean con `ir_new_label` e `ir_new_temp`, que les dan número. |
| `IrProgram *ir_program_new(void)` | Crea un programa vacío. |
| `void ir_program_free(IrProgram *prog)` | Libera el programa con todo lo que contiene (variables, métodos e instrucciones). Acepta `NULL`. |
| `IrVar *ir_add_global(IrProgram *prog, const char *name)` | Agrega una global (el nombre se copia). |
| `IrFunc *ir_add_func(IrProgram *prog, const char *name, int returns_value)` | Agrega un método al final del programa. |
| `IrVar *ir_add_param(IrFunc *f, const char *base)`, `ir_add_local(f, base)` | Agregan un parámetro o una variable local. Si el método ya tiene una variable con ese nombre, la nueva se llama `base.2`, `base.3`, ...; así cada variable tiene un nombre **único dentro de su método**. |
| `Operand ir_new_temp(IrFunc *f)` | Crea el temporal siguiente del método (`t1`, `t2`, ...). |
| `Operand ir_new_label(IrProgram *prog)` | Crea la etiqueta siguiente del programa (`L1`, `L2`, ...), única en todo el programa. |
| `void ir_emit(IrFunc *f, IrOp op, Operand a, Operand b, Operand result)` | Agrega una instrucción al final del método. |
| `void ir_print(FILE *out, const IrProgram *prog)` | Escribe el programa en el formato del `.ci`: las líneas `global`, y por cada método `func nombre(params)`, sus `local`, las instrucciones con 2 espacios de sangría (las etiquetas sin sangría) y `end`, separando con líneas en blanco. |

Funciones internas de `ir.c` (todas `static`):

| Función | Qué hace |
|---------|----------|
| `xmalloc`, `xrealloc`, `xstrdup` | Reservan memoria y copian cadenas; si no hay memoria informan y terminan con código 2. |
| `GROW(arr, n, cap)` | Macro que agranda un arreglo dinámico cuando está lleno. |
| `make_operand(kind, value)` | Arma un operando con todos sus campos inicializados. |
| `var_new(name, is_global)` | Crea una variable con el nombre copiado. |
| `name_taken(f, name)`, `unique_name(f, base)` | Detectan si un nombre ya lo usa un parámetro o una local del método y calculan el nombre único (`base`, `base.2`, ...). |
| `vars_free(vars, n)` | Libera un arreglo de variables. |
| `print_operand(out, o)` | Escribe un operando: constante, `@global`, local, `tN`, `LN` o nombre de método. |
| `operator_text(op)` | Texto del operador de una instrucción aritmética, de comparación o de negación. |
| `print_instr(out, in)` | Escribe una instrucción en su formato de texto. |

## 15. `src/irgen.h` y `src/irgen.c`

El **generador de código intermedio** recorre el AST anotado por el análisis semántico (con símbolos y tipos) y produce el
programa en código de tres direcciones (`ir.h`): *reescritura del árbol* a una secuencia lineal de instrucciones. Sigue las
decisiones de `etapa-4-codigo-intermedio.md`.

La idea central es `gen_expr`: genera el código de una expresión y **devuelve el operando donde queda su valor** (una
constante, una variable o un temporal). Quien la llama usa ese operando como argumento de la instrucción siguiente. Las
sentencias de control (`if`, `while`) se convierten en etiquetas y saltos, y `&&` y `||` en saltos para cumplir el
cortocircuito.

`irgen.h` expone una sola función:

| Función | Qué hace |
|---------|----------|
| `IrProgram *irgen_generate(const Ast *program, const char *filename)` | Genera el código del programa (la raíz `AST_PROGRAM`, ya analizada sin errores). Los símbolos que referencia el AST tienen que seguir vivos. Si el programa usa `float` informa `codegen error: type 'float' is not supported by the code generator` por cada aparición y devuelve `NULL`. Si no, devuelve el programa, que el que llama libera con `ir_program_free`. |

`Gen` es el estado de la generación: el programa que se arma, el método actual y la correspondencia entre símbolos de la
tabla y variables del código intermedio.

Funciones internas de `irgen.c` (todas `static`):

| Función | Qué hace |
|---------|----------|
| `xrealloc` | `realloc` que, si falla, informa y termina con código 2. |
| `map_add(g, sym, var)`, `map_find(g, sym)` | La correspondencia símbolo → variable del código intermedio (un arreglo dinámico, con búsqueda lineal). `map_find` termina con error interno si el símbolo no está. |
| `find_func(g, name)` | El método del código intermedio con ese nombre. Siempre existe: un método solo puede llamar a los declarados antes o a sí mismo. |
| `emit(g, op, a, b, result)` | Agrega una instrucción al método actual. |
| `has_call(e)` | Indica si una expresión contiene una llamada (que podría modificar una global). |
| `is_global(o)` | Indica si el operando es una variable global. |
| `protect(g, value, next_expr)` | Si `value` es una global y la expresión que se evalúa a continuación tiene una llamada, copia el valor a un temporal **antes** de evaluarla (decisión 9: orden de izquierda a derecha). Devuelve el operando a usar. |
| `gen_expr(g, e)` | Genera una expresión y devuelve su operando: constante → `ir_const`; booleano → 0 o 1; identificador → su variable; llamada → `gen_call`; binaria → `gen_binary`; unaria → un temporal con `IR_NEG` o `IR_NOT`. |
| `gen_binary(g, e)` | Aritmética y comparaciones: genera el operando izquierdo (protegiéndolo si hace falta), el derecho, pide un temporal y emite la operación. `&&` y `||` van a `gen_short_circuit`. |
| `binary_op(op)` | Del operador del AST (`OP_ADD`, ...) a la instrucción (`IR_ADD`, ...). |
| `gen_short_circuit(g, e)` | `a && b` / `a \|\| b`: evalúa `a`; crea el temporal `r` y le copia `a`; crea la etiqueta de fin; emite `jz r, fin` (`&&`) o `jnz r, fin` (`\|\|`); evalúa `b`, se lo copia a `r` y define la etiqueta. Así `b` solo se ejecuta si `a` no decide el resultado. |
| `gen_call(g, call)` | Evalúa **todos** los argumentos de izquierda a derecha (protegiendo las globales si un argumento posterior tiene una llamada), emite los `param` consecutivos y el `call`. Si el método devuelve valor, el resultado queda en un temporal (aunque la llamada sea una sentencia). |
| `gen_stmt(g, s)` | Una sentencia: asignación (`x = valor`), llamada, `if`, `while`, `return`, bloque o sentencia vacía. |
| `gen_block(g, block)` | Declara las variables del bloque como locales del método (con nombre único) y genera sus sentencias. |
| `gen_method(g, m)` | Agrega el método, declara sus parámetros, genera el cuerpo y, si es `void`, agrega el `ret` implícito. |
| `check_floats(n, file)` | Recorre todo el AST e informa cada declaración, parámetro, método o literal de tipo `float` (decisión 12). Devuelve cuántos hay. |

**Cómo se traducen las sentencias de control** (las etiquetas se crean en este orden, y de ello depende la numeración
del `.ci`):

| Sentencia | Código generado |
|-----------|-----------------|
| `if (c) A` | `c`; crear `Lfin`; `jz c, Lfin`; `A`; `Lfin:` |
| `if (c) A else B` | `c`; crear `Lelse` y `Lfin`; `jz c, Lelse`; `A`; `jmp Lfin`; `Lelse:`; `B`; `Lfin:` |
| `while (c) A` | crear `Linicio` y `Lfin`; `Linicio:`; `c`; `jz c, Lfin`; `A`; `jmp Linicio`; `Lfin:` |
| `return e` / `return` | `e`; `ret e` / `ret` |

