/*
 * irgen.c - Generador de codigo intermedio de C-TDS (ver irgen.h).
 *
 * Reescritura del AST a una secuencia lineal de instrucciones de tres
 * direcciones. El corazon es gen_expr: genera el codigo de una expresion y
 * devuelve el OPERANDO donde queda su valor (una constante, una variable o un
 * temporal). Las sentencias de control (if, while) se traducen a etiquetas y
 * saltos; && y || se traducen a saltos para cumplir el cortocircuito.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "irgen.h"
#include "symtab.h"

/* Estado de la generacion. */
typedef struct {
    IrProgram *prog;
    IrFunc *func;               /* metodo que se esta generando */
    /* Correspondencia simbolo de la tabla -> variable del codigo intermedio. */
    struct { const Symbol *sym; const IrVar *var; } *map;
    int nmap, cap_map;
} Gen;

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n);
    if (!p) {
        fprintf(stderr, "c-tds: out of memory\n");
        exit(2);
    }
    return p;
}

/* -------------------------------------------------- simbolo -> variable IR */

static void map_add(Gen *g, const Symbol *sym, const IrVar *var)
{
    if (g->nmap == g->cap_map) {
        g->cap_map = g->cap_map ? g->cap_map * 2 : 32;
        g->map = xrealloc(g->map, (size_t)g->cap_map * sizeof *g->map);
    }
    g->map[g->nmap].sym = sym;
    g->map[g->nmap].var = var;
    g->nmap++;
}

static const IrVar *map_find(const Gen *g, const Symbol *sym)
{
    for (int i = 0; i < g->nmap; i++)
        if (g->map[i].sym == sym)
            return g->map[i].var;
    fprintf(stderr, "c-tds: internal error: no IR variable for symbol '%s'\n",
            sym ? sym->name : "?");
    exit(2);
}

/* Metodo del codigo intermedio con ese nombre. Un metodo solo puede llamar a los
 * declarados antes que el o a si mismo, asi que ya existe. */
static IrFunc *find_func(const Gen *g, const char *name)
{
    for (IrFunc *f = g->prog->first; f; f = f->next)
        if (strcmp(f->name, name) == 0)
            return f;
    fprintf(stderr, "c-tds: internal error: no IR method '%s'\n", name);
    exit(2);
}

/* ------------------------------------------------------------ utilidades */

static void emit(Gen *g, IrOp op, Operand a, Operand b, Operand result)
{
    ir_emit(g->func, op, a, b, result);
}

/* Indica si la expresion contiene alguna llamada (que podria modificar una
 * variable global). */
static int has_call(const Ast *e)
{
    if (!e)
        return 0;
    if (e->kind == AST_CALL)
        return 1;
    return has_call(e->left) || has_call(e->right);
}

/* Indica si el operando es una variable global (el unico tipo de operando que
 * un metodo llamado puede cambiar). */
static int is_global(Operand o)
{
    return o.kind == OPD_VAR && o.var->is_global;
}

/* Si `value` es una global y la expresion que se evalua a continuacion tiene
 * una llamada, guarda el valor en un temporal ahora: la llamada podria
 * modificar la global y la spec exige evaluar de izquierda a derecha
 * (decision 9). Devuelve el operando a usar. */
static Operand protect(Gen *g, Operand value, const Ast *next_expr)
{
    Operand copy;

    if (!is_global(value) || !has_call(next_expr))
        return value;
    copy = ir_new_temp(g->func);
    emit(g, IR_MOVE, value, ir_none(), copy);
    return copy;
}

/* ------------------------------------------------------------ expresiones */

static Operand gen_expr(Gen *g, const Ast *e);

/* Codigo de una llamada. Primero se evaluan todos los argumentos, de izquierda
 * a derecha; despues se emiten los param seguidos y el call (decision 10).
 * Devuelve el temporal con el valor devuelto, u OPD_NONE si el metodo es void. */
