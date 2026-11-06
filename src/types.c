/* types.c - Tipos de C-TDS: nombres y compatibilidad (ver types.h). */
#include "types.h"

int type_is_numeric(Type t)
{
    return t == TYPE_INT || t == TYPE_FLOAT;
}

int types_compatible(Type expected, Type actual)
{
    if (expected == TYPE_ERROR || actual == TYPE_ERROR)
        return 1;
    if (type_is_numeric(expected) && type_is_numeric(actual))
        return 1;
    return expected == actual;
}

const char *type_name(Type t)
{
    switch (t) {
    case TYPE_UNKNOWN: return "unknown";
    case TYPE_INT:     return "int";
    case TYPE_FLOAT:   return "float";
    case TYPE_BOOLEAN: return "boolean";
    case TYPE_VOID:    return "void";
    case TYPE_ERROR:   return "error";
    }
    return "unknown";
}
