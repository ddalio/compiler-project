/*
 * ir.h - Codigo intermedio de C-TDS: codigo de tres direcciones (Etapa 4).
 *
 * Un programa en codigo intermedio es: la lista de variables globales y, por
 * cada metodo, una lista lineal de instrucciones (cuadruplas). Cada instruccion
 * tiene una operacion, hasta dos operandos y un resultado:
 *
 *     t3 = a + b          op = IR_ADD, a, b, result = t3
 *
 * Operandos: constante entera, variable (global, local o parametro), temporal,
 * etiqueta o nombre de metodo. Un boolean es un entero (0 falso, 1 verdadero).
 * El formato de texto y las decisiones de diseno estan en
 * docs/etapa-4-codigo-intermedio.md.
 *
 * Campos que usa cada operacion (a, b y result; los no listados quedan en
 * OPD_NONE):
 *
 *   IR_MOVE                        result = a
 *   IR_ADD IR_SUB IR_MUL IR_DIV IR_MOD
 *   IR_LT IR_GT IR_EQ              result = a (op) b
 *   IR_NEG IR_NOT                  result = (op) a
 *   IR_LABEL                       a = etiqueta que se define
 *   IR_JMP                         a = etiqueta destino
 *   IR_JZ IR_JNZ                   a = condicion, b = etiqueta destino
 *   IR_PARAM                       a = argumento
 *   IR_CALL                        a = metodo (OPD_FUNC), b = cantidad de
 *                                  argumentos (OPD_CONST), result = temporal
 *                                  con el valor devuelto (OPD_NONE si no hay)
 *   IR_RET                         a = valor devuelto (OPD_NONE si no hay)
 */
#ifndef IR_H
#define IR_H

#include <stdio.h>

/* ------------------------------------------------------------- operandos */

typedef enum {
    OPD_NONE,           /* sin operando */
    OPD_CONST,          /* constante entera (true = 1, false = 0) */
    OPD_VAR,            /* variable global, local o parametro */
    OPD_TEMP,           /* temporal t1, t2, ... (de un metodo) */
    OPD_LABEL,          /* etiqueta L1, L2, ... (de todo el programa) */
    OPD_FUNC            /* nombre de un metodo (solo en IR_CALL) */
} OperandKind;

/* Una variable del codigo intermedio. Pertenece al programa (globales) o al
 * metodo (parametros y locales); su nombre es unico dentro de su metodo. */
typedef struct IrVar {
    char *name;
    int is_global;      /* se escribe con prefijo '@' */
} IrVar;

typedef struct {
    OperandKind kind;
    long long value;        /* OPD_CONST: la constante; OPD_TEMP y OPD_LABEL: el numero */
    const IrVar *var;       /* OPD_VAR */
    const char *name;       /* OPD_FUNC: nombre del metodo */
} Operand;

/* Constructores de operandos. */
Operand ir_none(void);
Operand ir_const(long long value);
Operand ir_var(const IrVar *var);
Operand ir_func(const char *name);

/* ---------------------------------------------------------- instrucciones */

typedef enum {
    IR_MOVE,
    IR_ADD, IR_SUB, IR_MUL, IR_DIV, IR_MOD,
    IR_NEG,
    IR_LT, IR_GT, IR_EQ,
    IR_NOT,
    IR_LABEL, IR_JMP, IR_JZ, IR_JNZ,
    IR_PARAM, IR_CALL, IR_RET
} IrOp;

typedef struct {
    IrOp op;
    Operand a, b, result;
} Instr;

/* --------------------------------------------------- metodos y programa */

typedef struct IrFunc {
    char *name;
    int returns_value;          /* 1 si el metodo devuelve un valor (no es void) */
    IrVar **params;             /* parametros, en orden */
    int nparams;
    IrVar **locals;             /* variables locales, en orden de declaracion */
    int nlocals;
    Instr *code;                /* instrucciones, en orden */
    int ncode;
    int ntemps;                 /* temporales usados: t1 .. t<ntemps> */
    int cap_params, cap_locals, cap_code;       /* capacidades reservadas */
    struct IrFunc *next;        /* siguiente metodo del programa */
} IrFunc;

typedef struct {
    IrVar **globals;            /* variables globales, en orden de declaracion */
    int nglobals;
    int cap_globals;
    IrFunc *first, *last;       /* metodos, en orden del fuente */
    int nlabels;                /* etiquetas creadas: L1 .. L<nlabels> */
} IrProgram;

/* Crea un programa vacio. */
IrProgram *ir_program_new(void);

/* Libera el programa con todo lo que contiene. Acepta NULL. */
void ir_program_free(IrProgram *prog);

/* Agrega una variable global. `name` se copia. */
IrVar *ir_add_global(IrProgram *prog, const char *name);

/* Agrega un metodo al final del programa. `name` se copia. */
IrFunc *ir_add_func(IrProgram *prog, const char *name, int returns_value);

/* Agrega un parametro o una variable local al metodo y la devuelve. `base` es
 * el nombre del fuente; si el metodo ya tiene un parametro o una local con ese
 * nombre, la nueva se llama base.2, base.3, ... para que cada variable tenga un
 * nombre unico dentro de su metodo. */
IrVar *ir_add_param(IrFunc *f, const char *base);
IrVar *ir_add_local(IrFunc *f, const char *base);

/* Crea un temporal nuevo del metodo (t1, t2, ...) y lo devuelve como operando. */
Operand ir_new_temp(IrFunc *f);

/* Crea una etiqueta nueva del programa (L1, L2, ...) y la devuelve como
 * operando. */
Operand ir_new_label(IrProgram *prog);

/* Agrega una instruccion al final del metodo. */
void ir_emit(IrFunc *f, IrOp op, Operand a, Operand b, Operand result);

/* Escribe el programa en el formato de texto del archivo .ci. */
void ir_print(FILE *out, const IrProgram *prog);

#endif /* IR_H */
