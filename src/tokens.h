/*
 * tokens.h - Tokens del lenguaje C-TDS.
 *
 * Los codigos de los tokens (TOK_EOF, TOK_KW_INT, TOK_ID, ...) los define
 * bison a partir de los %token de bison.y y los deja en el header generado
 * build/bison.tab.h. Asi el scanner y el parser comparten una unica
 * definicion y no pueden quedar desincronizados.
 *
 * Los espacios y comentarios NO son tokens.
 */
#ifndef TOKENS_H
#define TOKENS_H

#include "bison.tab.h"

/* Nombre imprimible de un token (ej: TOK_KW_IF -> "KW_IF"). Es el nombre
 * que se escribe en el archivo .lex. */
const char *token_name(int token);

#endif /* TOKENS_H */
