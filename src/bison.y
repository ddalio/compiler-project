/*
 * bison.y - Gramatica de C-TDS (Etapa 2: analisis sintactico).
 *
 * Es la gramatica de "01-TDS-spec-lenguaje" llevada a bison:
 *   - Las repeticiones (*, +, listas con coma) son reglas recursivas.
 *   - La ambiguedad de <expr> se resuelve con la tabla de precedencia de la
 *     spec (declaraciones %left / %precedence), no con niveles de no
 *     terminales.
 *   - Solo se verifica la forma del programa; las reglas semanticas (tipos,
 *     nombres, main, ...) quedan para el analisis semantico.
 *
 * Cada regla arma un nodo del arbol de parsing (ver tree.h) que se escribe en
 * el archivo .sint. Los tokens que aportan informacion (identificadores,
 * literales, tipos y operadores) llegan desde el scanner como hojas ya
 * creadas; los signos de puntuacion y las palabras if/else/while/return no
 * generan hojas porque el nombre del nodo ya los implica.
 *
 * Los codigos TOK_* que genera bison (bison.tab.h) son los que usa el scanner.
 */

%define parse.error custom
%define parse.trace
%expect 0

/* Sin reducciones por defecto: el parser solo reduce con un token valido de
 * anticipacion y detecta el error antes de reducir. Asi la lista "expecting"
 * de los mensajes es exacta (con reducciones por defecto, p. ej. un programa
 * que empieza con un identificador diria "expecting end of file"). */
%define lr.default-reduction accepting

%code requires {
#include "tree.h"
}

%code {
#include <stdio.h>
#include "parser.h"
#include "scanner.h"
}

/* Valor semantico: todo simbolo con valor es un nodo del arbol. */
%union {
    Node *node;
}

/* Si el parser descarta simbolos (por un error sintactico), se libera el
 * subarbol que cada uno tenia armado. */
%destructor { node_free($$); } <node>

/* ---- Tokens ----
 * El texto entre comillas es el nombre que se muestra en los mensajes de
 * error. <node> indica que el scanner entrega una hoja con el lexema. */
%token TOK_EOF 0 "end of file"

%token TOK_KW_ELSE   "else"
%token TOK_KW_IF     "if"
%token TOK_KW_RETURN "return"
%token TOK_KW_WHILE  "while"
%token <node> TOK_KW_BOOLEAN "boolean"
%token <node> TOK_KW_FALSE   "false"
%token <node> TOK_KW_FLOAT   "float"
%token <node> TOK_KW_INT     "int"
%token <node> TOK_KW_TRUE    "true"
%token <node> TOK_KW_VOID    "void"

%token <node> TOK_ID            "identifier"
%token <node> TOK_INT_LITERAL   "integer literal"
%token <node> TOK_FLOAT_LITERAL "float literal"

%token <node> TOK_PLUS      "+"
%token <node> TOK_MINUS     "-"
%token <node> TOK_STAR      "*"
%token <node> TOK_SLASH     "/"
%token <node> TOK_PERCENT   "%"
%token <node> TOK_LT        "<"
%token <node> TOK_GT        ">"
%token <node> TOK_EQ        "=="
%token <node> TOK_AND       "&&"
%token <node> TOK_OR        "||"
%token <node> TOK_NOT       "!"
%token TOK_ASSIGN    "="
%token TOK_SEMICOLON ";"
%token TOK_COMMA     ","
%token TOK_LPAREN    "("
%token TOK_RPAREN    ")"
%token TOK_LBRACE    "{"
%token TOK_RBRACE    "}"

/* ---- Precedencia ----
 * De menor a mayor, segun la tabla de la spec. Todos los operadores binarios
 * asocian a izquierda; los unarios solo necesitan nivel. TOK_MINUS en su
 * forma unaria usa UMINUS (%prec). */
%left  TOK_OR
%left  TOK_AND
%left  TOK_EQ
%left  TOK_LT TOK_GT
%left  TOK_PLUS TOK_MINUS
%left  TOK_STAR TOK_SLASH TOK_PERCENT
%precedence TOK_NOT
%precedence UMINUS

/* ---- Tipos de los no terminales ---- */
%type <node> program var_decl_list method_decl_list var_decl id_list
%type <node> method_decl param_list_opt param_list type
%type <node> block block_var_decls statement_list statement
%type <node> method_call arg_list_opt arg_list expr literal

%start program

%%

/* <program> -> <var_decl>* <method_decl>*
 *
 * Escrita con listas NO vacias: con dos listas que admiten vacio
 * (var_decl* seguida de method_decl*) bison no puede decidir, mirando solo
 * "int", si hay que cerrar la lista de variables o empezar un metodo. Con
 * esta forma ambos casos comparten el prefijo "type id" y se distinguen con
 * el token siguiente ("(" para metodo; ";" o "," para variable).
 *
 * Arbol: "program" con las declaraciones como hijos directos. La raiz se
 * guarda en parse_root y el simbolo no lleva valor: bison destruye el valor
 * del simbolo inicial al aceptar y eso liberaria el arbol. */
