/*
 * test_flow.c - Tests unitarios del analisis de flujo (src/flow.c): que
 * operandos lee y escribe cada instruccion, y los bloques basicos y el grafo de
 * flujo de control de un metodo. Se compila y ejecuta con `make test`.
 *
 * Los metodos se arman con la API del codigo intermedio (ir.h) y los bloques y
 * sucesores esperados se calcularon a mano mirando el codigo.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "flow.h"
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

/* ------------------------------------------- operandos de una instruccion */

/* Instruccion con operandos reconocibles: a = 7, b = 8, result = temp 9. */
static Instr make(IrOp op)
{
    Instr in;

    in.op = op;
    in.a = ir_const(7);
    in.b = ir_const(8);
    in.result = ir_const(0);
    return in;
}

/* Las operaciones aritmeticas y las comparaciones leen a y b y escriben result. */
static void test_binary_instructions(void)
{
    static const IrOp ops[] = { IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MOD, IR_LT,
                                IR_GT, IR_EQ };

    for (size_t k = 0; k < sizeof ops / sizeof ops[0]; k++) {
        Instr in = make(ops[k]);
        Operand *r[2];

        CHECK(instr_reads(&in, r) == 2);
        CHECK(r[0] == &in.a && r[1] == &in.b);      /* punteros a los campos */
        CHECK(instr_write(&in) == &in.result);
    }
}

/* MOVE, NEG y NOT leen solo a. */
static void test_unary_instructions(void)
{
    static const IrOp ops[] = { IR_MOVE, IR_NEG, IR_NOT };

    for (size_t k = 0; k < sizeof ops / sizeof ops[0]; k++) {
        Instr in = make(ops[k]);
        Operand *r[2];

        CHECK(instr_reads(&in, r) == 1 && r[0] == &in.a);
        CHECK(instr_write(&in) == &in.result);
    }
}

/* Los saltos condicionales y param leen a pero no escriben; jmp y label no leen. */
static void test_control_instructions(void)
{
    Instr jz = make(IR_JZ), jnz = make(IR_JNZ), pr = make(IR_PARAM);
    Instr jmp = make(IR_JMP), lbl = make(IR_LABEL);
    Operand *r[2];

    CHECK(instr_reads(&jz, r) == 1 && r[0] == &jz.a && instr_write(&jz) == NULL);
    CHECK(instr_reads(&jnz, r) == 1 && r[0] == &jnz.a && instr_write(&jnz) == NULL);
    CHECK(instr_reads(&pr, r) == 1 && r[0] == &pr.a && instr_write(&pr) == NULL);
    /* la etiqueta destino no es un valor que se lea */
    CHECK(instr_reads(&jmp, r) == 0 && instr_write(&jmp) == NULL);
    CHECK(instr_reads(&lbl, r) == 0 && instr_write(&lbl) == NULL);
}

/* ret lee su valor solo si lo tiene. */
static void test_return_and_call(void)
{
    Instr ret = make(IR_RET), ret_void = make(IR_RET), call = make(IR_CALL);
    Operand *r[2];

    CHECK(instr_reads(&ret, r) == 1 && r[0] == &ret.a && instr_write(&ret) == NULL);
    ret_void.a = NONE;
    CHECK(instr_reads(&ret_void, r) == 0);
    /* call: el nombre del metodo y la cantidad de argumentos no son valores; solo
     * escribe si tiene resultado */
    call.result = NONE;
    CHECK(instr_reads(&call, r) == 0 && instr_write(&call) == NULL);
    call.result = ir_const(5);
    CHECK(instr_write(&call) == &call.result);
}

/* is_location, same_location e instr_is_jump. */
static void test_helpers(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *f = ir_add_func(p, "f", 0);
    IrFunc *g = ir_add_func(p, "g", 0);
    IrVar *x = ir_add_local(f, "x"), *y = ir_add_local(f, "y");
    IrVar *glob = ir_add_global(p, "g");
    Operand t1 = ir_new_temp(f), t2 = ir_new_temp(f), t1b = ir_new_temp(g);

    CHECK(is_location(ir_var(x)) && is_location(t1) && is_location(ir_var(glob)));
    CHECK(!is_location(ir_const(1)) && !is_location(NONE));
    CHECK(!is_location(ir_new_label(p)) && !is_location(ir_func("f")));

    CHECK(same_location(ir_var(x), ir_var(x)));
    CHECK(!same_location(ir_var(x), ir_var(y)));
    CHECK(same_location(t1, t1) && !same_location(t1, t2));
    CHECK(same_location(t1, t1b));                  /* mismo numero: t1 */
    CHECK(!same_location(ir_var(x), t1));           /* variable contra temporal */
    CHECK(!same_location(ir_const(3), ir_const(3)));    /* una constante no es ubicacion */

    {
        Instr a = make(IR_JMP), b = make(IR_JZ), c = make(IR_JNZ), d = make(IR_ADD),
              e = make(IR_RET), l = make(IR_LABEL);

        CHECK(instr_is_jump(&a) && instr_is_jump(&b) && instr_is_jump(&c));
        CHECK(!instr_is_jump(&d) && !instr_is_jump(&e) && !instr_is_jump(&l));
    }
    ir_program_free(p);
}

