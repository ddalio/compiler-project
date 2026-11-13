/*
 * test_x86gen.c - Tests unitarios del generador de assembly (src/x86gen.c).
 *
 * Los programas se arman directamente con la API del codigo intermedio (ir.h) y
 * se compara el assembly generado con el escrito a mano: la traduccion de cada
 * operacion, el marco de pila, el pasaje de argumentos, los nombres de simbolos
 * y la estructura del archivo. Se compila y ejecuta con `make test`.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ir.h"
#include "x86gen.h"

static int checks, failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            printf("  FAIL (linea %d): %s\n", __LINE__, #cond);              \
        }                                                                    \
    } while (0)

#define NONE ir_none()

/* Una instruccion de assembly: 8 espacios de sangria. */
#define I(text) "        " text "\n"

/* Assembly de un programa, en una cadena (hay que liberarla). */
static char *asm_of(const IrProgram *p)
{
    char *buf = NULL;
    size_t len = 0;
    FILE *mem = open_memstream(&buf, &len);

    x86_generate(mem, p, "t.ctds");
    fclose(mem);
    return buf;
}

/* Linea con comentario al final: el texto se completa con espacios hasta la
 * columna 32 (al menos uno) y despues va "# comentario". */
static char *with_comment(const char *text, const char *comment)
{
    size_t len = strlen(text), size = len + 40 + strlen(comment);
    char *out = malloc(size);
    int pad = 32 - (int)len;

    snprintf(out, size, "%s%*s# %s\n", text, pad < 1 ? 1 : pad, "", comment);
    return out;
}

/* Indica si el assembly contiene exactamente `snippet`; si no, muestra ambos. */
static int has(const char *assembly, const char *snippet)
{
    int found = strstr(assembly, snippet) != NULL;

    if (!found)
        printf("  --- no se encontro:\n%s  --- en:\n%s", snippet, assembly);
    return found;
}

/* Cuenta las apariciones de `needle`. */
static int count(const char *s, const char *needle)
{
    int n = 0;

    for (; (s = strstr(s, needle)) != NULL; s += strlen(needle))
        n++;
    return n;
}

/* Programa con un metodo `f` (no main) y un unico `ret` al final; `fill` agrega
 * las instrucciones a probar. Devuelve el assembly. */
typedef void (*fill_fn)(IrProgram *, IrFunc *);

static char *gen_f(int nparams, int nlocals, fill_fn fill)
{
    IrProgram *p = ir_program_new();
    IrFunc *f = ir_add_func(p, "f", 0);
    char *out;

    ir_add_global(p, "g");
    for (int i = 0; i < nparams; i++) {
        char name[16];

        snprintf(name, sizeof name, "p%d", i);
        ir_add_param(f, name);
    }
    for (int i = 0; i < nlocals; i++) {
        char name[16];

        snprintf(name, sizeof name, "x%d", i);
        ir_add_local(f, name);
    }
    fill(p, f);
    ir_emit(f, IR_RET, NONE, NONE, NONE);
    out = asm_of(p);
    ir_program_free(p);
    return out;
}

/* ---------------------------------------------------- estructura del archivo */

static void test_minimal_program(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *m = ir_add_func(p, "main", 0);
    char *got, *label;
    char expected[600];

    ir_emit(m, IR_RET, NONE, NONE, NONE);
    got = asm_of(p);
    label = with_comment("main:", "func main()");
    snprintf(expected, sizeof expected,
             "# c-tds: t.ctds\n"
             "        .text\n"
             I(".globl  main")
             "%s"
             I("pushq   %%rbp")
             I("movq    %%rsp, %%rbp")
             I("# ret")
             I("movl    $0, %%eax")
             I("leave")
             I("ret")
             "\n"
             "        .section .note.GNU-stack,\"\",@progbits\n", label);
    CHECK(strcmp(got, expected) == 0);
    if (strcmp(got, expected) != 0)
        printf("  --- obtenido:\n%s  --- esperado:\n%s", got, expected);
    free(label);
    free(got);
    ir_program_free(p);
}

