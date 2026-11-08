/*
 * test_ir.c - Tests unitarios del codigo intermedio (src/ir.c): operandos,
 * temporales y etiquetas, nombres unicos de variables, instrucciones e
 * impresion. Se compila y ejecuta con `make test`.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Devuelve el programa impreso en una cadena (hay que liberarla). */
static char *print_to_string(const IrProgram *p)
{
    char *buf = NULL;
    size_t len = 0;
    FILE *mem = open_memstream(&buf, &len);

    ir_print(mem, p);
    fclose(mem);
    return buf;
}

static int prints_as(const IrProgram *p, const char *expected)
{
    char *got = print_to_string(p);
    int same = strcmp(got, expected) == 0;

    if (!same)
        printf("  --- obtenido:\n%s  --- esperado:\n%s", got, expected);
    free(got);
    return same;
}

/* Constructores de operandos. */
static void test_operands(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *f = ir_add_func(p, "f", 1);
    IrVar *v = ir_add_local(f, "v");

    CHECK(ir_none().kind == OPD_NONE);
    CHECK(ir_const(42).kind == OPD_CONST && ir_const(42).value == 42);
    CHECK(ir_const(-7).value == -7);
    CHECK(ir_var(v).kind == OPD_VAR && ir_var(v).var == v);
    CHECK(ir_func("f").kind == OPD_FUNC && strcmp(ir_func("f").name, "f") == 0);
    ir_program_free(p);
}

/* Los temporales se numeran por metodo y las etiquetas en todo el programa. */
static void test_temps_and_labels(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *f = ir_add_func(p, "f", 0), *g = ir_add_func(p, "g", 0);
    Operand t1 = ir_new_temp(f), t2 = ir_new_temp(f), u1 = ir_new_temp(g);
    Operand l1 = ir_new_label(p), l2 = ir_new_label(p);

    CHECK(t1.kind == OPD_TEMP && t1.value == 1 && t2.value == 2);
    CHECK(u1.value == 1);                           /* g empieza de nuevo en t1 */
    CHECK(f->ntemps == 2 && g->ntemps == 1);
    CHECK(l1.kind == OPD_LABEL && l1.value == 1 && l2.value == 2);
    CHECK(p->nlabels == 2);
    CHECK(ir_new_label(p).value == 3);              /* sigue en todo el programa */
    ir_program_free(p);
}

/* Variables: orden, clase e indice. */
static void test_variables(void)
{
    IrProgram *p = ir_program_new();
    IrVar *g0 = ir_add_global(p, "g0"), *g1 = ir_add_global(p, "g1");
    IrFunc *f = ir_add_func(p, "f", 1);
    IrVar *a = ir_add_param(f, "a"), *b = ir_add_param(f, "b");
    IrVar *x = ir_add_local(f, "x"), *y = ir_add_local(f, "y");

    CHECK(p->nglobals == 2 && p->globals[0] == g0 && p->globals[1] == g1);
    CHECK(g0->is_global && g0->index == 0 && g1->index == 1);
    CHECK(!g0->is_param && strcmp(g1->name, "g1") == 0);
    CHECK(f->nparams == 2 && f->params[0] == a && f->params[1] == b);
    CHECK(!a->is_global && a->is_param && a->index == 0 && b->index == 1);
    CHECK(f->nlocals == 2 && f->locals[0] == x && f->locals[1] == y);
    CHECK(!x->is_global && !x->is_param && x->index == 0 && y->index == 1);
    ir_program_free(p);
}

/* Nombres unicos dentro de un metodo (x, x.2, x.3, ...). */
static void test_unique_names(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *f = ir_add_func(p, "f", 0), *g = ir_add_func(p, "g", 0);
    IrVar *p1 = ir_add_param(f, "x");
    IrVar *l1 = ir_add_local(f, "x");               /* choca con el parametro */
    IrVar *l2 = ir_add_local(f, "x");
    IrVar *l3 = ir_add_local(f, "x.2");             /* "x.2" ya esta ocupado */
    IrVar *l4 = ir_add_local(f, "y");
    IrVar *other = ir_add_local(g, "x");            /* otro metodo: no choca */

    CHECK(strcmp(p1->name, "x") == 0);
    CHECK(strcmp(l1->name, "x.2") == 0);
    CHECK(strcmp(l2->name, "x.3") == 0);
    CHECK(strcmp(l3->name, "x.2.2") == 0);
    CHECK(strcmp(l4->name, "y") == 0);
    CHECK(strcmp(other->name, "x") == 0);
    /* una global con el mismo nombre no cuenta: se escribe con '@' */
    CHECK(strcmp(ir_add_global(p, "x")->name, "x") == 0);
    ir_program_free(p);
}

