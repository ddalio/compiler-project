/*
 * test_opt.c - Tests unitarios del optimizador (src/opt.c).
 *
 * Cada caso arma un metodo en codigo intermedio con la API de ir.h, le aplica
 * UNA optimizacion (o varias) con opt_run, y compara el programa resultante,
 * impreso con ir_print, con el texto esperado ESCRITO A MANO. Para cada
 * optimizacion hay casos de lo que debe optimizar, de lo que NO debe tocar (las
 * reglas de seguridad: division por cero, llamadas, globales, bloques) y de
 * bordes. Se compila y ejecuta con `make test`.
 *
 * Todos los metodos son `f(a, b)` con locales x e y, y el programa tiene una
 * global `g`, asi el encabezado del texto esperado es siempre el mismo.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ir.h"
#include "opt.h"

static int checks, failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            printf("  FAIL (linea %d): %s\n", __LINE__, #cond);              \
        }                                                                    \
    } while (0)

/* Compara el texto obtenido con el esperado y, si difieren, muestra ambos. */
#define CHECK_TEXT(got, expected)                                            \
    do {                                                                     \
        checks++;                                                            \
        if (strcmp((got), (expected)) != 0) {                                \
            failures++;                                                      \
            printf("  FAIL (linea %d): codigo distinto\n"                    \
                   "  --- obtenido:\n%s  --- esperado:\n%s", __LINE__, got,       \
                   expected);                                                \
        }                                                                    \
    } while (0)

#define NONE ir_none()

/* Texto esperado: encabezado, una instruccion (2 espacios), una etiqueta (sin
 * sangria) y el final de un metodo. */
#define HDR "global @g\n\nfunc f(a, b)\n  local x\n  local y\n"
#define IN(text) "  " text "\n"
#define LB(text) text "\n"
#define END "end\n"

/* ------------------------------------------------------------- utilidades */

typedef void (*build_fn)(IrProgram *p, IrFunc *f);

/* Operandos del metodo de prueba. */
static Operand A(IrFunc *f) { return ir_var(f->params[0]); }
static Operand B(IrFunc *f) { return ir_var(f->params[1]); }
static Operand X(IrFunc *f) { return ir_var(f->locals[0]); }
static Operand Y(IrFunc *f) { return ir_var(f->locals[1]); }
static Operand G(IrProgram *p) { return ir_var(p->globals[0]); }

/* El temporal tN: crea los que falten hasta llegar a N. */
static Operand T(IrFunc *f, int n)
{
    Operand t = NONE;

    while (f->ntemps < n)
        t = ir_new_temp(f);
    t.kind = OPD_TEMP;
    t.value = n;
    return t;
}

static void emit(IrFunc *f, IrOp op, Operand a, Operand b, Operand r)
{
    ir_emit(f, op, a, b, r);
}

/* El programa impreso, en una cadena (hay que liberarla). */
static char *render(const IrProgram *p)
{
    char *buf = NULL;
    size_t len = 0;
    FILE *mem = open_memstream(&buf, &len);

    ir_print(mem, p);
    fclose(mem);
    return buf;
}

/* Arma el programa con `build`, lo optimiza con `passes` y devuelve su texto.
 * Deja las estadisticas en *st y, si `prog_out` no es NULL, el programa mismo
 * (para seguir mirandolo); si es NULL lo libera. */
static char *optimize(build_fn build, unsigned passes, OptStats *st, IrProgram **prog_out)
{
    IrProgram *p = ir_program_new();
    IrFunc *f;
    char *text;

    ir_add_global(p, "g");
    f = ir_add_func(p, "f", 1);
    ir_add_param(f, "a");
    ir_add_param(f, "b");
    ir_add_local(f, "x");
    ir_add_local(f, "y");
    build(p, f);
    opt_run(p, passes, NULL, st);
    text = render(p);
    if (prog_out)
        *prog_out = p;
    else
        ir_program_free(p);
    return text;
}

/* Atajo para los casos que solo miran el texto. */
static char *run(build_fn build, unsigned passes)
{
    OptStats st;

    return optimize(build, passes, &st, NULL);
}

/* =================================================================== fold */

static void build_fold_constants(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_MUL, ir_const(3), ir_const(4), T(f, 1));
    emit(f, IR_SUB, ir_const(10), ir_const(4), T(f, 2));
    emit(f, IR_DIV, ir_const(7), ir_const(2), T(f, 3));
    emit(f, IR_MOD, ir_const(7), ir_const(2), T(f, 4));
    emit(f, IR_LT, ir_const(3), ir_const(4), T(f, 5));
    emit(f, IR_GT, ir_const(3), ir_const(4), T(f, 6));
    emit(f, IR_EQ, ir_const(5), ir_const(5), T(f, 7));
    emit(f, IR_NEG, ir_const(6), NONE, T(f, 8));
    emit(f, IR_NOT, ir_const(0), NONE, T(f, 9));
    emit(f, IR_NOT, ir_const(5), NONE, T(f, 10));
    emit(f, IR_NE, ir_const(3), ir_const(4), T(f, 11));
    emit(f, IR_NE, ir_const(5), ir_const(5), T(f, 12));
}

