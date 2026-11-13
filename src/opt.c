/*
 * opt.c - Optimizador del codigo intermedio de C-TDS (ver opt.h).
 *
 * Cada optimizacion es una funcion que recorre el codigo de UN metodo, lo
 * modifica en el lugar y devuelve 1 si cambio algo. opt_run las aplica en este
 * orden y repite hasta que ninguna cambie nada (punto fijo):
 *   fold -> propagate -> jumps -> dce
 * y al final, una sola vez, compacta los temporales.
 *
 * Para borrar instrucciones se las marca en un arreglo `dead` y despues se
 * compacta el codigo (delete_marked).
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "flow.h"
#include "opt.h"

/* Tope de seguridad del punto fijo: MAX_ROUNDS mas una vuelta por cada
 * instruccion del metodo. Cada vuelta pliega un solo nivel de una cadena de
 * constantes (1+2, luego 3+3, ...), asi que una suma de n constantes necesita
 * unas n vueltas; con un tope fijo de 50 quedaba a medias. */
#define MAX_ROUNDS 50

/* ------------------------------------------------------------- utilidades */

static void *xcalloc(size_t n, size_t size)
{
    void *p = calloc(n ? n : 1, size);

    if (!p) {
        fprintf(stderr, "c-tds: out of memory\n");
        exit(2);
    }
    return p;
}

/* Elimina las instrucciones marcadas y compacta el codigo. Devuelve cuantas
 * borro. */
static int delete_marked(IrFunc *f, const char *dead)
{
    int out = 0;

    for (int i = 0; i < f->ncode; i++)
        if (!dead[i])
            f->code[out++] = f->code[i];
    out = f->ncode - out;
    f->ncode -= out;
    return out;
}

static int is_const(Operand o)
{
    return o.kind == OPD_CONST;
}

static int is_const_value(Operand o, long long v)
{
    return o.kind == OPD_CONST && o.value == v;
}

/* Convierte la instruccion en una copia `result = src`. */
static void make_move(Instr *in, Operand src)
{
    in->op = IR_MOVE;
    in->a = src;
    in->b = ir_none();
}

/* Operaciones de 32 bits con signo que dan la vuelta, igual que el codigo
 * nativo (instrucciones de 32 bits). */
static long long wrap(uint32_t v)
{
    return (int32_t)v;
}

/* Resultado de `a op b` con constantes. Devuelve 0 si la operacion no se puede
 * plegar (division o resto por cero). */
