/*
 * tokens.h - Tokens del lenguaje C-TDS (Etapa 1: analisis lexico).
 *
 * Un token por cada terminal de la gramatica de la especificacion
 * ("01-TDS-spec-lenguaje"). Los espacios y comentarios NO son tokens.
 */
#ifndef TOKENS_H
#define TOKENS_H

typedef enum {
    TOK_EOF = 0,        /* fin de archivo: flex devuelve 0 en EOF */

    /* Palabras reservadas (solo minusculas, case-sensitive) */
    TOK_KW_BOOLEAN,
    TOK_KW_ELSE,
    TOK_KW_FALSE,
    TOK_KW_FLOAT,
    TOK_KW_IF,
    TOK_KW_INT,
    TOK_KW_RETURN,
    TOK_KW_TRUE,
    TOK_KW_VOID,
    TOK_KW_WHILE,

    /* Identificadores y literales */
    TOK_ID,
    TOK_INT_LITERAL,
    TOK_FLOAT_LITERAL,

    /* Operadores aritmeticos */
    TOK_PLUS,           /* +  */
    TOK_MINUS,          /* -  */
    TOK_STAR,           /* *  */
    TOK_SLASH,          /* /  */
    TOK_PERCENT,        /* %  */

    /* Operadores relacionales */
    TOK_LT,             /* <  */
    TOK_GT,             /* >  */
    TOK_EQ,             /* == */

    /* Operadores logicos */
    TOK_AND,            /* && */
    TOK_OR,             /* || */
    TOK_NOT,            /* !  */

    /* Asignacion */
    TOK_ASSIGN,         /* =  */

    /* Delimitadores */
    TOK_SEMICOLON,      /* ;  */
    TOK_COMMA,          /* ,  */
    TOK_LPAREN,         /* (  */
    TOK_RPAREN,         /* )  */
    TOK_LBRACE,         /* {  */
    TOK_RBRACE          /* }  */
} TokenType;

/* Nombre imprimible de un token (ej: TOK_KW_IF -> "KW_IF"). */
const char *token_name(TokenType t);

#endif /* TOKENS_H */