program
    : %empty
        { parse_root = node_new("program"); $$ = NULL; }
    | var_decl_list
        { parse_root = node_adopt(node_new("program"), $1); $$ = NULL; }
    | var_decl_list method_decl_list
        { parse_root = node_adopt(node_adopt(node_new("program"), $1), $2);
          $$ = NULL; }
    | method_decl_list
        { parse_root = node_adopt(node_new("program"), $1); $$ = NULL; }
    ;

/* Las listas se arman como nodos auxiliares "list" cuyos hijos se pasan al
 * nodo padre con node_adopt. */
var_decl_list
    : var_decl                      { $$ = node1("list", $1); }
    | var_decl_list var_decl        { $$ = node_add($1, $2); }
    ;

method_decl_list
    : method_decl                   { $$ = node1("list", $1); }
    | method_decl_list method_decl  { $$ = node_add($1, $2); }
    ;

/* <var_decl> -> <type> { <id> }+, ;
 * Arbol: var_decl(tipo, id, id, ...) */
var_decl
    : type id_list TOK_SEMICOLON
        { $$ = node_adopt(node1("var_decl", $1), $2); }
    ;

id_list
    : TOK_ID                        { $$ = node1("list", $1); }
    | id_list TOK_COMMA TOK_ID      { $$ = node_add($1, $3); }
    ;

/* <method_decl> -> { <type> | void } <id> ( [ { <type> <id> }+, ] ) <block>
 * Arbol: method_decl(tipo de retorno, id, params, block) */
method_decl
    : type TOK_ID TOK_LPAREN param_list_opt TOK_RPAREN block
        { $$ = node4("method_decl", $1, $2, $4, $6); }
    | TOK_KW_VOID TOK_ID TOK_LPAREN param_list_opt TOK_RPAREN block
        { $$ = node4("method_decl", $1, $2, $4, $6); }
    ;

/* Arbol: params(param(tipo, id), ...); "params" sin hijos si no hay. */
param_list_opt
    : %empty                        { $$ = node_new("params"); }
    | param_list                    { $$ = $1; }
    ;

param_list
    : type TOK_ID
        { $$ = node1("params", node2("param", $1, $2)); }
    | param_list TOK_COMMA type TOK_ID
        { $$ = node_add($1, node2("param", $3, $4)); }
    ;

/* <block> -> { <var_decl>* <statement>* }
 * Aca si alcanzan las listas que admiten vacio: tras "{" las declaraciones
 * empiezan con un tipo y las sentencias nunca.
 * Arbol: block(var_decl..., sentencias...) */
block
    : TOK_LBRACE block_var_decls statement_list TOK_RBRACE
        { $$ = node_adopt(node_adopt(node_new("block"), $2), $3); }
    ;

block_var_decls
    : %empty                        { $$ = node_new("list"); }
    | block_var_decls var_decl      { $$ = node_add($1, $2); }
    ;

statement_list
    : %empty                        { $$ = node_new("list"); }
    | statement_list statement      { $$ = node_add($1, $2); }
    ;

/* <type> -> int | boolean | float
 * Arbol: la hoja del token (KW_INT int). */
type
    : TOK_KW_INT                    { $$ = $1; }
    | TOK_KW_BOOLEAN                { $$ = $1; }
    | TOK_KW_FLOAT                  { $$ = $1; }
    ;

/* <statement>
 * Nota: la gramatica de la spec define "while <expr> <block>" sin parentesis
 * obligatorios; "while (x) {...}" es el caso en que <expr> es "( <expr> )".
 * Arbol: assign(id, expr), call, if(cond, then), if_else(cond, then, else),
 * while(cond, body), return / return(expr), empty_stmt, block. */
statement
    : TOK_ID TOK_ASSIGN expr TOK_SEMICOLON
        { $$ = node2("assign", $1, $3); }
    | method_call TOK_SEMICOLON
        { $$ = $1; }
    | TOK_KW_IF TOK_LPAREN expr TOK_RPAREN block
        { $$ = node2("if", $3, $5); }
    | TOK_KW_IF TOK_LPAREN expr TOK_RPAREN block TOK_KW_ELSE block
        { $$ = node3("if_else", $3, $5, $7); }
    | TOK_KW_WHILE expr block
        { $$ = node2("while", $2, $3); }
    | TOK_KW_RETURN TOK_SEMICOLON
        { $$ = node_new("return"); }
    | TOK_KW_RETURN expr TOK_SEMICOLON
        { $$ = node1("return", $2); }
    | TOK_SEMICOLON
        { $$ = node_new("empty_stmt"); }
    | block
        { $$ = $1; }
    ;

/* <method_call> -> <id> ( [ { <expr> }+, ] )
 * Arbol: call(id, argumento, ...) */
