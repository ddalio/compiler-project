/*
 * interp.c - Interprete del codigo intermedio de C-TDS (ver interp.h).
 *
 * Estructuras:
 *   FuncInfo  datos de un metodo para ejecutarlo: su codigo y la posicion de
 *             cada etiqueta (se calcula una sola vez);
 *   Frame     el marco de una llamada: el metodo, la proxima instruccion (pc),
 *             los valores de sus parametros, locales y temporales (slots) y, si
 *             esta esperando que vuelva una llamada, la instruccion call;
 *   Interp    el estado: globales, pila de marcos y pila de argumentos (los
 *             param que se emitieron y todavia no los consumio un call).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interp.h"

typedef struct {
    const IrFunc *fn;
    int *label_pos;             /* etiqueta -> indice de instruccion (-1: no esta) */
} FuncInfo;

typedef struct {
    const FuncInfo *info;
    int pc;                     /* proxima instruccion a ejecutar */
    int32_t *slots;             /* parametros, locales y temporales */
    const Instr *call;          /* llamada pendiente (si espera un callee) */
} Frame;

typedef struct {
    const IrProgram *prog;
    FuncInfo *funcs;
    int nfuncs;
    int32_t *globals;
    Frame *stack;               /* marcos; el ultimo es el que se ejecuta */
    int depth, cap_stack;
    int32_t *args;              /* argumentos cargados por param */
    int nargs, cap_args;
} Interp;

/* ---------------------------------------------------------------- memoria */

static void *xcalloc(size_t n, size_t size)
{
    void *p = calloc(n ? n : 1, size);       /* nunca NULL para n = 0 */

    if (!p) {
        fprintf(stderr, "c-tds: out of memory\n");
        exit(2);
    }
    return p;
}

static void *xrealloc(void *p, size_t n)
{
    p = realloc(p, n);
    if (!p) {
        fprintf(stderr, "c-tds: out of memory\n");
        exit(2);
    }
    return p;
}

/* ------------------------------------------------------------ preparacion */

/* Arma la informacion de cada metodo: la posicion de cada etiqueta. */
static void build_funcs(Interp *in)
{
    int i = 0;

    for (const IrFunc *f = in->prog->first; f; f = f->next)
        in->nfuncs++;
    in->funcs = xcalloc((size_t)in->nfuncs, sizeof *in->funcs);
    for (const IrFunc *f = in->prog->first; f; f = f->next, i++) {
        int n = in->prog->nlabels + 1;
        int *pos = xcalloc((size_t)n, sizeof *pos);

        for (int k = 0; k < n; k++)
            pos[k] = -1;
        for (int k = 0; k < f->ncode; k++)
            if (f->code[k].op == IR_LABEL)
                pos[f->code[k].a.value] = k;
        in->funcs[i].fn = f;
        in->funcs[i].label_pos = pos;
    }
}

static const FuncInfo *find_func(const Interp *in, const char *name)
{
    for (int i = 0; i < in->nfuncs; i++)
        if (strcmp(in->funcs[i].fn->name, name) == 0)
            return &in->funcs[i];
    return NULL;
}

/* -------------------------------------------------- operandos y variables */

static int32_t read_operand(const Interp *in, const Frame *fr, Operand o)
{
    switch (o.kind) {
    case OPD_CONST:
        return (int32_t)o.value;
    case OPD_VAR:
        if (o.var->is_global)
            return in->globals[o.var->index];
        return fr->slots[ir_slot(fr->info->fn, o)];
    case OPD_TEMP:
        return fr->slots[ir_slot(fr->info->fn, o)];
    default:
        return 0;
    }
}

static void write_operand(Interp *in, Frame *fr, Operand o, int32_t value)
{
    if (o.kind == OPD_VAR && o.var->is_global)
        in->globals[o.var->index] = value;
    else
        fr->slots[ir_slot(fr->info->fn, o)] = value;
}

/* Aritmetica de 32 bits con signo que da la vuelta, sin comportamiento
 * indefinido de C en el desborde. */
static int32_t wrap_add(int32_t a, int32_t b) { return (int32_t)((uint32_t)a + (uint32_t)b); }
static int32_t wrap_sub(int32_t a, int32_t b) { return (int32_t)((uint32_t)a - (uint32_t)b); }
static int32_t wrap_mul(int32_t a, int32_t b) { return (int32_t)((uint32_t)a * (uint32_t)b); }
static int32_t wrap_neg(int32_t a)            { return (int32_t)(0u - (uint32_t)a); }

/* --------------------------------------------------------------- marcos */

/* Apila el marco de una llamada a `info` con los `n` ultimos argumentos. */
static void push_frame(Interp *in, const FuncInfo *info, int n)
{
    const IrFunc *f = info->fn;
    int nslots = f->nparams + f->nlocals + f->ntemps;
    Frame *fr;

    if (in->depth == in->cap_stack) {
        in->cap_stack = in->cap_stack ? in->cap_stack * 2 : 64;
        in->stack = xrealloc(in->stack, (size_t)in->cap_stack * sizeof *in->stack);
    }
    fr = &in->stack[in->depth++];
    fr->info = info;
    fr->pc = 0;
    fr->call = NULL;
    fr->slots = xcalloc((size_t)nslots, sizeof *fr->slots);   /* todo en 0 */
    for (int i = 0; i < n && i < f->nparams; i++)
        fr->slots[i] = in->args[in->nargs - n + i];
    in->nargs -= n;
}