/* ir_emit guarda los campos y los arreglos crecen sin perder datos. */
static void test_emit_and_growth(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *f = ir_add_func(p, "f", 0);
    char name[16];

    ir_emit(f, IR_ADD, ir_const(1), ir_const(2), ir_new_temp(f));
    CHECK(f->ncode == 1 && f->code[0].op == IR_ADD);
    CHECK(f->code[0].a.value == 1 && f->code[0].b.value == 2 &&
          f->code[0].result.kind == OPD_TEMP);

    for (int i = 0; i < 1000; i++)
        ir_emit(f, IR_MOVE, ir_const(i), ir_none(), ir_new_temp(f));
    CHECK(f->ncode == 1001 && f->ntemps == 1001);
    CHECK(f->code[1].a.value == 0 && f->code[1000].a.value == 999);

    for (int i = 0; i < 100; i++) {
        snprintf(name, sizeof name, "v%d", i);
        ir_add_local(f, name);
        ir_add_global(p, name);
    }
    CHECK(f->nlocals == 100 && p->nglobals == 100);
    CHECK(strcmp(f->locals[99]->name, "v99") == 0 && f->locals[99]->index == 99);
    CHECK(strcmp(p->globals[42]->name, "v42") == 0);
    ir_program_free(p);
}

/* Formato de texto de todas las instrucciones. */
static void test_print_all_instructions(void)
{
    IrProgram *p = ir_program_new();
    IrVar *g = ir_add_global(p, "g");
    IrFunc *f = ir_add_func(p, "f", 1);
    IrVar *a = ir_add_param(f, "a"), *b = ir_add_param(f, "b");
    IrVar *x = ir_add_local(f, "x");
    Operand t1 = ir_new_temp(f), t2 = ir_new_temp(f), t3 = ir_new_temp(f);
    Operand l1 = ir_new_label(p);
    IrFunc *m = ir_add_func(p, "main", 0);

    ir_emit(f, IR_ADD, ir_var(a), ir_var(b), t1);
    ir_emit(f, IR_SUB, t1, ir_const(1), t2);
    ir_emit(f, IR_MUL, t2, ir_var(g), t3);
    ir_emit(f, IR_DIV, t3, ir_const(2), t1);
    ir_emit(f, IR_MOD, t1, ir_const(3), t2);
    ir_emit(f, IR_NEG, t2, ir_none(), t3);
    ir_emit(f, IR_NOT, t3, ir_none(), t1);
    ir_emit(f, IR_LT, t1, ir_const(0), t2);
    ir_emit(f, IR_GT, t1, ir_const(0), t2);
    ir_emit(f, IR_EQ, t1, ir_const(0), t2);
    ir_emit(f, IR_MOVE, t2, ir_none(), ir_var(x));
    ir_emit(f, IR_MOVE, ir_const(-5), ir_none(), ir_var(g));
    ir_emit(f, IR_JZ, t2, l1, ir_none());
    ir_emit(f, IR_JNZ, ir_var(x), l1, ir_none());
    ir_emit(f, IR_JMP, l1, ir_none(), ir_none());
    ir_emit(f, IR_LABEL, l1, ir_none(), ir_none());
    ir_emit(f, IR_PARAM, ir_var(a), ir_none(), ir_none());
    ir_emit(f, IR_PARAM, ir_const(7), ir_none(), ir_none());
    ir_emit(f, IR_CALL, ir_func("main"), ir_const(2), t1);
    ir_emit(f, IR_CALL, ir_func("main"), ir_const(0), ir_none());
    ir_emit(f, IR_RET, t1, ir_none(), ir_none());
    ir_emit(m, IR_RET, ir_none(), ir_none(), ir_none());

    CHECK(prints_as(p,
        "global @g\n"
        "\n"
        "func f(a, b)\n"
        "  local x\n"
        "  t1 = a + b\n"
        "  t2 = t1 - 1\n"
        "  t3 = t2 * @g\n"
        "  t1 = t3 / 2\n"
        "  t2 = t1 % 3\n"
        "  t3 = - t2\n"
        "  t1 = ! t3\n"
        "  t2 = t1 < 0\n"
        "  t2 = t1 > 0\n"
        "  t2 = t1 == 0\n"
        "  x = t2\n"
        "  @g = -5\n"
        "  jz t2, L1\n"
        "  jnz x, L1\n"
        "  jmp L1\n"
        "L1:\n"
        "  param a\n"
        "  param 7\n"
        "  t1 = call main, 2\n"
        "  call main, 0\n"
        "  ret t1\n"
        "end\n"
        "\n"
        "func main()\n"
        "  ret\n"
        "end\n"));
    ir_program_free(p);
}

/* Sin globales el archivo empieza con el primer metodo; un metodo vacio. */
static void test_print_layout(void)
{
    IrProgram *p = ir_program_new();

    CHECK(prints_as(p, ""));                        /* programa vacio */
    ir_add_func(p, "a", 0);
    ir_add_func(p, "b", 1);
    CHECK(prints_as(p, "func a()\nend\n\nfunc b()\nend\n"));
    ir_add_global(p, "g");
    CHECK(prints_as(p, "global @g\n\nfunc a()\nend\n\nfunc b()\nend\n"));
    ir_program_free(p);
}

static void test_free_null(void)
{
    ir_program_free(NULL);                          /* acepta NULL */
    CHECK(1);
}

int main(void)
{
    test_operands();
    test_temps_and_labels();
    test_variables();
    test_unique_names();
    test_emit_and_growth();
    test_print_all_instructions();
    test_print_layout();
    test_free_null();

    printf("test_ir: %d comprobaciones, %d fallaron\n", checks, failures);
    return failures != 0;
}