static void test_globals_and_symbols(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *fact, *m;
    char *a;

    ir_add_global(p, "g");
    ir_add_global(p, "counter");
    fact = ir_add_func(p, "fact", 1);
    ir_emit(fact, IR_RET, ir_const(1), NONE, NONE);
    m = ir_add_func(p, "main", 0);
    ir_emit(m, IR_RET, NONE, NONE, NONE);
    a = asm_of(p);
    CHECK(has(a, "        .data\nctds_g: .long   0\nctds_counter: .long   0\n\n        .text\n"));
    CHECK(has(a, "ctds_fact:"));                    /* los metodos llevan ctds_ */
    CHECK(has(a, "\nmain:"));                       /* main no */
    CHECK(count(a, ".globl") == 1);                 /* solo main es global */
    CHECK(has(a, "        .globl  main\nmain:"));
    CHECK(has(a, "\n\n        .globl  main\n"));    /* linea en blanco entre metodos */
    CHECK(has(a, "        .section .note.GNU-stack,\"\",@progbits\n"));
    free(a);
    ir_program_free(p);

    p = ir_program_new();                           /* sin globales: sin .data */
    ir_add_func(p, "main", 0);
    a = asm_of(p);
    CHECK(strstr(a, ".data") == NULL);
    free(a);
    ir_program_free(p);
}

/* ------------------------------------------------------------ marco de pila */

static void fill_none(IrProgram *p, IrFunc *f)
{
    (void)p;
    (void)f;
}

static void test_frame_size(void)
{
    /* nslots = params + locales + temporales, 8 bytes cada uno, redondeado a 16 */
    static const struct { int np, nl, nt, bytes; const char *comment; } cases[] = {
        { 0, 0, 0, 0,  NULL },
        { 1, 0, 0, 16, "1 posicion = 8 bytes -> 16" },
        { 0, 2, 0, 16, "2 posiciones = 16 bytes -> 16" },
        { 2, 1, 0, 32, "3 posiciones = 24 bytes -> 32" },
        { 2, 3, 3, 64, "8 posiciones = 64 bytes -> 64" },
        { 3, 3, 3, 80, "9 posiciones = 72 bytes -> 80" },
    };

    for (size_t k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        IrProgram *p = ir_program_new();
        IrFunc *f = ir_add_func(p, "f", 0);
        char *a, *line, text[64];

        for (int i = 0; i < cases[k].np; i++)
            ir_add_param(f, "p");
        for (int i = 0; i < cases[k].nl; i++)
            ir_add_local(f, "l");
        for (int i = 0; i < cases[k].nt; i++)
            ir_new_temp(f);
        ir_emit(f, IR_RET, NONE, NONE, NONE);
        a = asm_of(p);
        if (cases[k].bytes == 0) {
            CHECK(strstr(a, "subq") == NULL);       /* sin marco: no se reserva */
        } else {
            snprintf(text, sizeof text, "        subq    $%d, %%rsp", cases[k].bytes);
            line = with_comment(text, cases[k].comment);
            CHECK(has(a, line));
            free(line);
        }
        CHECK(has(a, I("pushq   %rbp") I("movq    %rsp, %rbp")));
        free(a);
        ir_program_free(p);
    }
}

static void test_prologue_params_and_locals(void)
{
    char *a = gen_f(8, 2, fill_none), *l;

    /* los primeros 6 parametros llegan en registros */
    l = with_comment("        movl    %edi, -8(%rbp)", "p0");   CHECK(has(a, l)); free(l);
    l = with_comment("        movl    %esi, -16(%rbp)", "p1");  CHECK(has(a, l)); free(l);
    l = with_comment("        movl    %edx, -24(%rbp)", "p2");  CHECK(has(a, l)); free(l);
    l = with_comment("        movl    %ecx, -32(%rbp)", "p3");  CHECK(has(a, l)); free(l);
    l = with_comment("        movl    %r8d, -40(%rbp)", "p4");  CHECK(has(a, l)); free(l);
    l = with_comment("        movl    %r9d, -48(%rbp)", "p5");  CHECK(has(a, l)); free(l);
    /* el 7.o y el 8.o vienen por la pila: 16(%rbp) y 24(%rbp) */
    l = with_comment("        movl    %r10d, -56(%rbp)", "p6 (de la pila)");
    CHECK(has(a, I("movl    16(%rbp), %r10d") "") && has(a, l));
    free(l);
    l = with_comment("        movl    %r10d, -64(%rbp)", "p7 (de la pila)");
    CHECK(has(a, I("movl    24(%rbp), %r10d") "") && has(a, l));
    free(l);
    /* las locales van despues de los parametros y empiezan en 0 */
    l = with_comment("        movl    $0, -72(%rbp)", "x0 = 0");  CHECK(has(a, l)); free(l);
    l = with_comment("        movl    $0, -80(%rbp)", "x1 = 0");  CHECK(has(a, l)); free(l);
    free(a);
}

