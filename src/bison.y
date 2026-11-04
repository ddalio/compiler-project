/*
 * bison.y - Gramatica de C-TDS (Etapa 2: analisis sintactico).
 *
 * Es la gramatica de "01-TDS-spec-lenguaje" llevada a bison:
 *   - Las repeticiones (*, +, listas con coma) son reglas recursivas.
 *   - La ambiguedad de <expr> se resuelve con la tabla de precedencia de la
 *     spec (declaraciones %left / %right), no con niveles de no terminales.
 *   - Solo se verifica la forma del programa; las reglas semanticas (tipos,
 *     nombres, main, ...) quedan para el analisis semantico.
 *
 * Los nombres de los tokens son los mismos que los de src/tokens.h.
 */

%define parse.error verbose
%expect 0

/* ---- Tokens ----
 * El texto entre comillas es el nombre que se muestra en los mensajes de
 * error ("unexpected ..., expecting ..."). */
%token TOK_EOF 0 "end of file"

%token TOK_KW_BOOLEAN "boolean"
%token TOK_KW_ELSE    "else"
%token TOK_KW_FALSE   "false"
%token TOK_KW_FLOAT   "float"
%token TOK_KW_IF      "if"
%token TOK_KW_INT     "int"
%token TOK_KW_RETURN  "return"
%token TOK_KW_TRUE    "true"
%token TOK_KW_VOID    "void"
%token TOK_KW_WHILE   "while"

%token TOK_ID            "identifier"
%token TOK_INT_LITERAL   "integer literal"
%token TOK_FLOAT_LITERAL "float literal"

%token TOK_PLUS      "+"
%token TOK_MINUS     "-"
%token TOK_STAR      "*"
%token TOK_SLASH     "/"
%token TOK_PERCENT   "%"
%token TOK_LT        "<"
%token TOK_GT        ">"
%token TOK_EQ        "=="
%token TOK_AND       "&&"
%token TOK_OR        "||"
%token TOK_NOT       "!"
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

%start program

%%

/* <program> -> <var_decl>* <method_decl>*
 *
 * Escrita con listas NO vacias: con dos listas que admiten vacio
 * (var_decl* seguida de method_decl*) bison no puede decidir, mirando solo
 * "int", si hay que cerrar la lista de variables o empezar un metodo. Con
 * esta forma ambos casos comparten el prefijo "type id" y se distinguen con
 * el token siguiente ("(" para metodo; ";" o "," para variable). */
program
    : %empty
    | var_decl_list
    | var_decl_list method_decl_list
    | method_decl_list
    ;

var_decl_list
    : var_decl
    | var_decl_list var_decl
    ;

method_decl_list
    : method_decl
    | method_decl_list method_decl
    ;

/* <var_decl> -> <type> { <id> }+, ; */
var_decl
    : type id_list TOK_SEMICOLON
    ;

id_list
    : TOK_ID
    | id_list TOK_COMMA TOK_ID
    ;

/* <method_decl> -> { <type> | void } <id> ( [ { <type> <id> }+, ] ) <block> */
method_decl
    : type TOK_ID TOK_LPAREN param_list_opt TOK_RPAREN block
    | TOK_KW_VOID TOK_ID TOK_LPAREN param_list_opt TOK_RPAREN block
    ;

param_list_opt
    : %empty
    | param_list
    ;

param_list
    : type TOK_ID
    | param_list TOK_COMMA type TOK_ID
    ;

/* <block> -> { <var_decl>* <statement>* }
 * Aca si alcanzan las listas que admiten vacio: tras "{" las declaraciones
 * empiezan con un tipo y las sentencias nunca. */
block
    : TOK_LBRACE block_var_decls statement_list TOK_RBRACE
    ;

block_var_decls
    : %empty
    | block_var_decls var_decl
    ;

statement_list
    : %empty
    | statement_list statement
    ;

/* <type> -> int | boolean | float */
type
    : TOK_KW_INT
    | TOK_KW_BOOLEAN
    | TOK_KW_FLOAT
    ;

/* <statement>
 * Nota: la gramatica de la spec define "while <expr> <block>" sin parentesis
 * obligatorios; "while (x) {...}" es el caso en que <expr> es "( <expr> )". */
statement
    : TOK_ID TOK_ASSIGN expr TOK_SEMICOLON
    | method_call TOK_SEMICOLON
    | TOK_KW_IF TOK_LPAREN expr TOK_RPAREN block
    | TOK_KW_IF TOK_LPAREN expr TOK_RPAREN block TOK_KW_ELSE block
    | TOK_KW_WHILE expr block
    | TOK_KW_RETURN TOK_SEMICOLON
    | TOK_KW_RETURN expr TOK_SEMICOLON
    | TOK_SEMICOLON
    | block
    ;

/* <method_call> -> <id> ( [ { <expr> }+, ] ) */
method_call
    : TOK_ID TOK_LPAREN arg_list_opt TOK_RPAREN
    ;

arg_list_opt
    : %empty
    | arg_list
    ;

arg_list
    : expr
    | arg_list TOK_COMMA expr
    ;

/* <expr>
 * <bin_op> se expande en cada alternativa para que %left pueda asignarle la
 * precedencia a cada operador. */
expr
    : TOK_ID
    | method_call
    | literal
    | expr TOK_PLUS expr
    | expr TOK_MINUS expr
    | expr TOK_STAR expr
    | expr TOK_SLASH expr
    | expr TOK_PERCENT expr
    | expr TOK_LT expr
    | expr TOK_GT expr
    | expr TOK_EQ expr
    | expr TOK_AND expr
    | expr TOK_OR expr
    | TOK_MINUS expr %prec UMINUS
    | TOK_NOT expr
    | TOK_LPAREN expr TOK_RPAREN
    ;

/* <literal> -> <int_literal> | <bool_literal> | <float_literal> */
literal
    : TOK_INT_LITERAL
    | TOK_KW_TRUE
    | TOK_KW_FALSE
    | TOK_FLOAT_LITERAL
    ;

%%
