/* ir.c - Codigo intermedio de C-TDS: codigo de tres direcciones (ver ir.h). */
#include <stdlib.h>
#include <string.h>

#include "ir.h"

/* ---------------------------------------------------------------- memoria */

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
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

static char *xstrdup(const char *s)
{
    size_t len = strlen(s) + 1;

    return memcpy(xmalloc(len), s, len);
}

/* Asegura lugar para un elemento mas en un arreglo dinamico (empieza en 8 y
 * duplica). */
#define GROW(arr, n, cap)                                                    \
    do {                                                                     \
        if ((n) == (cap)) {                                                  \
            (cap) = (cap) ? (cap) * 2 : 8;                                   \
            (arr) = xrealloc((arr), (size_t)(cap) * sizeof *(arr));          \
        }                                                                    \
    } while (0)

/* -------------------------------------------------------------- operandos */

static Operand make_operand(OperandKind kind, long long value)
{
    Operand o;

    o.kind = kind;
    o.value = value;
    o.var = NULL;
    o.name = NULL;
    return o;
}

Operand ir_none(void)
{
    return make_operand(OPD_NONE, 0);
}

Operand ir_const(long long value)
{
    return make_operand(OPD_CONST, value);
}

Operand ir_var(const IrVar *var)
{
    Operand o = make_operand(OPD_VAR, 0);

    o.var = var;
    return o;
}

Operand ir_func(const char *name)
{
    Operand o = make_operand(OPD_FUNC, 0);

    o.name = name;
    return o;
}

/* --------------------------------------------------------------- programa */

IrProgram *ir_program_new(void)
{
    IrProgram *prog = xmalloc(sizeof *prog);

    prog->globals = NULL;
    prog->nglobals = prog->cap_globals = 0;
    prog->first = prog->last = NULL;
    prog->nlabels = 0;
    return prog;
}

static IrVar *var_new(const char *name, int is_global, int is_param, int index)
{
    IrVar *v = xmalloc(sizeof *v);

    v->name = xstrdup(name);
    v->is_global = is_global;
    v->is_param = is_param;
    v->index = index;
    return v;
}

IrVar *ir_add_global(IrProgram *prog, const char *name)
{
    IrVar *v = var_new(name, 1, 0, prog->nglobals);

    GROW(prog->globals, prog->nglobals, prog->cap_globals);
    prog->globals[prog->nglobals++] = v;
    return v;
}

IrFunc *ir_add_func(IrProgram *prog, const char *name, int returns_value)
{
    IrFunc *f = xmalloc(sizeof *f);

    f->name = xstrdup(name);
    f->returns_value = returns_value;
    f->params = f->locals = NULL;
    f->nparams = f->nlocals = f->cap_params = f->cap_locals = 0;
    f->code = NULL;
    f->ncode = f->cap_code = 0;
    f->ntemps = 0;
    f->next = NULL;
    if (prog->last)
        prog->last->next = f;
    else
        prog->first = f;
    prog->last = f;
    return f;
}

/* Indica si `name` tiene la forma de un temporal (t1, t2, ...) o de una etiqueta
 * (L1, L2, ...): una variable no puede llamarse asi porque en el texto del
 * codigo intermedio se confundiria con ellos. */
static int looks_like_temp_or_label(const char *name)
{
    if (name[0] != 't' && name[0] != 'L')
        return 0;
    if (name[1] == '\0')
        return 0;
    for (const char *c = name + 1; *c; c++)
        if (*c < '0' || *c > '9')
            return 0;
    return 1;
}

/* Indica si el nombre no se puede usar para una variable del metodo: porque ya
 * hay un parametro o una local con ese nombre, o porque se parece a un
 * temporal o a una etiqueta. */
static int name_taken(const IrFunc *f, const char *name)
{
    if (looks_like_temp_or_label(name))
        return 1;
    for (int i = 0; i < f->nparams; i++)
        if (strcmp(f->params[i]->name, name) == 0)
            return 1;
    for (int i = 0; i < f->nlocals; i++)
        if (strcmp(f->locals[i]->name, name) == 0)
            return 1;
    return 0;
}

/* Nombre unico dentro del metodo: `base`, o base.2, base.3, ... (tambien cuando
 * `base` se parece a un temporal o a una etiqueta, como t1 o L2: pasa a ser
 * t1.2 o L2.2). Hay que liberarlo con free. */
static char *unique_name(const IrFunc *f, const char *base)
{
    size_t len;
    char *name;

    if (!name_taken(f, base))
        return xstrdup(base);
    len = strlen(base) + 24;                /* "." y un entero largo */
    name = xmalloc(len);
    for (int k = 2;; k++) {
        snprintf(name, len, "%s.%d", base, k);
        if (!name_taken(f, name))
            return name;
    }
}

IrVar *ir_add_param(IrFunc *f, const char *base)
{
    char *name = unique_name(f, base);
    IrVar *v = var_new(name, 0, 1, f->nparams);

    free(name);
    GROW(f->params, f->nparams, f->cap_params);
    f->params[f->nparams++] = v;
    return v;
}

IrVar *ir_add_local(IrFunc *f, const char *base)
{
    char *name = unique_name(f, base);
    IrVar *v = var_new(name, 0, 0, f->nlocals);

    free(name);
    GROW(f->locals, f->nlocals, f->cap_locals);
    f->locals[f->nlocals++] = v;
    return v;
}

Operand ir_new_temp(IrFunc *f)
{
    return make_operand(OPD_TEMP, ++f->ntemps);
}