/* ----------------------------------------------------- bloques y grafo */

/* Comprueba un bloque: primera y ultima instruccion y sucesores. */
static int block_is(const Cfg *c, int b, int first, int last, int nsucc, int s0, int s1)
{
    const Block *k = &c->blocks[b];

    if (k->first != first || k->last != last || k->nsucc != nsucc)
        return 0;
    if (nsucc >= 1 && k->succ[0] != s0)
        return 0;
    if (nsucc >= 2 && k->succ[1] != s1)
        return 0;
    return 1;
}

/* Un metodo vacio no tiene bloques. */
static void test_empty_function(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *f = ir_add_func(p, "f", 0);
    Cfg *c = cfg_build(f);

    CHECK(c->nblocks == 0);
    cfg_free(c);
    cfg_free(NULL);                                 /* acepta NULL */
    ir_program_free(p);
}

/* Codigo sin saltos: un solo bloque. */
static void test_straight_line(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *f = ir_add_func(p, "f", 0);
    Operand t = ir_new_temp(f);
    Cfg *c;

    ir_emit(f, IR_MOVE, ir_const(1), NONE, t);              /* 0 */
    ir_emit(f, IR_ADD, t, ir_const(2), t);                  /* 1 */
    ir_emit(f, IR_RET, t, NONE, NONE);                      /* 2 */
    c = cfg_build(f);
    CHECK(c->nblocks == 1);
    CHECK(block_is(c, 0, 0, 2, 0, 0, 0));                   /* ret: sin sucesores */
    cfg_free(c);
    ir_program_free(p);
}

/*   0: jz c, L1        bloque 0 (0..0)   -> L1 (bloque 2) y el siguiente (1)
 *   1: x = 1           bloque 1 (1..2)   -> 3
 *   2: jmp L2
 *   3: L1:             bloque 2 (3..4)   -> 3
 *   4: x = 2
 *   5: L2:             bloque 3 (5..5)   -> sin sucesores
 *   6: ret                                  (la etiqueta y el ret van juntos)
 */
static void test_if_else(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *f = ir_add_func(p, "f", 0);
    IrVar *x = ir_add_local(f, "x"), *cnd = ir_add_local(f, "c");
    Operand l1 = ir_new_label(p), l2 = ir_new_label(p);
    Cfg *c;

    ir_emit(f, IR_JZ, ir_var(cnd), l1, NONE);               /* 0 */
    ir_emit(f, IR_MOVE, ir_const(1), NONE, ir_var(x));      /* 1 */
    ir_emit(f, IR_JMP, l2, NONE, NONE);                     /* 2 */
    ir_emit(f, IR_LABEL, l1, NONE, NONE);                   /* 3 */
    ir_emit(f, IR_MOVE, ir_const(2), NONE, ir_var(x));      /* 4 */
    ir_emit(f, IR_LABEL, l2, NONE, NONE);                   /* 5 */
    ir_emit(f, IR_RET, NONE, NONE, NONE);                   /* 6 */
    c = cfg_build(f);
    CHECK(c->nblocks == 4);
    CHECK(block_is(c, 0, 0, 0, 2, 2, 1));                   /* destino del jz, y la caida */
    CHECK(block_is(c, 1, 1, 2, 1, 3, 0));                   /* el jmp va a L2 */
    CHECK(block_is(c, 2, 3, 4, 1, 3, 0));                   /* cae en L2 */
    CHECK(block_is(c, 3, 5, 6, 0, 0, 0));                   /* termina en ret */
    cfg_free(c);
    ir_program_free(p);
}

/*   0: L1:             bloque 0 (0..1)  -> 1 (cuerpo) y 2 (salida)
 *   1: jz c, L2
 *   2: x = 1           bloque 1 (2..3)  -> 0 (vuelve)
 *   3: jmp L1
 *   4: L2:             bloque 2 (4..5)
 *   5: ret
 */