static void pop_frame(Interp *in)
{
    free(in->stack[--in->depth].slots);
}

/* ------------------------------------------------------------- ejecucion */

static int runtime_error(const char *msg)
{
    fprintf(stderr, "runtime error: %s\n", msg);
    return 1;
}

/* Ejecuta el programa. Devuelve 0 o 1 (error de ejecucion). */
static int execute(Interp *in, const FuncInfo *main_info, int *has_value,
                   int *value)
{
    *has_value = 0;
    *value = 0;
    push_frame(in, main_info, 0);

    while (in->depth > 0) {
        Frame *fr = &in->stack[in->depth - 1];
        const IrFunc *fn = fr->info->fn;
        const Instr *ins;
        int32_t a, b;

        if (fr->pc >= fn->ncode) {              /* llego al final sin ret */
            pop_frame(in);
            continue;
        }
        ins = &fn->code[fr->pc++];

        switch (ins->op) {
        case IR_MOVE:
            write_operand(in, fr, ins->result, read_operand(in, fr, ins->a));
            break;
        case IR_ADD: case IR_SUB: case IR_MUL:
        case IR_LT: case IR_GT: case IR_EQ:
            a = read_operand(in, fr, ins->a);
            b = read_operand(in, fr, ins->b);
            switch (ins->op) {
            case IR_ADD: a = wrap_add(a, b); break;
            case IR_SUB: a = wrap_sub(a, b); break;
            case IR_MUL: a = wrap_mul(a, b); break;
            case IR_LT:  a = a < b;          break;
            case IR_GT:  a = a > b;          break;
            default:     a = a == b;         break;      /* IR_EQ */
            }
            write_operand(in, fr, ins->result, a);
            break;
        case IR_DIV: case IR_MOD:
            a = read_operand(in, fr, ins->a);
            b = read_operand(in, fr, ins->b);
            if (b == 0)
                return runtime_error("division by zero");
            /* INT_MIN / -1 desborda en C: por la vuelta vale INT_MIN, y el
             * resto es 0. */
            if (b == -1)
                a = ins->op == IR_DIV ? wrap_neg(a) : 0;
            else
                a = ins->op == IR_DIV ? a / b : a % b;
            write_operand(in, fr, ins->result, a);
            break;
        case IR_NEG:
            write_operand(in, fr, ins->result,
                          wrap_neg(read_operand(in, fr, ins->a)));
            break;
        case IR_NOT:
            write_operand(in, fr, ins->result,
                          read_operand(in, fr, ins->a) == 0);
            break;
        case IR_LABEL:
            break;
        case IR_JMP:
            fr->pc = fr->info->label_pos[ins->a.value];
            break;
        case IR_JZ:
            if (read_operand(in, fr, ins->a) == 0)
                fr->pc = fr->info->label_pos[ins->b.value];
            break;
        case IR_JNZ:
            if (read_operand(in, fr, ins->a) != 0)
                fr->pc = fr->info->label_pos[ins->b.value];
            break;
        case IR_PARAM:
            if (in->nargs == in->cap_args) {
                in->cap_args = in->cap_args ? in->cap_args * 2 : 16;
                in->args = xrealloc(in->args, (size_t)in->cap_args * sizeof *in->args);
            }
            in->args[in->nargs++] = read_operand(in, fr, ins->a);
            break;
        case IR_CALL: {
            const FuncInfo *callee = find_func(in, ins->a.name);

            if (!callee) {
                fprintf(stderr, "c-tds: internal error: no method '%s'\n", ins->a.name);
                exit(2);
            }
            if (in->depth >= INTERP_MAX_DEPTH)
                return runtime_error("call stack overflow");
            fr->call = ins;                     /* para guardar el resultado */
            push_frame(in, callee, (int)ins->b.value);
            break;
        }
        case IR_RET: {
            int has = ins->a.kind != OPD_NONE;
            int32_t v = has ? read_operand(in, fr, ins->a) : 0;

            pop_frame(in);
            if (in->depth == 0) {               /* termino main */
                *has_value = has;
                *value = v;
            } else {
                Frame *caller = &in->stack[in->depth - 1];

                if (has && caller->call && caller->call->result.kind != OPD_NONE)
                    write_operand(in, caller, caller->call->result, v);
                caller->call = NULL;
            }
            break;
        }
        }
    }
    return 0;
}

int interp_run(const IrProgram *prog, int *has_value, int *value)
{
    Interp in;
    const FuncInfo *main_info;
    int status;

    memset(&in, 0, sizeof in);
    in.prog = prog;
    build_funcs(&in);
    in.globals = xcalloc((size_t)prog->nglobals, sizeof *in.globals);   /* en 0 */

    main_info = find_func(&in, "main");
    if (!main_info) {
        fprintf(stderr, "c-tds: internal error: program has no 'main' method\n");
        exit(2);
    }
    status = execute(&in, main_info, has_value, value);

    while (in.depth > 0)                        /* tras un error quedan marcos */
        pop_frame(&in);
    for (int i = 0; i < in.nfuncs; i++)
        free(in.funcs[i].label_pos);
    free(in.funcs);
    free(in.globals);
    free(in.stack);
    free(in.args);
    return status;
}