method_call
    : TOK_ID TOK_LPAREN arg_list_opt TOK_RPAREN
        { $$ = node_adopt(node1("call", $1), $3); }
    ;

arg_list_opt
    : %empty                        { $$ = node_new("list"); }
    | arg_list                      { $$ = $1; }
    ;

arg_list
    : expr                          { $$ = node1("list", $1); }
    | arg_list TOK_COMMA expr       { $$ = node_add($1, $3); }
    ;

/* <expr>
 * <bin_op> se expande en cada alternativa para que %left pueda asignarle la
 * precedencia a cada operador.
 * Arbol: hoja (ID, literal), call, binary_expr(izq, operador, der),
 * unary_expr(operador, expr). Los parentesis no generan nodo: el
 * agrupamiento ya queda expresado por la forma del arbol. */
expr
    : TOK_ID                        { $$ = $1; }
    | method_call                   { $$ = $1; }
    | literal                       { $$ = $1; }
    | expr TOK_PLUS expr            { $$ = node3("binary_expr", $1, $2, $3); }
    | expr TOK_MINUS expr           { $$ = node3("binary_expr", $1, $2, $3); }
    | expr TOK_STAR expr            { $$ = node3("binary_expr", $1, $2, $3); }
    | expr TOK_SLASH expr           { $$ = node3("binary_expr", $1, $2, $3); }
    | expr TOK_PERCENT expr         { $$ = node3("binary_expr", $1, $2, $3); }
    | expr TOK_LT expr              { $$ = node3("binary_expr", $1, $2, $3); }
    | expr TOK_GT expr              { $$ = node3("binary_expr", $1, $2, $3); }
    | expr TOK_EQ expr              { $$ = node3("binary_expr", $1, $2, $3); }
    | expr TOK_AND expr             { $$ = node3("binary_expr", $1, $2, $3); }
    | expr TOK_OR expr              { $$ = node3("binary_expr", $1, $2, $3); }
    | TOK_MINUS expr %prec UMINUS   { $$ = node2("unary_expr", $1, $2); }
    | TOK_NOT expr                  { $$ = node2("unary_expr", $1, $2); }
    | TOK_LPAREN expr TOK_RPAREN    { $$ = $2; }
    ;

/* <literal> -> <int_literal> | <bool_literal> | <float_literal> */
literal
    : TOK_INT_LITERAL               { $$ = $1; }
    | TOK_KW_TRUE                   { $$ = $1; }
    | TOK_KW_FALSE                  { $$ = $1; }
    | TOK_FLOAT_LITERAL             { $$ = $1; }
    ;

%%

Node *parse_root = NULL;

/* ---- Errores ---- */

/* Nombre de un token para los mensajes. Las clases de tokens ("identifier",
 * "end of file", ...) van tal cual; los simbolos y palabras reservadas, entre
 * comillas simples: ')' , 'while'. */
static void print_symbol(FILE *out, yysymbol_kind_t kind)
{
    const char *name = yysymbol_name(kind);

    switch (kind) {
    case YYSYMBOL_YYEOF:
    case YYSYMBOL_TOK_ID:
    case YYSYMBOL_TOK_INT_LITERAL:
    case YYSYMBOL_TOK_FLOAT_LITERAL:
        fputs(name, out);
        break;
    default:
        fprintf(out, "'%s'", name);
    }
}

/* Mensaje de error sintactico:
 *   archivo:linea:columna: syntax error, unexpected X[, expecting A or B ...]
 * La posicion es la del ultimo token leido, que es el que no se pudo aceptar.
 * Se lista lo esperado solo si son pocos tokens (hasta 4); con mas, la lista
 * no ayuda. */
static int yyreport_syntax_error(const yypcontext_t *ctx)
{
    enum { MAX_EXPECTED = 4 };
    yysymbol_kind_t expected[MAX_EXPECTED + 1];
    yysymbol_kind_t unexpected = yypcontext_token(ctx);
    int n = yypcontext_expected_tokens(ctx, expected, MAX_EXPECTED + 1);

    if (n < 0)
        return n;                       /* sin memoria */
    fprintf(stderr, "%s:%d:%d: syntax error", scanner_source_name(),
            tok_line, tok_col);
    if (unexpected != YYSYMBOL_YYEMPTY) {
        fputs(", unexpected ", stderr);
        print_symbol(stderr, unexpected);
    }
    if (n >= 1 && n <= MAX_EXPECTED) {
        fputs(", expecting ", stderr);
        for (int i = 0; i < n; i++) {
            if (i > 0)
                fputs(" or ", stderr);
            print_symbol(stderr, expected[i]);
        }
    }
    fputc('\n', stderr);
    return 0;
}

/* Otros errores del parser (por ejemplo, memoria agotada). */
void yyerror(const char *msg)
{
    fprintf(stderr, "%s:%d:%d: error: %s\n", scanner_source_name(),
            tok_line, tok_col, msg);
}
