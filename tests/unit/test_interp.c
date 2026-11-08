/*
 * test_interp.c - Tests unitarios del interprete (src/interp.c). Los programas
 * se arman directamente con la API del codigo intermedio (ir.h), sin pasar por
 * el generador, asi se prueba el interprete de forma independiente. Se compila
 * y ejecuta con `make test`.
 *
 * Los errores de ejecucion que se provocan a proposito imprimen su mensaje por
 * stderr; el script de tests solo muestra la salida cuando un test falla.
 */
#define _POSIX_C_SOURCE 200809L

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "interp.h"
#include "ir.h"

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

/* Ejecuta el programa y devuelve el estado; deja en *has y *val el resultado. */
static int run(const IrProgram *p, int *has, int *val)
{
    return interp_run(p, has, val);
}

/* Programa con un `main` que devuelve `a op b`. Devuelve 0 y el valor en *out, o
 * el estado de error. */
static int eval_binary(IrOp op, long long a, long long b, int *out)
{
    IrProgram *p = ir_program_new();
    IrFunc *m = ir_add_func(p, "main", 1);
    Operand t = ir_new_temp(m);
    int has, status;

    ir_emit(m, op, ir_const(a), ir_const(b), t);
    ir_emit(m, IR_RET, t, NONE, NONE);
    status = run(p, &has, out);
    ir_program_free(p);
    return status;
}

static int eval_unary(IrOp op, long long a, int *out)
{
    IrProgram *p = ir_program_new();
    IrFunc *m = ir_add_func(p, "main", 1);
    Operand t = ir_new_temp(m);
    int has, status;

    ir_emit(m, op, ir_const(a), NONE, t);
    ir_emit(m, IR_RET, t, NONE, NONE);
    status = run(p, &has, out);
    ir_program_free(p);
    return status;
}

/* main devuelve un valor, o no devuelve nada. */
static void test_main_result(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *m = ir_add_func(p, "main", 1);
    int has = 0, val = 0;

    ir_emit(m, IR_RET, ir_const(5), NONE, NONE);
    CHECK(run(p, &has, &val) == 0 && has == 1 && val == 5);
    ir_program_free(p);

    p = ir_program_new();
    m = ir_add_func(p, "main", 0);
    ir_emit(m, IR_RET, NONE, NONE, NONE);
    has = 1;
    CHECK(run(p, &has, &val) == 0 && has == 0);
    ir_program_free(p);

    p = ir_program_new();                           /* sin ret: termina al final */
    ir_add_func(p, "main", 0);
    CHECK(run(p, &has, &val) == 0 && has == 0);
    ir_program_free(p);
}

/* Cada operacion aritmetica y de comparacion. */
static void test_operations(void)
{
    int v;

    CHECK(eval_binary(IR_ADD, 7, 3, &v) == 0 && v == 10);
    CHECK(eval_binary(IR_SUB, 7, 3, &v) == 0 && v == 4);
    CHECK(eval_binary(IR_MUL, 7, 3, &v) == 0 && v == 21);
    CHECK(eval_binary(IR_DIV, 7, 2, &v) == 0 && v == 3);
    CHECK(eval_binary(IR_DIV, -7, 2, &v) == 0 && v == -3);     /* hacia cero */
    CHECK(eval_binary(IR_MOD, 7, 2, &v) == 0 && v == 1);
    CHECK(eval_binary(IR_MOD, -7, 2, &v) == 0 && v == -1);
    CHECK(eval_binary(IR_MOD, 7, -2, &v) == 0 && v == 1);
    CHECK(eval_binary(IR_LT, 1, 2, &v) == 0 && v == 1);
    CHECK(eval_binary(IR_LT, 2, 1, &v) == 0 && v == 0);
    CHECK(eval_binary(IR_LT, 2, 2, &v) == 0 && v == 0);
    CHECK(eval_binary(IR_GT, 2, 1, &v) == 0 && v == 1);
    CHECK(eval_binary(IR_GT, 1, 2, &v) == 0 && v == 0);
    CHECK(eval_binary(IR_EQ, 3, 3, &v) == 0 && v == 1);
    CHECK(eval_binary(IR_EQ, 3, 4, &v) == 0 && v == 0);
    CHECK(eval_unary(IR_NEG, 5, &v) == 0 && v == -5);
    CHECK(eval_unary(IR_NEG, -5, &v) == 0 && v == 5);
    CHECK(eval_unary(IR_NOT, 0, &v) == 0 && v == 1);
    CHECK(eval_unary(IR_NOT, 1, &v) == 0 && v == 0);
    CHECK(eval_unary(IR_NOT, 7, &v) == 0 && v == 0);           /* cualquier no cero */
}

