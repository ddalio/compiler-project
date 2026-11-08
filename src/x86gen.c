/*
 * x86gen.c - Generador de assembly x86-64 de C-TDS (ver x86gen.h).
 *
 * Esquema (decisiones de docs/etapa-5-codigo-objeto.md):
 *   - enteros de 32 bits: instrucciones "l" y registros %r10d, %r11d, %eax;
 *   - marco de pila: una posicion de 8 bytes por parametro, local y temporal,
 *     la posicion k en -8*(k+1)(%rbp); el tamano se redondea a multiplo de 16;
 *   - cada operacion carga sus operandos en %r10 / %r11, opera y guarda el
 *     resultado en la pila;
 *   - convencion de llamada de Linux: 6 argumentos en registros, el resto en la
 *     pila (de derecha a izquierda), valor devuelto en %eax;
 *   - los metodos y las globales llevan el prefijo ctds_ (salvo main).
 */
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "x86gen.h"

#define ARG_REGS 6              /* argumentos que viajan en registros */

/* Registros de 32 bits para los argumentos, en orden. */
static const char *const ARG_REG32[ARG_REGS] = {
    "%edi", "%esi", "%edx", "%ecx", "%r8d", "%r9d"
};

/* Columna donde empiezan los comentarios al final de una linea (con al menos
 * un espacio de separacion). */
#define COMMENT_COLUMN 32

typedef struct {
    FILE *out;
    const IrFunc *fn;           /* metodo que se esta generando */
    int div_labels;             /* etiquetas .Ldiv<n> usadas hasta ahora */
    Operand *pending;           /* argumentos de los param aun sin llamada */
    int npending, cap_pending;
} Gen;

/* ---------------------------------------------------------- escritura */

/* Escribe una instruccion: "        mnemonico operandos", y opcionalmente un
 * comentario alineado en la columna COMMENT_COLUMN. */
static void put(Gen *g, const char *comment, const char *mnem, const char *fmt, ...)
{
    char ops[200], line[300];
    va_list ap;
    int len;

    va_start(ap, fmt);
    vsnprintf(ops, sizeof ops, fmt, ap);
    va_end(ap);
    if (ops[0])
        len = snprintf(line, sizeof line, "        %-8s%s", mnem, ops);
    else
        len = snprintf(line, sizeof line, "        %s", mnem);
    fputs(line, g->out);
    if (comment) {
        int pad = COMMENT_COLUMN - len;

        fprintf(g->out, "%*s# %s", pad < 1 ? 1 : pad, "", comment);
    }
    fputc('\n', g->out);
}

/* Escribe una etiqueta ("nombre:") con un comentario opcional. */
static void put_label(Gen *g, const char *name, const char *comment)
{
    int len = fprintf(g->out, "%s:", name);

    if (comment) {
        int pad = COMMENT_COLUMN - len;

        fprintf(g->out, "%*s# %s", pad < 1 ? 1 : pad, "", comment);
    }
    fputc('\n', g->out);
}

/* Simbolo de un metodo en assembly: main se llama main; el resto lleva el
 * prefijo ctds_ (decision 11). */
static void symbol_text(const char *name, char *buf, size_t n)
{
    if (strcmp(name, "main") == 0)
        snprintf(buf, n, "main");
    else
        snprintf(buf, n, "ctds_%s", name);
}

/* ------------------------------------------------------------- operandos */

/* Desplazamiento respecto de %rbp de la posicion k del marco. */
static int slot_offset(int k)
{
    return -8 * (k + 1);
}

/* Texto de un operando como origen o destino: una constante ($5), una posicion
 * del marco (-8(%rbp)) o una global (ctds_g(%rip)). */
static void operand_text(const Gen *g, Operand o, char *buf, size_t n)
{
    switch (o.kind) {
    case OPD_CONST:
        snprintf(buf, n, "$%lld", o.value);
        break;
    case OPD_VAR:
        if (o.var->is_global) {
            snprintf(buf, n, "ctds_%s(%%rip)", o.var->name);
            break;
        }
        /* fallthrough */
    case OPD_TEMP:
        snprintf(buf, n, "%d(%%rbp)", slot_offset(ir_slot(g->fn, o)));
        break;
    default:
        snprintf(buf, n, "?");
        break;
    }
}

/* movl <operando>, %reg */
static void load(Gen *g, Operand o, const char *reg)
{
    char t[96];

    operand_text(g, o, t, sizeof t);
    put(g, NULL, "movl", "%s, %s", t, reg);
}

/* movl %reg, <operando> */
static void store(Gen *g, const char *reg, Operand o)
{
    char t[96];

    operand_text(g, o, t, sizeof t);
    put(g, NULL, "movl", "%s, %s", reg, t);
}

/* ------------------------------------------------------- instrucciones */

/* Operaciones de dos operandos que dejan el resultado en %r10d: la primera
 * carga `a`, y la instruccion `mnem` combina con `b`. */
static void gen_arith(Gen *g, const Instr *in, const char *mnem)
{
    char b[96];

    load(g, in->a, "%r10d");
    operand_text(g, in->b, b, sizeof b);
    put(g, NULL, mnem, "%s, %%r10d", b);
    store(g, "%r10d", in->result);
}

