/*
 * semantic.h - Analisis semantico de C-TDS (Etapa 3).
 *
 * Recorre el AST (ast.h) usando la tabla de simbolos (symtab.h) y verifica las
 * reglas semanticas de la especificacion. Mientras recorre, vincula cada
 * identificador y cada llamada con su simbolo (campo `sym` del AST) y completa
 * los tipos de las expresiones.
 */
#ifndef SEMANTIC_H
#define SEMANTIC_H

#include "ast.h"
#include "symtab.h"

/* Analiza el programa `program` (la raiz AST_PROGRAM que devuelve
 * ast_from_tree). `tab` debe ser una tabla recien creada (solo con el nivel
 * global abierto); `filename` se usa en los mensajes de error.
 *
 * Los errores se informan por stderr con el formato
 *   archivo:linea:columna: semantic error: mensaje
 * en el orden en que aparecen en el fuente, y el analisis continua despues de
 * cada uno para informar todos.
 *
 * Devuelve la cantidad de errores semanticos (0 = el programa es correcto). */
int semantic_analyze(Ast *program, const char *filename, SymTab *tab);

#endif /* SEMANTIC_H */
