/*
 * test_symtab.c - Tests unitarios de la tabla de simbolos (src/symtab.c) y de
 * los tipos (src/types.c). Se compila y ejecuta con `make test`.
 *
 * Cada CHECK que falla imprime la linea; el programa devuelve 1 si alguno
 * fallo.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "symtab.h"
#include "types.h"

static int checks, failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            printf("  FAIL (linea %d): %s\n", __LINE__, #cond);              \
        }                                                                    \
    } while (0)

/* Compara el texto producido por `fn` con `expected`. */
typedef void (*dump_fn)(FILE *, const SymTab *);

static int dump_equals(dump_fn fn, const SymTab *tab, const char *expected)
{
    char *buf = NULL;
    size_t len = 0;
    FILE *mem = open_memstream(&buf, &len);
    int same;

    fn(mem, tab);
    fclose(mem);
    same = strcmp(buf, expected) == 0;
    if (!same)
        printf("  --- obtenido:\n%s  --- esperado:\n%s", buf, expected);
    free(buf);
    return same;
}

/* Una tabla nueva solo tiene el nivel global. */
static void test_new_table(void)
{
    SymTab *t = symtab_new();

    CHECK(symtab_depth(t) == 1);
    CHECK(symtab_lookup(t, "x") == NULL);
    CHECK(symtab_lookup_current(t, "x") == NULL);
    CHECK(dump_equals(symtab_dump, t, ""));
    symtab_free(t);
}

/* Insertar guarda todos los datos del simbolo; los repetidos se rechazan. */
static void test_insert_and_duplicates(void)
{
    SymTab *t = symtab_new();
    Symbol *x = symtab_insert(t, SYM_VARIABLE, "x", TYPE_INT, 3, 7);
    Symbol *f = symtab_insert(t, SYM_METHOD, "f", TYPE_FLOAT, 4, 1);

    CHECK(x && f && x != f);
    CHECK(x->cls == SYM_VARIABLE && strcmp(x->name, "x") == 0);
    CHECK(x->type == TYPE_INT && x->line == 3 && x->col == 7 && x->level == 1);
    CHECK(f->cls == SYM_METHOD && f->type == TYPE_FLOAT && f->nparams == 0);
    CHECK(symtab_lookup(t, "x") == x && symtab_lookup_current(t, "f") == f);

    /* mismo nombre en el mismo nivel: no se inserta y el original no cambia */
    CHECK(symtab_insert(t, SYM_VARIABLE, "x", TYPE_BOOLEAN, 9, 9) == NULL);
    CHECK(symtab_lookup_current(t, "x") == x && x->type == TYPE_INT);
    /* variables y metodos comparten el espacio de nombres */
    CHECK(symtab_insert(t, SYM_VARIABLE, "f", TYPE_INT, 9, 9) == NULL);
    symtab_free(t);
}

/* Los parametros formales de un metodo se copian. */
static void test_method_params(void)
{
    SymTab *t = symtab_new();
    Symbol *m = symtab_insert(t, SYM_METHOD, "m", TYPE_VOID, 1, 6);
    Type params[] = { TYPE_INT, TYPE_BOOLEAN, TYPE_FLOAT };

    symtab_set_params(m, params, 3);
    params[0] = TYPE_VOID;                          /* no debe afectar a la copia */
    CHECK(m->nparams == 3);
    CHECK(m->param_types[0] == TYPE_INT && m->param_types[1] == TYPE_BOOLEAN &&
          m->param_types[2] == TYPE_FLOAT);
    symtab_set_params(m, NULL, 0);                  /* se pueden redefinir */
    CHECK(m->nparams == 0 && m->param_types == NULL);
    symtab_free(t);
}

