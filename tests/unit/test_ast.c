/*
 * test_ast.c - Tests unitarios del AST (src/ast.c): conversion desde el arbol
 * de parsing, posiciones, listas, valores de literales e impresion. Se compila
 * y ejecuta con `make test`.
 *
 * Cada programa de prueba se parsea desde una cadena con fmemopen. El arbol de
 * parsing se libera apenas se construye el AST: si el AST dependiera de el, las
 * comprobaciones (y AddressSanitizer en `make test-asan`) lo detectarian.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ast.h"
#include "parser.h"
#include "scanner.h"

static int checks, failures;

#define CHECK(cond)                                                          \
    do {                                                                     \
        checks++;                                                            \
        if (!(cond)) {                                                       \
            failures++;                                                      \
            printf("  FAIL (linea %d): %s\n", __LINE__, #cond);              \
        }                                                                    \
    } while (0)

/* Parsea `src` (que debe ser sintacticamente correcto) y devuelve su AST. */
static Ast *build(const char *src)
{
    FILE *f = fmemopen((void *)src, strlen(src), "r");
    Ast *ast;
    int rc;

    scanner_init("test", f);
    rc = yyparse();
    fclose(f);
    if (rc != 0 || lex_errors > 0) {
        printf("  el programa de prueba no parsea: %s\n", src);
        exit(2);
    }
    ast = ast_from_tree(parse_root);
    node_free(parse_root);
    parse_root = NULL;
    return ast;
}

static int at(const Ast *n, int line, int col)
{
    return n->line == line && n->col == col;
}

/* Una declaracion por identificador, con la posicion de cada uno. */
static void test_declarations(void)
{
    Ast *p = build("int a, b;\nfloat c;\n");
    Ast *a = p->left, *b = a ? a->next : NULL, *c = b ? b->next : NULL;

    CHECK(p->kind == AST_PROGRAM && p->mid == NULL);
    CHECK(a && b && c && c->next == NULL);
    CHECK(a->kind == AST_VAR_DECL && strcmp(a->name, "a") == 0 &&
          a->type == TYPE_INT && at(a, 1, 5));
    CHECK(strcmp(b->name, "b") == 0 && b->type == TYPE_INT && at(b, 1, 8));
    CHECK(strcmp(c->name, "c") == 0 && c->type == TYPE_FLOAT && at(c, 2, 7));
    ast_free(p);
}

/* Metodo: tipo de retorno, parametros en orden y cuerpo. */
static void test_method(void)
{
    Ast *p = build("int f(int x, boolean y) { return x; }\n");
    Ast *m = p->mid, *x, *y, *body;

    CHECK(p->left == NULL && m && m->next == NULL);
    CHECK(m->kind == AST_METHOD_DECL && strcmp(m->name, "f") == 0 &&
          m->type == TYPE_INT && at(m, 1, 5));
    x = m->left;
    y = x ? x->next : NULL;
    CHECK(x && y && y->next == NULL);
    CHECK(x->kind == AST_PARAM && strcmp(x->name, "x") == 0 &&
          x->type == TYPE_INT && at(x, 1, 11));
    CHECK(strcmp(y->name, "y") == 0 && y->type == TYPE_BOOLEAN && at(y, 1, 22));
    body = m->right;
    CHECK(body && body->kind == AST_BLOCK && at(body, 1, 25));
    CHECK(body->left == NULL && body->mid && body->mid->kind == AST_RETURN);
    CHECK(body->mid->left && body->mid->left->kind == AST_ID &&
          strcmp(body->mid->left->name, "x") == 0);
    ast_free(p);
}

/* El metodo sin parametros no tiene lista de parametros. */
static void test_method_without_params(void)
{
    Ast *p = build("void main() { }\n");

    CHECK(p->mid && p->mid->left == NULL && p->mid->type == TYPE_VOID);
    CHECK(p->mid->right && p->mid->right->left == NULL &&
          p->mid->right->mid == NULL);
    ast_free(p);
}

