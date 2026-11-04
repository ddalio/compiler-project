/* tree.c - Arbol de parsing de C-TDS (ver tree.h). */
#include <stdlib.h>
#include <string.h>

#include "tree.h"

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) {
        fprintf(stderr, "c-tds: out of memory\n");
        exit(2);
    }
    return p;
}

Node *node_new(const char *label)
{
    Node *n = xmalloc(sizeof *n);

    n->label = label;
    n->text = NULL;
    n->kids = NULL;
    n->nkids = 0;
    n->cap = 0;
    n->line = n->col = 0;
    return n;
}

Node *node_at(Node *n, int line, int col)
{
    n->line = line;
    n->col = col;
    return n;
}

Node *leaf_new(const char *label, const char *text)
{
    Node *n = node_new(label);
    size_t len = strlen(text) + 1;

    n->text = xmalloc(len);
    memcpy(n->text, text, len);
    return n;
}

Node *node_add(Node *parent, Node *child)
{
    if (parent->nkids == parent->cap) {
        int cap = parent->cap ? parent->cap * 2 : 4;
        Node **kids = xmalloc((size_t)cap * sizeof *kids);

        if (parent->nkids)
            memcpy(kids, parent->kids, (size_t)parent->nkids * sizeof *kids);
        free(parent->kids);
        parent->kids = kids;
        parent->cap = cap;
    }
    parent->kids[parent->nkids++] = child;
    return parent;
}

Node *node1(const char *label, Node *a)
{
    return node_add(node_new(label), a);
}

Node *node2(const char *label, Node *a, Node *b)
{
    return node_add(node1(label, a), b);
}

Node *node3(const char *label, Node *a, Node *b, Node *c)
{
    return node_add(node2(label, a, b), c);
}

Node *node4(const char *label, Node *a, Node *b, Node *c, Node *d)
{
    return node_add(node3(label, a, b, c), d);
}

Node *node_adopt(Node *dst, Node *list)
{
    for (int i = 0; i < list->nkids; i++)
        node_add(dst, list->kids[i]);
    list->nkids = 0;                /* los hijos ya son de `dst` */
    node_free(list);
    return dst;
}

static void print_node(FILE *out, const Node *n, int depth)
{
    fprintf(out, "%*s%s", depth * 2, "", n->label);
    if (n->text)
        fprintf(out, " %s", n->text);
    fputc('\n', out);
    for (int i = 0; i < n->nkids; i++)
        print_node(out, n->kids[i], depth + 1);
}

void tree_print(FILE *out, const Node *root)
{
    if (root)
        print_node(out, root, 0);
}

void node_free(Node *n)
{
    if (!n)
        return;
    for (int i = 0; i < n->nkids; i++)
        node_free(n->kids[i]);
    free(n->kids);
    free(n->text);
    free(n);
}