static Operand gen_call(Gen *g, const Ast *call)
{
    const IrFunc *callee = find_func(g, call->name);
    Operand *args = NULL;
    Operand result = ir_none();
    int n = 0, i = 0;

    for (const Ast *a = call->left; a; a = a->next)
        n++;
    if (n > 0)
        args = xrealloc(NULL, (size_t)n * sizeof *args);

    for (const Ast *a = call->left; a; a = a->next, i++) {
        args[i] = gen_expr(g, a);
        /* si un argumento posterior tiene una llamada, esta global se copia ya */
        if (a->next) {
            int later_call = 0;

            for (const Ast *b = a->next; b; b = b->next)
                later_call = later_call || has_call(b);
            if (is_global(args[i]) && later_call) {
                Operand copy = ir_new_temp(g->func);

                emit(g, IR_MOVE, args[i], ir_none(), copy);
                args[i] = copy;
            }
        }
    }
    for (i = 0; i < n; i++)
        emit(g, IR_PARAM, args[i], ir_none(), ir_none());
    free(args);

    if (callee->returns_value)
        result = ir_new_temp(g->func);
    emit(g, IR_CALL, ir_func(callee->name), ir_const(n), result);
    return result;
}

/* a && b y a || b: el resultado r vale a; si a no decide el resultado se evalua
 * b y r pasa a valer b. (&&: b se evalua solo si a es verdadero; ||: solo si a
 * es falso.) */
static Operand gen_short_circuit(Gen *g, const Ast *e)
{
    Operand a = gen_expr(g, e->left);
    Operand r = ir_new_temp(g->func);
    Operand end, b;

    emit(g, IR_MOVE, a, ir_none(), r);
    end = ir_new_label(g->prog);
    emit(g, e->op == OP_AND ? IR_JZ : IR_JNZ, r, end, ir_none());
    b = gen_expr(g, e->right);
    emit(g, IR_MOVE, b, ir_none(), r);
    emit(g, IR_LABEL, end, ir_none(), ir_none());
    return r;
}

/* Instruccion de tres direcciones que corresponde a un operador binario. */
static IrOp binary_op(OpKind op)
{
    switch (op) {
    case OP_ADD: return IR_ADD;
    case OP_SUB: return IR_SUB;
    case OP_MUL: return IR_MUL;
    case OP_DIV: return IR_DIV;
    case OP_MOD: return IR_MOD;
    case OP_LT:  return IR_LT;
    case OP_GT:  return IR_GT;
    case OP_EQ:  return IR_EQ;
    default:
        fprintf(stderr, "c-tds: internal error: unexpected binary operator\n");
        exit(2);
    }
}

static Operand gen_binary(Gen *g, const Ast *e)
{
    Operand l, r, t;

    if (e->op == OP_AND || e->op == OP_OR)
        return gen_short_circuit(g, e);

    l = gen_expr(g, e->left);
    l = protect(g, l, e->right);
    r = gen_expr(g, e->right);
    t = ir_new_temp(g->func);
    emit(g, binary_op(e->op), l, r, t);
    return t;
}

static Operand gen_expr(Gen *g, const Ast *e)
{
    Operand v, t;

    switch (e->kind) {
    case AST_INT_LIT:
        return ir_const(e->value.i);
    case AST_BOOL_LIT:
        return ir_const(e->value.b);
    case AST_ID:
        return ir_var(map_find(g, e->sym));
    case AST_CALL:
        return gen_call(g, e);
    case AST_BINARY:
        return gen_binary(g, e);
    case AST_UNARY:
        v = gen_expr(g, e->left);
        t = ir_new_temp(g->func);
        emit(g, e->op == OP_NOT ? IR_NOT : IR_NEG, v, ir_none(), t);
        return t;
    default:
        fprintf(stderr, "c-tds: internal error: unexpected expression node\n");
        exit(2);
    }
}

/* -------------------------------------------------------------- sentencias */

static void gen_block(Gen *g, const Ast *block);