Operand ir_new_label(IrProgram *prog)
{
    return make_operand(OPD_LABEL, ++prog->nlabels);
}

void ir_emit(IrFunc *f, IrOp op, Operand a, Operand b, Operand result)
{
    Instr *in;

    GROW(f->code, f->ncode, f->cap_code);
    in = &f->code[f->ncode++];
    in->op = op;
    in->a = a;
    in->b = b;
    in->result = result;
}

/* ---------------------------------------------------------------- liberar */

static void vars_free(IrVar **vars, int n)
{
    for (int i = 0; i < n; i++) {
        free(vars[i]->name);
        free(vars[i]);
    }
    free(vars);
}

void ir_program_free(IrProgram *prog)
{
    IrFunc *f;

    if (!prog)
        return;
    f = prog->first;
    while (f) {
        IrFunc *next = f->next;

        vars_free(f->params, f->nparams);
        vars_free(f->locals, f->nlocals);
        free(f->code);
        free(f->name);
        free(f);
        f = next;
    }
    vars_free(prog->globals, prog->nglobals);
    free(prog);
}

/* --------------------------------------------------------------- impresion */

static void print_operand(FILE *out, Operand o)
{
    switch (o.kind) {
    case OPD_NONE:
        break;
    case OPD_CONST:
        fprintf(out, "%lld", o.value);
        break;
    case OPD_VAR:
        fprintf(out, "%s%s", o.var->is_global ? "@" : "", o.var->name);
        break;
    case OPD_TEMP:
        fprintf(out, "t%lld", o.value);
        break;
    case OPD_LABEL:
        fprintf(out, "L%lld", o.value);
        break;
    case OPD_FUNC:
        fputs(o.name, out);
        break;
    }
}

/* Texto del operador de una instruccion aritmetica, de comparacion o de
 * negacion; NULL para las demas. */
static const char *operator_text(IrOp op)
{
    switch (op) {
    case IR_ADD: return "+";
    case IR_SUB: return "-";
    case IR_MUL: return "*";
    case IR_DIV: return "/";
    case IR_MOD: return "%";
    case IR_NEG: return "-";
    case IR_LT:  return "<";
    case IR_GT:  return ">";
    case IR_EQ:  return "==";
    case IR_NOT: return "!";
    default:     return NULL;
    }
}

void ir_print_instr(FILE *out, const Instr *in)
{
    switch (in->op) {
    case IR_LABEL:
        print_operand(out, in->a);
        fputc(':', out);
        break;
    case IR_MOVE:
        print_operand(out, in->result);
        fputs(" = ", out);
        print_operand(out, in->a);
        break;
    case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_MOD:
    case IR_LT: case IR_GT: case IR_EQ:
        print_operand(out, in->result);
        fputs(" = ", out);
        print_operand(out, in->a);
        fprintf(out, " %s ", operator_text(in->op));
        print_operand(out, in->b);
        break;
    case IR_NEG: case IR_NOT:
        print_operand(out, in->result);
        fprintf(out, " = %s ", operator_text(in->op));
        print_operand(out, in->a);
        break;
    case IR_JMP:
        fputs("jmp ", out);
        print_operand(out, in->a);
        break;
    case IR_JZ: case IR_JNZ:
        fputs(in->op == IR_JZ ? "jz " : "jnz ", out);
        print_operand(out, in->a);
        fputs(", ", out);
        print_operand(out, in->b);
        break;
    case IR_PARAM:
        fputs("param ", out);
        print_operand(out, in->a);
        break;
    case IR_CALL:
        if (in->result.kind != OPD_NONE) {
            print_operand(out, in->result);
            fputs(" = ", out);
        }
        fputs("call ", out);
        print_operand(out, in->a);
        fputs(", ", out);
        print_operand(out, in->b);
        break;
    case IR_RET:
        fputs("ret", out);
        if (in->a.kind != OPD_NONE) {
            fputc(' ', out);
            print_operand(out, in->a);
        }
        break;
    }
}

/* Una linea del .ci: las etiquetas sin sangria y el resto con 2 espacios. */
static void print_instr_line(FILE *out, const Instr *in)
{
    if (in->op != IR_LABEL)
        fputs("  ", out);
    ir_print_instr(out, in);
    fputc('\n', out);
}

void ir_print(FILE *out, const IrProgram *prog)
{
    int first = 1;                          /* para separar con lineas en blanco */

    for (int i = 0; i < prog->nglobals; i++)
        fprintf(out, "global @%s\n", prog->globals[i]->name);
    if (prog->nglobals > 0)
        first = 0;

    for (const IrFunc *f = prog->first; f; f = f->next) {
        if (!first)
            fputc('\n', out);
        first = 0;
        fprintf(out, "func %s(", f->name);
        for (int i = 0; i < f->nparams; i++)
            fprintf(out, "%s%s", i ? ", " : "", f->params[i]->name);
        fputs(")\n", out);
        for (int i = 0; i < f->nlocals; i++)
            fprintf(out, "  local %s\n", f->locals[i]->name);
        for (int i = 0; i < f->ncode; i++)
            print_instr_line(out, &f->code[i]);
        fputs("end\n", out);
    }
}

int ir_slot(const IrFunc *f, Operand o)
{
    if (o.kind == OPD_TEMP)
        return f->nparams + f->nlocals + (int)o.value - 1;
    return o.var->is_param ? o.var->index : f->nparams + o.var->index;
}
