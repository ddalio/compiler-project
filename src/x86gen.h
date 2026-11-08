/*
 * x86gen.h - Generador de assembly x86-64 de C-TDS (Etapa 5).
 *
 * Traduce un programa en codigo intermedio (ir.h) a assembly x86-64 de Linux en
 * sintaxis AT&T, la que acepta el ensamblador de GNU. El codigo es simple y sin
 * optimizar: cada parametro, variable local y temporal vive en una posicion del
 * marco de pila y cada operacion usa %r10 y %r11 como registros temporales.
 * Las decisiones (enteros de 32 bits, marco de pila, convencion de llamada,
 * nombres de simbolos, ...) estan en docs/etapa-5-codigo-objeto.md.
 */
#ifndef X86GEN_H
#define X86GEN_H

#include <stdio.h>

#include "ir.h"

/* Escribe en `out` el assembly de `prog`. `source_name` es el nombre del fuente,
 * que solo aparece en un comentario. El programa tiene que ser el de un
 * programa C-TDS valido (con un metodo `main`); no puede fallar. */
void x86_generate(FILE *out, const IrProgram *prog, const char *source_name);

#endif /* X86GEN_H */