/* Enteros de 32 bits que dan la vuelta, sin comportamiento indefinido. */
static void test_wraparound(void)
{
    int v;

    CHECK(eval_binary(IR_ADD, INT_MAX, 1, &v) == 0 && v == INT_MIN);
    CHECK(eval_binary(IR_SUB, INT_MIN, 1, &v) == 0 && v == INT_MAX);
    CHECK(eval_binary(IR_MUL, 65536, 65536, &v) == 0 && v == 0);
    CHECK(eval_binary(IR_MUL, INT_MAX, 2, &v) == 0 && v == -2);
    CHECK(eval_unary(IR_NEG, INT_MIN, &v) == 0 && v == INT_MIN);
    CHECK(eval_binary(IR_DIV, INT_MIN, -1, &v) == 0 && v == INT_MIN);
    CHECK(eval_binary(IR_MOD, INT_MIN, -1, &v) == 0 && v == 0);
}

/* La division y el resto por cero son un error de ejecucion. */
static void test_division_by_zero(void)
{
    int v;

    CHECK(eval_binary(IR_DIV, 1, 0, &v) == 1);
    CHECK(eval_binary(IR_MOD, 1, 0, &v) == 1);
    CHECK(eval_binary(IR_DIV, 0, 0, &v) == 1);
}

/* Saltos: un bucle que suma 1..10 con jz, jmp y etiquetas. */
static void test_loop_with_jumps(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *m = ir_add_func(p, "main", 1);
    IrVar *i = ir_add_local(m, "i"), *s = ir_add_local(m, "s");
    Operand start = ir_new_label(p), end = ir_new_label(p);
    Operand c = ir_new_temp(m), t = ir_new_temp(m);
    int has, v;

    ir_emit(m, IR_MOVE, ir_const(1), NONE, ir_var(i));
    ir_emit(m, IR_LABEL, start, NONE, NONE);
    ir_emit(m, IR_LT, ir_var(i), ir_const(11), c);
    ir_emit(m, IR_JZ, c, end, NONE);
    ir_emit(m, IR_ADD, ir_var(s), ir_var(i), t);
    ir_emit(m, IR_MOVE, t, NONE, ir_var(s));
    ir_emit(m, IR_ADD, ir_var(i), ir_const(1), t);
    ir_emit(m, IR_MOVE, t, NONE, ir_var(i));
    ir_emit(m, IR_JMP, start, NONE, NONE);
    ir_emit(m, IR_LABEL, end, NONE, NONE);
    ir_emit(m, IR_RET, ir_var(s), NONE, NONE);
    CHECK(run(p, &has, &v) == 0 && has == 1 && v == 55);
    ir_program_free(p);
}

/* jz salta con 0 y jnz con distinto de 0, y no al reves. */
static void test_conditional_jumps(void)
{
    static const struct { IrOp op; long long cond; int expect; } cases[] = {
        { IR_JZ, 0, 1 }, { IR_JZ, 5, 0 }, { IR_JNZ, 0, 0 }, { IR_JNZ, 5, 1 },
    };

    for (size_t k = 0; k < sizeof cases / sizeof cases[0]; k++) {
        IrProgram *p = ir_program_new();
        IrFunc *m = ir_add_func(p, "main", 1);
        Operand skip = ir_new_label(p);
        int has, v;

        ir_emit(m, cases[k].op, ir_const(cases[k].cond), skip, NONE);
        ir_emit(m, IR_RET, ir_const(0), NONE, NONE);        /* no salto */
        ir_emit(m, IR_LABEL, skip, NONE, NONE);
        ir_emit(m, IR_RET, ir_const(1), NONE, NONE);        /* salto */
        CHECK(run(p, &has, &v) == 0 && v == cases[k].expect);
        ir_program_free(p);
    }
}

/* Variables globales y locales empiezan en 0, y las globales se modifican. */
static void test_globals_and_locals(void)
{
    IrProgram *p = ir_program_new();
    IrVar *g = ir_add_global(p, "g"), *h = ir_add_global(p, "h");
    IrFunc *m = ir_add_func(p, "main", 1);
    IrVar *x = ir_add_local(m, "x");
    Operand t = ir_new_temp(m), u = ir_new_temp(m);
    int has, v;

    ir_emit(m, IR_ADD, ir_var(g), ir_const(5), t);          /* g = g + 5 */
    ir_emit(m, IR_MOVE, t, NONE, ir_var(g));
    ir_emit(m, IR_ADD, ir_var(g), ir_var(h), u);            /* h sigue en 0 */
    ir_emit(m, IR_ADD, u, ir_var(x), t);                    /* x vale 0 */
    ir_emit(m, IR_RET, t, NONE, NONE);
    CHECK(run(p, &has, &v) == 0 && v == 5);
    ir_program_free(p);
}

