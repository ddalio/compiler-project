/* tokens.c - Nombres imprimibles de los tokens (ver tokens.h). */
#include "tokens.h"

/* El switch sobre el enum de bison no tiene "default" a proposito: si se
 * agrega un %token (o un nombre de precedencia) en bison.y y se olvida aca,
 * gcc avisa con -Wswitch. */
const char *token_name(int token)
{
    switch ((enum yytokentype)token) {
    case TOK_EOF:           return "EOF";

    case TOK_KW_BOOLEAN:    return "KW_BOOLEAN";
    case TOK_KW_ELSE:       return "KW_ELSE";
    case TOK_KW_FALSE:      return "KW_FALSE";
    case TOK_KW_FLOAT:      return "KW_FLOAT";
    case TOK_KW_IF:         return "KW_IF";
    case TOK_KW_INT:        return "KW_INT";
    case TOK_KW_RETURN:     return "KW_RETURN";
    case TOK_KW_TRUE:       return "KW_TRUE";
    case TOK_KW_VOID:       return "KW_VOID";
    case TOK_KW_WHILE:      return "KW_WHILE";

    case TOK_ID:            return "ID";
    case TOK_INT_LITERAL:   return "INT_LITERAL";
    case TOK_FLOAT_LITERAL: return "FLOAT_LITERAL";

    case TOK_PLUS:          return "PLUS";
    case TOK_MINUS:         return "MINUS";
    case TOK_STAR:          return "STAR";
    case TOK_SLASH:         return "SLASH";
    case TOK_PERCENT:       return "PERCENT";
    case TOK_LT:            return "LT";
    case TOK_GT:            return "GT";
    case TOK_EQ:            return "EQ";
    case TOK_NE:            return "NE";
    case TOK_AND:           return "AND";
    case TOK_OR:            return "OR";
    case TOK_NOT:           return "NOT";
    case TOK_ASSIGN:        return "ASSIGN";

    case TOK_SEMICOLON:     return "SEMICOLON";
    case TOK_COMMA:         return "COMMA";
    case TOK_LPAREN:        return "LPAREN";
    case TOK_RPAREN:        return "RPAREN";
    case TOK_LBRACE:        return "LBRACE";
    case TOK_RBRACE:        return "RBRACE";

    /* Valores internos de bison y el pseudo-token de precedencia: el scanner
     * nunca los devuelve. */
    case YYEMPTY:
    case YYerror:
    case YYUNDEF:
    case UMINUS:
        break;
    }
    return "UNKNOWN";
}