/* ------------------------------------------------- operaciones, una por una */

static void fill_move(IrProgram *p, IrFunc *f)
{
    (void)p;
    ir_emit(f, IR_MOVE, ir_const(5), NONE, ir_var(f->locals[0]));        /* x0 = 5 */
    ir_emit(f, IR_MOVE, ir_var(f->params[0]), NONE, ir_var(f->locals[0]));
    ir_emit(f, IR_MOVE, ir_var(f->locals[0]), NONE, ir_var(p->globals[0]));
    ir_emit(f, IR_MOVE, ir_var(p->globals[0]), NONE, ir_var(f->locals[0]));
}

static void test_move(void)
{
    char *a = gen_f(1, 1, fill_move);

    /* p0 en -8, x0 en -16 */
    CHECK(has(a, I("# x0 = 5") I("movl    $5, %r10d") I("movl    %r10d, -16(%rbp)")));
    CHECK(has(a, I("# x0 = p0") I("movl    -8(%rbp), %r10d") I("movl    %r10d, -16(%rbp)")));
    CHECK(has(a, I("# @g = x0") I("movl    -16(%rbp), %r10d") I("movl    %r10d, ctds_g(%rip)")));
    CHECK(has(a, I("# x0 = @g") I("movl    ctds_g(%rip), %r10d") I("movl    %r10d, -16(%rbp)")));
    free(a);
}

static void fill_arith(IrProgram *p, IrFunc *f)
{
    Operand t1 = ir_new_temp(f), t2 = ir_new_temp(f), t3 = ir_new_temp(f),
            t4 = ir_new_temp(f), t5 = ir_new_temp(f);

    ir_emit(f, IR_ADD, ir_var(f->params[0]), ir_var(f->params[1]), t1);
    ir_emit(f, IR_SUB, ir_var(p->globals[0]), ir_const(7), t2);
    ir_emit(f, IR_MUL, t1, ir_var(p->globals[0]), t3);
    ir_emit(f, IR_MUL, t2, ir_const(3), t4);
    ir_emit(f, IR_SUB, ir_const(10), t3, t5);
}

static void test_arithmetic(void)
{
    char *a = gen_f(2, 0, fill_arith);

    /* p0 -8, p1 -16, t1 -24, t2 -32, t3 -40, t4 -48, t5 -56 */
    CHECK(has(a, I("# t1 = p0 + p1") I("movl    -8(%rbp), %r10d")
                 I("addl    -16(%rbp), %r10d") I("movl    %r10d, -24(%rbp)")));
    CHECK(has(a, I("# t2 = @g - 7") I("movl    ctds_g(%rip), %r10d")
                 I("subl    $7, %r10d") I("movl    %r10d, -32(%rbp)")));
    CHECK(has(a, I("# t3 = t1 * @g") I("movl    -24(%rbp), %r10d")
                 I("imull   ctds_g(%rip), %r10d") I("movl    %r10d, -40(%rbp)")));
    CHECK(has(a, I("# t4 = t2 * 3") I("movl    -32(%rbp), %r10d")
                 I("imull   $3, %r10d") I("movl    %r10d, -48(%rbp)")));
    CHECK(has(a, I("# t5 = 10 - t3") I("movl    $10, %r10d")
                 I("subl    -40(%rbp), %r10d") I("movl    %r10d, -56(%rbp)")));
    free(a);
}

static void fill_unary(IrProgram *p, IrFunc *f)
{
    Operand t1 = ir_new_temp(f), t2 = ir_new_temp(f);

    (void)p;
    ir_emit(f, IR_NEG, ir_var(f->params[0]), NONE, t1);
    ir_emit(f, IR_NOT, t1, NONE, t2);
}