/* Llamada: los argumentos llegan en orden a los parametros. */
static void test_call_arguments(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *sub = ir_add_func(p, "sub", 1);
    IrVar *a = ir_add_param(sub, "a"), *b = ir_add_param(sub, "b");
    Operand ts = ir_new_temp(sub);
    IrFunc *m = ir_add_func(p, "main", 1);
    Operand r = ir_new_temp(m);
    int has, v;

    ir_emit(sub, IR_SUB, ir_var(a), ir_var(b), ts);
    ir_emit(sub, IR_RET, ts, NONE, NONE);
    ir_emit(m, IR_PARAM, ir_const(10), NONE, NONE);
    ir_emit(m, IR_PARAM, ir_const(3), NONE, NONE);
    ir_emit(m, IR_CALL, ir_func("sub"), ir_const(2), r);
    ir_emit(m, IR_RET, r, NONE, NONE);
    CHECK(run(p, &has, &v) == 0 && v == 7);                 /* 10 - 3, no 3 - 10 */
    ir_program_free(p);
}

/* f(g(1), 2): los param de la llamada interna no se mezclan con los de la externa. */
static void test_nested_calls(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *g = ir_add_func(p, "g", 1);             /* g(a) = a * 100 */
    IrVar *ga = ir_add_param(g, "a");
    Operand gt = ir_new_temp(g);
    IrFunc *f = ir_add_func(p, "f", 1);             /* f(a, b) = a + b */
    IrVar *fa = ir_add_param(f, "a"), *fb = ir_add_param(f, "b");
    Operand ft = ir_new_temp(f);
    IrFunc *m = ir_add_func(p, "main", 1);
    Operand t1 = ir_new_temp(m), t2 = ir_new_temp(m);
    int has, v;

    ir_emit(g, IR_MUL, ir_var(ga), ir_const(100), gt);
    ir_emit(g, IR_RET, gt, NONE, NONE);
    ir_emit(f, IR_ADD, ir_var(fa), ir_var(fb), ft);
    ir_emit(f, IR_RET, ft, NONE, NONE);
    ir_emit(m, IR_PARAM, ir_const(1), NONE, NONE);
    ir_emit(m, IR_CALL, ir_func("g"), ir_const(1), t1);     /* t1 = g(1) = 100 */
    ir_emit(m, IR_PARAM, t1, NONE, NONE);
    ir_emit(m, IR_PARAM, ir_const(2), NONE, NONE);
    ir_emit(m, IR_CALL, ir_func("f"), ir_const(2), t2);     /* f(100, 2) */
    ir_emit(m, IR_RET, t2, NONE, NONE);
    CHECK(run(p, &has, &v) == 0 && v == 102);
    ir_program_free(p);
}

/* Una llamada sin resultado ejecuta el metodo (modifica una global). */
static void test_call_without_result(void)
{
    IrProgram *p = ir_program_new();
    IrVar *g = ir_add_global(p, "g");
    IrFunc *tick = ir_add_func(p, "tick", 0);
    Operand t = ir_new_temp(tick);
    IrFunc *m = ir_add_func(p, "main", 1);
    int has, v;

    ir_emit(tick, IR_ADD, ir_var(g), ir_const(1), t);
    ir_emit(tick, IR_MOVE, t, NONE, ir_var(g));
    ir_emit(tick, IR_RET, NONE, NONE, NONE);
    ir_emit(m, IR_CALL, ir_func("tick"), ir_const(0), NONE);
    ir_emit(m, IR_CALL, ir_func("tick"), ir_const(0), NONE);
    ir_emit(m, IR_RET, ir_var(g), NONE, NONE);
    CHECK(run(p, &has, &v) == 0 && v == 2);
    ir_program_free(p);
}

