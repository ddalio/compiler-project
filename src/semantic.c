/*
 * semantic.c - Analisis semantico de C-TDS (ver semantic.h).
 *
 * Un unico recorrido del AST en el orden del fuente, que arma la tabla de
 * simbolos a medida que avanza:
 *   - programa: se declaran las variables globales y despues cada metodo;
 *   - metodo: se lo inserta en el nivel global ANTES de analizar el cuerpo
 *     (asi puede llamarse a si mismo), se abre un nivel para sus parametros y
 *     las variables de su cuerpo (comparten nivel) y se analizan sus
 *     sentencias;
 *   - bloque anidado (cuerpo de if/while o bloque suelto): un nivel propio.
 * Como las declaraciones siempre se recorren antes que sus usos, un nombre que
 * todavia no esta en la tabla es un uso antes de la declaracion.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "semantic.h"

/* Estado del analisis. */
typedef struct {
    SymTab *tab;
    const char *file;       /* para los mensajes */
    int errors;             /* errores informados hasta ahora */
} Ctx;

/* Informa un error semantico en (line, col) y lo cuenta. */
static void sem_error(Ctx *c, int line, int col, const char *fmt, ...)
{
    va_list ap;

    c->errors++;
    fprintf(stderr, "%s:%d:%d: semantic error: ", c->file, line, col);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ------------------------------------------------------------ declaraciones */

/* Inserta en el nivel actual la declaracion `decl` (AST_VAR_DECL, AST_PARAM o
 * AST_METHOD_DECL). Si el nombre ya esta declarado en ese nivel es un error
 * (regla 1) y la declaracion queda sin simbolo. */
static Symbol *declare(Ctx *c, Ast *decl, SymClass cls)
{
    Symbol *s = symtab_insert(c->tab, cls, decl->name, decl->type,
                              decl->line, decl->col);

    if (!s) {
        const Symbol *prev = symtab_lookup_current(c->tab, decl->name);

        sem_error(c, decl->line, decl->col,
                  "redeclaration of '%s' (previous declaration at %d:%d)",
                  decl->name, prev->line, prev->col);
    }
    decl->sym = s;
    return s;
}

/* ---------------------------------------------------------------- usos */

/* Resuelve un identificador usado como valor o como ubicacion: tiene que estar
 * declarado (reglas 2 y 8) y ser una variable o un parametro, no un metodo. Le
 * da al nodo el simbolo y su tipo; si hay error, TYPE_ERROR. */
static void resolve_variable(Ctx *c, Ast *id)
{
    Symbol *s = symtab_lookup(c->tab, id->name);

    id->sym = NULL;
    id->type = TYPE_ERROR;
    if (!s) {
        sem_error(c, id->line, id->col, "'%s' undeclared", id->name);
    } else if (s->cls == SYM_METHOD) {
        sem_error(c, id->line, id->col, "'%s' is a method, not a variable",
                  id->name);
    } else {
        id->sym = s;
        id->type = s->type;
    }
}

/* Resuelve el nombre de una llamada: tiene que estar declarado y ser un
 * metodo. El tipo de la llamada es el de retorno del metodo. */
static void resolve_call(Ctx *c, Ast *call)
{
    Symbol *s = symtab_lookup(c->tab, call->name);

    call->sym = NULL;
    call->type = TYPE_ERROR;
    if (!s) {
        sem_error(c, call->line, call->col, "'%s' undeclared", call->name);
    } else if (s->cls != SYM_METHOD) {
        sem_error(c, call->line, call->col, "'%s' is not a method",
                  call->name);
    } else {
        call->sym = s;
        call->type = s->type;
    }
}

/* ------------------------------------------------------------ expresiones */

static void analyze_expr(Ctx *c, Ast *e)
{
    switch (e->kind) {
    case AST_ID:
        resolve_variable(c, e);
        break;
    case AST_CALL:
        resolve_call(c, e);
        for (Ast *arg = e->left; arg; arg = arg->next)
            analyze_expr(c, arg);
        break;
    case AST_INT_LIT:
        e->type = TYPE_INT;
        break;
    case AST_FLOAT_LIT:
        e->type = TYPE_FLOAT;
        break;
    case AST_BOOL_LIT:
        e->type = TYPE_BOOLEAN;
        break;
    case AST_BINARY:
        analyze_expr(c, e->left);
        analyze_expr(c, e->right);
        break;
    case AST_UNARY:
        analyze_expr(c, e->left);
        break;
    default:
        break;
    }
}

/* -------------------------------------------------------------- sentencias */

static void analyze_block(Ctx *c, Ast *block, int open_level);

static void analyze_stmt(Ctx *c, Ast *s)
{
    switch (s->kind) {
    case AST_ASSIGN:
        resolve_variable(c, s->left);       /* la ubicacion */
        analyze_expr(c, s->right);
        break;
    case AST_CALL:                          /* llamada como sentencia */
        analyze_expr(c, s);
        break;
    case AST_IF:
        analyze_expr(c, s->left);
        analyze_block(c, s->mid, 1);
        if (s->right)
            analyze_block(c, s->right, 1);
        break;
    case AST_WHILE:
        analyze_expr(c, s->left);
        analyze_block(c, s->mid, 1);
        break;
    case AST_RETURN:
        if (s->left)
            analyze_expr(c, s->left);
        break;
    case AST_BLOCK:
        analyze_block(c, s, 1);
        break;
    default:                                /* AST_EMPTY */
        break;
    }
}

/* Declara las variables del bloque y analiza sus sentencias. Un bloque anidado
 * abre su propio nivel (open_level = 1); el cuerpo de un metodo no, porque
 * comparte el nivel con los parametros. */
static void analyze_block(Ctx *c, Ast *block, int open_level)
{
    if (open_level)
        symtab_open_level(c->tab);
    for (Ast *d = block->left; d; d = d->next)
        declare(c, d, SYM_VARIABLE);
    for (Ast *s = block->mid; s; s = s->next)
        analyze_stmt(c, s);
    if (open_level)
        symtab_close_level(c->tab);
}

/* ---------------------------------------------------------------- metodos */

static void analyze_method(Ctx *c, Ast *m)
{
    Symbol *sym = declare(c, m, SYM_METHOD);    /* antes del cuerpo: recursion */
    int n = 0;

    for (Ast *p = m->left; p; p = p->next)
        n++;
    if (sym && n > 0) {
        Type *types = malloc((size_t)n * sizeof *types);
        int i = 0;

        if (!types) {
            fprintf(stderr, "c-tds: out of memory\n");
            exit(2);
        }
        for (Ast *p = m->left; p; p = p->next)
            types[i++] = p->type;
        symtab_set_params(sym, types, n);
        free(types);
    }

    symtab_open_level(c->tab);                  /* parametros + cuerpo */
    for (Ast *p = m->left; p; p = p->next)
        declare(c, p, SYM_PARAM);
    analyze_block(c, m->right, 0);
    symtab_close_level(c->tab);
}

/* Regla 3: tiene que haber un metodo `main` sin parametros. */
static void check_main(Ctx *c)
{
    const Symbol *s = symtab_lookup(c->tab, "main");

    if (!s)
        sem_error(c, 1, 1, "program has no 'main' method");
    else if (s->cls != SYM_METHOD)
        sem_error(c, s->line, s->col, "'main' must be a method");
    else if (s->nparams != 0)
        sem_error(c, s->line, s->col, "'main' must not have parameters");
}

int semantic_analyze(Ast *program, const char *filename, SymTab *tab)
{
    Ctx c = { tab, filename, 0 };

    for (Ast *v = program->left; v; v = v->next)
        declare(&c, v, SYM_VARIABLE);
    for (Ast *m = program->mid; m; m = m->next)
        analyze_method(&c, m);
    check_main(&c);
    return c.errors;
}
