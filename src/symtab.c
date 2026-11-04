/* symtab.c - Tabla de Simbolos de C-TDS (ver symtab.h). */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "symtab.h"

/* Un nivel: la lista de simbolos de un scope y el nivel que lo contiene. */
typedef struct Level {
    Symbol *first, *last;
    struct Level *up;
} Level;

struct SymTab {
    Level *top;                 /* nivel actual (el mas interno) */
    int depth;                  /* niveles abiertos; 1 = solo el global */
    Symbol *all_first, *all_last;   /* todos los simbolos, en orden de alta */
    FILE *trace;                /* NULL = sin traza */
};

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) {
        fprintf(stderr, "c-tds: out of memory\n");
        exit(2);
    }
    return p;
}

static Level *level_new(Level *up)
{
    Level *l = xmalloc(sizeof *l);

    l->first = l->last = NULL;
    l->up = up;
    return l;
}

SymTab *symtab_new(void)
{
    SymTab *tab = xmalloc(sizeof *tab);

    tab->top = level_new(NULL);             /* nivel global */
    tab->depth = 1;
    tab->all_first = tab->all_last = NULL;
    tab->trace = NULL;
    return tab;
}

void symtab_free(SymTab *tab)
{
    Symbol *s = tab->all_first;
    Level *l = tab->top;

    while (s) {
        Symbol *next = s->next_all;

        free(s->name);
        free(s->param_types);
        free(s);
        s = next;
    }
    while (l) {                             /* niveles que quedaron abiertos */
        Level *up = l->up;

        free(l);
        l = up;
    }
    free(tab);
}

void symtab_set_trace(SymTab *tab, FILE *out)
{
    tab->trace = out;
}

void symtab_open_level(SymTab *tab)
{
    tab->top = level_new(tab->top);
    tab->depth++;
    if (tab->trace)
        fprintf(tab->trace, "[symtab] open level %d\n", tab->depth);
}

void symtab_close_level(SymTab *tab)
{
    Level *closed = tab->top;

    assert(tab->depth > 1);                 /* el nivel global no se cierra */
    if (tab->trace)
        fprintf(tab->trace, "[symtab] close level %d\n", tab->depth);
    tab->top = closed->up;
    tab->depth--;
    free(closed);       /* los simbolos siguen vivos: los libera symtab_free */
}

int symtab_depth(const SymTab *tab)
{
    return tab->depth;
}

/* Busca `name` en la lista de simbolos de un nivel. */
static Symbol *find_in_level(const Level *level, const char *name)
{
    for (Symbol *s = level->first; s; s = s->next_in_level)
        if (strcmp(s->name, name) == 0)
            return s;
    return NULL;
}

Symbol *symtab_insert(SymTab *tab, SymClass cls, const char *name, Type type,
                      int line, int col)
{
    Symbol *s;
    size_t len;

    if (find_in_level(tab->top, name))
        return NULL;                        /* redeclaracion */

    s = xmalloc(sizeof *s);
    len = strlen(name) + 1;
    s->name = memcpy(xmalloc(len), name, len);
    s->cls = cls;
    s->type = type;
    s->line = line;
    s->col = col;
    s->level = tab->depth;
    s->param_types = NULL;
    s->nparams = 0;
    s->next_in_level = s->next_all = NULL;

    if (tab->top->last)
        tab->top->last->next_in_level = s;
    else
        tab->top->first = s;
    tab->top->last = s;

    if (tab->all_last)
        tab->all_last->next_all = s;
    else
        tab->all_first = s;
    tab->all_last = s;

    if (tab->trace)
        fprintf(tab->trace, "[symtab] insert %s (%s %s) at level %d\n",
                s->name, sym_class_name(cls), type_name(type), s->level);
    return s;
}

void symtab_set_params(Symbol *method, const Type *types, int n)
{
    free(method->param_types);
    method->param_types = NULL;
    method->nparams = n;
    if (n > 0) {
        method->param_types = xmalloc((size_t)n * sizeof *method->param_types);
        memcpy(method->param_types, types, (size_t)n * sizeof *types);
    }
}

Symbol *symtab_lookup_current(const SymTab *tab, const char *name)
{
    return find_in_level(tab->top, name);
}

Symbol *symtab_lookup(const SymTab *tab, const char *name)
{
    for (const Level *l = tab->top; l; l = l->up) {
        Symbol *s = find_in_level(l, name);

        if (s)
            return s;
    }
    return NULL;
}

const char *sym_class_name(SymClass cls)
{
    switch (cls) {
    case SYM_VARIABLE: return "variable";
    case SYM_PARAM:    return "param";
    case SYM_METHOD:   return "method";
    }
    return "unknown";
}

void symtab_dump(FILE *out, const SymTab *tab)
{
    for (const Symbol *s = tab->all_first; s; s = s->next_all) {
        fprintf(out, "level %d %s %s : %s", s->level, sym_class_name(s->cls),
                s->name, type_name(s->type));
        if (s->cls == SYM_METHOD) {
            fputs(" (", out);
            for (int i = 0; i < s->nparams; i++)
                fprintf(out, "%s%s", i ? ", " : "", type_name(s->param_types[i]));
            fputc(')', out);
        }
        fprintf(out, " @%d:%d\n", s->line, s->col);
    }
}