static void test_while_loop(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *f = ir_add_func(p, "f", 0);
    IrVar *x = ir_add_local(f, "x"), *cnd = ir_add_local(f, "c");
    Operand l1 = ir_new_label(p), l2 = ir_new_label(p);
    Cfg *c;

    ir_emit(f, IR_LABEL, l1, NONE, NONE);                   /* 0 */
    ir_emit(f, IR_JZ, ir_var(cnd), l2, NONE);               /* 1 */
    ir_emit(f, IR_MOVE, ir_const(1), NONE, ir_var(x));      /* 2 */
    ir_emit(f, IR_JMP, l1, NONE, NONE);                     /* 3 */
    ir_emit(f, IR_LABEL, l2, NONE, NONE);                   /* 4 */
    ir_emit(f, IR_RET, NONE, NONE, NONE);                   /* 5 */
    c = cfg_build(f);
    CHECK(c->nblocks == 3);
    CHECK(block_is(c, 0, 0, 1, 2, 2, 1));
    CHECK(block_is(c, 1, 2, 3, 1, 0, 0));                   /* el salto vuelve al principio */
    CHECK(block_is(c, 2, 4, 5, 0, 0, 0));
    cfg_free(c);
    ir_program_free(p);
}

/* Lo que sigue a un ret es otro bloque (inalcanzable): empieza en la instruccion
 * siguiente aunque no haya una etiqueta. */
static void test_block_after_return(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *f = ir_add_func(p, "f", 1);
    Cfg *c;

    ir_emit(f, IR_RET, ir_const(1), NONE, NONE);            /* 0 */
    ir_emit(f, IR_RET, ir_const(2), NONE, NONE);            /* 1 */
    c = cfg_build(f);
    CHECK(c->nblocks == 2);
    CHECK(block_is(c, 0, 0, 0, 0, 0, 0));
    CHECK(block_is(c, 1, 1, 1, 0, 0, 0));
    cfg_free(c);
    ir_program_free(p);
}

/* Dos etiquetas seguidas son bloques distintos, y un metodo que termina en una
 * etiqueta cae en un bloque sin salida propia. */
static void test_consecutive_labels_and_final_label(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *f = ir_add_func(p, "f", 0);
    Operand l1 = ir_new_label(p), l2 = ir_new_label(p);
    Cfg *c;

    ir_emit(f, IR_JMP, l2, NONE, NONE);                     /* 0 */
    ir_emit(f, IR_LABEL, l1, NONE, NONE);                   /* 1 */
    ir_emit(f, IR_LABEL, l2, NONE, NONE);                   /* 2 */
    c = cfg_build(f);
    CHECK(c->nblocks == 3);
    CHECK(block_is(c, 0, 0, 0, 1, 2, 0));                   /* jmp a L2 (bloque 2) */
    CHECK(block_is(c, 1, 1, 1, 1, 2, 0));                   /* L1 cae en L2 */
    CHECK(block_is(c, 2, 2, 2, 0, 0, 0));                   /* el ultimo: sin siguiente */
    cfg_free(c);
    ir_program_free(p);
}

/* cfg_print escribe una linea por bloque. */
static void test_print(void)
{
    IrProgram *p = ir_program_new();
    IrFunc *f = ir_add_func(p, "f", 0);
    Operand l1 = ir_new_label(p);
    Cfg *c;
    char *buf = NULL;
    size_t len = 0;
    FILE *mem;

    ir_emit(f, IR_JMP, l1, NONE, NONE);
    ir_emit(f, IR_LABEL, l1, NONE, NONE);
    ir_emit(f, IR_RET, NONE, NONE, NONE);
    c = cfg_build(f);
    mem = open_memstream(&buf, &len);
    cfg_print(mem, f, c);
    fclose(mem);
    CHECK(strcmp(buf,
        "block 0: instructions 0..0, first: jmp L1 -> 1\n"
        "block 1: instructions 1..2, first: L1: -> (exit)\n") == 0);
    if (strcmp(buf, "block 0: instructions 0..0, first: jmp L1 -> 1\n"
                    "block 1: instructions 1..2, first: L1: -> (exit)\n") != 0)
        printf("%s", buf);
    free(buf);
    cfg_free(c);
    ir_program_free(p);
}

int main(void)
{
    test_binary_instructions();
    test_unary_instructions();
    test_control_instructions();
    test_return_and_call();
    test_helpers();
    test_empty_function();
    test_straight_line();
    test_if_else();
    test_while_loop();
    test_block_after_return();
    test_consecutive_labels_and_final_label();
    test_print();

    printf("test_flow: %d comprobaciones, %d fallaron\n", checks, failures);
    return failures != 0;
}