static void test_fold_constants(void)
{
    OptStats st;
    char *got = optimize(build_fold_constants, OPT_FOLD, &st, NULL);

    CHECK_TEXT(got,
        HDR IN("t1 = 12") IN("t2 = 6") IN("t3 = 3") IN("t4 = 1") IN("t5 = 1")
            IN("t6 = 0") IN("t7 = 1") IN("t8 = -6") IN("t9 = 1") IN("t10 = 0")
            IN("t11 = 1") IN("t12 = 0") END);
    CHECK(st.folded == 12);                         /* una por cada operacion */
    CHECK(st.before == 12 && st.after == 12);       /* fold no cambia la cantidad */
    free(got);
}

/* Aritmetica de 32 bits que da la vuelta, igual que el codigo nativo. */
static void build_fold_wraparound(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_ADD, ir_const(2147483647), ir_const(1), T(f, 1));
    emit(f, IR_SUB, ir_const(-2147483647), ir_const(2), T(f, 2));
    emit(f, IR_MUL, ir_const(65536), ir_const(65536), T(f, 3));
    emit(f, IR_NEG, ir_const(-2147483647 - 1), NONE, T(f, 4));
    emit(f, IR_DIV, ir_const(-2147483647 - 1), ir_const(-1), T(f, 5));
    emit(f, IR_MOD, ir_const(-2147483647 - 1), ir_const(-1), T(f, 6));
    emit(f, IR_DIV, ir_const(-7), ir_const(2), T(f, 7));
    emit(f, IR_MOD, ir_const(-7), ir_const(2), T(f, 8));
}

static void test_fold_wraparound(void)
{
    char *got = run(build_fold_wraparound, OPT_FOLD);

    CHECK_TEXT(got,
        HDR IN("t1 = -2147483648") IN("t2 = 2147483647") IN("t3 = 0")
            IN("t4 = -2147483648") IN("t5 = -2147483648") IN("t6 = 0")
            IN("t7 = -3") IN("t8 = -1") END);
    free(got);
}

/* SEGURIDAD: una division o un resto por cero NO se pliega (tiene que fallar al
 * ejecutarse). */
static void build_fold_division_by_zero(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_DIV, ir_const(5), ir_const(0), T(f, 1));
    emit(f, IR_MOD, ir_const(5), ir_const(0), T(f, 2));
    emit(f, IR_DIV, ir_const(0), ir_const(0), T(f, 3));
}

static void test_fold_division_by_zero(void)
{
    OptStats st;
    char *got = optimize(build_fold_division_by_zero, OPT_FOLD, &st, NULL);

    CHECK_TEXT(got, HDR IN("t1 = 5 / 0") IN("t2 = 5 % 0") IN("t3 = 0 / 0") END);
    CHECK(st.folded == 0);
    free(got);
}

static void build_fold_algebra(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_ADD, A(f), ir_const(0), T(f, 1));
    emit(f, IR_ADD, ir_const(0), A(f), T(f, 2));
    emit(f, IR_SUB, A(f), ir_const(0), T(f, 3));
    emit(f, IR_SUB, A(f), A(f), T(f, 4));
    emit(f, IR_MUL, A(f), ir_const(1), T(f, 5));
    emit(f, IR_MUL, ir_const(1), B(f), T(f, 6));
    emit(f, IR_MUL, A(f), ir_const(0), T(f, 7));
    emit(f, IR_MUL, ir_const(0), B(f), T(f, 8));
    emit(f, IR_DIV, A(f), ir_const(1), T(f, 9));
    emit(f, IR_MOD, A(f), ir_const(1), T(f, 10));
    emit(f, IR_EQ, A(f), A(f), T(f, 11));
    emit(f, IR_LT, A(f), A(f), T(f, 12));
    emit(f, IR_GT, A(f), A(f), T(f, 13));
    emit(f, IR_NE, A(f), A(f), T(f, 14));
}

static void test_fold_algebra(void)
{
    OptStats st;
    char *got = optimize(build_fold_algebra, OPT_FOLD, &st, NULL);

    CHECK_TEXT(got,
        HDR IN("t1 = a") IN("t2 = a") IN("t3 = a") IN("t4 = 0") IN("t5 = a")
            IN("t6 = b") IN("t7 = 0") IN("t8 = 0") IN("t9 = a") IN("t10 = 0")
            IN("t11 = 1") IN("t12 = 0") IN("t13 = 0") IN("t14 = 0") END);
    CHECK(st.folded == 14);
    free(got);
}

/* Lo que NO se simplifica: operandos distintos, un divisor que no es constante y
 * restas con el cero a la izquierda. */
static void build_fold_untouched(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_ADD, A(f), B(f), T(f, 1));
    emit(f, IR_SUB, A(f), B(f), T(f, 2));
    emit(f, IR_DIV, A(f), B(f), T(f, 3));
    emit(f, IR_SUB, ir_const(0), A(f), T(f, 4));
    emit(f, IR_MUL, A(f), ir_const(2), T(f, 5));
    emit(f, IR_DIV, ir_const(0), A(f), T(f, 6));
    emit(f, IR_MOD, A(f), B(f), T(f, 7));
    emit(f, IR_EQ, A(f), B(f), T(f, 8));
    emit(f, IR_ADD, A(f), ir_const(1), T(f, 9));
    emit(f, IR_MOVE, ir_const(5), NONE, X(f));
}

static void test_fold_untouched(void)
{
    OptStats st;
    char *got = optimize(build_fold_untouched, OPT_FOLD, &st, NULL);

    CHECK_TEXT(got,
        HDR IN("t1 = a + b") IN("t2 = a - b") IN("t3 = a / b") IN("t4 = 0 - a")
            IN("t5 = a * 2") IN("t6 = 0 / a") IN("t7 = a % b") IN("t8 = a == b")
            IN("t9 = a + 1") IN("x = 5") END);
    CHECK(st.folded == 0);
    free(got);
}

