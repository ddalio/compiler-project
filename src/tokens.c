/* tokens.c - Nombres imprimibles de los tokens (ver tokens.h). */
#include "tokens.h"

/* Indexada por TokenType: mantener el mismo orden que el enum. */
static const char *const TOKEN_NAMES[] = {
    "EOF",
    "KW_BOOLEAN", "KW_ELSE", "KW_FALSE", "KW_FLOAT", "KW_IF",
    "KW_INT", "KW_RETURN", "KW_TRUE", "KW_VOID", "KW_WHILE",
    "ID", "INT_LITERAL", "FLOAT_LITERAL",
    "PLUS", "MINUS", "STAR", "SLASH", "PERCENT",
    "LT", "GT", "EQ",
    "AND", "OR", "NOT",
    "ASSIGN",
    "SEMICOLON", "COMMA", "LPAREN", "RPAREN", "LBRACE", "RBRACE"
};

const char *token_name(TokenType t)
{
    if (t < 0 || t > TOK_RBRACE)
        return "UNKNOWN";
    return TOKEN_NAMES[t];
}
