/*
 * flow.h - Analisis del flujo del codigo intermedio (Etapa 6: optimizador).
 *
 * Dos cosas que necesitan las optimizaciones:
 *   - que operandos lee y cual escribe cada instruccion (instr_reads,
 *     instr_write);
 *   - la division del codigo de un metodo en BLOQUES BASICOS (tramos sin saltos
 *     ni etiquetas en el medio) y el GRAFO DE FLUJO DE CONTROL que los une.
 */
#ifndef FLOW_H
#define FLOW_H

#include <stdio.h>

#include "ir.h"

/* ---------------------------------------------- operandos de una instruccion */

/* Carga en out[] punteros a los operandos que LEE la instruccion (los campos de
 * `in`, para poder reemplazarlos) y devuelve cuantos son (0, 1 o 2). Pueden ser
 * constantes. No incluye etiquetas, nombres de metodo ni la cantidad de
 * argumentos de un call, que no son valores. */
int instr_reads(Instr *in, Operand *out[2]);

/* Puntero al operando (una variable o un temporal) que ESCRIBE la instruccion,
 * o NULL si no escribe ninguno. */
Operand *instr_write(Instr *in);

/* Indica si el operando es una ubicacion (variable o temporal), no una
 * constante ni una etiqueta. */
int is_location(Operand o);

/* Indica si dos operandos son la misma ubicacion (la misma variable o el mismo
 * temporal). */
int same_location(Operand a, Operand b);

/* Indica si la instruccion es un salto (jmp, jz o jnz). */
int instr_is_jump(const Instr *in);

/* --------------------------------------------- bloques y grafo de control */

typedef struct {
    int first, last;        /* primera y ultima instruccion del bloque (indices) */
    int nsucc;              /* cantidad de sucesores: 0, 1 o 2 */
    int succ[2];            /* indices de los bloques sucesores */
} Block;

typedef struct {
    Block *blocks;
    int nblocks;
} Cfg;

/* Arma el grafo de flujo de control de un metodo. Un bloque empieza en la
 * primera instruccion, en cada etiqueta y en la instruccion que sigue a un salto
 * o a un ret. Sus sucesores son: el destino de un jmp; el destino de un salto
 * condicional y el bloque siguiente; ninguno para un ret; y el bloque siguiente
 * en los demas casos. Un metodo sin codigo tiene 0 bloques. */
Cfg *cfg_build(const IrFunc *f);

/* Libera el grafo. Acepta NULL. */
void cfg_free(Cfg *cfg);

/* Escribe el grafo para depurar: una linea por bloque con sus instrucciones y
 * sus sucesores. */
void cfg_print(FILE *out, const IrFunc *f, const Cfg *cfg);

#endif /* FLOW_H */