/* ============================================================== propagate */

static void build_prop_constants(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_MOVE, ir_const(5), NONE, X(f));
    emit(f, IR_MOVE, X(f), NONE, Y(f));
    emit(f, IR_ADD, Y(f), X(f), T(f, 1));
    emit(f, IR_RET, T(f, 1), NONE, NONE);
}

static void test_propagate_constants(void)
{
    OptStats st;
    char *got = optimize(build_prop_constants, OPT_PROPAGATE, &st, NULL);

    /* x vale 5 y y es copia de x, o sea 5: las dos lecturas pasan a ser 5 */
    CHECK_TEXT(got, HDR IN("x = 5") IN("y = 5") IN("t1 = 5 + 5") IN("ret t1") END);
    CHECK(st.propagated == 3);                      /* x, y y x */
    free(got);
}

static void build_prop_copies(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_MOVE, A(f), NONE, X(f));
    emit(f, IR_MOVE, X(f), NONE, Y(f));
    emit(f, IR_ADD, Y(f), B(f), T(f, 1));
    emit(f, IR_RET, T(f, 1), NONE, NONE);
}

static void test_propagate_copies(void)
{
    char *got = run(build_prop_copies, OPT_PROPAGATE);

    CHECK_TEXT(got, HDR IN("x = a") IN("y = a") IN("t1 = a + b") IN("ret t1") END);
    free(got);
}

/* Si se vuelve a escribir la variable, lo que se sabia de ella se olvida. */
static void build_prop_redefinition(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_MOVE, ir_const(5), NONE, X(f));
    emit(f, IR_MOVE, A(f), NONE, X(f));
    emit(f, IR_MOVE, X(f), NONE, T(f, 1));          /* x ya no vale 5: es copia de a */
    emit(f, IR_RET, T(f, 1), NONE, NONE);
}

static void test_propagate_redefinition(void)
{
    char *got = run(build_prop_redefinition, OPT_PROPAGATE);

    /* x ya no vale 5: es copia de a, y t1 = a, asi que el ret tambien devuelve a */
    CHECK_TEXT(got, HDR IN("x = 5") IN("x = a") IN("t1 = a") IN("ret a") END);
    free(got);
}

/* Si se escribe el ORIGEN de una copia, la copia deja de valer. */
static void build_prop_source_overwritten(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_MOVE, A(f), NONE, X(f));
    emit(f, IR_MOVE, A(f), NONE, Y(f));
    emit(f, IR_MOVE, ir_const(9), NONE, A(f));      /* a cambia: x e y ya no son a */
    emit(f, IR_MOVE, Y(f), NONE, T(f, 1));
    emit(f, IR_MOVE, X(f), NONE, T(f, 2));
}

static void test_propagate_source_overwritten(void)
{
    char *got = run(build_prop_source_overwritten, OPT_PROPAGATE);

    CHECK_TEXT(got, HDR IN("x = a") IN("y = a") IN("a = 9") IN("t1 = y") IN("t2 = x") END);
    free(got);
}

/* SEGURIDAD: lo que se sabe se descarta en cada bloque nuevo (etiqueta). */
static void build_prop_block_boundary(IrProgram *p, IrFunc *f)
{
    Operand l1 = ir_new_label(p);

    emit(f, IR_MOVE, ir_const(5), NONE, X(f));
    emit(f, IR_LABEL, l1, NONE, NONE);
    emit(f, IR_MOVE, X(f), NONE, T(f, 1));          /* podria venir de otro camino */
    emit(f, IR_RET, T(f, 1), NONE, NONE);
}

static void test_propagate_block_boundary(void)
{
    char *got = run(build_prop_block_boundary, OPT_PROPAGATE);

    /* x = 5 NO se propaga a t1 = x (hay una etiqueta en el medio); despues, dentro
     * del bloque nuevo, t1 es copia de x y el ret pasa a devolver x */
    CHECK_TEXT(got, HDR IN("x = 5") LB("L1:") IN("t1 = x") IN("ret x") END);
    free(got);
}

/* SEGURIDAD: una llamada puede cambiar las globales (no se propaga su valor a
 * traves de ella), pero no las locales. */
static void build_prop_call(IrProgram *p, IrFunc *f)
{
    emit(f, IR_MOVE, ir_const(3), NONE, G(p));
    emit(f, IR_MOVE, G(p), NONE, T(f, 1));          /* antes de la llamada: se propaga */
    emit(f, IR_MOVE, ir_const(4), NONE, X(f));
    emit(f, IR_CALL, ir_func("h"), ir_const(0), NONE);
    emit(f, IR_MOVE, G(p), NONE, T(f, 2));          /* despues: la global pudo cambiar */
    emit(f, IR_MOVE, X(f), NONE, T(f, 3));          /* la local no */
}

static void test_propagate_call(void)
{
    char *got = run(build_prop_call, OPT_PROPAGATE);

    CHECK_TEXT(got,
        HDR IN("@g = 3") IN("t1 = 3") IN("x = 4") IN("call h, 0") IN("t2 = @g")
            IN("t3 = 4") END);
    free(got);
}