static int compute(IrOp op, long long a, long long b, long long *out)
{
    uint32_t ua = (uint32_t)a, ub = (uint32_t)b;
    int32_t sa = (int32_t)a, sb = (int32_t)b;

    switch (op) {
    case IR_ADD: *out = wrap(ua + ub); return 1;
    case IR_SUB: *out = wrap(ua - ub); return 1;
    case IR_MUL: *out = wrap(ua * ub); return 1;
    case IR_LT:  *out = sa < sb;       return 1;
    case IR_GT:  *out = sa > sb;       return 1;
    case IR_EQ:  *out = sa == sb;      return 1;
    case IR_NE:  *out = sa != sb;      return 1;
    case IR_DIV:
    case IR_MOD:
        if (sb == 0)
            return 0;                   /* tiene que fallar al ejecutarse */
        if (sb == -1)                   /* INT_MIN / -1 da la vuelta */
            *out = op == IR_DIV ? wrap(0u - ua) : 0;
        else
            *out = op == IR_DIV ? sa / sb : sa % sb;
        return 1;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------- fold */

/* Pliega una instruccion si se puede: la reemplaza por una copia de una
 * constante o de uno de sus operandos. Devuelve 1 si la cambio. */
static int fold_instr(Instr *in)
{
    long long v;
    int folded = 0;

    switch (in->op) {
    case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV: case IR_MOD:
    case IR_LT: case IR_GT: case IR_EQ: case IR_NE:
        if (is_const(in->a) && is_const(in->b) &&
            compute(in->op, in->a.value, in->b.value, &v)) {
            make_move(in, ir_const(v));
            folded = 1;
            break;
        }
        switch (in->op) {
        case IR_ADD:
            if (is_const_value(in->b, 0)) {
                make_move(in, in->a); folded = 1;
            } else if (is_const_value(in->a, 0)) {
                make_move(in, in->b); folded = 1;
            }
            break;
        case IR_SUB:
            if (is_const_value(in->b, 0)) {
                make_move(in, in->a); folded = 1;
            } else if (same_location(in->a, in->b)) {
                make_move(in, ir_const(0)); folded = 1;
            }
            break;
        case IR_MUL:
            if (is_const_value(in->b, 1)) {
                make_move(in, in->a); folded = 1;
            } else if (is_const_value(in->a, 1)) {
                make_move(in, in->b); folded = 1;
            } else if (is_const_value(in->a, 0) || is_const_value(in->b, 0)) {
                make_move(in, ir_const(0)); folded = 1;
            }
            break;
        case IR_DIV:
            if (is_const_value(in->b, 1)) {
                make_move(in, in->a); folded = 1;
            }
            break;
        case IR_MOD:
            if (is_const_value(in->b, 1)) {
                make_move(in, ir_const(0)); folded = 1;
            }
            break;
        case IR_EQ:
            if (same_location(in->a, in->b)) {
                make_move(in, ir_const(1)); folded = 1;
            }
            break;
        case IR_NE: case IR_LT: case IR_GT:
            if (same_location(in->a, in->b)) {
                make_move(in, ir_const(0)); folded = 1;
            }
            break;
        default:
            break;
        }
        break;
    case IR_NEG:
        if (is_const(in->a)) {
            make_move(in, ir_const(wrap(0u - (uint32_t)in->a.value)));
            folded = 1;
        }
        break;
    case IR_NOT:
        if (is_const(in->a)) {
            make_move(in, ir_const(in->a.value == 0));
            folded = 1;
        }
        break;
    default:
        break;
    }

    return folded;
}

/* Plegado de constantes y simplificaciones algebraicas. Una operacion entre
 * constantes se reemplaza por su resultado; x+0, x*1, x*0, x-x, etc. se
 * reemplazan por una copia o una constante. Leer una variable no tiene efectos,
 * asi que estas simplificaciones nunca pierden un efecto. */
static int fold_func(IrFunc *f, OptStats *st)
{
    int changed = 0;

    for (int i = 0; i < f->ncode; i++) {
        if (fold_instr(&f->code[i])) {
            st->folded++;
            changed = 1;
        }
    }
    return changed;
}

/* -------------------------------------------------------------- propagate */

/* Un hecho conocido dentro de un bloque: la ubicacion `loc` vale `val`, que es
 * una constante u otra ubicacion de la que `loc` es copia. */
typedef struct {
    Operand loc, val;
} Fact;

typedef struct {
    Fact *items;
    int n, cap;
} Facts;

static int facts_find(const Facts *t, Operand loc)
{
    for (int i = 0; i < t->n; i++)
        if (same_location(t->items[i].loc, loc))
            return i;
    return -1;
}

/* Olvida lo que se sabia de `loc` y todo hecho que dependa de el (los que lo
 * tienen como valor). Se llama cuando se escribe `loc`. */
static void facts_kill(Facts *t, Operand loc)
{
    int out = 0;

    for (int i = 0; i < t->n; i++)
        if (!same_location(t->items[i].loc, loc) &&
            !same_location(t->items[i].val, loc))
            t->items[out++] = t->items[i];
    t->n = out;
}

/* Olvida todo lo que involucre una variable global: una llamada puede
 * modificarlas. */
static void facts_kill_globals(Facts *t)
{
    int out = 0;
    int g;

    for (int i = 0; i < t->n; i++) {
        g = (t->items[i].loc.kind == OPD_VAR && t->items[i].loc.var->is_global) ||
            (t->items[i].val.kind == OPD_VAR && t->items[i].val.var->is_global);
        if (!g)
            t->items[out++] = t->items[i];
    }
    t->n = out;
}

static void facts_set(Facts *t, Operand loc, Operand val)
{
    if (t->n == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 16;
        t->items = realloc(t->items, (size_t)t->cap * sizeof *t->items);
        if (!t->items) {
            fprintf(stderr, "c-tds: out of memory\n");
            exit(2);
        }
    }
    t->items[t->n].loc = loc;
    t->items[t->n].val = val;
    t->n++;
}

/* Propagacion de constantes y copias dentro de cada bloque basico: si se sabe
 * que `x` vale 5 (o es copia de `y`), cada lectura posterior de `x` se
 * reemplaza por 5 (o por `y`). Lo que se sabe se descarta en cada bloque nuevo,
 * al escribir una variable y, para las globales, en cada llamada. */
static int propagate_func(IrFunc *f, OptStats *st, int fold_too)
{
    Cfg *cfg = cfg_build(f);
    Facts facts = { NULL, 0, 0 };
    int changed = 0;

    for (int b = 0; b < cfg->nblocks; b++) {
        facts.n = 0;                            /* bloque nuevo: nada se sabe */
        for (int i = cfg->blocks[b].first; i <= cfg->blocks[b].last; i++) {
            Instr *in = &f->code[i];
            Operand *reads[2];
            int nr = instr_reads(in, reads);
            Operand *w;

            /* 1. reemplazar las lecturas por lo que se sabe */
            int rewrote = 0;

            for (int k = 0; k < nr; k++) {
                int idx;

                if (!is_location(*reads[k]))
                    continue;
                idx = facts_find(&facts, *reads[k]);
                if (idx >= 0) {
                    *reads[k] = facts.items[idx].val;
                    st->propagated++;
                    changed = 1;
                    rewrote = 1;
                }
            }
            /* Si ahora es una operacion entre constantes (t1 = 3 + 4) y el
             * plegado esta activado, se pliega en el momento: asi una cadena de
             * constantes se resuelve en una sola pasada y no en una vuelta por
             * cada eslabon. Con -opt propagate solo, no se pliega nada. */
            if (rewrote && fold_too && fold_instr(in))
                st->folded++;

            /* 2. actualizar lo que se sabe segun lo que escribe */
            if (in->op == IR_CALL) {
                facts_kill_globals(&facts);
                if (in->result.kind != OPD_NONE)
                    facts_kill(&facts, in->result);
            } else if (in->op == IR_MOVE) {
                facts_kill(&facts, in->result);
                if (!same_location(in->result, in->a))
                    facts_set(&facts, in->result, in->a);
            } else if ((w = instr_write(in)) != NULL) {
                facts_kill(&facts, *w);
            }
        }
    }
    free(facts.items);
    cfg_free(cfg);
    return changed;
}

/* -------------------------------------------------------------------- dce */

/* Cantidad de lecturas de cada ubicacion (que no sea global) en el metodo. */
typedef struct {
    int *temps;             /* indexado por el numero del temporal (1..ntemps) */
    int *params;
    int *locals;
} Reads;

static int *read_counter(Reads *r, Operand o)
{
    if (o.kind == OPD_TEMP)
        return &r->temps[o.value];
    if (o.kind == OPD_VAR && !o.var->is_global)
        return o.var->is_param ? &r->params[o.var->index] : &r->locals[o.var->index];
    return NULL;
}

static void count_reads(IrFunc *f, Reads *r)
{
    memset(r->temps, 0, ((size_t)f->ntemps + 1) * sizeof *r->temps);
    memset(r->params, 0, ((size_t)f->nparams + 1) * sizeof *r->params);
    memset(r->locals, 0, ((size_t)f->nlocals + 1) * sizeof *r->locals);
    for (int i = 0; i < f->ncode; i++) {
        Operand *reads[2];
        int nr = instr_reads(&f->code[i], reads);

        for (int k = 0; k < nr; k++) {
            int *c = read_counter(r, *reads[k]);

            if (c)
                (*c)++;
        }
    }
}

/* Indica si una instruccion que escribe un resultado se puede eliminar cuando
 * nadie lo lee: no puede tener efectos. Una division o un resto solo si el
 * divisor es una constante distinta de 0 (con 0 el programa debe fallar). */
static int removable_if_unused(const Instr *in)
{
    switch (in->op) {
    case IR_MOVE: case IR_ADD: case IR_SUB: case IR_MUL:
    case IR_NEG: case IR_NOT: case IR_LT: case IR_GT: case IR_EQ: case IR_NE:
        return 1;
    case IR_DIV:
    case IR_MOD:
        return is_const(in->b) && in->b.value != 0;
    default:
        return 0;
    }
}

/* Elimina el codigo inalcanzable: los bloques a los que no se llega desde el
 * principio del metodo (por ejemplo, lo que sigue a un ret o a un jmp hasta la
 * proxima etiqueta que alguien usa). */
static int remove_unreachable(IrFunc *f, OptStats *st)
{
    Cfg *cfg = cfg_build(f);
    char *reach = xcalloc((size_t)cfg->nblocks, 1);
    int *stack = xcalloc((size_t)cfg->nblocks, sizeof *stack);
    char *dead = xcalloc((size_t)f->ncode, 1);
    int sp = 0, removed = 0;

    if (cfg->nblocks > 0) {
        reach[0] = 1;
        stack[sp++] = 0;
    }
    while (sp > 0) {
        const Block *blk = &cfg->blocks[stack[--sp]];

        for (int s = 0; s < blk->nsucc; s++)
            if (!reach[blk->succ[s]]) {
                reach[blk->succ[s]] = 1;
                stack[sp++] = blk->succ[s];
            }
    }
    for (int b = 0; b < cfg->nblocks; b++)
        if (!reach[b])
            for (int i = cfg->blocks[b].first; i <= cfg->blocks[b].last; i++)
                dead[i] = 1;
    removed = delete_marked(f, dead);
    st->dead += removed;
    free(dead);
    free(stack);
    free(reach);
    cfg_free(cfg);
    return removed > 0;
}

/* Eliminacion de codigo muerto: codigo inalcanzable, copias de una variable a
 * si misma y resultados que nadie lee. Se repite porque eliminar una
 * instruccion puede dejar sin uso a las que la alimentaban. Un call sin uso del
 * resultado se conserva (puede tener efectos) pero deja de guardarlo. */
static int dce_func(IrFunc *f, OptStats *st)
{
    Reads r;
    int changed = remove_unreachable(f, st);
    int again;

    r.temps = xcalloc((size_t)f->ntemps + 1, sizeof *r.temps);
    r.params = xcalloc((size_t)f->nparams + 1, sizeof *r.params);
    r.locals = xcalloc((size_t)f->nlocals + 1, sizeof *r.locals);
    do {
        char *dead = xcalloc((size_t)f->ncode + 1, 1);
        int removed;

        again = 0;
        count_reads(f, &r);
        for (int i = 0; i < f->ncode; i++) {
            Instr *in = &f->code[i];
            Operand *w = instr_write(in);
            int *reads;

            if (in->op == IR_MOVE && same_location(in->result, in->a)) {
                dead[i] = 1;                    /* x = x */
                continue;
            }
            if (!w)
                continue;
            reads = read_counter(&r, *w);
            if (!reads || *reads > 0)
                continue;                       /* global, o alguien la lee */
            if (in->op == IR_CALL) {
                in->result = ir_none();         /* conserva la llamada */
                st->dead++;
                again = 1;
            } else if (removable_if_unused(in)) {
                dead[i] = 1;
            }
        }
        removed = delete_marked(f, dead);
        st->dead += removed;
        if (removed > 0)
            again = 1;
        if (again)
            changed = 1;
        free(dead);
    } while (again);
    free(r.temps);
    free(r.params);
    free(r.locals);
    return changed;
}

/* ------------------------------------------------------------------ jumps */

/* Indice de la instruccion que ocupa el lugar de la etiqueta `label` (la
 * primera que no es una etiqueta, a partir de donde se define), o -1. */
static int label_target(const IrFunc *f, long long label)
{
    for (int i = 0; i < f->ncode; i++)
        if (f->code[i].op == IR_LABEL && f->code[i].a.value == label) {
            int j = i;

            while (j < f->ncode && f->code[j].op == IR_LABEL)
                j++;
            return j;
        }
    return -1;
}

/* Indica si `label` esta definida entre las etiquetas que siguen
 * inmediatamente a la instruccion `i` (o sea, saltar a ella equivale a seguir
 * de largo). */
static int label_follows(const IrFunc *f, int i, long long label)
{
    for (int j = i + 1; j < f->ncode && f->code[j].op == IR_LABEL; j++)
        if (f->code[j].a.value == label)
            return 1;
    return 0;
}

/* Etiqueta destino de un salto, siguiendo los saltos a saltos: si el destino
 * es un `jmp M`, el destino final es M. */
static long long final_target(const IrFunc *f, long long label)
{
    for (int depth = 0; depth < 16; depth++) {
        int t = label_target(f, label);

        if (t < 0 || t >= f->ncode || f->code[t].op != IR_JMP)
            return label;
        label = f->code[t].a.value;
    }
    return label;
}

/* Optimizacion de saltos: un salto condicional con condicion constante se
 * resuelve (se vuelve un jmp o desaparece); un salto a un salto va directo al
 * destino final; un salto a la instruccion siguiente se elimina; y las
 * etiquetas que ningun salto usa se eliminan. */
static int jumps_func(IrFunc *f, OptStats *st)
{
    char *dead = xcalloc((size_t)f->ncode + 1, 1);
    int changed = 0;
    int maxl = 0;
    int *refs;

    for (int i = 0; i < f->ncode; i++) {
        Instr *in = &f->code[i];
        long long *target = NULL;

        if (in->op == IR_JMP)
            target = &in->a.value;
        else if (in->op == IR_JZ || in->op == IR_JNZ)
            target = &in->b.value;
        if (!target)
            continue;

        /* condicion constante */
        if ((in->op == IR_JZ || in->op == IR_JNZ) && is_const(in->a)) {
            int jumps = (in->op == IR_JZ) == (in->a.value == 0);

            if (jumps) {                        /* siempre salta: es un jmp */
                in->op = IR_JMP;
                in->a = in->b;
                in->b = ir_none();
                target = &in->a.value;
            } else {                            /* nunca salta: sobra */
                dead[i] = 1;
                st->jumps++;
                changed = 1;
                continue;
            }
            st->jumps++;
            changed = 1;
        }

        /* salto a un salto */
        {
            long long final = final_target(f, *target);

            if (final != *target) {
                *target = final;
                st->jumps++;
                changed = 1;
            }
        }

        /* salto a la instruccion siguiente */
        if (label_follows(f, i, *target)) {
            dead[i] = 1;
            st->jumps++;
            changed = 1;
        }
    }
    changed |= delete_marked(f, dead) > 0;
    free(dead);

    /* etiquetas que nadie usa */
    for (int i = 0; i < f->ncode; i++) {
        const Instr *in = &f->code[i];

        if (in->op == IR_LABEL && in->a.value > maxl)
            maxl = (int)in->a.value;
    }
    refs = xcalloc((size_t)maxl + 1, sizeof *refs);
    dead = xcalloc((size_t)f->ncode + 1, 1);
    for (int i = 0; i < f->ncode; i++) {
        const Instr *in = &f->code[i];

        if (in->op == IR_JMP)
            refs[in->a.value]++;
        else if (in->op == IR_JZ || in->op == IR_JNZ)
            refs[in->b.value]++;
    }
    for (int i = 0; i < f->ncode; i++)
        if (f->code[i].op == IR_LABEL && refs[f->code[i].a.value] == 0) {
            dead[i] = 1;
            st->jumps++;
        }
    if (delete_marked(f, dead) > 0)
        changed = 1;
    free(dead);
    free(refs);
    return changed;
}

/* ------------------------------------------------------------------ temps */

/* Compacta los temporales: renumera los que se usan como t1, t2, ... sin
 * huecos, y baja ntemps. Asi el marco de pila (una posicion por temporal) no
 * guarda lugar para los que las otras optimizaciones dejaron sin uso. */
static void temps_func(IrFunc *f)
{
    int *map = xcalloc((size_t)f->ntemps + 1, sizeof *map);
    int used = 0;

    for (int i = 0; i < f->ncode; i++) {
        Operand *reads[2];
        int nr = instr_reads(&f->code[i], reads);
        Operand *w = instr_write(&f->code[i]);

        for (int k = 0; k < nr; k++)
            if (reads[k]->kind == OPD_TEMP)
                map[reads[k]->value] = 1;
        if (w && w->kind == OPD_TEMP)
            map[w->value] = 1;
    }
    for (int t = 1; t <= f->ntemps; t++)
        if (map[t])
            map[t] = ++used;                    /* nuevo numero */
    for (int i = 0; i < f->ncode; i++) {
        Operand *reads[2];
        int nr = instr_reads(&f->code[i], reads);
        Operand *w = instr_write(&f->code[i]);

        for (int k = 0; k < nr; k++)
            if (reads[k]->kind == OPD_TEMP)
                reads[k]->value = map[reads[k]->value];
        if (w && w->kind == OPD_TEMP)
            w->value = map[w->value];
    }
    f->ntemps = used;
    free(map);
}

/* ------------------------------------------------------------ interfaz */

unsigned opt_from_name(const char *name)
{
    static const struct { const char *name; unsigned bit; } table[] = {
        { "fold", OPT_FOLD }, { "propagate", OPT_PROPAGATE },
        { "dce", OPT_DCE }, { "jumps", OPT_JUMPS }, { "temps", OPT_TEMPS },
        { "all", OPT_ALL },
    };

    for (size_t i = 0; i < sizeof table / sizeof table[0]; i++)
        if (strcmp(name, table[i].name) == 0)
            return table[i].bit;
    return 0;
}

const char *opt_names(void)
{
    return "fold, propagate, dce, jumps, temps, all";
}

void opt_run(IrProgram *prog, unsigned passes, FILE *report, OptStats *stats)
{
    memset(stats, 0, sizeof *stats);
    for (IrFunc *f = prog->first; f; f = f->next) {
        int before = f->ncode, temps_before = f->ntemps;
        int rounds = 0, changed;

        do {
            changed = 0;
            if (passes & OPT_FOLD)
                changed |= fold_func(f, stats);
            if (passes & OPT_PROPAGATE)
                changed |= propagate_func(f, stats, (passes & OPT_FOLD) != 0);
            if (passes & OPT_JUMPS)
                changed |= jumps_func(f, stats);
            if (passes & OPT_DCE)
                changed |= dce_func(f, stats);
            rounds++;
        } while (changed && rounds < MAX_ROUNDS + before);
        if (passes & OPT_TEMPS)
            temps_func(f);

        stats->before += before;
        stats->after += f->ncode;
        stats->temps_before += temps_before;
        stats->temps_after += f->ntemps;
        if (rounds > stats->rounds)
            stats->rounds = rounds;
        if (report)
            fprintf(report, "[opt] %s: %d -> %d instructions, %d -> %d temporaries\n",
                    f->name, before, f->ncode, temps_before, f->ntemps);
    }
}