/* Comparaciones: cmpl y una instruccion set* que deja 1 o 0. */
static void gen_compare(Gen *g, const Instr *in, const char *set)
{
    char b[96];

    load(g, in->a, "%r10d");
    operand_text(g, in->b, b, sizeof b);
    put(g, NULL, "cmpl", "%s, %%r10d", b);
    put(g, NULL, set, "%%r11b");
    put(g, NULL, "movzbl", "%%r11b, %%r11d");
    store(g, "%r11d", in->result);
}

/* Division y resto. idivl divide %edx:%eax; si el divisor es -1 no se ejecuta
 * (INT_MIN / -1 provoca una excepcion de hardware): el cociente es -a y el
 * resto 0, que es lo que da la aritmetica de 32 bits que da la vuelta. */
static void gen_divide(Gen *g, const Instr *in)
{
    int n = ++g->div_labels;
    int is_div = in->op == IR_DIV;

    load(g, in->a, "%eax");
    put(g, NULL, "cltd", "");
    load(g, in->b, "%r10d");
    put(g, NULL, "cmpl", "$-1, %%r10d");
    put(g, NULL, "je", ".Ldiv%d", n);
    put(g, NULL, "idivl", "%%r10d");
    put(g, NULL, "jmp", ".Ldiv%de", n);
    fprintf(g->out, ".Ldiv%d:\n", n);
    if (is_div)
        put(g, NULL, "negl", "%%eax");
    else
        put(g, NULL, "xorl", "%%edx, %%edx");
    fprintf(g->out, ".Ldiv%de:\n", n);
    store(g, is_div ? "%eax" : "%edx", in->result);
}

/* Salto condicional sobre un operando: se carga en %r10d (puede ser una
 * constante) y se prueba contra cero. */
static void gen_cond_jump(Gen *g, const Instr *in)
{
    load(g, in->a, "%r10d");
    put(g, NULL, "testl", "%%r10d, %%r10d");
    put(g, NULL, in->op == IR_JZ ? "je" : "jne", ".L%lld", in->b.value);
}

/* param: solo se anota el argumento; se carga en el call correspondiente. */
static void gen_param(Gen *g, const Instr *in)
{
    if (g->npending == g->cap_pending) {
        g->cap_pending = g->cap_pending ? g->cap_pending * 2 : 8;
        g->pending = realloc(g->pending, (size_t)g->cap_pending * sizeof *g->pending);
        if (!g->pending) {
            fprintf(stderr, "c-tds: out of memory\n");
            exit(2);
        }
    }
    g->pending[g->npending++] = in->a;
}

/* call: los primeros 6 argumentos van a los registros y el resto se apila de
 * derecha a izquierda; si hay una cantidad impar en la pila se agrega un relleno
 * de 8 bytes para mantener la pila alineada a 16 antes del call. */
static void gen_call(Gen *g, const Instr *in)
{
    int n = (int)in->b.value;
    const Operand *args = g->pending + (g->npending - n);
    int on_stack = n > ARG_REGS ? n - ARG_REGS : 0;
    int pad = on_stack % 2;
    char sym[160];

    if (pad)
        put(g, "alineacion de la pila", "subq", "$8, %%rsp");
    for (int i = n - 1; i >= ARG_REGS; i--) {
        load(g, args[i], "%r10d");
        put(g, NULL, "pushq", "%%r10");
    }
    for (int i = 0; i < n && i < ARG_REGS; i++)
        load(g, args[i], ARG_REG32[i]);

    symbol_text(in->a.name, sym, sizeof sym);
    put(g, NULL, "call", "%s", sym);
    if (on_stack + pad > 0)
        put(g, NULL, "addq", "$%d, %%rsp", 8 * (on_stack + pad));
    if (in->result.kind != OPD_NONE)
        store(g, "%eax", in->result);
    g->npending -= n;
}

/* ret: el valor devuelto va en %eax. main sin valor devuelve 0, asi el programa
 * termina con codigo de salida 0 (decision 16). */
static void gen_return(Gen *g, const Instr *in)
{
    if (in->a.kind != OPD_NONE)
        load(g, in->a, "%eax");
    else if (strcmp(g->fn->name, "main") == 0)
        put(g, NULL, "movl", "$0, %%eax");
    put(g, NULL, "leave", "");
    put(g, NULL, "ret", "");
}

