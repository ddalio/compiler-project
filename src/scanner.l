/*
 * scanner.l - Analizador lexico de C-TDS (Etapa 1).
 *
 * Reglas:
 *   - Palabras reservadas: solo minusculas; el lenguaje es case-sensitive.
 *   - Identificador:  alpha (alpha | digit | '_')*
 *   - Entero:         digit+            (32 bits con signo)
 *   - Real:           digit+ '.' digit+
 *   - Comentarios:    de linea (doble barra) y de bloque (multilinea)
 *   - Espacios, tabulaciones, saltos de linea y comentarios se descartan.
 *
 * Un error lexico se informa por stderr y el analisis continua, de modo que
 * una sola corrida reporta todos los errores del archivo.
 */

%option noyywrap nounput noinput nodefault

%{
#include <errno.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "scanner.h"
#include "tree.h"

/* Mayor entero permitido (32 bits con signo). */
#define INT_LITERAL_MAX 2147483647LL

int tok_line = 1;
int tok_col = 1;
int lex_errors = 0;

static const char *src_name = "<input>";
static int cur_line = 1;        /* posicion del proximo caracter a leer */
static int cur_col = 1;
static int comment_line;        /* donde empezo el comentario en curso */
static int comment_col;

/* Avanza la posicion actual sobre el texto reconocido. Un tab cuenta como
 * una columna. */
static void advance(const char *text, int len)
{
    for (int i = 0; i < len; i++) {
        if (text[i] == '\n') {
            cur_line++;
            cur_col = 1;
        } else {
            cur_col++;
        }
    }
}

/* Fija yylloc (la ubicacion del token que usa bison con %locations) a partir
 * de tok_line/tok_col (inicio) y cur_line/cur_col (fin, ya avanzado). */
#define SET_YYLLOC() \
    do { yylloc.first_line = tok_line; yylloc.first_column = tok_col; \
         yylloc.last_line = cur_line;  yylloc.last_column = cur_col; } while (0)

/* Se ejecuta antes de cada accion: fija donde empieza el token, avanza,
 * guarda la ubicacion en yylloc y deja sin valor semantico a yylval (asi, un
 * token sin hoja nunca arrastra la del anterior). */
#define YY_USER_ACTION \
    tok_line = cur_line; tok_col = cur_col; advance(yytext, yyleng); \
    SET_YYLLOC(); yylval.node = NULL;

/* Devuelve el token `tok` junto con una hoja del arbol de parsing que guarda
 * su lexema y su posicion. Lo usan los tokens que aportan informacion al
 * arbol (tipos, identificadores, literales y operadores). El que recibe el
 * token es dueño de la hoja y debe liberarla (el parser lo hace; ver
 * bison.y). */
#define LEAF(tok) \
    do { yylval.node = node_at(leaf_new(token_name(tok), yytext), \
                               tok_line, tok_col); \
         return tok; } while (0)

static void lex_error(int line, int col, const char *fmt, ...)
{
    va_list ap;

    lex_errors++;
    fprintf(stderr, "%s:%d:%d: lexical error: ", src_name, line, col);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

%}

DIGIT   [0-9]
ALPHA   [A-Za-z]
ID      {ALPHA}({ALPHA}|{DIGIT}|_)*

%x BLOCK_COMMENT

%%

 /* ---- Espacios y comentarios (se descartan) ---- */
[ \t\r\n]+              { /* espacios */ }
"//".*                  { /* comentario de linea (no consume el \n) */ }
"/*"                    { comment_line = tok_line; comment_col = tok_col;
                          BEGIN(BLOCK_COMMENT); }

<BLOCK_COMMENT>"*/"     { BEGIN(INITIAL); }
<BLOCK_COMMENT>[^*\n]+  { /* contenido del comentario */ }
<BLOCK_COMMENT>"*"      { /* '*' que no cierra el comentario */ }
<BLOCK_COMMENT>\n       { /* salto de linea dentro del comentario */ }
<BLOCK_COMMENT><<EOF>>  { lex_error(comment_line, comment_col,
                                    "unterminated comment");
                          BEGIN(INITIAL);
                          tok_line = cur_line; tok_col = cur_col;
                          SET_YYLLOC(); yylval.node = NULL;
                          return TOK_EOF; }

 /* ---- Palabras reservadas ----
  * Van antes que la regla de identificadores: ante igual longitud flex elige
  * la primera regla. "whiletrue" sigue siendo un identificador porque la
  * regla ID reconoce un lexema mas largo. */
"boolean"               { LEAF(TOK_KW_BOOLEAN); }
"else"                  { return TOK_KW_ELSE; }
"false"                 { LEAF(TOK_KW_FALSE); }
"float"                 { LEAF(TOK_KW_FLOAT); }
"if"                    { return TOK_KW_IF; }
"int"                   { LEAF(TOK_KW_INT); }
"return"                { return TOK_KW_RETURN; }
"true"                  { LEAF(TOK_KW_TRUE); }
"void"                  { LEAF(TOK_KW_VOID); }
"while"                 { return TOK_KW_WHILE; }

 /* ---- Identificadores y literales ---- */
{ID}                    { LEAF(TOK_ID); }

{DIGIT}+                {
    errno = 0;
    long long v = strtoll(yytext, NULL, 10);
    if (errno == ERANGE || v > INT_LITERAL_MAX) {
        lex_error(tok_line, tok_col,
                  "integer literal '%s' out of range (max %lld)",
                  yytext, INT_LITERAL_MAX);
    } else {
        LEAF(TOK_INT_LITERAL);
    }
}

{DIGIT}+"."{DIGIT}+     {
    errno = 0;
    double v = strtod(yytext, NULL);
    if (errno == ERANGE && fabs(v) == HUGE_VAL) {
        lex_error(tok_line, tok_col,
                  "float literal '%s' out of range", yytext);
    } else {
        LEAF(TOK_FLOAT_LITERAL);
    }
}

 /* ---- Operadores ---- */
"+"                     { LEAF(TOK_PLUS); }
"-"                     { LEAF(TOK_MINUS); }
"*"                     { LEAF(TOK_STAR); }
"/"                     { LEAF(TOK_SLASH); }
"%"                     { LEAF(TOK_PERCENT); }
"<"                     { LEAF(TOK_LT); }
">"                     { LEAF(TOK_GT); }
"=="                    { LEAF(TOK_EQ); }
"&&"                    { LEAF(TOK_AND); }
"||"                    { LEAF(TOK_OR); }
"!"                     { LEAF(TOK_NOT); }
"="                     { return TOK_ASSIGN; }

 /* ---- Delimitadores ---- */
";"                     { return TOK_SEMICOLON; }
","                     { return TOK_COMMA; }
"("                     { return TOK_LPAREN; }
")"                     { return TOK_RPAREN; }
"{"                     { return TOK_LBRACE; }
"}"                     { return TOK_RBRACE; }

 /* ---- Fin de archivo ----
  * El EOF tiene posicion propia (el final del archivo), asi un error
  * sintactico por "fin de archivo inesperado" se informa donde corresponde. */
<<EOF>>                 { tok_line = cur_line; tok_col = cur_col;
                          SET_YYLLOC(); yylval.node = NULL;
                          return TOK_EOF; }

 /* ---- Errores ---- */
[&|]                    {
    lex_error(tok_line, tok_col,
              "unexpected character '%c' (did you mean '%c%c'?)",
              yytext[0], yytext[0], yytext[0]);
}

.                       {
    unsigned char c = (unsigned char)yytext[0];
    if (isprint(c))
        lex_error(tok_line, tok_col, "unexpected character '%c'", c);
    else
        lex_error(tok_line, tok_col,
                  "unexpected character (byte 0x%02X)", c);
}

%%

/* INITIAL se define en el codigo generado, por eso va despues del scanner. */
const char *scanner_source_name(void)
{
    return src_name;
}

void scanner_init(const char *filename, FILE *in)
{
    src_name = filename;
    cur_line = cur_col = 1;
    tok_line = tok_col = 1;
    lex_errors = 0;
    yyrestart(in);
    BEGIN(INITIAL);
}
