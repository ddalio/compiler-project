/*
 * opt.h - Optimizador del codigo intermedio de C-TDS (Etapa 6).
 *
 * Transforma el codigo de tres direcciones (ir.h) para que sea mas corto sin
 * cambiar lo que hace el programa. Las optimizaciones (se eligen con -opt):
 *
 *   fold       plegado de constantes y simplificaciones algebraicas
 *   propagate  propagacion de constantes y de copias (dentro de cada bloque)
 *   dce        eliminacion de codigo muerto (resultados sin uso y codigo
 *              inalcanzable)
 *   jumps      optimizacion de saltos y de etiquetas
 *   temps      compactacion de temporales (achica el marco de pila)
 *
 * Reglas de seguridad (docs/etapa-6-optimizador.md): aritmetica de 32 bits que
 * da la vuelta como en el codigo nativo; una division o un resto por cero nunca se
 * pliega ni se elimina; las llamadas nunca se eliminan ni se mueven y una
 * llamada invalida lo que se sabe de las variables globales.
 */
#ifndef OPT_H
#define OPT_H

#include <stdio.h>

#include "ir.h"

/* Optimizaciones, como bits de una mascara. */
enum {
    OPT_FOLD      = 1 << 0,
    OPT_PROPAGATE = 1 << 1,
    OPT_DCE       = 1 << 2,
    OPT_JUMPS     = 1 << 3,
    OPT_TEMPS     = 1 << 4,
    OPT_ALL       = (1 << 5) - 1
};

/* Lo que hizo el optimizador sobre un programa. */
typedef struct {
    int before, after;      /* instrucciones antes y despues */
    int temps_before, temps_after;      /* temporales antes y despues */
    int folded;             /* operaciones plegadas o simplificadas */
    int propagated;         /* operandos reemplazados por una constante o copia */
    int dead;               /* instrucciones o resultados eliminados */
    int jumps;              /* saltos y etiquetas eliminados o redirigidos */
    int rounds;             /* vueltas hasta que el codigo dejo de cambiar */
} OptStats;

/* Convierte el nombre de una optimizacion ("fold", ..., "all") en su bit.
 * Devuelve 0 si el nombre no existe. */
unsigned opt_from_name(const char *name);

/* Lista de nombres validos para los mensajes de error. */
const char *opt_names(void);

/* Aplica a `prog` las optimizaciones de la mascara `passes`. Cada metodo se
 * optimiza hasta que el codigo deja de cambiar. Si `report` no es NULL escribe
 * ahi una linea por metodo con las instrucciones antes y despues. Deja los
 * totales en *stats. */
void opt_run(IrProgram *prog, unsigned passes, FILE *report, OptStats *stats);

#endif /* OPT_H */