/* Niveles anidados: visibilidad, ocultamiento y cierre. */
static void test_levels_and_shadowing(void)
{
    SymTab *t = symtab_new();
    Symbol *gx = symtab_insert(t, SYM_VARIABLE, "x", TYPE_INT, 1, 5);
    Symbol *f = symtab_insert(t, SYM_METHOD, "f", TYPE_INT, 2, 5);
    Symbol *p, *lx, *lf, *bp;

    symtab_open_level(t);                           /* nivel 2: un metodo */
    CHECK(symtab_depth(t) == 2);
    p = symtab_insert(t, SYM_PARAM, "p", TYPE_INT, 2, 11);
    CHECK(p->level == 2);
    CHECK(symtab_lookup(t, "x") == gx);             /* el global se ve desde adentro */
    CHECK(symtab_lookup_current(t, "x") == NULL);   /* ...pero no es del nivel actual */

    lx = symtab_insert(t, SYM_VARIABLE, "x", TYPE_FLOAT, 3, 9);   /* oculta al global */
    CHECK(lx && lx != gx && symtab_lookup(t, "x") == lx);
    lf = symtab_insert(t, SYM_VARIABLE, "f", TYPE_BOOLEAN, 3, 12); /* oculta a un metodo */
    CHECK(lf && symtab_lookup(t, "f") == lf);

    symtab_open_level(t);                           /* nivel 3: un bloque */
    bp = symtab_insert(t, SYM_VARIABLE, "p", TYPE_BOOLEAN, 4, 9);  /* mismo nombre en otro nivel */
    CHECK(bp && symtab_lookup(t, "p") == bp && bp->level == 3);
    CHECK(symtab_lookup(t, "x") == lx);
    symtab_close_level(t);
    CHECK(symtab_depth(t) == 2 && symtab_lookup(t, "p") == p);

    symtab_close_level(t);                          /* se cierra el metodo */
    CHECK(symtab_depth(t) == 1);
    CHECK(symtab_lookup(t, "x") == gx);             /* vuelve a verse el global */
    CHECK(symtab_lookup(t, "f") == f);
    CHECK(symtab_lookup(t, "p") == NULL);           /* el parametro dejo de verse */

    /* un nivel nuevo es independiente del anterior */
    symtab_open_level(t);
    CHECK(symtab_lookup_current(t, "p") == NULL);
    CHECK(symtab_insert(t, SYM_VARIABLE, "p", TYPE_INT, 8, 8) != NULL);
    symtab_close_level(t);
    symtab_free(t);
}

/* Cerrar un nivel solo oculta sus simbolos: siguen validos (el AST los usa). */
static void test_symbols_survive_close(void)
{
    SymTab *t = symtab_new();
    Symbol *s;

    symtab_open_level(t);
    s = symtab_insert(t, SYM_VARIABLE, "local", TYPE_BOOLEAN, 5, 3);
    symtab_close_level(t);
    CHECK(symtab_lookup(t, "local") == NULL);
    CHECK(strcmp(s->name, "local") == 0 && s->type == TYPE_BOOLEAN &&
          s->level == 2 && s->line == 5 && s->col == 3);
    symtab_free(t);                                 /* libera tambien a `s` */
}

/* El volcado incluye los simbolos de niveles ya cerrados, en orden de alta. */
static void test_dump(void)
{
    SymTab *t = symtab_new();
    Symbol *f = symtab_insert(t, SYM_METHOD, "f", TYPE_INT, 2, 5);
    Type params[] = { TYPE_INT, TYPE_FLOAT };

    symtab_insert(t, SYM_VARIABLE, "g", TYPE_FLOAT, 1, 7);
    symtab_set_params(f, params, 2);
    symtab_open_level(t);
    symtab_insert(t, SYM_PARAM, "p", TYPE_INT, 2, 11);
    symtab_open_level(t);
    symtab_insert(t, SYM_VARIABLE, "z", TYPE_BOOLEAN, 3, 9);
    symtab_close_level(t);
    symtab_close_level(t);
    symtab_insert(t, SYM_METHOD, "main", TYPE_VOID, 6, 6);

    CHECK(dump_equals(symtab_dump, t,
        "level 1 method f : int (int, float) @2:5\n"
        "level 1 variable g : float @1:7\n"
        "level 2 param p : int @2:11\n"
        "level 3 variable z : boolean @3:9\n"
        "level 1 method main : void () @6:6\n"));
    symtab_free(t);
}

