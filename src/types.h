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

/* Nombre de un tipo para los mensajes y las salidas ("int", "float", ...). */
const char *type_name(Type t);

#endif /* TYPES_H */
