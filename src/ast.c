/* ast.c - Arbol Sintactico Abstracto de C-TDS (ver ast.h). */
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "symtab.h"

/* ---------------------------------------------------------------- memoria */

static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (!p) {
        fprintf(stderr, "c-tds: out of memory\n");
        exit(2);
    }
    return p;
}

static char *xstrdup(const char *s)
{
    size_t len = strlen(s) + 1;

    return memcpy(xmalloc(len), s, len);
}

/* ------------------------------------------------------------ constructores */

Ast *ast_node(AstKind kind, int line, int col,
              Ast *left, Ast *mid, Ast *right)
{
    Ast *n = xmalloc(sizeof *n);

    n->kind = kind;
    n->line = line;
    n->col = col;
    n->type = TYPE_UNKNOWN;
    n->name = NULL;
    n->op = OP_NONE;
    n->value.i = 0;
    n->sym = NULL;
    n->left = left;
    n->mid = mid;
    n->right = right;
    n->next = NULL;
    return n;
}

Ast *ast_leaf(AstKind kind, int line, int col)
{
    return ast_node(kind, line, col, NULL, NULL, NULL);
}

/* Lista en construccion: guarda el primer y el ultimo elemento para agregar
 * al final sin recorrer la lista. */
typedef struct {
    Ast *first, *last;
} Chain;

static void chain_add(Chain *c, Ast *n)
{
    if (c->last)
        c->last->next = n;
    else
        c->first = n;
    c->last = n;
}

/* ------------------------------------------------ arbol de parsing -> AST */

/* El parser garantiza la forma del arbol de parsing; si no coincide con lo
 * que se espera, es un error del compilador y no del programa de entrada. */
static void internal_error(const Node *n)
{
    fprintf(stderr, "c-tds: internal error: unexpected parse tree node '%s'\n",
            n->label);
    exit(2);
}

static int is(const Node *n, const char *label)
{
    return strcmp(n->label, label) == 0;
}

/* Tipo que representa la hoja de un tipo o de `void` (KW_INT int, ...). */
static Type type_of_leaf(const Node *leaf)
{
    if (is(leaf, "KW_INT"))
        return TYPE_INT;
    if (is(leaf, "KW_FLOAT"))
        return TYPE_FLOAT;
    if (is(leaf, "KW_BOOLEAN"))
        return TYPE_BOOLEAN;
    if (is(leaf, "KW_VOID"))
        return TYPE_VOID;
    internal_error(leaf);
    return TYPE_ERROR;
}

/* Operador que representa la hoja de un operador (PLUS +, MINUS -, ...).
 * `unary` distingue el '-' unario del binario. */
static OpKind op_of_leaf(const Node *leaf, int unary)
{
    static const struct { const char *label; OpKind op; } ops[] = {
        { "PLUS", OP_ADD }, { "STAR", OP_MUL }, { "SLASH", OP_DIV },
        { "PERCENT", OP_MOD }, { "LT", OP_LT }, { "GT", OP_GT },
        { "EQ", OP_EQ }, { "NE", OP_NE }, { "AND", OP_AND }, { "OR", OP_OR },
        { "NOT", OP_NOT },
    };

    if (is(leaf, "MINUS"))
        return unary ? OP_NEG : OP_SUB;
    for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++)
        if (is(leaf, ops[i].label))
            return ops[i].op;
    internal_error(leaf);
    return OP_NONE;
}

/* Nodo con nombre (declaraciones, identificadores, llamadas). */
static Ast *named(AstKind kind, const Node *id)
{
    Ast *a = ast_leaf(kind, id->line, id->col);

    a->name = xstrdup(id->text);
    return a;
}

static Ast *convert_expr(const Node *n);
static Ast *convert_block(const Node *n);

/* call(ID, argumento, ...) */
static Ast *convert_call(const Node *n)
{
    Ast *call = named(AST_CALL, n->kids[0]);
    Chain args = { NULL, NULL };

    for (int i = 1; i < n->nkids; i++)
        chain_add(&args, convert_expr(n->kids[i]));
    call->left = args.first;
    return call;
}

