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

  Etapa 3 (en construcción): árbol de parsing ──► ast.c: ast_from_tree() ──► AST
                             (el análisis semántico recorrerá el AST con la tabla de símbolos)
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
| `static int check_target(const char *target)` | Acepta `scan` y `parse`; para `codinter` y `assembly` informa "not available yet" y para cualquier otro "unknown target". |
| `static int parse_args(int argc, char **argv, Options *opt)` | Recorre los argumentos y completa `Options`: `-o` y `-target` toman el argumento siguiente, `-debug` es una bandera y cualquier otro argumento que empiece con `-` es una opción desconocida (por eso un archivo no puede empezar con `-`). Valida que haya un solo archivo de entrada, que termine en `.ctds` y que el target sea válido. El target por defecto es `parse`. |
| `static char *output_path(const Options *opt, const char *stage_ext)` | Calcula el archivo de salida: el de `-o` si se dio; si no, el del fuente cambiando `.ctds` por la extensión de la etapa (`.lex`, `.sint`). Devuelve memoria que hay que liberar con `free`. |
| `static FILE *open_input(const Options *opt)` | Abre el fuente; si falla informa el motivo. |
| `static int write_failed(const char *path)` | Informa que no se pudo escribir la salida y devuelve el código 2. |
| `static int run_scan(const Options *opt)` | Etapa scan: llama a `yylex` en un bucle, acumula `línea:columna NOMBRE lexema` en memoria (`open_memstream`) y libera la hoja de cada token. Si no hubo errores léxicos escribe el `.lex`; con `-debug` imprime las líneas por `stdout`. |
| `static int run_parse(const Options *opt)` | Etapa parse: prepara el scanner, activa `yydebug` si hay `-debug`, llama a `yyparse` y, si no hubo errores ni sintácticos ni léxicos, escribe el árbol en el `.sint`. Con `-debug` también lo imprime por `stdout`. Libera el árbol al final. |
| `int main(int argc, char **argv)` | Interpreta los argumentos y llama a `run_scan` o `run_parse`. |

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
| `make test` | Compila y corre `tests/run_tests.sh`. |
| `make test-asan` | Corre los mismos tests con un ejecutable compilado con AddressSanitizer y UBSan (detectan pérdidas de memoria y accesos inválidos). |
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
| Línea de comandos | (en el script) | Errores de uso, nombres de salida por defecto y con `-o`, target por defecto y `-target scan`. |

## 9. `src/types.h` y `src/types.c`

`Type` es un `enum` con los tipos del lenguaje: `TYPE_INT`, `TYPE_FLOAT`, `TYPE_BOOLEAN`, `TYPE_VOID` (retorno de un
método sin valor), `TYPE_UNKNOWN` (todavía no calculado) y `TYPE_ERROR` (una expresión con error: se la trata como
compatible con todo para no encadenar errores).

| Función | Qué hace |
|---------|----------|
| `const char *type_name(Type t)` | Nombre del tipo para mensajes y salidas: `"int"`, `"float"`, `"boolean"`, `"void"`, `"error"`, `"unknown"`. |

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

Esta parte del análisis (nombres y alcance) verifica: la regla 1 (redeclaración), la 2 (uso antes de declarar), la 3
(`main`) y la 8 (la ubicación de una asignación), más que un método no se use como variable ni una variable como
método. Las reglas de tipos se agregan en la parte siguiente.

`semantic.h` expone una sola función:

| Función | Qué hace |
|---------|----------|
| `int semantic_analyze(Ast *program, const char *filename, SymTab *tab)` | Analiza el programa (la raíz `AST_PROGRAM`). `tab` debe ser una tabla recién creada. Informa cada error por `stderr` (`archivo:línea:col: semantic error: mensaje`), sigue después de cada uno y devuelve la cantidad de errores. Deja en el AST los símbolos (`sym`) y los tipos de identificadores, llamadas y literales. |

Funciones internas de `semantic.c` (todas `static`). `Ctx` agrupa el estado: la tabla, el nombre del archivo y la cantidad
de errores.

| Función | Qué hace |
|---------|----------|
| `sem_error(c, line, col, fmt, ...)` | Cuenta e imprime un error semántico en esa posición. Es variádica como `printf`. |
| `declare(c, decl, cls)` | Inserta una declaración (variable, parámetro o método) en el nivel actual y guarda el símbolo en `decl->sym`. Si el nombre ya está en ese nivel informa `redeclaration of 'x' (previous declaration at L:C)` (regla 1) y la declaración queda sin símbolo. |
| `resolve_variable(c, id)` | Resuelve un identificador usado como valor o como ubicación: busca desde el nivel actual al global. Si no existe: `'x' undeclared` (reglas 2 y 8); si es un método: `'x' is a method, not a variable`. Si está bien, `id->sym` es el símbolo y `id->type` su tipo; si no, el tipo es `TYPE_ERROR`. |
| `resolve_call(c, call)` | Igual para el nombre de una llamada: tiene que existir (`'f' undeclared`) y ser un método (`'x' is not a method`). El tipo de la llamada es el tipo de retorno del método. |
| `analyze_expr(c, e)` | Recorre una expresión: resuelve identificadores y llamadas (y sus argumentos), da tipo a los literales y baja por las operaciones. |
| `analyze_stmt(c, s)` | Recorre una sentencia: la ubicación y la expresión de una asignación, la condición y los bloques de `if`/`while`, el valor de `return`, los bloques sueltos. |
| `analyze_block(c, block, open_level)` | Declara las variables del bloque y analiza sus sentencias. Abre un nivel propio si `open_level` es 1 (bloques anidados); el cuerpo de un método usa 0 porque comparte nivel con los parámetros. |
| `analyze_method(c, m)` | Inserta el método en el nivel global **antes** de su cuerpo (así puede llamarse a sí mismo), guarda los tipos de sus parámetros con `symtab_set_params`, abre un nivel, declara los parámetros, analiza el cuerpo y cierra el nivel. |
| `check_main(c)` | Regla 3: tiene que existir un método `main` sin parámetros. Los errores son `program has no 'main' method` (en la posición 1:1, porque no hay un lugar donde apuntar), `'main' must be a method` y `'main' must not have parameters`. |