/* if: condicion, then y else (opcional); las posiciones son las del if. */
static void test_if(void)
{
    Ast *p = build("void m() { if (a) { } if (a) { } else { } }\n");
    Ast *s1 = p->mid->right->mid, *s2 = s1 ? s1->next : NULL;

    CHECK(s1 && s2 && s2->next == NULL);
    CHECK(s1->kind == AST_IF && at(s1, 1, 12));
    CHECK(s1->left && s1->left->kind == AST_ID);    /* los parentesis no son un nodo */
    CHECK(s1->mid && s1->mid->kind == AST_BLOCK && s1->right == NULL);
    CHECK(s2->kind == AST_IF && at(s2, 1, 23));
    CHECK(s2->mid && s2->right && s2->right->kind == AST_BLOCK);
    ast_free(p);
}

/* while, return con y sin valor, sentencia vacia y bloque suelto. */
static void test_other_statements(void)
{
    Ast *p = build("int m() { while (a) { } ; { } return; return 1; }\n");
    Ast *s = p->mid->right->mid;

    CHECK(s && s->kind == AST_WHILE && s->left && s->mid &&
          s->mid->kind == AST_BLOCK);
    s = s->next;
    CHECK(s && s->kind == AST_EMPTY);
    s = s->next;
    CHECK(s && s->kind == AST_BLOCK);
    s = s->next;
    CHECK(s && s->kind == AST_RETURN && s->left == NULL);
    s = s->next;
    CHECK(s && s->kind == AST_RETURN && s->left &&
          s->left->kind == AST_INT_LIT && s->next == NULL);
    ast_free(p);
}

/* Una operacion toma la posicion de su operador; - unario y binario distintos. */
static void test_operators(void)
{
    Ast *p = build("void m() { x = -a - b * 2; }\n");
    Ast *assign = p->mid->right->mid;
    Ast *sub = assign->right, *neg = sub->left, *mul = sub->right;

    CHECK(assign->kind == AST_ASSIGN && at(assign, 1, 12));
    CHECK(assign->left && assign->left->kind == AST_ID &&
          strcmp(assign->left->name, "x") == 0 && at(assign->left, 1, 12));
    CHECK(sub->kind == AST_BINARY && sub->op == OP_SUB && at(sub, 1, 19));
    CHECK(neg->kind == AST_UNARY && neg->op == OP_NEG && at(neg, 1, 16));
    CHECK(neg->left && strcmp(neg->left->name, "a") == 0);
    CHECK(mul->kind == AST_BINARY && mul->op == OP_MUL && at(mul, 1, 23));
    CHECK(strcmp(op_text(sub->op), "-") == 0 && strcmp(op_text(OP_AND), "&&") == 0 &&
          strcmp(op_text(OP_NOT), "!") == 0);
    ast_free(p);
}

/* Todos los operadores binarios y los dos unarios se convierten bien. */
static void test_all_operators(void)
{
    static const struct { const char *src; OpKind op; } cases[] = {
        { "a + b", OP_ADD }, { "a - b", OP_SUB }, { "a * b", OP_MUL },
        { "a / b", OP_DIV }, { "a % b", OP_MOD }, { "a < b", OP_LT },
        { "a > b", OP_GT },  { "a == b", OP_EQ }, { "a && b", OP_AND },
        { "a || b", OP_OR },
    };
    char src[64];

    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        Ast *p;

        snprintf(src, sizeof src, "void m() { x = %s; }\n", cases[i].src);
        p = build(src);
        CHECK(p->mid->right->mid->right->kind == AST_BINARY &&
              p->mid->right->mid->right->op == cases[i].op);
        ast_free(p);
    }
    {
        Ast *p = build("void m() { x = !a; }\n");

        CHECK(p->mid->right->mid->right->kind == AST_UNARY &&
              p->mid->right->mid->right->op == OP_NOT);
        ast_free(p);
    }
}

/* Valores y lexemas de los literales. */
static void test_literals(void)
{
    Ast *p = build("void m() { x = 42; y = 2.5; z = true; w = false; }\n");
    Ast *s = p->mid->right->mid;
    Ast *i = s->right, *f = s->next->right, *t = s->next->next->right,
        *n = s->next->next->next->right;

    CHECK(i->kind == AST_INT_LIT && i->value.i == 42 && strcmp(i->name, "42") == 0);
    CHECK(f->kind == AST_FLOAT_LIT && f->value.f == 2.5 && strcmp(f->name, "2.5") == 0);
    CHECK(t->kind == AST_BOOL_LIT && t->value.b == 1);
    CHECK(n->kind == AST_BOOL_LIT && n->value.b == 0);
    CHECK(i->type == TYPE_UNKNOWN);                 /* los tipos los calcula la semantica */
    ast_free(p);
}

