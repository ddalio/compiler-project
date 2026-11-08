/*
 * interp.h - Interprete del codigo intermedio de C-TDS (Etapa 4).
 *
 * Ejecuta un programa en codigo de tres direcciones (ir.h) empezando por el
 * metodo `main`. Es una interpretacion ITERATIVA (la que describe la teoria
 * para el codigo intermedio): un bucle que ejecuta una instruccion por vez
 * sobre una pila de marcos propia, sin recursion de C, asi que la recursion del
 * programa interpretado no desborda la pila del interprete.
 *
 * Semantica (decisiones 13, 14, 18 a 20 de docs/etapa-4-codigo-intermedio.md):
 *   - los enteros son de 32 bits con signo y las operaciones dan la vuelta
 *     (aritmetica modular, como en la maquina);
 *   - las variables globales y las locales empiezan en 0;
 *   - la division y el resto por cero son un error de ejecucion;
 *   - el resultado del programa es el valor que devuelve `main`.
 */
#ifndef INTERP_H
#define INTERP_H

#include "ir.h"

/* Profundidad maxima de llamadas anidadas; superarla es un error de ejecucion
 * ("call stack overflow"), por ejemplo en una recursion infinita. */
#define INTERP_MAX_DEPTH 100000

/* Ejecuta `prog` desde `main`. Devuelve 0 si el programa termino bien; entonces
 * *has_value indica si `main` devolvio un valor y, de ser asi, *value es ese
 * valor (un boolean se devuelve como 1 o 0). Devuelve 1 si hubo un error de
 * ejecucion, que se informa por stderr como "runtime error: ...". */
int interp_run(const IrProgram *prog, int *has_value, int *value);

#endif /* INTERP_H */
