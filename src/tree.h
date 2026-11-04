/*
 * tree.h - Arbol de parsing de C-TDS (Etapa 2).
 *
 * Es un arbol generico: cada nodo tiene una etiqueta, opcionalmente un texto
 * (el lexema, en las hojas) y una lista de hijos. Lo arma el parser (bison.y)
 * y se escribe en el archivo .sint con tree_print.
 */
#ifndef TREE_H
#define TREE_H

#include <stdio.h>

typedef struct Node {
    const char *label;      /* no terminal ("if", "assign") o token ("ID") */
    char *text;             /* lexema (solo hojas), NULL en nodos internos */
    struct Node **kids;
    int nkids;
    int cap;                /* capacidad del arreglo kids */
} Node;

/* Nodo interno sin hijos. `label` debe ser una cadena que viva todo el
 * programa (un literal). */
Node *node_new(const char *label);

/* Hoja: `label` es el nombre del token y `text` su lexema (se copia). */
Node *leaf_new(const char *label, const char *text);

/* Agrega `child` al final de los hijos de `parent`. Devuelve `parent`. */
Node *node_add(Node *parent, Node *child);

/* Atajos: nodo interno con 1, 2, 3 o 4 hijos. */
Node *node1(const char *label, Node *a);
Node *node2(const char *label, Node *a, Node *b);
Node *node3(const char *label, Node *a, Node *b, Node *c);
Node *node4(const char *label, Node *a, Node *b, Node *c, Node *d);

/* Mueve todos los hijos de `list` al final de los de `dst` y libera `list`.
 * Sirve para convertir una lista auxiliar del parser en hijos de un nodo.
 * Devuelve `dst`. */
Node *node_adopt(Node *dst, Node *list);

/* Escribe el arbol: un nodo por linea, 2 espacios de sangria por nivel.
 * Hojas: "NOMBRE_TOKEN lexema". Nodos internos: solo la etiqueta. */
void tree_print(FILE *out, const Node *root);

/* Libera el nodo y todo su subarbol. Acepta NULL. */
void node_free(Node *n);

#endif /* TREE_H */
