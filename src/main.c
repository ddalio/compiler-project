/*
 * main.c - Interfaz de linea de comandos de c-tds (Etapa 1: analisis lexico).
 *
 *   c-tds [opcion] nombreArchivo.ctds
 *
 *   -o <salida>      nombre del archivo de salida
 *   -target <etapa>  etapa hasta la que se compila (hoy solo: scan)
 *   -debug           imprime por stdout los tokens reconocidos
 *
 * Salida de la etapa "scan": archivo <nombre>.lex con un token por linea:
 *
 *   <linea>:<columna> <NOMBRE_TOKEN> <lexema>
 *
 * Codigos de salida: 0 = ok, 1 = errores lexicos, 2 = error de uso o de E/S.
 */
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scanner.h"

#define SOURCE_EXT ".ctds"
#define EXIT_OK        0
#define EXIT_LEX_ERROR 1
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
        "  -target <stage>  compile up to <stage> (available: scan)\n"
        "  -debug           print debugging information (tokens) to stdout\n");
}

static int has_extension(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext);
    return n > e && strcmp(name + n - e, ext) == 0;
}

/* Etapas que define la catedra; por ahora solo "scan" esta implementada. */
static int check_target(const char *target)
{
    if (strcmp(target, "scan") == 0)
        return 1;
    if (strcmp(target, "parse") == 0 || strcmp(target, "codinter") == 0 ||
        strcmp(target, "assembly") == 0)
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
    opt->target = "scan";           /* por defecto: hasta la etapa actual */
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

/* Nombre por defecto: el del fuente con la extension de la etapa (.lex). */
static char *default_output(const char *input, const char *new_ext)
{
    size_t base = strlen(input) - strlen(SOURCE_EXT);
    char *out = malloc(base + strlen(new_ext) + 1);

    if (!out) {
        perror("c-tds");
        exit(EXIT_USAGE);
    }
    memcpy(out, input, base);
    strcpy(out + base, new_ext);
    return out;
}

/* Corre el scanner. Los tokens se acumulan en memoria (`buf`) y recien se
 * escriben al archivo de salida si no hubo errores lexicos. */
static int run_scan(const Options *opt)
{
    FILE *in = fopen(opt->input, "r");
    if (!in) {
        fprintf(stderr, "c-tds: cannot open '%s': %s\n",
                opt->input, strerror(errno));
        return EXIT_USAGE;
    }

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
    }
    fclose(in);
    fclose(mem);

    int status = EXIT_OK;
    if (lex_errors > 0) {
        status = EXIT_LEX_ERROR;        /* no se genera archivo de salida */
    } else {
        char *dflt = opt->output ? NULL : default_output(opt->input, ".lex");
        const char *path = opt->output ? opt->output : dflt;
        FILE *out = fopen(path, "w");

        if (!out || fwrite(buf, 1, buf_len, out) != buf_len ||
            fclose(out) != 0) {
            fprintf(stderr, "c-tds: cannot write '%s': %s\n",
                    path, strerror(errno));
            status = EXIT_USAGE;
        }
        free(dflt);
    }
    free(buf);
    return status;
}

int main(int argc, char **argv)
{
    Options opt;

    if (!parse_args(argc, argv, &opt)) {
        usage(stderr);
        return EXIT_USAGE;
    }
    return run_scan(&opt);
}
