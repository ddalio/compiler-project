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
 *
 * El mismo recorrido calcula el tipo de cada expresion (campo `type` del AST)
 * y verifica las reglas de tipos. Una expresion con error recibe TYPE_ERROR,
 * que es compatible con todo, para no encadenar errores.
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
    const Ast *method;      /* metodo que se esta analizando (NULL fuera) */
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

static void analyze_expr(Ctx *c, Ast *e);

/* Analiza una llamada (como sentencia o como expresion): la resuelve, analiza
 * los argumentos y verifica la regla 4: la cantidad y los tipos de los
 * argumentos coinciden con los parametros formales. */
static void analyze_call(Ctx *c, Ast *call)
{
    const Symbol *m;
    int nargs = 0;
    int i;

    resolve_call(c, call);
    for (Ast *arg = call->left; arg; arg = arg->next) {
        analyze_expr(c, arg);
        nargs++;
    }
    m = call->sym;
    if (!m)
        return;                             /* ya informado */

    if (nargs != m->nparams) {
        sem_error(c, call->line, call->col,
                  "call to '%s' has %d argument%s, expected %d", call->name,
                  nargs, nargs == 1 ? "" : "s", m->nparams);
        return;
    }
    i = 0;
    for (const Ast *arg = call->left; arg; arg = arg->next, i++) {
        if (!types_compatible(m->param_types[i], arg->type))
            sem_error(c, arg->line, arg->col,
                      "argument %d of call to '%s' has type %s, expected %s",
                      i + 1, call->name, type_name(arg->type),
                      type_name(m->param_types[i]));
    }
}

/* Operadores aritmeticos y relacionales (regla 10): los dos operandos deben
 * ser int o float. Aritmetico: el resultado es float si algun operando lo es,
 * si no int. Relacional: boolean. */
static void check_numeric_op(Ctx *c, Ast *e, Type l, Type r)
{
    if (!type_is_numeric(l) || !type_is_numeric(r)) {
        sem_error(c, e->line, e->col,
                  "operands of '%s' must be int or float, got %s and %s",
                  op_text(e->op), type_name(l), type_name(r));
        return;
    }
    if (e->op == OP_LT || e->op == OP_GT)
        e->type = TYPE_BOOLEAN;
    else
        e->type = (l == TYPE_FLOAT || r == TYPE_FLOAT) ? TYPE_FLOAT : TYPE_INT;
}

static void analyze_binary(Ctx *c, Ast *e)
{
    Type l, r;

    analyze_expr(c, e->left);
    analyze_expr(c, e->right);
    l = e->left->type;
    r = e->right->type;

    e->type = TYPE_ERROR;
    if (l == TYPE_ERROR || r == TYPE_ERROR)
        return;                             /* error ya informado */

    switch (e->op) {
    case OP_EQ:                             /* regla 11 */
    case OP_NE:
        if (!types_compatible(l, r))
            sem_error(c, e->line, e->col,
                      "operands of '%s' must have the same type, got %s and %s",
                      op_text(e->op), type_name(l), type_name(r));
        else
            e->type = TYPE_BOOLEAN;
        break;
    case OP_AND:                            /* regla 12 */
    case OP_OR:
        if (l != TYPE_BOOLEAN || r != TYPE_BOOLEAN)
            sem_error(c, e->line, e->col,
                      "operands of '%s' must be boolean, got %s and %s",
                      op_text(e->op), type_name(l), type_name(r));
        else
            e->type = TYPE_BOOLEAN;
        break;
    default:                                /* + - * / % < >: regla 10 */
        check_numeric_op(c, e, l, r);
        break;
    }
}

static void analyze_unary(Ctx *c, Ast *e)
{
    Type t;

    analyze_expr(c, e->left);
    t = e->left->type;
    e->type = TYPE_ERROR;
    if (t == TYPE_ERROR)
        return;

    if (e->op == OP_NOT) {                  /* regla 12 */
        if (t != TYPE_BOOLEAN)
            sem_error(c, e->line, e->col,
                      "operand of '!' must be boolean, got %s", type_name(t));
        else
            e->type = TYPE_BOOLEAN;
    } else {                                /* '-' unario: regla 10 */
        if (!type_is_numeric(t))
            sem_error(c, e->line, e->col,
                      "operand of '-' must be int or float, got %s",
                      type_name(t));
        else
            e->type = t;
    }
}