/* Cada llamada tiene su propio marco: los locales no se pisan al recursar. */
static void test_recursion_frames(void)
{
    /* int f(int n) { int x; x = n; if (n < 1) return 0; return x + f(n - 1); } */
    IrProgram *p = ir_program_new();
    IrFunc *f = ir_add_func(p, "f", 1);
    IrVar *n = ir_add_param(f, "n"), *x = ir_add_local(f, "x");
    Operand c = ir_new_temp(f), d = ir_new_temp(f), r = ir_new_temp(f),
            s = ir_new_temp(f);
    Operand skip = ir_new_label(p);
    IrFunc *m = ir_add_func(p, "main", 1);
    Operand t = ir_new_temp(m);
    int has, v;

    ir_emit(f, IR_MOVE, ir_var(n), NONE, ir_var(x));
    ir_emit(f, IR_LT, ir_var(n), ir_const(1), c);
    ir_emit(f, IR_JZ, c, skip, NONE);
    ir_emit(f, IR_RET, ir_const(0), NONE, NONE);
    ir_emit(f, IR_LABEL, skip, NONE, NONE);
    ir_emit(f, IR_SUB, ir_var(n), ir_const(1), d);
    ir_emit(f, IR_PARAM, d, NONE, NONE);
    ir_emit(f, IR_CALL, ir_func("f"), ir_const(1), r);
    ir_emit(f, IR_ADD, ir_var(x), r, s);                    /* x despues de la llamada */
    ir_emit(f, IR_RET, s, NONE, NONE);
    ir_emit(m, IR_PARAM, ir_const(10), NONE, NONE);
    ir_emit(m, IR_CALL, ir_func("f"), ir_const(1), t);
    ir_emit(m, IR_RET, t, NONE, NONE);
    CHECK(run(p, &has, &v) == 0 && v == 55);                /* 10 + 9 + ... + 1 */
    ir_program_free(p);
}

/* Recursion infinita: error de ejecucion, no se cae el interprete. */
static void test_call_stack_overflow(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *f = ir_add_func(p, "f", 1);
    Operand t = ir_new_temp(f);
    IrFunc *m = ir_add_func(p, "main", 1);
    Operand r = ir_new_temp(m);
    int has, v;

    ir_emit(f, IR_CALL, ir_func("f"), ir_const(0), t);
    ir_emit(f, IR_RET, t, NONE, NONE);
    ir_emit(m, IR_CALL, ir_func("f"), ir_const(0), r);
    ir_emit(m, IR_RET, r, NONE, NONE);
    CHECK(run(p, &has, &v) == 1);
    ir_program_free(p);
}

/* Una recursion profunda pero finita (50000 niveles) funciona. */
static void test_deep_recursion(void)
{
    /* int sum(int n) { if (n < 1) return 0; return n + sum(n - 1); } */
    IrProgram *p = ir_program_new();
    IrFunc *f = ir_add_func(p, "sum", 1);
    IrVar *n = ir_add_param(f, "n");
    Operand c = ir_new_temp(f), d = ir_new_temp(f), r = ir_new_temp(f),
            s = ir_new_temp(f);
    Operand skip = ir_new_label(p);
    IrFunc *m = ir_add_func(p, "main", 1);
    Operand t = ir_new_temp(m);
    int has, v;

    ir_emit(f, IR_LT, ir_var(n), ir_const(1), c);
    ir_emit(f, IR_JZ, c, skip, NONE);
    ir_emit(f, IR_RET, ir_const(0), NONE, NONE);
    ir_emit(f, IR_LABEL, skip, NONE, NONE);
    ir_emit(f, IR_SUB, ir_var(n), ir_const(1), d);
    ir_emit(f, IR_PARAM, d, NONE, NONE);
    ir_emit(f, IR_CALL, ir_func("sum"), ir_const(1), r);
    ir_emit(f, IR_ADD, ir_var(n), r, s);
    ir_emit(f, IR_RET, s, NONE, NONE);
    ir_emit(m, IR_PARAM, ir_const(50000), NONE, NONE);
    ir_emit(m, IR_CALL, ir_func("sum"), ir_const(1), t);
    ir_emit(m, IR_RET, t, NONE, NONE);
    CHECK(run(p, &has, &v) == 0 && v == 1250025000);
    ir_program_free(p);
}

int main(void)
{
    test_main_result();
    test_operations();
    test_wraparound();
    test_division_by_zero();
    test_loop_with_jumps();
    test_conditional_jumps();
    test_globals_and_locals();
    test_call_arguments();
    test_nested_calls();
    test_call_without_result();
    test_recursion_frames();
    test_call_stack_overflow();
    test_deep_recursion();

    printf("test_interp: %d comprobaciones, %d fallaron\n", checks, failures);
    return failures != 0;
}