static Ast *convert_expr(const Node *n)
{
    if (n->text) {                          /* hoja */
        Ast *a;

        if (is(n, "ID"))
            return named(AST_ID, n);
        if (is(n, "INT_LITERAL")) {
            a = named(AST_INT_LIT, n);
            a->value.i = strtoll(n->text, NULL, 10);
            return a;
        }
        if (is(n, "FLOAT_LITERAL")) {
            a = named(AST_FLOAT_LIT, n);
            a->value.f = strtod(n->text, NULL);
            return a;
        }
        if (is(n, "KW_TRUE") || is(n, "KW_FALSE")) {
            a = named(AST_BOOL_LIT, n);
            a->value.b = is(n, "KW_TRUE");
            return a;
        }
        internal_error(n);
    }
    if (is(n, "call"))
        return convert_call(n);
    if (is(n, "binary_expr")) {             /* binary_expr(izq, op, der) */
        const Node *op = n->kids[1];
        Ast *a = ast_node(AST_BINARY, op->line, op->col,
                          convert_expr(n->kids[0]), NULL,
                          convert_expr(n->kids[2]));

        a->op = op_of_leaf(op, 0);
        return a;
    }
    if (is(n, "unary_expr")) {              /* unary_expr(op, expr) */
        const Node *op = n->kids[0];
        Ast *a = ast_node(AST_UNARY, op->line, op->col,
                          convert_expr(n->kids[1]), NULL, NULL);

        a->op = op_of_leaf(op, 1);
        return a;
    }
    internal_error(n);
    return NULL;
}

static Ast *convert_stmt(const Node *n)
{
    if (is(n, "assign"))                    /* assign(ID, expr) */
        return ast_node(AST_ASSIGN, n->line, n->col,
                        named(AST_ID, n->kids[0]), NULL,
                        convert_expr(n->kids[1]));
    if (is(n, "call"))
        return convert_call(n);
    if (is(n, "if"))                        /* if(cond, then) */
        return ast_node(AST_IF, n->line, n->col, convert_expr(n->kids[0]),
                        convert_block(n->kids[1]), NULL);
    if (is(n, "if_else"))                   /* if_else(cond, then, else) */
        return ast_node(AST_IF, n->line, n->col, convert_expr(n->kids[0]),
                        convert_block(n->kids[1]), convert_block(n->kids[2]));
    if (is(n, "while"))                     /* while(cond, body) */
        return ast_node(AST_WHILE, n->line, n->col, convert_expr(n->kids[0]),
                        convert_block(n->kids[1]), NULL);
    if (is(n, "return"))                    /* return o return(expr) */
        return ast_node(AST_RETURN, n->line, n->col,
                        n->nkids ? convert_expr(n->kids[0]) : NULL,
                        NULL, NULL);
    if (is(n, "empty_stmt"))
        return ast_leaf(AST_EMPTY, n->line, n->col);
    if (is(n, "block"))
        return convert_block(n);
    internal_error(n);
    return NULL;
}

/* var_decl(tipo, id, id, ...): una declaracion por cada identificador, cada
 * una en la posicion de su identificador (asi un "redeclarado" apunta al
 * nombre). Agrega las declaraciones a `out`. */
static void convert_var_decl(const Node *n, Chain *out)
{
    Type type = type_of_leaf(n->kids[0]);

    for (int i = 1; i < n->nkids; i++) {
        Ast *v = named(AST_VAR_DECL, n->kids[i]);

        v->type = type;
        chain_add(out, v);
    }
}

/* block(var_decl..., sentencia...) */
static Ast *convert_block(const Node *n)
{
    Chain decls = { NULL, NULL }, stmts = { NULL, NULL };

    for (int i = 0; i < n->nkids; i++) {
        if (is(n->kids[i], "var_decl"))
            convert_var_decl(n->kids[i], &decls);
        else
            chain_add(&stmts, convert_stmt(n->kids[i]));
    }
    return ast_node(AST_BLOCK, n->line, n->col, decls.first, stmts.first,
                    NULL);
}

