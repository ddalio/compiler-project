/*
 * ast.h - Arbol Sintactico Abstracto (AST) de C-TDS (Etapa 3).
 *
 * El AST se arma a partir del arbol de parsing (tree.h) y es el que recorre el
 * analisis semantico. A diferencia del arbol de parsing, cada nodo tiene un
 * tipo (AstKind), referencia a la tabla de simbolos, el tipo de la expresion y
 * la posicion en el fuente.
 *
 * Forma de los nodos (siguiendo la teoria: hasta tres hijos, y los condicionales
 * usan los tres). Las secuencias (declaraciones, sentencias, parametros,
 * argumentos) son listas enlazadas por `next`: un hijo que es una secuencia
 * apunta a su primer elemento.
 *
 *   AST_PROGRAM     left = variables globales,  mid = metodos
 *   AST_VAR_DECL    name, type            (una por cada identificador declarado)
 *   AST_METHOD_DECL name, type = retorno, left = parametros, right = cuerpo
 *                   (cada metodo es la raiz de "su" AST)
 *   AST_PARAM       name, type
 *   AST_BLOCK       left = variables declaradas, mid = sentencias
 *   AST_ASSIGN      left = AST_ID (ubicacion), right = expresion
 *   AST_CALL        name, left = argumentos
 *   AST_IF          left = condicion, mid = then, right = else (puede ser NULL)
 *   AST_WHILE       left = condicion, mid = cuerpo
 *   AST_RETURN      left = expresion (NULL si no lleva)
 *   AST_EMPTY       (sentencia vacia)
 *   AST_BINARY      op, left, right       (posicion: la del operador)
 *   AST_UNARY       op, left              (posicion: la del operador)
 *   AST_ID          name
 *   AST_INT_LIT / AST_FLOAT_LIT / AST_BOOL_LIT   name = lexema, value
 */
#ifndef AST_H
#define AST_H

#include <stdio.h>

#include "tree.h"
#include "types.h"

struct Symbol;      /* entrada de la tabla de simbolos (symtab.h) */

typedef enum {
    AST_PROGRAM,
    AST_VAR_DECL,
    AST_METHOD_DECL,
    AST_PARAM,
    AST_BLOCK,
    AST_ASSIGN,
    AST_CALL,
    AST_IF,
    AST_WHILE,
    AST_RETURN,
    AST_EMPTY,
    AST_BINARY,
    AST_UNARY,
    AST_ID,
    AST_INT_LIT,
    AST_FLOAT_LIT,
    AST_BOOL_LIT
} AstKind;

typedef enum {
    OP_NONE,
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,     /* aritmeticos */
    OP_LT, OP_GT, OP_EQ,                        /* relacionales */
    OP_AND, OP_OR,                              /* logicos */
    OP_NOT,                                     /* ! (unario) */
    OP_NEG                                      /* - (unario) */
} OpKind;

typedef struct Ast {
    AstKind kind;
    int line, col;          /* posicion en el fuente (desde 1) */
    Type type;              /* tipo (de la expresion o de lo declarado);
                             * TYPE_UNKNOWN hasta el analisis semantico */
    char *name;             /* identificador, o lexema de un literal */
    OpKind op;              /* operador (AST_BINARY y AST_UNARY) */
    union {                 /* valor de un literal */
        long long i;        /*   AST_INT_LIT */
        double f;           /*   AST_FLOAT_LIT */
        int b;              /*   AST_BOOL_LIT (0 o 1) */
    } value;
    struct Symbol *sym;     /* simbolo al que se refiere (lo completa el
                             * analisis semantico); el AST no es su dueño */
    struct Ast *left, *mid, *right;
    struct Ast *next;       /* siguiente elemento si el nodo esta en una lista */
} Ast;

/* Crea un nodo con los tres hijos dados (pueden ser NULL). Sin nombre, sin
 * simbolo y con tipo TYPE_UNKNOWN. */
Ast *ast_node(AstKind kind, int line, int col,
              Ast *left, Ast *mid, Ast *right);

/* Hoja: nodo sin hijos. */
Ast *ast_leaf(AstKind kind, int line, int col);

/* Construye el AST de un programa a partir de su arbol de parsing (la raiz
 * "program" que deja el parser). Copia todo lo que necesita: despues de
 * llamarla el arbol de parsing se puede liberar. */
Ast *ast_from_tree(const Node *program);

/* Nombre de un nodo ("method_decl", "binary_expr", ...) y texto de un
 * operador ("+", "&&", "!", ...). */
const char *ast_kind_name(AstKind kind);
const char *op_text(OpKind op);

/* Escribe el AST: un nodo por linea con 2 espacios de sangria por nivel.
 * Cada linea es "<nodo> [nombre u operador] [: tipo] [[clase]]": el tipo se
 * muestra solo si ya se calculo, y la clase del simbolo (variable, param o
 * method) en los identificadores y llamadas ya vinculados con la tabla de
 * simbolos. */
void ast_print(FILE *out, const Ast *root);

/* Libera el nodo, sus hijos y todos los que le siguen en su lista. No libera
 * los simbolos. Acepta NULL. */
void ast_free(Ast *n);

#endif /* AST_H */