/* Llamada: argumentos en orden y cada uno con su posicion. */
static void test_call_arguments(void)
{
    Ast *p = build("void m() { f(1, 2, 3); }\n");
    Ast *call = p->mid->right->mid;
    Ast *a1 = call->left, *a2 = a1 ? a1->next : NULL, *a3 = a2 ? a2->next : NULL;

    CHECK(call->kind == AST_CALL && strcmp(call->name, "f") == 0 && at(call, 1, 12));
    CHECK(a1 && a2 && a3 && a3->next == NULL);
    CHECK(a1->value.i == 1 && at(a1, 1, 14));
    CHECK(a2->value.i == 2 && at(a2, 1, 17));
    CHECK(a3->value.i == 3 && at(a3, 1, 20));
    ast_free(p);
}

/* Variables locales y sentencias de un bloque van en listas distintas. */
static void test_block_lists(void)
{
    Ast *p = build("void m() { int a, b; a = 1; { int c; } b = 2; }\n");
    Ast *blk = p->mid->right;
    Ast *inner;

    CHECK(blk->left && strcmp(blk->left->name, "a") == 0 &&
          blk->left->next && strcmp(blk->left->next->name, "b") == 0 &&
          blk->left->next->next == NULL);
    CHECK(blk->mid && blk->mid->kind == AST_ASSIGN);
    inner = blk->mid->next;
    CHECK(inner && inner->kind == AST_BLOCK && inner->left &&
          strcmp(inner->left->name, "c") == 0);
    CHECK(inner->next && inner->next->kind == AST_ASSIGN && inner->next->next == NULL);
    ast_free(p);
}

/* Impresion: un nodo por linea, 2 espacios por nivel, tipo solo si ya se calculo. */
static void test_print(void)
{
    Ast *p = build("int g;\nvoid main() { g = 1; }\n");
    char *buf = NULL;
    size_t len = 0;
    FILE *mem = open_memstream(&buf, &len);

    ast_print(mem, p);
    fclose(mem);
    CHECK(strcmp(buf,
        "program\n"
        "  var_decl g : int\n"
        "  method_decl main : void\n"
        "    block\n"
        "      assign\n"
        "        id g\n"
        "        int_literal 1\n") == 0);
    free(buf);

    p->mid->right->mid->right->type = TYPE_FLOAT;   /* con el tipo calculado */
    mem = open_memstream(&buf, &len);
    ast_print(mem, p->mid->right->mid->right);
    fclose(mem);
    CHECK(strcmp(buf, "int_literal 1 : float\n") == 0);
    free(buf);
    ast_free(p);
}

/* ast_node / ast_leaf / ast_kind_name: la interfaz basica del TAD. */
static void test_constructors(void)
{
    Ast *l = ast_leaf(AST_INT_LIT, 3, 4);
    Ast *r = ast_leaf(AST_ID, 3, 8);
    Ast *n = ast_node(AST_BINARY, 3, 6, l, NULL, r);

    CHECK(l->left == NULL && l->mid == NULL && l->right == NULL && l->next == NULL);
    CHECK(l->type == TYPE_UNKNOWN && l->sym == NULL && l->name == NULL);
    CHECK(n->left == l && n->right == r && n->mid == NULL && at(n, 3, 6));
    CHECK(strcmp(ast_kind_name(AST_METHOD_DECL), "method_decl") == 0);
    CHECK(strcmp(ast_kind_name(AST_BINARY), "binary_expr") == 0);
    ast_free(n);                                    /* libera tambien l y r */
    ast_free(NULL);                                 /* acepta NULL */
}

int main(void)
{
    test_declarations();
    test_method();
    test_method_without_params();
    test_if();
    test_other_statements();
    test_operators();
    test_all_operators();
    test_literals();
    test_call_arguments();
    test_block_lists();
    test_print();
    test_constructors();

    printf("test_ast: %d comprobaciones, %d fallaron\n", checks, failures);
    return failures != 0;
}
