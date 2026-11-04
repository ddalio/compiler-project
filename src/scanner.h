/*
 * scanner.h - Interfaz del analizador lexico generado con flex (scanner.l).
 */
#ifndef SCANNER_H
#define SCANNER_H

#include <stdio.h>
#include "tokens.h"

/* Prepara el scanner para leer `in`. `filename` solo se usa en los mensajes
 * de error. Reinicia posicion (linea 1, columna 1) y contador de errores. */
void scanner_init(const char *filename, FILE *in);

/* Devuelve el siguiente token (uno de los TOK_* de bison.y) o TOK_EOF al
 * terminar.
 * Los errores lexicos se informan por stderr y el scanner continua. */
int yylex(void);

/* Texto (lexema) del ultimo token devuelto por yylex. */
extern char *yytext;

/* Posicion (1-based) donde empieza el ultimo token devuelto. */
extern int tok_line;
extern int tok_col;

/* Cantidad de errores lexicos informados desde scanner_init. */
extern int lex_errors;

#endif /* SCANNER_H */