/* Una copia de una global tambien se olvida en la llamada. */
static void build_prop_global_copy(IrProgram *p, IrFunc *f)
{
    emit(f, IR_MOVE, G(p), NONE, X(f));
    emit(f, IR_MOVE, X(f), NONE, T(f, 1));
    emit(f, IR_CALL, ir_func("h"), ir_const(0), NONE);
    emit(f, IR_MOVE, X(f), NONE, T(f, 2));
}

static void test_propagate_global_copy(void)
{
    char *got = run(build_prop_global_copy, OPT_PROPAGATE);

    CHECK_TEXT(got, HDR IN("x = @g") IN("t1 = @g") IN("call h, 0") IN("t2 = x") END);
    free(got);
}

/* Se propaga tambien a param, ret y a la condicion de un salto. */
static void build_prop_into_control(IrProgram *p, IrFunc *f)
{
    Operand l1 = ir_new_label(p);

    emit(f, IR_MOVE, ir_const(2), NONE, X(f));
    emit(f, IR_PARAM, X(f), NONE, NONE);
    emit(f, IR_JZ, X(f), l1, NONE);
    emit(f, IR_RET, X(f), NONE, NONE);
    emit(f, IR_LABEL, l1, NONE, NONE);
}

static void test_propagate_into_control(void)
{
    char *got = run(build_prop_into_control, OPT_PROPAGATE);

    /* el ret esta en un bloque nuevo (despues del salto): no se propaga */
    CHECK_TEXT(got, HDR IN("x = 2") IN("param 2") IN("jz 2, L1") IN("ret x") LB("L1:") END);
    free(got);
}

/* Una copia x = x no genera un hecho, y las expresiones no son hechos. */
static void build_prop_expression_results(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_MOVE, ir_const(5), NONE, X(f));
    emit(f, IR_ADD, X(f), ir_const(1), T(f, 1));
    emit(f, IR_MOVE, T(f, 1), NONE, X(f));
    emit(f, IR_MOVE, X(f), NONE, T(f, 2));          /* x es ahora copia de t1 */
    emit(f, IR_MOVE, X(f), NONE, Y(f));
}

static void test_propagate_expression_results(void)
{
    char *got = run(build_prop_expression_results, OPT_PROPAGATE);

    /* t1 = 5 + 1 es una expresion: no es un hecho, pero x = t1 si lo es */
    CHECK_TEXT(got, HDR IN("x = 5") IN("t1 = 5 + 1") IN("x = t1") IN("t2 = t1") IN("y = t1") END);
    free(got);
}

/* Si x es copia de a y se escribe a, tambien se olvida lo que dependia de x. */
static void build_prop_chain_kill(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_MOVE, A(f), NONE, X(f));
    emit(f, IR_MOVE, X(f), NONE, Y(f));
    emit(f, IR_MOVE, ir_const(9), NONE, A(f));
    emit(f, IR_MOVE, Y(f), NONE, T(f, 1));
}

static void test_propagate_chain_kill(void)
{
    char *got = run(build_prop_chain_kill, OPT_PROPAGATE);

    CHECK_TEXT(got, HDR IN("x = a") IN("y = a") IN("a = 9") IN("t1 = y") END);
    free(got);
}

/* SEGURIDAD: el valor de un resultado de cortocircuito que se une desde dos
 * caminos no se propaga a traves de la etiqueta de union. */
static void build_short_circuit(IrProgram *p, IrFunc *f)
{
    Operand l1 = ir_new_label(p), l2 = ir_new_label(p);

    emit(f, IR_MOVE, ir_const(1), NONE, T(f, 1));
    emit(f, IR_JZ, T(f, 1), l1, NONE);
    emit(f, IR_MOVE, ir_const(0), NONE, T(f, 2));
    emit(f, IR_JNZ, T(f, 2), l2, NONE);
    emit(f, IR_MOVE, G(p), NONE, T(f, 2));
    emit(f, IR_LABEL, l2, NONE, NONE);
    emit(f, IR_MOVE, T(f, 2), NONE, T(f, 1));
    emit(f, IR_LABEL, l1, NONE, NONE);
    emit(f, IR_MOVE, T(f, 1), NONE, T(f, 3));
    emit(f, IR_RET, T(f, 3), NONE, NONE);
}

static void test_propagate_short_circuit(void)
{
    char *got = run(build_short_circuit, OPT_PROPAGATE);

    /* la condicion de los saltos si pasa a ser constante, pero t1 en la union
     * (despues de L1) no se reemplaza por 1: puede venir de dos caminos. Dentro
     * del bloque de la union, t3 = t1 hace que el ret devuelva t1 */
    CHECK_TEXT(got,
        HDR IN("t1 = 1") IN("jz 1, L1") IN("t2 = 0") IN("jnz 0, L2") IN("t2 = @g")
            LB("L2:") IN("t1 = t2") LB("L1:") IN("t3 = t1") IN("ret t1") END);
    free(got);
}

/* ==================================================================== dce */

static void build_dce_chain(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_ADD, A(f), B(f), T(f, 1));
    emit(f, IR_MUL, T(f, 1), ir_const(2), T(f, 2));
    emit(f, IR_MOVE, T(f, 2), NONE, X(f));          /* x nunca se lee */
    emit(f, IR_RET, A(f), NONE, NONE);
}

static void test_dce_chain(void)
{
    OptStats st;
    char *got = optimize(build_dce_chain, OPT_DCE, &st, NULL);

    /* x no se lee, asi que t2 no se lee, asi que t1 no se lee: cae toda la cadena */
    CHECK_TEXT(got, HDR IN("ret a") END);
    CHECK(st.dead == 3 && st.before == 4 && st.after == 1);
    free(got);
}

