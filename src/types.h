/*
 * types.h - Tipos del lenguaje C-TDS (Etapa 3: analisis semantico).
 */
#ifndef TYPES_H
#define TYPES_H

typedef enum {
    TYPE_UNKNOWN = 0,   /* todavia no calculado (antes del analisis semantico) */
    TYPE_INT,
    TYPE_FLOAT,
    TYPE_BOOLEAN,
    TYPE_VOID,          /* tipo de retorno de un metodo que no devuelve valor */
    TYPE_ERROR          /* una expresion con error: compatible con todo, para
                         * no encadenar errores (ver docs/etapa-3-semantico.md) */
} Type;

/* int o float. */
int type_is_numeric(Type t);

/* Indica si un valor de tipo `actual` puede usarse donde se espera `expected`
 * (asignacion, return, argumento, comparacion con ==). Son compatibles dos
 * tipos iguales y tambien int con float en cualquier sentido (regla 14: se
 * permiten coerciones o truncamientos entre int y float). boolean no es
 * compatible con ningun otro tipo. TYPE_ERROR es compatible con todo: una
 * expresion que ya tuvo un error no produce errores nuevos. */
int types_compatible(Type expected, Type actual);

/* Nombre de un tipo para los mensajes y las salidas ("int", "float", ...). */
const char *type_name(Type t);

#endif /* TYPES_H */