/* method_decl(tipo de retorno, ID, params, block) */
static Ast *convert_method(const Node *n)
{
    Ast *m = named(AST_METHOD_DECL, n->kids[1]);
    const Node *params = n->kids[2];             /* params(param(tipo, ID)...) */
    Chain list = { NULL, NULL };

    m->type = type_of_leaf(n->kids[0]);
    for (int i = 0; i < params->nkids; i++) {
        const Node *param = params->kids[i];
        Ast *p = named(AST_PARAM, param->kids[1]);

        p->type = type_of_leaf(param->kids[0]);
        chain_add(&list, p);
    }
    m->left = list.first;
    m->right = convert_block(n->kids[3]);
    return m;
}

Ast *ast_from_tree(const Node *program)
{
    Chain vars = { NULL, NULL }, methods = { NULL, NULL };

    if (!is(program, "program"))
        internal_error(program);
    for (int i = 0; i < program->nkids; i++) {
        const Node *d = program->kids[i];

        if (is(d, "var_decl"))
            convert_var_decl(d, &vars);
        else if (is(d, "method_decl"))
            chain_add(&methods, convert_method(d));
        else
            internal_error(d);
    }
    return ast_node(AST_PROGRAM, program->line, program->col, vars.first,
                    methods.first, NULL);
}

/* --------------------------------------------------------------- impresion */

const char *ast_kind_name(AstKind kind)
{
    switch (kind) {
    case AST_PROGRAM:     return "program";
    case AST_VAR_DECL:    return "var_decl";
    case AST_METHOD_DECL: return "method_decl";
    case AST_PARAM:       return "param";
    case AST_BLOCK:       return "block";
    case AST_ASSIGN:      return "assign";
    case AST_CALL:        return "call";
    case AST_IF:          return "if";
    case AST_WHILE:       return "while";
    case AST_RETURN:      return "return";
    case AST_EMPTY:       return "empty";
    case AST_BINARY:      return "binary_expr";
    case AST_UNARY:       return "unary_expr";
    case AST_ID:          return "id";
    case AST_INT_LIT:     return "int_literal";
    case AST_FLOAT_LIT:   return "float_literal";
    case AST_BOOL_LIT:    return "bool_literal";
    }
    return "unknown";
}

const char *op_text(OpKind op)
{
    switch (op) {
    case OP_NONE: return "";
    case OP_ADD:  return "+";
    case OP_SUB:  return "-";
    case OP_MUL:  return "*";
    case OP_DIV:  return "/";
    case OP_MOD:  return "%";
    case OP_LT:   return "<";
    case OP_GT:   return ">";
    case OP_EQ:   return "==";
    case OP_NE:   return "!=";
    case OP_AND:  return "&&";
    case OP_OR:   return "||";
    case OP_NOT:  return "!";
    case OP_NEG:  return "-";
    }
    return "";
}

static void print_node(FILE *out, const Ast *n, int depth);

/* Imprime una lista de nodos (n, n->next, ...) al mismo nivel. */
static void print_list(FILE *out, const Ast *list, int depth)
{
    for (; list; list = list->next)
        print_node(out, list, depth);
}

static void print_node(FILE *out, const Ast *n, int depth)
{
    fprintf(out, "%*s%s", depth * 2, "", ast_kind_name(n->kind));
    if (n->kind == AST_BINARY || n->kind == AST_UNARY)
        fprintf(out, " %s", op_text(n->op));
    else if (n->name)
        fprintf(out, " %s", n->name);
    if (n->type != TYPE_UNKNOWN)
        fprintf(out, " : %s", type_name(n->type));
    if ((n->kind == AST_ID || n->kind == AST_CALL) && n->sym)
        fprintf(out, " [%s]", sym_class_name(n->sym->cls));
    fputc('\n', out);
    print_list(out, n->left, depth + 1);
    print_list(out, n->mid, depth + 1);
    print_list(out, n->right, depth + 1);
}

void ast_print(FILE *out, const Ast *root)
{
    if (root)
        print_node(out, root, 0);
}

/* ----------------------------------------------------------------- liberar */

void ast_free(Ast *n)
{
    while (n) {
        Ast *next = n->next;

        ast_free(n->left);
        ast_free(n->mid);
        ast_free(n->right);
        free(n->name);
        free(n);
        n = next;
    }
}