static void gen_stmt(Gen *g, const Ast *s)
{
    Operand v, els, end;

    switch (s->kind) {
    case AST_ASSIGN:                        /* x = expr */
        v = gen_expr(g, s->right);
        emit(g, IR_MOVE, v, ir_none(), ir_var(map_find(g, s->left->sym)));
        break;
    case AST_CALL:                          /* llamada como sentencia */
        gen_call(g, s);
        break;
    case AST_IF:
        v = gen_expr(g, s->left);
        if (!s->right) {                    /* sin else: jz cond, Lfin */
            end = ir_new_label(g->prog);
            emit(g, IR_JZ, v, end, ir_none());
            gen_block(g, s->mid);
            emit(g, IR_LABEL, end, ir_none(), ir_none());
        } else {                            /* con else */
            els = ir_new_label(g->prog);
            end = ir_new_label(g->prog);
            emit(g, IR_JZ, v, els, ir_none());
            gen_block(g, s->mid);
            emit(g, IR_JMP, end, ir_none(), ir_none());
            emit(g, IR_LABEL, els, ir_none(), ir_none());
            gen_block(g, s->right);
            emit(g, IR_LABEL, end, ir_none(), ir_none());
        }
        break;
    case AST_WHILE:                         /* Linicio: cond; jz Lfin; cuerpo; jmp Linicio */
        els = ir_new_label(g->prog);        /* inicio */
        end = ir_new_label(g->prog);
        emit(g, IR_LABEL, els, ir_none(), ir_none());
        v = gen_expr(g, s->left);
        emit(g, IR_JZ, v, end, ir_none());
        gen_block(g, s->mid);
        emit(g, IR_JMP, els, ir_none(), ir_none());
        emit(g, IR_LABEL, end, ir_none(), ir_none());
        break;
    case AST_RETURN:
        if (s->left)
            emit(g, IR_RET, gen_expr(g, s->left), ir_none(), ir_none());
        else
            emit(g, IR_RET, ir_none(), ir_none(), ir_none());
        break;
    case AST_BLOCK:
        gen_block(g, s);
        break;
    default:                                /* AST_EMPTY */
        break;
    }
}

/* Declara las variables del bloque como locales del metodo (todas las locales
 * se declaran al principio del metodo, con nombres unicos) y genera sus
 * sentencias. */
static void gen_block(Gen *g, const Ast *block)
{
    for (const Ast *d = block->left; d; d = d->next)
        map_add(g, d->sym, ir_add_local(g->func, d->name));
    for (const Ast *s = block->mid; s; s = s->next)
        gen_stmt(g, s);
}

/* ---------------------------------------------------------------- metodos */

static void gen_method(Gen *g, const Ast *m)
{
    g->func = ir_add_func(g->prog, m->name, m->type != TYPE_VOID);
    for (const Ast *p = m->left; p; p = p->next)
        map_add(g, p->sym, ir_add_param(g->func, p->name));
    gen_block(g, m->right);
    if (m->type == TYPE_VOID)               /* retorno implicito */
        emit(g, IR_RET, ir_none(), ir_none(), ir_none());
    g->func = NULL;
}

/* ----------------------------------------------------------------- floats */

/* Informa cada aparicion de float (decision 12) y devuelve cuantas hay. Recorre
 * el AST completo en el orden del fuente. */
static int check_floats(const Ast *n, const char *file)
{
    int errors = 0;

    for (; n; n = n->next) {
        int is_float_decl = (n->kind == AST_VAR_DECL || n->kind == AST_PARAM ||
                             n->kind == AST_METHOD_DECL) &&
                            n->type == TYPE_FLOAT;

        if (is_float_decl || n->kind == AST_FLOAT_LIT) {
            fprintf(stderr, "%s:%d:%d: codegen error: type 'float' is not "
                    "supported by the code generator\n", file, n->line, n->col);
            errors++;
        }
        errors += check_floats(n->left, file);
        errors += check_floats(n->mid, file);
        errors += check_floats(n->right, file);
    }
    return errors;
}

IrProgram *irgen_generate(const Ast *program, const char *filename)
{
    Gen g = { NULL, NULL, NULL, 0, 0 };

    if (check_floats(program, filename) > 0)
        return NULL;

    g.prog = ir_program_new();
    for (const Ast *v = program->left; v; v = v->next)
        map_add(&g, v->sym, ir_add_global(g.prog, v->name));
    for (const Ast *m = program->mid; m; m = m->next)
        gen_method(&g, m);
    free(g.map);
    return g.prog;
}