static void build_dce_used(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_MOVE, ir_const(1), NONE, X(f));
    emit(f, IR_MOVE, X(f), NONE, T(f, 1));
    emit(f, IR_ADD, T(f, 1), B(f), T(f, 2));
    emit(f, IR_RET, T(f, 2), NONE, NONE);
}

static void test_dce_keeps_used(void)
{
    OptStats st;
    char *got = optimize(build_dce_used, OPT_DCE, &st, NULL);

    CHECK_TEXT(got, HDR IN("x = 1") IN("t1 = x") IN("t2 = t1 + b") IN("ret t2") END);
    CHECK(st.dead == 0);
    free(got);
}

/* SEGURIDAD: las escrituras a una global son observables y las llamadas tienen
 * efectos: no se eliminan (de la llamada solo se quita el resultado sin uso). */
static void build_dce_effects(IrProgram *p, IrFunc *f)
{
    emit(f, IR_MOVE, ir_const(5), NONE, G(p));
    emit(f, IR_CALL, ir_func("h"), ir_const(0), T(f, 1));
    emit(f, IR_PARAM, ir_const(2), NONE, NONE);
    emit(f, IR_CALL, ir_func("k"), ir_const(1), T(f, 2));
    emit(f, IR_RET, NONE, NONE, NONE);
}

static void test_dce_keeps_effects(void)
{
    char *got = run(build_dce_effects, OPT_DCE);

    CHECK_TEXT(got, HDR IN("@g = 5") IN("call h, 0") IN("param 2") IN("call k, 1") IN("ret") END);
    free(got);
}

/* SEGURIDAD: una division o un resto que podria dividir por cero no se elimina
 * aunque su resultado no se use; con divisor constante distinto de 0 si. */
static void build_dce_division(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_DIV, A(f), ir_const(0), T(f, 1));
    emit(f, IR_MOD, A(f), B(f), T(f, 2));
    emit(f, IR_DIV, A(f), ir_const(2), T(f, 3));
    emit(f, IR_MOD, A(f), ir_const(-1), T(f, 4));
    emit(f, IR_RET, NONE, NONE, NONE);
}

static void test_dce_division(void)
{
    char *got = run(build_dce_division, OPT_DCE);

    CHECK_TEXT(got, HDR IN("t1 = a / 0") IN("t2 = a % b") IN("ret") END);
    free(got);
}

static void build_dce_unreachable_after_ret(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_RET, A(f), NONE, NONE);
    emit(f, IR_MOVE, ir_const(1), NONE, X(f));
    emit(f, IR_RET, B(f), NONE, NONE);
}

static void test_dce_unreachable_after_return(void)
{
    char *got = run(build_dce_unreachable_after_ret, OPT_DCE);

    CHECK_TEXT(got, HDR IN("ret a") END);
    free(got);
}

/* Lo que sigue a un jmp hasta la proxima etiqueta es inalcanzable; lo que esta
 * detras de una etiqueta a la que se salta, no. */
static void build_dce_unreachable_after_jmp(IrProgram *p, IrFunc *f)
{
    Operand l1 = ir_new_label(p);

    emit(f, IR_JMP, l1, NONE, NONE);
    emit(f, IR_MOVE, ir_const(1), NONE, X(f));
    emit(f, IR_MOVE, ir_const(2), NONE, Y(f));
    emit(f, IR_LABEL, l1, NONE, NONE);
    emit(f, IR_RET, A(f), NONE, NONE);
}

static void test_dce_unreachable_after_jmp(void)
{
    char *got = run(build_dce_unreachable_after_jmp, OPT_DCE);

    CHECK_TEXT(got, HDR IN("jmp L1") LB("L1:") IN("ret a") END);
    free(got);
}

static void build_dce_reachable_label(IrProgram *p, IrFunc *f)
{
    Operand l1 = ir_new_label(p);

    emit(f, IR_JZ, A(f), l1, NONE);
    emit(f, IR_MOVE, ir_const(1), NONE, X(f));
    emit(f, IR_RET, X(f), NONE, NONE);
    emit(f, IR_LABEL, l1, NONE, NONE);
    emit(f, IR_RET, B(f), NONE, NONE);
}

static void test_dce_keeps_reachable_code(void)
{
    char *got = run(build_dce_reachable_label, OPT_DCE);

    CHECK_TEXT(got, HDR IN("jz a, L1") IN("x = 1") IN("ret x") LB("L1:") IN("ret b") END);
    free(got);
}

static void build_dce_self_and_param(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_MOVE, X(f), NONE, X(f));
    emit(f, IR_MOVE, ir_const(5), NONE, A(f));      /* se escribe un parametro que no se lee */
    emit(f, IR_RET, B(f), NONE, NONE);
}

static void test_dce_self_move_and_unused_param(void)
{
    char *got = run(build_dce_self_and_param, OPT_DCE);

    CHECK_TEXT(got, HDR IN("ret b") END);
    free(got);
}

/* ================================================================== jumps */

