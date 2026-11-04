/*
 * symtab.h - Tabla de Simbolos de C-TDS (Etapa 3).
 *
 * Es una PILA DE NIVELES (como en la teoria, 06-TDS-TS): cada nivel es la
 * lista de simbolos de un scope. El nivel 1 es el global (variables y
 * metodos); al entrar a un metodo o a un bloque se abre un nivel y al salir se
 * lo cierra.
 *
 *   symtab_open_level    abre un nivel (AbrirNivel)
 *   symtab_close_level   cierra el nivel actual (CerrarNivel)
 *   symtab_insert        agrega un simbolo al nivel actual (InsertarSimbolo)
 *   symtab_lookup        busca un nombre desde el nivel actual hasta el global
 *                        (BuscarSimbolo): un nombre de un nivel interno oculta
 *                        al de igual nombre de un nivel externo
 *   symtab_lookup_current busca solo en el nivel actual (para detectar
 *                        redeclaraciones)
 *
 * Cerrar un nivel solo hace INVISIBLES sus simbolos: siguen existiendo porque
 * los nodos del AST los referencian (campo `sym`). Todos se liberan juntos con
 * symtab_free.
 */
#ifndef SYMTAB_H
#define SYMTAB_H

#include <stdio.h>

#include "types.h"

typedef enum {
    SYM_VARIABLE,       /* variable global o local */
    SYM_PARAM,          /* parametro formal de un metodo */
    SYM_METHOD
} SymClass;

typedef struct Symbol {
    SymClass cls;
    char *name;
    Type type;          /* de una variable o parametro; de retorno en un metodo */
    int line, col;      /* donde se declaro */
    int level;          /* nivel en el que se declaro (1 = global) */
    Type *param_types;  /* metodo: tipos de los parametros formales, en orden */
    int nparams;        /* metodo: cantidad de parametros */

    /* Enlaces internos de la tabla; no usarlos desde afuera. */
    struct Symbol *next_in_level;
    struct Symbol *next_all;
} Symbol;

typedef struct SymTab SymTab;

/* Crea una tabla con el nivel global (nivel 1) ya abierto. */
SymTab *symtab_new(void);

/* Libera la tabla y TODOS sus simbolos, incluso los de niveles ya cerrados. */
void symtab_free(SymTab *tab);

/* Si `out` no es NULL, cada operacion (abrir, cerrar, insertar) se informa
 * ahi, una linea por operacion. Es la traza que se muestra con -debug. */
void symtab_set_trace(SymTab *tab, FILE *out);

/* Abre un nivel nuevo, anidado en el actual. */
void symtab_open_level(SymTab *tab);

/* Cierra el nivel actual. No se puede cerrar el nivel global. */
void symtab_close_level(SymTab *tab);

/* Cantidad de niveles abiertos (1 = solo el global). */
int symtab_depth(const SymTab *tab);

/* Agrega un simbolo al nivel actual y lo devuelve. Si ya hay un simbolo con
 * ese nombre en el nivel actual NO lo agrega y devuelve NULL (el que llama
 * puede obtener el anterior con symtab_lookup_current). `name` se copia. */
Symbol *symtab_insert(SymTab *tab, SymClass cls, const char *name, Type type,
                      int line, int col);

/* Define los tipos de los parametros formales de un metodo (se copian). */
void symtab_set_params(Symbol *method, const Type *types, int n);

/* Busca `name` solo en el nivel actual. NULL si no esta. */
Symbol *symtab_lookup_current(const SymTab *tab, const char *name);

/* Busca `name` desde el nivel actual hacia el global; devuelve el primero que
 * encuentra (el mas interno). NULL si no esta. */
Symbol *symtab_lookup(const SymTab *tab, const char *name);

/* Nombre de una clase de simbolo: "variable", "param" o "method". */
const char *sym_class_name(SymClass cls);

/* Escribe todos los simbolos que se insertaron (tambien los de niveles ya
 * cerrados), uno por linea, en orden de declaracion:
 *   level 1 method f : int (int, float) @2:5
 *   level 2 variable x : int @3:7                                          */
void symtab_dump(FILE *out, const SymTab *tab);

#endif /* SYMTAB_H */
