/*
 * main.c - Interfaz de linea de comandos de c-tds
 *          (Etapas 1 a 4: analisis lexico, sintactico, semantico y generacion
 *          de codigo intermedio).
 *
 *   c-tds [opcion] nombreArchivo.ctds
 *
 *   -o <salida>      nombre del archivo de salida
 *   -target <etapa>  etapa hasta la que se compila: scan, parse, sem o codinter
 *                    (por defecto codinter, la etapa actual)
 *   -debug           imprime informacion de depuracion
 *
 * Salida de cada etapa:
 *   scan   archivo <nombre>.lex con un token por linea:
 *            <linea>:<columna> <NOMBRE_TOKEN> <lexema>
 *          -debug imprime esos mismos tokens por stdout.
 *   parse  archivo <nombre>.sint con el arbol de parsing, un nodo por linea
 *          con 2 espacios de sangria por nivel (ver tree.h).
 *          -debug imprime ese arbol por stdout y la traza del parser por
 *          stderr.
 *   sem    archivo <nombre>.sem con el AST anotado (tipos y clase de cada
 *          identificador), un nodo por linea (ver ast.h).
 *          -debug imprime ese AST por stdout y la traza de la tabla de
 *          simbolos por stderr.
 *   codinter  archivo <nombre>.ci con el codigo de tres direcciones (ver ir.h).
 *          -debug imprime ese codigo por stdout.
 *
 * Si hay errores no se genera el archivo de salida.
 * Codigos de salida: 0 = ok, 1 = errores lexicos, sintacticos o semanticos,
 *                    2 = error de uso o de E/S.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "ir.h"
#include "irgen.h"
#include "parser.h"
#include "scanner.h"
#include "semantic.h"
#include "symtab.h"
#include "tree.h"

#define SOURCE_EXT ".ctds"
#define EXIT_OK        0
#define EXIT_SRC_ERROR 1
#define EXIT_USAGE     2

typedef struct {
    const char *input;      /* archivo fuente (.ctds) */
    const char *output;     /* NULL => nombre por defecto */
    const char *target;     /* etapa final */
    int debug;
} Options;

static void usage(FILE *out)
{
    fprintf(out,
        "Usage: c-tds [options] file.ctds\n"
        "Options:\n"
        "  -o <output>      write the output to <output>\n"
        "  -target <stage>  compile up to <stage> (available: scan, parse, sem,\n"
        "                   codinter; default: codinter)\n"
        "  -debug           print debugging information\n");
}

static int has_extension(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext);
    return n > e && strcmp(name + n - e, ext) == 0;
}

/* Etapas que define la catedra (scan, parse, codinter, assembly) mas "sem",
 * el analisis semantico, que la catedra no lista como target pero tiene su
 * propia extension de salida (.sem). Por ahora estan implementadas scan, parse,
 * sem y codinter. */
static int check_target(const char *target)
{
    if (strcmp(target, "scan") == 0 || strcmp(target, "parse") == 0 ||
        strcmp(target, "sem") == 0 || strcmp(target, "codinter") == 0)
        return 1;
    if (strcmp(target, "assembly") == 0)
        fprintf(stderr, "c-tds: target '%s' is not available yet\n", target);
    else
        fprintf(stderr, "c-tds: unknown target '%s'\n", target);
    return 0;
}

/* Devuelve 1 si los argumentos son validos; 0 (ya informado) si no. */
static int parse_args(int argc, char **argv, Options *opt)
{
    opt->input = NULL;
    opt->output = NULL;
    opt->target = "codinter";       /* por defecto: hasta la etapa actual */
    opt->debug = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (a[0] != '-') {
            if (opt->input) {
                fprintf(stderr, "c-tds: only one input file is allowed\n");
                return 0;
            }
            opt->input = a;
        } else if (strcmp(a, "-o") == 0 || strcmp(a, "-target") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "c-tds: option '%s' requires an argument\n", a);
                return 0;
            }
            if (a[1] == 'o')
                opt->output = argv[++i];
            else
                opt->target = argv[++i];
        } else if (strcmp(a, "-debug") == 0) {
            opt->debug = 1;
        } else {
            fprintf(stderr, "c-tds: unknown option '%s'\n", a);
            return 0;
        }
    }

    if (!opt->input) {
        fprintf(stderr, "c-tds: no input file\n");
        return 0;
    }
    if (!has_extension(opt->input, SOURCE_EXT)) {
        fprintf(stderr, "c-tds: input file '%s' must have the '%s' extension\n",
                opt->input, SOURCE_EXT);
        return 0;
    }
    return check_target(opt->target);
}

