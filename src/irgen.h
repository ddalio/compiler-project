/*
 * irgen.h - Generador de codigo intermedio de C-TDS (Etapa 4).
 *
 * Recorre el AST anotado por el analisis semantico (con los simbolos y los
 * tipos ya calculados) y produce el programa en codigo de tres direcciones
 * (ir.h), siguiendo las decisiones de docs/etapa-4-codigo-intermedio.md.
 */
#ifndef IRGEN_H
#define IRGEN_H

#include "ast.h"
#include "ir.h"

/* Genera el codigo intermedio de `program` (la raiz AST_PROGRAM, ya analizada
 * por semantic_analyze sin errores). Los simbolos que referencia el AST tienen
 * que seguir vivos (la tabla de simbolos no se puede haber liberado).
 *
 * Esta etapa soporta int y boolean; un programa que use float (en una
 * declaracion, un parametro, el tipo de un metodo o un literal) se rechaza: se
 * informa por stderr un error por cada aparicion,
 *     archivo:linea:columna: codegen error: type 'float' is not supported ...
 * y se devuelve NULL. En cualquier otro caso devuelve el programa generado, que
 * el que llama debe liberar con ir_program_free. */
IrProgram *irgen_generate(const Ast *program, const char *filename);

#endif /* IRGEN_H */