static void build_jumps_const_cond(IrProgram *p, IrFunc *f)
{
    Operand l1 = ir_new_label(p), l2 = ir_new_label(p), l3 = ir_new_label(p),
            l4 = ir_new_label(p);

    emit(f, IR_JZ, ir_const(0), l1, NONE);          /* siempre salta */
    emit(f, IR_JZ, ir_const(1), l2, NONE);          /* nunca salta */
    emit(f, IR_JNZ, ir_const(7), l3, NONE);         /* siempre salta */
    emit(f, IR_JNZ, ir_const(0), l4, NONE);         /* nunca salta */
    emit(f, IR_MOVE, ir_const(1), NONE, X(f));
    emit(f, IR_LABEL, l1, NONE, NONE);
    emit(f, IR_MOVE, ir_const(2), NONE, X(f));
    emit(f, IR_LABEL, l2, NONE, NONE);
    emit(f, IR_LABEL, l3, NONE, NONE);
    emit(f, IR_LABEL, l4, NONE, NONE);
    emit(f, IR_RET, X(f), NONE, NONE);
}

static void test_jumps_constant_conditions(void)
{
    OptStats st;
    char *got = optimize(build_jumps_const_cond, OPT_JUMPS, &st, NULL);

    /* jz 0 y jnz 7 siempre saltan y pasan a ser jmp; jz 1 y jnz 0 nunca saltan y
     * desaparecen; a las etiquetas L2 y L4 ya no salta nadie, asi que se eliminan */
    CHECK_TEXT(got,
        HDR IN("jmp L1") IN("jmp L3") IN("x = 1") LB("L1:") IN("x = 2") LB("L3:") IN("ret x") END);
    CHECK(st.jumps > 0);
    free(got);
}

static void build_jumps_to_next(IrProgram *p, IrFunc *f)
{
    Operand l1 = ir_new_label(p), l2 = ir_new_label(p), l3 = ir_new_label(p);

    emit(f, IR_JMP, l1, NONE, NONE);                /* salta a la instruccion siguiente */
    emit(f, IR_LABEL, l1, NONE, NONE);
    emit(f, IR_JMP, l3, NONE, NONE);                /* hay otra etiqueta en el medio */
    emit(f, IR_LABEL, l2, NONE, NONE);
    emit(f, IR_LABEL, l3, NONE, NONE);
    emit(f, IR_RET, A(f), NONE, NONE);
}

static void test_jumps_to_next_instruction(void)
{
    char *got = run(build_jumps_to_next, OPT_JUMPS);

    CHECK_TEXT(got, HDR IN("ret a") END);           /* sobran los saltos y las etiquetas */
    free(got);
}

static void build_jumps_conditional_to_next(IrProgram *p, IrFunc *f)
{
    Operand l1 = ir_new_label(p);

    emit(f, IR_JZ, A(f), l1, NONE);                 /* salte o no, sigue en la misma */
    emit(f, IR_LABEL, l1, NONE, NONE);
    emit(f, IR_RET, B(f), NONE, NONE);
}

static void test_jumps_conditional_to_next(void)
{
    char *got = run(build_jumps_conditional_to_next, OPT_JUMPS);

    CHECK_TEXT(got, HDR IN("ret b") END);
    free(got);
}

static void build_jumps_threading(IrProgram *p, IrFunc *f)
{
    Operand l1 = ir_new_label(p), l2 = ir_new_label(p);

    emit(f, IR_JZ, A(f), l1, NONE);
    emit(f, IR_RET, A(f), NONE, NONE);
    emit(f, IR_LABEL, l1, NONE, NONE);
    emit(f, IR_JMP, l2, NONE, NONE);                /* L1 solo salta a L2 */
    emit(f, IR_MOVE, ir_const(5), NONE, X(f));
    emit(f, IR_LABEL, l2, NONE, NONE);
    emit(f, IR_RET, B(f), NONE, NONE);
}

static void test_jumps_threading(void)
{
    char *got = run(build_jumps_threading, OPT_JUMPS);

    /* el salto condicional va directo a L2; L1 ya no la usa nadie */
    CHECK_TEXT(got,
        HDR IN("jz a, L2") IN("ret a") IN("jmp L2") IN("x = 5") LB("L2:") IN("ret b") END);
    free(got);
}

/* Lo que se necesita se conserva: etiquetas con saltos y un bucle. */
static void build_jumps_keeps_needed(IrProgram *p, IrFunc *f)
{
    Operand l1 = ir_new_label(p), l2 = ir_new_label(p);

    emit(f, IR_LABEL, l1, NONE, NONE);
    emit(f, IR_JZ, A(f), l2, NONE);
    emit(f, IR_MOVE, ir_const(1), NONE, X(f));
    emit(f, IR_JMP, l1, NONE, NONE);
    emit(f, IR_LABEL, l2, NONE, NONE);
    emit(f, IR_RET, B(f), NONE, NONE);
}

static void test_jumps_keeps_needed(void)
{
    OptStats st;
    char *got = optimize(build_jumps_keeps_needed, OPT_JUMPS, &st, NULL);

    CHECK_TEXT(got,
        HDR LB("L1:") IN("jz a, L2") IN("x = 1") IN("jmp L1") LB("L2:") IN("ret b") END);
    CHECK(st.jumps == 0);
    free(got);
}

/* BORDE: un salto a si mismo (bucle infinito) no hace que el optimizador se
 * quede dando vueltas siguiendo saltos. */
static void build_jumps_self_loop(IrProgram *p, IrFunc *f)
{
    Operand l1 = ir_new_label(p);

    emit(f, IR_LABEL, l1, NONE, NONE);
    emit(f, IR_JMP, l1, NONE, NONE);
    (void)f;
}