static void test_unary(void)
{
    char *a = gen_f(1, 0, fill_unary);

    CHECK(has(a, I("# t1 = - p0") I("movl    -8(%rbp), %r10d") I("negl    %r10d")
                 I("movl    %r10d, -16(%rbp)")));
    CHECK(has(a, I("# t2 = ! t1") I("movl    -16(%rbp), %r10d") I("cmpl    $0, %r10d")
                 I("sete    %r11b") I("movzbl  %r11b, %r11d") I("movl    %r11d, -24(%rbp)")));
    free(a);
}

static void fill_compare(IrProgram *p, IrFunc *f)
{
    Operand t1 = ir_new_temp(f), t2 = ir_new_temp(f), t3 = ir_new_temp(f);
    Operand t4 = ir_new_temp(f);

    (void)p;
    ir_emit(f, IR_LT, ir_var(f->params[0]), ir_var(f->params[1]), t1);
    ir_emit(f, IR_GT, ir_var(f->params[0]), ir_const(4), t2);
    ir_emit(f, IR_EQ, t1, ir_var(p->globals[0]), t3);
    ir_emit(f, IR_NE, ir_var(f->params[0]), ir_const(7), t4);
}

static void test_comparisons(void)
{
    char *a = gen_f(2, 0, fill_compare);

    CHECK(has(a, I("# t1 = p0 < p1") I("movl    -8(%rbp), %r10d") I("cmpl    -16(%rbp), %r10d")
                 I("setl    %r11b") I("movzbl  %r11b, %r11d") I("movl    %r11d, -24(%rbp)")));
    CHECK(has(a, I("# t2 = p0 > 4") I("movl    -8(%rbp), %r10d") I("cmpl    $4, %r10d")
                 I("setg    %r11b") I("movzbl  %r11b, %r11d") I("movl    %r11d, -32(%rbp)")));
    CHECK(has(a, I("# t3 = t1 == @g") I("movl    -24(%rbp), %r10d") I("cmpl    ctds_g(%rip), %r10d")
                 I("sete    %r11b") I("movzbl  %r11b, %r11d") I("movl    %r11d, -40(%rbp)")));
    CHECK(has(a, I("# t4 = p0 != 7") I("movl    -8(%rbp), %r10d") I("cmpl    $7, %r10d")
                 I("setne   %r11b") I("movzbl  %r11b, %r11d") I("movl    %r11d, -48(%rbp)")));
    free(a);
}

static void fill_divide(IrProgram *p, IrFunc *f)
{
    Operand t1 = ir_new_temp(f), t2 = ir_new_temp(f);

    (void)p;
    ir_emit(f, IR_DIV, ir_var(f->params[0]), ir_var(f->params[1]), t1);
    ir_emit(f, IR_MOD, ir_var(f->params[0]), ir_const(5), t2);
}

static void test_division(void)
{
    char *a = gen_f(2, 0, fill_divide);

    /* el cociente sale de %eax y el resto de %edx; si el divisor es -1 no se
     * ejecuta idivl (INT_MIN / -1 es una excepcion de hardware) */
    CHECK(has(a, I("# t1 = p0 / p1") I("movl    -8(%rbp), %eax") I("cltd")
                 I("movl    -16(%rbp), %r10d") I("cmpl    $-1, %r10d") I("je      .Ldiv1")
                 I("idivl   %r10d") I("jmp     .Ldiv1e")
                 ".Ldiv1:\n" I("negl    %eax")
                 ".Ldiv1e:\n" I("movl    %eax, -24(%rbp)")));
    CHECK(has(a, I("# t2 = p0 % 5") I("movl    -8(%rbp), %eax") I("cltd")
                 I("movl    $5, %r10d") I("cmpl    $-1, %r10d") I("je      .Ldiv2")
                 I("idivl   %r10d") I("jmp     .Ldiv2e")
                 ".Ldiv2:\n" I("xorl    %edx, %edx")
                 ".Ldiv2e:\n" I("movl    %edx, -32(%rbp)")));
    free(a);
}

static void fill_jumps(IrProgram *p, IrFunc *f)
{
    Operand l1 = ir_new_label(p), l2 = ir_new_label(p), t1 = ir_new_temp(f);

    ir_emit(f, IR_MOVE, ir_var(f->params[0]), NONE, t1);
    ir_emit(f, IR_JZ, t1, l1, NONE);
    ir_emit(f, IR_JNZ, ir_const(1), l2, NONE);
    ir_emit(f, IR_JMP, l1, NONE, NONE);
    ir_emit(f, IR_LABEL, l1, NONE, NONE);
    ir_emit(f, IR_LABEL, l2, NONE, NONE);
}