/* Ruta del archivo de salida: la de -o si se dio; si no, el nombre del fuente
 * con la extension de la etapa (.lex, .sint). Hay que liberarla con free. */
static char *output_path(const Options *opt, const char *stage_ext)
{
    char *out;

    if (opt->output) {
        out = malloc(strlen(opt->output) + 1);
        if (out)
            strcpy(out, opt->output);
    } else {
        size_t base = strlen(opt->input) - strlen(SOURCE_EXT);

        out = malloc(base + strlen(stage_ext) + 1);
        if (out) {
            memcpy(out, opt->input, base);
            strcpy(out + base, stage_ext);
        }
    }
    if (!out) {
        perror("c-tds");
        exit(EXIT_USAGE);
    }
    return out;
}

static FILE *open_input(const Options *opt)
{
    FILE *in = fopen(opt->input, "r");

    if (!in)
        fprintf(stderr, "c-tds: cannot open '%s': %s\n",
                opt->input, strerror(errno));
    return in;
}

static int write_failed(const char *path)
{
    fprintf(stderr, "c-tds: cannot write '%s': %s\n", path, strerror(errno));
    return EXIT_USAGE;
}

/* Etapa scan. Los tokens se acumulan en memoria (`buf`) y recien se escriben
 * al archivo de salida si no hubo errores lexicos. */
static int run_scan(const Options *opt)
{
    FILE *in = open_input(opt);
    if (!in)
        return EXIT_USAGE;

    char *buf = NULL;
    size_t buf_len = 0;
    FILE *mem = open_memstream(&buf, &buf_len);
    if (!mem) {
        perror("c-tds");
        fclose(in);
        return EXIT_USAGE;
    }

    scanner_init(opt->input, in);
    int t;
    while ((t = yylex()) != TOK_EOF) {
        fprintf(mem, "%d:%d %s %s\n", tok_line, tok_col, token_name(t), yytext);
        if (opt->debug)
            printf("%d:%d %s %s\n", tok_line, tok_col, token_name(t), yytext);
        node_free(yylval.node);         /* hoja que el parser no va a usar */
    }
    fclose(in);
    fclose(mem);

    int status = EXIT_OK;
    if (lex_errors > 0) {
        status = EXIT_SRC_ERROR;        /* no se genera archivo de salida */
    } else {
        char *path = output_path(opt, ".lex");
        FILE *out = fopen(path, "w");

        if (!out || fwrite(buf, 1, buf_len, out) != buf_len ||
            fclose(out) != 0)
            status = write_failed(path);
        free(path);
    }
    free(buf);
    return status;
}

/* Lee el fuente con el scanner y el parser. Devuelve el resultado de yyparse
 * (0 correcto, 1 error sintactico, 2 sin memoria) o -1 si no se pudo abrir el
 * archivo (ya informado). Si devuelve 0, parse_root es el arbol de parsing, que
 * el que llama debe liberar; con errores lexicos tambien devuelve 0, asi que
 * hay que mirar lex_errors. `trace` activa la traza de bison por stderr. */
static int parse_source(const Options *opt, int trace)
{
    FILE *in = open_input(opt);
    int rc;

    if (!in)
        return -1;
    scanner_init(opt->input, in);
    parse_root = NULL;
    yydebug = trace;
    rc = yyparse();
    fclose(in);
    return rc;
}

/* Etapa parse: scanner + parser; escribe el arbol de parsing en el .sint. */
static int run_parse(const Options *opt)
{
    int rc = parse_source(opt, opt->debug);
    int status = EXIT_OK;

    if (rc < 0)
        return EXIT_USAGE;
    if (rc == 2) {
        status = EXIT_USAGE;            /* memoria agotada (ya informado) */
    } else if (rc != 0 || lex_errors > 0) {
        status = EXIT_SRC_ERROR;        /* no se genera archivo de salida */
    } else {
        char *path = output_path(opt, ".sint");
        FILE *out = fopen(path, "w");

        if (!out) {
            status = write_failed(path);
        } else {
            tree_print(out, parse_root);
            if (fclose(out) != 0)
                status = write_failed(path);
        }
        free(path);
    }
    if (opt->debug && rc == 0)
        tree_print(stdout, parse_root);
    node_free(parse_root);
    parse_root = NULL;
    return status;
}