static void test_jumps_self_loop(void)
{
    char *got = run(build_jumps_self_loop, OPT_JUMPS);

    CHECK_TEXT(got, HDR LB("L1:") IN("jmp L1") END);
    free(got);
}

/* ================================================================== temps */

static void build_temps(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_ADD, A(f), B(f), T(f, 3));           /* se usan t3, t5 y t8 de 8 */
    emit(f, IR_MUL, T(f, 3), ir_const(2), T(f, 8));
    emit(f, IR_CALL, ir_func("h"), ir_const(0), T(f, 5));
    emit(f, IR_ADD, T(f, 8), T(f, 5), T(f, 8));
    emit(f, IR_RET, T(f, 8), NONE, NONE);
    T(f, 8);
}

static void test_temps_compaction(void)
{
    OptStats st;
    IrProgram *p;
    char *got = optimize(build_temps, OPT_TEMPS, &st, &p);

    /* t3 pasa a ser t1, t5 a t2 y t8 a t3: los numeros se compactan en orden */
    CHECK_TEXT(got,
        HDR IN("t1 = a + b") IN("t3 = t1 * 2") IN("t2 = call h, 0") IN("t3 = t3 + t2")
            IN("ret t3") END);
    CHECK(p->first->ntemps == 3);
    CHECK(st.temps_before == 8 && st.temps_after == 3);
    free(got);
    ir_program_free(p);
}

static void build_no_temps(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_MOVE, ir_const(1), NONE, X(f));
    emit(f, IR_RET, X(f), NONE, NONE);
}

static void test_temps_without_temporaries(void)
{
    OptStats st;
    char *got = optimize(build_no_temps, OPT_TEMPS, &st, NULL);

    CHECK_TEXT(got, HDR IN("x = 1") IN("ret x") END);
    CHECK(st.temps_before == 0 && st.temps_after == 0);
    free(got);
}

/* ======================================================= todas juntas / punto fijo */

/* x = 3 * 4; y = x + 0; if (y == 12) g = y; else g = 99; return y; */
static void build_combined(IrProgram *p, IrFunc *f)
{
    Operand l1 = ir_new_label(p), l2 = ir_new_label(p);

    emit(f, IR_MUL, ir_const(3), ir_const(4), T(f, 1));
    emit(f, IR_MOVE, T(f, 1), NONE, X(f));
    emit(f, IR_ADD, X(f), ir_const(0), T(f, 2));
    emit(f, IR_MOVE, T(f, 2), NONE, Y(f));
    emit(f, IR_EQ, Y(f), ir_const(12), T(f, 3));
    emit(f, IR_JZ, T(f, 3), l1, NONE);
    emit(f, IR_MOVE, Y(f), NONE, G(p));
    emit(f, IR_JMP, l2, NONE, NONE);
    emit(f, IR_LABEL, l1, NONE, NONE);
    emit(f, IR_MOVE, ir_const(99), NONE, G(p));
    emit(f, IR_LABEL, l2, NONE, NONE);
    emit(f, IR_RET, Y(f), NONE, NONE);
}

static void test_all_together(void)
{
    OptStats st;
    IrProgram *p;
    char *got = optimize(build_combined, OPT_ALL, &st, &p);

    /* las optimizaciones se ayudan entre si: se calcula todo en compilacion */
    CHECK_TEXT(got, HDR IN("@g = 12") IN("ret 12") END);
    CHECK(st.before == 12 && st.after == 2);
    CHECK(st.temps_before == 3 && st.temps_after == 0);
    CHECK(st.rounds > 1);                           /* hizo falta mas de una vuelta */
    free(got);
    ir_program_free(p);
}

/* t1 = 1 + 2; t2 = t1 + 3; ...; t99 = t98 + 100; return t99  (1+2+...+100) */
static void build_constant_chain(IrProgram *p, IrFunc *f)
{
    (void)p;
    emit(f, IR_ADD, ir_const(1), ir_const(2), T(f, 1));
    for (int i = 2; i <= 99; i++)
        emit(f, IR_ADD, T(f, i - 1), ir_const(i + 1), T(f, i));
    emit(f, IR_RET, T(f, 99), NONE, NONE);
}

/* Una cadena de 100 constantes se pliega entera, y en pocas vueltas: la
 * propagacion pliega en el momento (con un tope fijo de 50 vueltas, una vuelta
 * por eslabon, quedaba a medias). */
static void test_constant_chain(void)
{
    OptStats st;
    IrProgram *p;
    char *got = optimize(build_constant_chain, OPT_ALL, &st, &p);

    CHECK_TEXT(got, HDR IN("ret 5050") END);        /* 100 * 101 / 2 */
    CHECK(st.before == 100 && st.after == 1);
    CHECK(st.temps_before == 99 && st.temps_after == 0);
    CHECK(st.rounds <= 3);
    free(got);
    ir_program_free(p);
}