static void gen_instr(Gen *g, const Instr *in)
{
    if (in->op == IR_LABEL) {
        fprintf(g->out, ".L%lld:\n", in->a.value);
        return;
    }
    fputs("        # ", g->out);
    ir_print_instr(g->out, in);
    fputc('\n', g->out);

    switch (in->op) {
    case IR_MOVE:
        load(g, in->a, "%r10d");
        store(g, "%r10d", in->result);
        break;
    case IR_ADD: gen_arith(g, in, "addl");  break;
    case IR_SUB: gen_arith(g, in, "subl");  break;
    case IR_MUL: gen_arith(g, in, "imull"); break;
    case IR_DIV:
    case IR_MOD: gen_divide(g, in);         break;
    case IR_NEG:
        load(g, in->a, "%r10d");
        put(g, NULL, "negl", "%%r10d");
        store(g, "%r10d", in->result);
        break;
    case IR_NOT:
        load(g, in->a, "%r10d");
        put(g, NULL, "cmpl", "$0, %%r10d");
        put(g, NULL, "sete", "%%r11b");
        put(g, NULL, "movzbl", "%%r11b, %%r11d");
        store(g, "%r11d", in->result);
        break;
    case IR_LT: gen_compare(g, in, "setl"); break;
    case IR_GT: gen_compare(g, in, "setg"); break;
    case IR_EQ: gen_compare(g, in, "sete"); break;
    case IR_JMP:
        put(g, NULL, "jmp", ".L%lld", in->a.value);
        break;
    case IR_JZ:
    case IR_JNZ:   gen_cond_jump(g, in);    break;
    case IR_PARAM: gen_param(g, in);        break;
    case IR_CALL:  gen_call(g, in);         break;
    case IR_RET:   gen_return(g, in);       break;
    case IR_LABEL: break;                   /* ya tratada arriba */
    }
}

/* ---------------------------------------------------------------- metodos */

/* Tamano del marco en bytes: 8 por posicion, redondeado a multiplo de 16. */
static int frame_size(const IrFunc *f)
{
    int bytes = 8 * (f->nparams + f->nlocals + f->ntemps);

    return (bytes + 15) / 16 * 16;
}

/* Prologo: marco de pila, parametros a sus posiciones y locales en 0. */
static void gen_prologue(Gen *g, const IrFunc *f)
{
    int nslots = f->nparams + f->nlocals + f->ntemps;
    int size = frame_size(f);
    char c[160];

    put(g, NULL, "pushq", "%%rbp");
    put(g, NULL, "movq", "%%rsp, %%rbp");
    if (size > 0) {
        snprintf(c, sizeof c, "%d %s = %d bytes -> %d", nslots,
                 nslots == 1 ? "posicion" : "posiciones", 8 * nslots, size);
        put(g, c, "subq", "$%d, %%rsp", size);
    }
    for (int i = 0; i < f->nparams; i++) {
        const char *name = f->params[i]->name;

        if (i < ARG_REGS) {
            put(g, name, "movl", "%s, %d(%%rbp)", ARG_REG32[i], slot_offset(i));
        } else {                                /* el 7.o argumento: 16(%rbp) */
            snprintf(c, sizeof c, "%s (de la pila)", name);
            put(g, NULL, "movl", "%d(%%rbp), %%r10d", 16 + 8 * (i - ARG_REGS));
            put(g, c, "movl", "%%r10d, %d(%%rbp)", slot_offset(i));
        }
    }
    for (int i = 0; i < f->nlocals; i++) {
        snprintf(c, sizeof c, "%s = 0", f->locals[i]->name);
        put(g, c, "movl", "$0, %d(%%rbp)", slot_offset(f->nparams + i));
    }
}

static void gen_method(Gen *g, const IrFunc *f)
{
    char sym[160], header[200];
    int len = 0;

    g->fn = f;
    g->npending = 0;
    symbol_text(f->name, sym, sizeof sym);
    len += snprintf(header + len, sizeof header - (size_t)len, "func %s(", f->name);
    for (int i = 0; i < f->nparams; i++)
        len += snprintf(header + len, sizeof header - (size_t)len, "%s%s",
                        i ? ", " : "", f->params[i]->name);
    snprintf(header + len, sizeof header - (size_t)len, ")");

    fputc('\n', g->out);
    if (strcmp(f->name, "main") == 0)
        put(g, NULL, ".globl", "main");
    put_label(g, sym, header);
    gen_prologue(g, f);
    for (int i = 0; i < f->ncode; i++)
        gen_instr(g, &f->code[i]);

    /* Si el codigo no termina en un ret (por ejemplo, termina en una etiqueta),
     * se agrega un retorno para no caer en el metodo siguiente. */
    if (f->ncode == 0 || f->code[f->ncode - 1].op != IR_RET) {
        Instr end = { IR_RET, ir_none(), ir_none(), ir_none() };

        fputs("        # (fin del metodo)\n", g->out);
        gen_return(g, &end);
    }
}

void x86_generate(FILE *out, const IrProgram *prog, const char *source_name)
{
    Gen g = { out, NULL, 0, NULL, 0, 0 };

    fprintf(out, "# c-tds: %s\n", source_name);
    if (prog->nglobals > 0) {
        fprintf(out, "        .data\n");
        for (int i = 0; i < prog->nglobals; i++)
            fprintf(out, "ctds_%s: .long   0\n", prog->globals[i]->name);
        fputc('\n', out);
    }
    fprintf(out, "        .text");
    for (const IrFunc *f = prog->first; f; f = f->next)
        gen_method(&g, f);
    fputc('\n', out);
    fprintf(out, "        .section .note.GNU-stack,\"\",@progbits\n");
    free(g.pending);
}