/* La traza informa cada operacion, y solo si se la activo. */
static void test_trace(void)
{
    SymTab *t = symtab_new();
    char *buf = NULL;
    size_t len = 0;
    FILE *mem = open_memstream(&buf, &len);

    symtab_insert(t, SYM_VARIABLE, "before", TYPE_INT, 1, 1);   /* sin traza */
    symtab_set_trace(t, mem);
    symtab_open_level(t);
    symtab_insert(t, SYM_PARAM, "p", TYPE_INT, 1, 1);
    symtab_insert(t, SYM_PARAM, "p", TYPE_INT, 1, 1);           /* rechazado: sin linea */
    symtab_close_level(t);
    symtab_set_trace(t, NULL);
    symtab_open_level(t);                                       /* sin traza */
    symtab_close_level(t);
    fclose(mem);

    CHECK(strcmp(buf,
        "[symtab] open level 2\n"
        "[symtab] insert p (param int) at level 2\n"
        "[symtab] close level 2\n") == 0);
    free(buf);
    symtab_free(t);
}

/* symtab_free libera tambien los niveles que quedaron abiertos (lo comprueba
 * AddressSanitizer en `make test-asan`). */
static void test_free_with_open_levels(void)
{
    SymTab *t = symtab_new();

    symtab_insert(t, SYM_VARIABLE, "a", TYPE_INT, 1, 1);
    symtab_open_level(t);
    symtab_open_level(t);
    symtab_insert(t, SYM_VARIABLE, "b", TYPE_INT, 1, 1);
    symtab_free(t);
    CHECK(1);
}

/* Tipos: nombres y compatibilidad (regla 14). */
static void test_types(void)
{
    CHECK(strcmp(type_name(TYPE_INT), "int") == 0);
    CHECK(strcmp(type_name(TYPE_FLOAT), "float") == 0);
    CHECK(strcmp(type_name(TYPE_BOOLEAN), "boolean") == 0);
    CHECK(strcmp(type_name(TYPE_VOID), "void") == 0);
    CHECK(strcmp(type_name(TYPE_ERROR), "error") == 0);

    CHECK(type_is_numeric(TYPE_INT) && type_is_numeric(TYPE_FLOAT));
    CHECK(!type_is_numeric(TYPE_BOOLEAN) && !type_is_numeric(TYPE_VOID));

    /* iguales */
    CHECK(types_compatible(TYPE_INT, TYPE_INT));
    CHECK(types_compatible(TYPE_BOOLEAN, TYPE_BOOLEAN));
    /* int y float, en los dos sentidos */
    CHECK(types_compatible(TYPE_INT, TYPE_FLOAT));
    CHECK(types_compatible(TYPE_FLOAT, TYPE_INT));
    /* boolean no se mezcla con nada */
    CHECK(!types_compatible(TYPE_BOOLEAN, TYPE_INT));
    CHECK(!types_compatible(TYPE_FLOAT, TYPE_BOOLEAN));
    /* void no es compatible ni consigo mismo con un valor numerico */
    CHECK(!types_compatible(TYPE_INT, TYPE_VOID));
    CHECK(!types_compatible(TYPE_VOID, TYPE_BOOLEAN));
    /* TYPE_ERROR es compatible con todo (evita errores en cascada) */
    CHECK(types_compatible(TYPE_ERROR, TYPE_BOOLEAN));
    CHECK(types_compatible(TYPE_INT, TYPE_ERROR));
    CHECK(types_compatible(TYPE_ERROR, TYPE_ERROR));
}

int main(void)
{
    test_new_table();
    test_insert_and_duplicates();
    test_method_params();
    test_levels_and_shadowing();
    test_symbols_survive_close();
    test_dump();
    test_trace();
    test_free_with_open_levels();
    test_types();

    printf("test_symtab: %d comprobaciones, %d fallaron\n", checks, failures);
    return failures != 0;
}