/* Cada optimizacion por separado hace menos que todas juntas. */
static void test_passes_are_independent(void)
{
    char *none = run(build_combined, 0);
    char *only_fold = run(build_combined, OPT_FOLD);
    char *only_dce = run(build_combined, OPT_DCE);

    /* sin optimizaciones el codigo no cambia */
    CHECK_TEXT(none,
        HDR IN("t1 = 3 * 4") IN("x = t1") IN("t2 = x + 0") IN("y = t2")
            IN("t3 = y == 12") IN("jz t3, L1") IN("@g = y") IN("jmp L2") LB("L1:")
            IN("@g = 99") LB("L2:") IN("ret y") END);
    /* solo fold: calcula las operaciones con constantes pero no propaga */
    CHECK_TEXT(only_fold,
        HDR IN("t1 = 12") IN("x = t1") IN("t2 = x") IN("y = t2") IN("t3 = y == 12")
            IN("jz t3, L1") IN("@g = y") IN("jmp L2") LB("L1:") IN("@g = 99") LB("L2:")
            IN("ret y") END);
    /* solo dce: no hay nada muerto todavia */
    CHECK_TEXT(only_dce, none);
    free(none);
    free(only_fold);
    free(only_dce);
}

/* El cortocircuito completo: se simplifica sin romperse. */
static void test_short_circuit_all(void)
{
    char *got = run(build_short_circuit, OPT_ALL);

    /* t1 = 1; jz 1 no salta; t2 = 0; jnz 0 no salta; t2 = @g; t1 = t2; t3 = t1; ret t3 */
    CHECK_TEXT(got, HDR IN("ret @g") END);
    free(got);
}

/* Optimizar un programa ya optimizado no lo cambia (idempotencia). */
static void test_idempotence(void)
{
    static const build_fn builds[] = {
        build_combined, build_short_circuit, build_fold_algebra, build_dce_effects,
        build_jumps_keeps_needed, build_prop_call, build_temps,
    };

    for (size_t k = 0; k < sizeof builds / sizeof builds[0]; k++) {
        OptStats st1, st2;
        IrProgram *p;
        char *first = optimize(builds[k], OPT_ALL, &st1, &p);
        char *second;

        opt_run(p, OPT_ALL, NULL, &st2);
        second = render(p);
        CHECK_TEXT(second, first);
        CHECK(st2.folded == 0 && st2.propagated == 0 && st2.dead == 0 && st2.jumps == 0);
        CHECK(st2.before == st2.after);
        free(first);
        free(second);
        ir_program_free(p);
    }
}

/* ================================================== nombres y estadisticas */

static void test_names(void)
{
    CHECK(opt_from_name("fold") == OPT_FOLD);
    CHECK(opt_from_name("propagate") == OPT_PROPAGATE);
    CHECK(opt_from_name("dce") == OPT_DCE);
    CHECK(opt_from_name("jumps") == OPT_JUMPS);
    CHECK(opt_from_name("temps") == OPT_TEMPS);
    CHECK(opt_from_name("all") == OPT_ALL);
    CHECK(opt_from_name("nope") == 0 && opt_from_name("") == 0 && opt_from_name("FOLD") == 0);
    /* los bits son distintos y `all` los tiene a todos */
    CHECK((OPT_FOLD | OPT_PROPAGATE | OPT_DCE | OPT_JUMPS | OPT_TEMPS) == OPT_ALL);
    CHECK((OPT_FOLD & OPT_PROPAGATE) == 0 && (OPT_DCE & OPT_JUMPS) == 0);
    CHECK(strstr(opt_names(), "fold") != NULL && strstr(opt_names(), "all") != NULL);
}

/* El informe por metodo y los totales de varios metodos. */
static void test_report_and_totals(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *f = ir_add_func(p, "uno", 1), *g = ir_add_func(p, "dos", 1);
    OptStats st;
    char *buf = NULL;
    size_t len = 0;
    FILE *mem = open_memstream(&buf, &len);

    emit(f, IR_ADD, ir_const(1), ir_const(2), T(f, 1));
    emit(f, IR_RET, T(f, 1), NONE, NONE);
    emit(g, IR_RET, ir_const(0), NONE, NONE);
    opt_run(p, OPT_ALL, mem, &st);
    fclose(mem);
    CHECK(strcmp(buf,
        "[opt] uno: 2 -> 1 instructions, 1 -> 0 temporaries\n"
        "[opt] dos: 1 -> 1 instructions, 0 -> 0 temporaries\n") == 0);
    CHECK(st.before == 3 && st.after == 2 && st.temps_before == 1 && st.temps_after == 0);
    free(buf);
    ir_program_free(p);
}

int main(void)
{
    test_fold_constants();
    test_fold_wraparound();
    test_fold_division_by_zero();
    test_fold_algebra();
    test_fold_untouched();

    test_propagate_constants();
    test_propagate_copies();
    test_propagate_redefinition();
    test_propagate_source_overwritten();
    test_propagate_block_boundary();
    test_propagate_call();
    test_propagate_global_copy();
    test_propagate_into_control();
    test_propagate_expression_results();
    test_propagate_chain_kill();
    test_propagate_short_circuit();

    test_dce_chain();
    test_dce_keeps_used();
    test_dce_keeps_effects();
    test_dce_division();
    test_dce_unreachable_after_return();
    test_dce_unreachable_after_jmp();
    test_dce_keeps_reachable_code();
    test_dce_self_move_and_unused_param();

    test_jumps_constant_conditions();
    test_jumps_to_next_instruction();
    test_jumps_conditional_to_next();
    test_jumps_threading();
    test_jumps_keeps_needed();
    test_jumps_self_loop();

    test_temps_compaction();
    test_temps_without_temporaries();

    test_all_together();
    test_constant_chain();
    test_passes_are_independent();
    test_short_circuit_all();
    test_idempotence();

    test_names();
    test_report_and_totals();

    printf("test_opt: %d comprobaciones, %d fallaron\n", checks, failures);
    return failures != 0;
}