static void test_jumps_and_labels(void)
{
    char *a = gen_f(1, 0, fill_jumps);

    /* el operando se carga en un registro (puede ser una constante) y se prueba */
    CHECK(has(a, I("# jz t1, L1") I("movl    -16(%rbp), %r10d") I("testl   %r10d, %r10d")
                 I("je      .L1")));
    CHECK(has(a, I("# jnz 1, L2") I("movl    $1, %r10d") I("testl   %r10d, %r10d")
                 I("jne     .L2")));
    CHECK(has(a, I("# jmp L1") I("jmp     .L1")));
    CHECK(has(a, "\n.L1:\n.L2:\n"));                /* las etiquetas van sin sangria */
    free(a);
}

/* ---------------------------------------------------- llamadas y retornos */

/* Programa con `main` que llama a `callee` con `n` argumentos constantes
 * 1..n y guarda el resultado (si `result`) en t1. */
static char *gen_call(int n, int result)
{
    IrProgram *p = ir_program_new();
    IrFunc *callee = ir_add_func(p, "callee", result);
    IrFunc *m = ir_add_func(p, "main", 0);
    Operand t1 = result ? ir_new_temp(m) : NONE;
    char *a;

    ir_emit(callee, IR_RET, result ? ir_const(0) : NONE, NONE, NONE);
    for (int i = 1; i <= n; i++)
        ir_emit(m, IR_PARAM, ir_const(i), NONE, NONE);
    ir_emit(m, IR_CALL, ir_func("callee"), ir_const(n), t1);
    ir_emit(m, IR_RET, NONE, NONE, NONE);
    a = asm_of(p);
    ir_program_free(p);
    return a;
}

static void test_calls(void)
{
    char *a;

    a = gen_call(0, 0);                             /* sin argumentos ni resultado */
    CHECK(has(a, I("# call callee, 0") I("call    ctds_callee") I("# ret")));
    free(a);

    a = gen_call(1, 1);                             /* con resultado: %eax a t1 */
    CHECK(has(a, I("# param 1") I("# t1 = call callee, 1") I("movl    $1, %edi")
                 I("call    ctds_callee") I("movl    %eax, -8(%rbp)")));
    free(a);

    a = gen_call(6, 0);                             /* 6 argumentos: todos en registros */
    CHECK(has(a, I("movl    $1, %edi") I("movl    $2, %esi") I("movl    $3, %edx")
                 I("movl    $4, %ecx") I("movl    $5, %r8d") I("movl    $6, %r9d")
                 I("call    ctds_callee") I("# ret")));
    CHECK(strstr(a, "pushq   %r10") == NULL && strstr(a, "addq") == NULL);   /* nada en la pila */
    free(a);

    a = gen_call(7, 0);                             /* 7: uno en la pila, con relleno */
    CHECK(has(a, I("# call callee, 7")
                 "        subq    $8, %rsp"));
    CHECK(has(a, I("movl    $7, %r10d") I("pushq   %r10") I("movl    $1, %edi")));
    CHECK(has(a, I("call    ctds_callee") I("addq    $16, %rsp")));
    free(a);

    a = gen_call(8, 0);                             /* 8: dos en la pila, sin relleno */
    CHECK(strstr(a, "subq    $8, %rsp") == NULL);
    CHECK(has(a, I("movl    $8, %r10d") I("pushq   %r10")          /* de derecha a izquierda */
                 I("movl    $7, %r10d") I("pushq   %r10")
                 I("movl    $1, %edi")));
    CHECK(has(a, I("call    ctds_callee") I("addq    $16, %rsp")));
    free(a);

    a = gen_call(9, 0);                             /* 9: tres en la pila, con relleno */
    CHECK(has(a, "        subq    $8, %rsp"));
    CHECK(has(a, I("movl    $9, %r10d") I("pushq   %r10") I("movl    $8, %r10d") I("pushq   %r10")
                 I("movl    $7, %r10d") I("pushq   %r10")));
    CHECK(has(a, I("call    ctds_callee") I("addq    $32, %rsp")));
    free(a);
}