static void analyze_expr(Ctx *c, Ast *e)
{
    switch (e->kind) {
    case AST_ID:
        resolve_variable(c, e);
        break;
    case AST_CALL:
        analyze_call(c, e);
        if (e->sym && e->type == TYPE_VOID) {       /* regla 5 */
            sem_error(c, e->line, e->col,
                      "'%s' returns no value and cannot be used in an expression",
                      e->name);
            e->type = TYPE_ERROR;
        }
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
        analyze_binary(c, e);
        break;
    case AST_UNARY:
        analyze_unary(c, e);
        break;
    default:
        break;
    }
}

/* -------------------------------------------------------------- sentencias */

static void analyze_block(Ctx *c, Ast *block, int open_level);

/* Regla 9: la condicion de un if o while debe ser boolean. `what` es "if" o
 * "while"; el error se informa en la posicion de la sentencia. */
static void check_condition(Ctx *c, const Ast *stmt, const char *what)
{
    Type t = stmt->left->type;

    if (!types_compatible(TYPE_BOOLEAN, t))
        sem_error(c, stmt->line, stmt->col,
                  "condition of '%s' must be boolean, got %s", what,
                  type_name(t));
}

/* Reglas 6 y 7: return con o sin expresion segun el tipo del metodo, y el tipo
 * de la expresion igual al de retorno. */
static void check_return(Ctx *c, const Ast *ret)
{
    const Ast *m = c->method;

    if (m->type == TYPE_VOID) {
        if (ret->left)
            sem_error(c, ret->line, ret->col,
                      "void method '%s' cannot return a value", m->name);
    } else if (!ret->left) {
        sem_error(c, ret->line, ret->col,
                  "method '%s' must return a value of type %s", m->name,
                  type_name(m->type));
    } else if (!types_compatible(m->type, ret->left->type)) {
        sem_error(c, ret->line, ret->col,
                  "cannot return %s from method '%s' of type %s",
                  type_name(ret->left->type), m->name, type_name(m->type));
    }
}

static void analyze_stmt(Ctx *c, Ast *s)
{
    switch (s->kind) {
    case AST_ASSIGN:
        resolve_variable(c, s->left);       /* la ubicacion */
        analyze_expr(c, s->right);
        if (!types_compatible(s->left->type, s->right->type))   /* regla 13 */
            sem_error(c, s->line, s->col,
                      "cannot assign %s to '%s' of type %s",
                      type_name(s->right->type), s->left->name,
                      type_name(s->left->type));
        break;
    case AST_CALL:                          /* llamada como sentencia */
        analyze_call(c, s);
        break;
    case AST_IF:
        analyze_expr(c, s->left);
        check_condition(c, s, "if");
        analyze_block(c, s->mid, 1);
        if (s->right)
            analyze_block(c, s->right, 1);
        break;
    case AST_WHILE:
        analyze_expr(c, s->left);
        check_condition(c, s, "while");
        analyze_block(c, s->mid, 1);
        break;
    case AST_RETURN:
        if (s->left)
            analyze_expr(c, s->left);
        check_return(c, s);
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

/* Indica si la sentencia nunca termina "normalmente": toda ejecucion que la
 * recorre pasa por un return (o no termina). Es un analisis conservador:
 *   - return: si;
 *   - bloque: si alguna de sus sentencias lo hace;
 *   - if con else: si las dos ramas lo hacen (un if sin else, no);
 *   - while: solo si la condicion es el literal true (no termina nunca);
 *   - cualquier otra sentencia: no. */
static int always_returns(const Ast *s)
{
    switch (s->kind) {
    case AST_RETURN:
        return 1;
    case AST_BLOCK:
        for (const Ast *x = s->mid; x; x = x->next)
            if (always_returns(x))
                return 1;
        return 0;
    case AST_IF:
        return s->right && always_returns(s->mid) && always_returns(s->right);
    case AST_WHILE:
        return s->left->kind == AST_BOOL_LIT && s->left->value.b;
    default:
        return 0;
    }
}

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
    c->method = m;
    analyze_block(c, m->right, 0);
    c->method = NULL;
    symtab_close_level(c->tab);

    /* Un metodo con tipo no puede alcanzar el final sin devolver un valor. */
    if (m->type != TYPE_VOID && !always_returns(m->right))
        sem_error(c, m->line, m->col,
                  "method '%s' may reach the end without returning a value",
                  m->name);
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
    Ctx c = { tab, filename, 0, NULL };

    for (Ast *v = program->left; v; v = v->next)
        declare(&c, v, SYM_VARIABLE);
    for (Ast *m = program->mid; m; m = m->next)
        analyze_method(&c, m);
    check_main(&c);
    return c.errors;
}
