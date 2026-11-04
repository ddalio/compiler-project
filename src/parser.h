/*
 * parser.h - Interfaz del analizador sintactico generado con bison (bison.y).
 */
#ifndef PARSER_H
#define PARSER_H

#include "bison.tab.h"      /* yydebug */
#include "tree.h"

/* Analiza el programa que lee el scanner (ver scanner_init). Devuelve 0 si es
 * sintacticamente correcto, 1 si hay un error sintactico (ya informado por
 * stderr) y 2 si se agoto la memoria. */
int yyparse(void);

/* Raiz del arbol de parsing; solo es valida si yyparse devolvio 0. El que
 * llama es responsable de liberarla con node_free. */
extern Node *parse_root;

/* Informa un error del parser por stderr (con posicion). */
void yyerror(const char *msg);

#endif /* PARSER_H */