/* Llamada a main y a un metodo cuyo nombre es el de una funcion de la
 * biblioteca de C: el prefijo ctds_ evita el choque. */
static void test_call_names(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *pf = ir_add_func(p, "printf", 0);
    IrFunc *m = ir_add_func(p, "main", 0);
    char *a;

    ir_add_global(p, "exit");
    ir_emit(pf, IR_RET, NONE, NONE, NONE);
    ir_emit(m, IR_CALL, ir_func("printf"), ir_const(0), NONE);
    ir_emit(m, IR_CALL, ir_func("main"), ir_const(0), NONE);
    ir_emit(m, IR_RET, NONE, NONE, NONE);
    a = asm_of(p);
    CHECK(has(a, "ctds_printf:"));
    CHECK(has(a, I("call    ctds_printf")));
    CHECK(has(a, I("call    main")));
    CHECK(has(a, "ctds_exit: .long   0"));
    CHECK(strstr(a, "\nprintf:") == NULL);
    free(a);
    ir_program_free(p);
}

static void test_returns(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *v = ir_add_func(p, "v", 0), *w = ir_add_func(p, "w", 1);
    IrFunc *m = ir_add_func(p, "main", 1);
    char *a;

    ir_add_local(w, "x");
    ir_emit(v, IR_RET, NONE, NONE, NONE);
    ir_emit(w, IR_RET, ir_var(w->locals[0]), NONE, NONE);
    ir_emit(m, IR_RET, ir_const(7), NONE, NONE);
    a = asm_of(p);
    /* un metodo void que no es main no toca %eax: del ret va directo a leave */
    CHECK(has(a, "ctds_v:") && has(a, I("# ret") I("leave") I("ret") "\n"));
    /* con valor: %eax */
    CHECK(has(a, I("# ret x") I("movl    -8(%rbp), %eax") I("leave") I("ret")));
    CHECK(has(a, I("# ret 7") I("movl    $7, %eax") I("leave") I("ret")));
    free(a);
    ir_program_free(p);

    /* main sin valor devuelve 0; un void que no es main no toca %eax */
    p = ir_program_new();
    v = ir_add_func(p, "v", 0);
    m = ir_add_func(p, "main", 0);
    ir_emit(v, IR_RET, NONE, NONE, NONE);
    ir_emit(m, IR_RET, NONE, NONE, NONE);
    a = asm_of(p);
    CHECK(count(a, "movl    $0, %eax") == 1);        /* solo el de main */
    free(a);
    ir_program_free(p);
}

/* Un metodo que termina en una etiqueta (sin ret) recibe un retorno al final. */
static void test_implicit_return(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *f = ir_add_func(p, "f", 1);
    IrFunc *m = ir_add_func(p, "main", 0);
    Operand l = ir_new_label(p);
    char *a;

    ir_emit(f, IR_JMP, l, NONE, NONE);
    ir_emit(f, IR_LABEL, l, NONE, NONE);            /* termina en una etiqueta */
    ir_emit(m, IR_RET, NONE, NONE, NONE);
    a = asm_of(p);
    CHECK(has(a, ".L1:\n        # (fin del metodo)\n" I("leave") I("ret")));
    free(a);
    ir_program_free(p);

    p = ir_program_new();                           /* main sin codigo: devuelve 0 */
    ir_add_func(p, "main", 0);
    a = asm_of(p);
    CHECK(has(a, I("# (fin del metodo)") I("movl    $0, %eax") I("leave") I("ret")));
    free(a);
    ir_program_free(p);
}

/* param no genera codigo: los argumentos se cargan en el call. */
static void test_param_generates_no_code(void)
{
    char *a = gen_call(2, 0);

    CHECK(has(a, I("# param 1") I("# param 2") I("# call callee, 2") I("movl    $1, %edi")));
    free(a);
}

int main(void)
{
    test_minimal_program();
    test_globals_and_symbols();
    test_frame_size();
    test_prologue_params_and_locals();
    test_move();
    test_arithmetic();
    test_unary();
    test_comparisons();
    test_division();
    test_jumps_and_labels();
    test_calls();
    test_call_names();
    test_returns();
    test_implicit_return();
    test_param_generates_no_code();

    printf("test_x86gen: %d comprobaciones, %d fallaron\n", checks, failures);
    return failures != 0;
}
