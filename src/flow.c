/* flow.c - Analisis del flujo del codigo intermedio (ver flow.h). */
#include <stdlib.h>

#include "flow.h"

/* ---------------------------------------------- operandos de una instruccion */

int instr_reads(Instr *in, Operand *out[2])
{
    switch (in->op) {
    case IR_MOVE:
    case IR_NEG:
    case IR_NOT:
    case IR_JZ:
    case IR_JNZ:
    case IR_PARAM:
        out[0] = &in->a;
        return 1;
    case IR_RET:
        if (in->a.kind == OPD_NONE)
            return 0;
        out[0] = &in->a;
        return 1;
    case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_MOD:
    case IR_LT: case IR_GT: case IR_EQ:
        out[0] = &in->a;
        out[1] = &in->b;
        return 2;
    case IR_LABEL:
    case IR_JMP:
    case IR_CALL:
        return 0;
    }
    return 0;
}

Operand *instr_write(Instr *in)
{
    switch (in->op) {
    case IR_MOVE:
    case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_MOD:
    case IR_NEG: case IR_NOT:
    case IR_LT: case IR_GT: case IR_EQ:
        return &in->result;
    case IR_CALL:
        return in->result.kind == OPD_NONE ? NULL : &in->result;
    default:
        return NULL;
    }
}

int is_location(Operand o)
{
    return o.kind == OPD_VAR || o.kind == OPD_TEMP;
}

int same_location(Operand a, Operand b)
{
    if (a.kind != b.kind)
        return 0;
    if (a.kind == OPD_TEMP)
        return a.value == b.value;
    return a.kind == OPD_VAR && a.var == b.var;
}

int instr_is_jump(const Instr *in)
{
    return in->op == IR_JMP || in->op == IR_JZ || in->op == IR_JNZ;
}

/* --------------------------------------------- bloques y grafo de control */

/* Numero de etiqueta mas alto que aparece en el codigo del metodo. */
static int max_label(const IrFunc *f)
{
    long long max = 0;

    for (int i = 0; i < f->ncode; i++) {
        const Instr *in = &f->code[i];

        if (in->op == IR_LABEL || in->op == IR_JMP) {
            if (in->a.value > max)
                max = in->a.value;
        } else if (in->op == IR_JZ || in->op == IR_JNZ) {
            if (in->b.value > max)
                max = in->b.value;
        }
    }
    return (int)max;
}

Cfg *cfg_build(const IrFunc *f)
{
    Cfg *cfg = malloc(sizeof *cfg);
    char *leader;
    int *label_block, *block_of;
    int n = f->ncode, nlabels = max_label(f), nb = 0;

    if (!cfg)
        exit(2);
    cfg->blocks = NULL;
    cfg->nblocks = 0;
    if (n == 0)
        return cfg;

    leader = calloc((size_t)n + 1, 1);
    block_of = malloc((size_t)n * sizeof *block_of);
    label_block = malloc(((size_t)nlabels + 1) * sizeof *label_block);
    if (!leader || !block_of || !label_block)
        exit(2);

    leader[0] = 1;
    for (int i = 0; i < n; i++) {
        const Instr *in = &f->code[i];

        if (in->op == IR_LABEL)
            leader[i] = 1;
        if ((instr_is_jump(in) || in->op == IR_RET) && i + 1 < n)
            leader[i + 1] = 1;
    }
    for (int i = 0; i < n; i++)
        if (leader[i])
            nb++;
    cfg->blocks = malloc((size_t)nb * sizeof *cfg->blocks);
    if (!cfg->blocks)
        exit(2);

    /* Bloques y a que bloque pertenece cada instruccion. */
    for (int i = 0, b = -1; i < n; i++) {
        if (leader[i]) {
            b++;
            cfg->blocks[b].first = i;
            cfg->blocks[b].nsucc = 0;
        }
        cfg->blocks[b].last = i;
        block_of[i] = b;
    }
    cfg->nblocks = nb;

    /* Etiqueta -> bloque donde se define. */
    for (int l = 0; l <= nlabels; l++)
        label_block[l] = -1;
    for (int i = 0; i < n; i++)
        if (f->code[i].op == IR_LABEL)
            label_block[f->code[i].a.value] = block_of[i];

    /* Sucesores segun la ultima instruccion de cada bloque. */
    for (int b = 0; b < nb; b++) {
        Block *blk = &cfg->blocks[b];
        const Instr *last = &f->code[blk->last];
        int has_next = b + 1 < nb;

        switch (last->op) {
        case IR_JMP:
            blk->succ[blk->nsucc++] = label_block[last->a.value];
            break;
        case IR_JZ:
        case IR_JNZ:
            blk->succ[blk->nsucc++] = label_block[last->b.value];
            if (has_next)
                blk->succ[blk->nsucc++] = b + 1;
            break;
        case IR_RET:
            break;
        default:
            if (has_next)
                blk->succ[blk->nsucc++] = b + 1;
            break;
        }
    }
    free(leader);
    free(block_of);
    free(label_block);
    return cfg;
}

void cfg_free(Cfg *cfg)
{
    if (!cfg)
        return;
    free(cfg->blocks);
    free(cfg);
}

void cfg_print(FILE *out, const IrFunc *f, const Cfg *cfg)
{
    for (int b = 0; b < cfg->nblocks; b++) {
        const Block *blk = &cfg->blocks[b];

        fprintf(out, "block %d: instructions %d..%d, first: ", b, blk->first,
                blk->last);
        ir_print_instr(out, &f->code[blk->first]);
        fputs(" ->", out);
        if (blk->nsucc == 0)
            fputs(" (exit)", out);
        for (int s = 0; s < blk->nsucc; s++)
            fprintf(out, " %d", blk->succ[s]);
        fputc('\n', out);
    }
}
