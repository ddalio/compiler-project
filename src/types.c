/* types.c - Nombres de los tipos de C-TDS (ver types.h). */
#include "types.h"

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