/* Resultado del front end: el AST analizado y la tabla de simbolos con la que
 * se lo analizo. La tabla tiene que seguir viva mientras se use el AST (sus
 * nodos referencian simbolos de la tabla). */
typedef struct {
    Ast *ast;
    SymTab *tab;
    int errors;             /* errores semanticos */
} Front;

static void front_free(Front *f)
{
    symtab_free(f->tab);
    ast_free(f->ast);
    f->tab = NULL;
    f->ast = NULL;
}

/* Front end completo: scanner, parser, AST y analisis semantico (que solo corre
 * si no hubo errores lexicos ni sintacticos). Devuelve EXIT_OK si llego a correr
 * el analisis semantico: entonces `f` tiene el AST y la tabla (que el que llama
 * debe liberar con front_free) y f->errors la cantidad de errores semanticos.
 * Si no, devuelve el codigo de salida (EXIT_SRC_ERROR o EXIT_USAGE) y no deja
 * nada que liberar. `trace` activa la traza de la tabla de simbolos. */
static int run_front(const Options *opt, int trace, Front *f)
{
    int rc = parse_source(opt, 0);

    f->ast = NULL;
    f->tab = NULL;
    f->errors = 0;
    if (rc < 0)
        return EXIT_USAGE;
    if (rc != 0 || lex_errors > 0) {
        node_free(parse_root);
        parse_root = NULL;
        return rc == 2 ? EXIT_USAGE : EXIT_SRC_ERROR;
    }

    f->ast = ast_from_tree(parse_root); /* el AST no depende del arbol de parsing */
    node_free(parse_root);
    parse_root = NULL;

    f->tab = symtab_new();
    if (trace)
        symtab_set_trace(f->tab, stderr);
    f->errors = semantic_analyze(f->ast, opt->input, f->tab);
    return EXIT_OK;
}

/* Escribe en el archivo de salida lo que produce `print` (ast_print o ir_print).
 * Devuelve EXIT_OK o EXIT_USAGE si no se pudo escribir. */
static int write_output(const Options *opt, const char *stage_ext,
                        void (*print)(FILE *, const void *), const void *what)
{
    char *path = output_path(opt, stage_ext);
    FILE *out = fopen(path, "w");
    int status = EXIT_OK;

    if (!out) {
        status = write_failed(path);
    } else {
        print(out, what);
        if (fclose(out) != 0)
            status = write_failed(path);
    }
    free(path);
    return status;
}

static void print_ast_cb(FILE *out, const void *ast)
{
    ast_print(out, ast);
}

static void print_ir_cb(FILE *out, const void *prog)
{
    ir_print(out, prog);
}

/* Etapa sem: front end completo; escribe el AST anotado en el .sem. */
static int run_sem(const Options *opt)
{
    Front f;
    int status = run_front(opt, opt->debug, &f);

    if (status != EXIT_OK)
        return status;
    if (f.errors > 0)
        status = EXIT_SRC_ERROR;        /* no se genera archivo de salida */
    else
        status = write_output(opt, ".sem", print_ast_cb, f.ast);
    if (opt->debug)
        ast_print(stdout, f.ast);
    front_free(&f);
    return status;
}

/* Etapa codinter: front end completo y generacion de codigo intermedio; escribe
 * el codigo de tres direcciones en el .ci. El codigo solo se genera si el
 * programa no tiene ningun error (lexico, sintactico ni semantico). */
static int run_codinter(const Options *opt)
{
    Front f;
    int status = run_front(opt, 0, &f);

    if (status != EXIT_OK)
        return status;
    if (f.errors > 0) {
        status = EXIT_SRC_ERROR;
    } else {
        IrProgram *prog = irgen_generate(f.ast, opt->input);

        if (!prog) {
            status = EXIT_SRC_ERROR;    /* p. ej. usa float (ya informado) */
        } else {
            status = write_output(opt, ".ci", print_ir_cb, prog);
            if (opt->debug)
                ir_print(stdout, prog);
            ir_program_free(prog);
        }
    }
    front_free(&f);
    return status;
}

int main(int argc, char **argv)
{
    Options opt;

    if (!parse_args(argc, argv, &opt)) {
        usage(stderr);
        return EXIT_USAGE;
    }
    if (strcmp(opt.target, "scan") == 0)
        return run_scan(&opt);
    if (strcmp(opt.target, "parse") == 0)
        return run_parse(&opt);
    if (strcmp(opt.target, "sem") == 0)
        return run_sem(&opt);
    return run_codinter(&opt);
}
