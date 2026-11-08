# c-tds - Compilador para el lenguaje C-TDS

Taller de Diseño de Software (Cod. 3306) - DC - FCEFQyN - UNRC - 2026

**Estado:** Etapas 1 (léxico), 2 (sintáctico), 3 (semántico) y 4 (código intermedio, con intérprete) terminadas. Etapa 5 (código objeto) en desarrollo.

Documentación: [etapa 1](docs/etapa-1-lexico.md), [etapa 2](docs/etapa-2-sintactico.md), [etapa 3](docs/etapa-3-semantico.md), [etapa 4](docs/etapa-4-codigo-intermedio.md), [etapa 5](docs/etapa-5-codigo-objeto.md). Para estudiar el código (qué hace cada archivo y cada función): [guía del código](docs/guia-del-codigo.md).

## Requisitos

`gcc`, `make`, `flex`, `bison` y `bash` (probado con gcc 13, GNU Make 4.3, flex 2.6.4 y bison 3.8.2).

En Ubuntu/Debian:

```
sudo apt install build-essential flex bison
```

## Compilar

```
make          # genera el ejecutable ./c-tds
make clean    # borra el ejecutable y la carpeta build/
```

## Ejecutar

```
./c-tds [opción] nombreArchivo.ctds
```

| Opción | Acción |
|--------|--------|
| `-o <salida>` | nombre del archivo de salida: el ejecutable, o el archivo de la etapa si se da `-target` |
| `-target <etapa>` | detiene la compilación en una etapa: `scan`, `parse`, `sem`, `codinter` o `assembly`. **Sin `-target` se genera el ejecutable** `<nombre>.out` |
| `-debug` | imprime información de depuración |
| `-run` | ejecuta el código intermedio generado desde `main` y, si `main` devuelve un valor, lo imprime. Implica `-target codinter` y no se puede combinar con otro target |

Salida de cada etapa:

| Etapa | Archivo | Contenido |
|-------|---------|-----------|
| `scan` | `<nombre>.lex` | un token por línea: `línea:columna NOMBRE lexema` |
| `parse` | `<nombre>.sint` | árbol de parsing, un nodo por línea con 2 espacios de sangría por nivel |
| `sem` | `<nombre>.sem` | AST anotado: un nodo por línea; las expresiones muestran su tipo y los identificadores la clase de símbolo (`id x : int [variable]`) |
| `codinter` | `<nombre>.ci` | código de tres direcciones: variables globales y, por método, `func nombre(params)`, sus variables locales, las instrucciones (`t1 = a + b`, `jz t1, L1`, `param`, `call`, `ret`) y `end` |
| `assembly` | `<nombre>.ass` | assembly x86-64 de Linux (sintaxis AT&T): cada instrucción del código intermedio aparece como comentario antes de su traducción |
| (sin `-target`) | `<nombre>.out` | ejecutable: el assembly se le pasa a `gcc`, que lo ensambla y lo enlaza con la biblioteca de C (el assembly no se guarda) |

Con `-debug` se imprime ese mismo contenido por pantalla (en el ejecutable, el assembly); además, en `parse` la traza del parser sale por `stderr` y en `sem` la traza de la tabla de símbolos.

`-run` ejecuta con enteros de 32 bits (que dan la vuelta al desbordar); una división por cero es un error de ejecución (código de salida 1). El lenguaje no tiene entrada ni salida, así que el único resultado de un programa es el valor que devuelve `main`.

El ejecutable termina con el código de salida que devuelve `main` (el sistema operativo conserva 8 bits: un resultado de 300 sale como 44) y, si `main` es `void`, con 0. Una división por cero termina el ejecutable con `SIGFPE` (código 136). Para generar el ejecutable hace falta `gcc` (Linux x86-64); con `-target assembly` no.

El código intermedio no soporta números reales: un programa con `float` es válido para `-target sem` pero se rechaza con `codegen error` al generar código.

Ejemplos:

```
./c-tds programa.ctds                  # compilación completa: genera el ejecutable programa.out
./c-tds -o programa programa.ctds      # genera el ejecutable programa
./c-tds -target assembly programa.ctds # solo hasta el assembly: genera programa.ass
./c-tds -target codinter programa.ctds # solo hasta el código intermedio: genera programa.ci
./c-tds -run programa.ctds             # interpreta el código intermedio e imprime lo que devuelve main
./c-tds -target sem programa.ctds      # solo hasta el análisis semántico: genera programa.sem
./c-tds -target parse programa.ctds    # solo hasta el parser: genera programa.sint
./c-tds -target scan programa.ctds     # solo el scanner: genera programa.lex
./c-tds -debug programa.ctds           # además imprime el assembly
```

Sin `-debug`, una compilación exitosa no imprime nada. Los errores léxicos, sintácticos, semánticos y de generación de código se
informan por `stderr` como `archivo:línea:columna: ...` y, si los hay, no se genera el archivo de salida. El análisis
semántico solo corre si el programa no tiene errores léxicos ni sintácticos.

Códigos de salida: `0` correcto, `1` errores en el programa fuente, `2` error de uso, de archivos o de herramientas (por ejemplo, no se pudo ejecutar `gcc`).

## Tests

```
make test                          # compila y corre todos los tests (unitarios y de programas)
make test-asan                     # los mismos tests con sanitizers (detecta errores de memoria)
bash tests/run_tests.sh ./c-tds    # corre los tests con un ejecutable ya compilado
```

Casos de test (cada `.ctds` tiene un `.expected` con la salida esperada):

| Carpeta | Casos | `.expected` |
|---------|-------|-------------|
| `tests/scanner/valid/` | programas válidos | contenido del `.lex` |
| `tests/scanner/errors/` | errores léxicos | mensajes de `stderr` |
| `tests/parser/valid/` | programas válidos | contenido del `.sint` (árbol de parsing) |
| `tests/parser/errors/` | errores léxicos y sintácticos | mensajes de `stderr` |
| `tests/semantic/valid/` | programas válidos | contenido del `.sem` (AST anotado) |
| `tests/semantic/errors/` | errores semánticos (uno por regla de la spec, y más) | mensajes de `stderr` |
| `tests/codinter/valid/` | programas válidos | contenido del `.ci` (código de tres direcciones) |
| `tests/codinter/errors/` | errores de generación de código (`float`) | mensajes de `stderr` |
| `tests/codinter/run/` | programas ejecutados con `-run` | `stdout`: el valor que devuelve `main` |
| `tests/codinter/run_errors/` | errores de ejecución con `-run` | mensajes de `stderr` |
| `tests/assembly/` | assembly de programas completos | contenido del `.ass` |
| `tests/native/` | programas compilados a ejecutable (se necesita `gcc`) | código de salida del ejecutable |
| `tests/unit/` | tests unitarios en C de la tabla de símbolos, los tipos, el AST, el código intermedio y el intérprete | (se ejecutan; no tienen `.expected`) |

Los tests del scanner usan `-target scan`, los del parser `-target parse`, los semánticos `-target sem` y los de código intermedio `-target codinter` (con `-run` para los programas que se ejecutan) y los de código objeto `-target assembly`. Además, los programas de `tests/codinter/run/` y de `tests/native/` se compilan a ejecutable y se ejecutan; ese grupo necesita Linux x86-64 y `gcc` y, si no están, se omite con un aviso. Los tests de la interfaz de línea de comandos están en `tests/run_tests.sh`. Los tests unitarios los arma y corre `make test` (el script solo los ejecuta si recibe `UNIT_DIR`).
