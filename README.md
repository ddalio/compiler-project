# c-tds - Compilador para el lenguaje C-TDS

Taller de Diseño de Software (Cod. 3306) - DC - FCEFQyN - UNRC - 2026

**Estado:** Etapas 1 (léxico), 2 (sintáctico) y 3 (semántico) terminadas. Etapa 4 (código intermedio) en desarrollo.

Documentación: [etapa 1](docs/etapa-1-lexico.md), [etapa 2](docs/etapa-2-sintactico.md), [etapa 3](docs/etapa-3-semantico.md), [etapa 4](docs/etapa-4-codigo-intermedio.md). Para estudiar el código (qué hace cada archivo y cada función): [guía del código](docs/guia-del-codigo.md).

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
| `-o <salida>` | nombre del archivo de salida |
| `-target <etapa>` | etapa hasta la que se compila: `scan`, `parse`, `sem` o `codinter` (por defecto `codinter`, la etapa actual) |
| `-debug` | imprime información de depuración |
| `-run` | con `-target codinter`: ejecuta el código intermedio generado desde `main` y, si `main` devuelve un valor, lo imprime |

Salida de cada etapa:

| Etapa | Archivo | Contenido |
|-------|---------|-----------|
| `scan` | `<nombre>.lex` | un token por línea: `línea:columna NOMBRE lexema` |
| `parse` | `<nombre>.sint` | árbol de parsing, un nodo por línea con 2 espacios de sangría por nivel |
| `sem` | `<nombre>.sem` | AST anotado: un nodo por línea; las expresiones muestran su tipo y los identificadores la clase de símbolo (`id x : int [variable]`) |
| `codinter` | `<nombre>.ci` | código de tres direcciones: variables globales y, por método, `func nombre(params)`, sus variables locales, las instrucciones (`t1 = a + b`, `jz t1, L1`, `param`, `call`, `ret`) y `end` |

Con `-debug` se imprime ese mismo contenido por pantalla; además, en `parse` la traza del parser sale por `stderr` y en `sem` la traza de la tabla de símbolos.

`-run` ejecuta con enteros de 32 bits (que dan la vuelta al desbordar); una división por cero es un error de ejecución (código de salida 1). El lenguaje no tiene entrada ni salida, así que el único resultado de un programa es el valor que devuelve `main`.

El código intermedio no soporta números reales: un programa con `float` es válido para `-target sem` pero se rechaza con `codegen error` al generar código.

Ejemplos:

```
./c-tds programa.ctds                  # compilación hasta código intermedio: genera programa.ci
./c-tds -o codigo.ci programa.ctds     # genera codigo.ci
./c-tds -run programa.ctds             # genera programa.ci, lo ejecuta e imprime lo que devuelve main
./c-tds -target sem programa.ctds      # solo hasta el análisis semántico: genera programa.sem
./c-tds -target parse programa.ctds    # solo hasta el parser: genera programa.sint
./c-tds -target scan programa.ctds     # solo el scanner: genera programa.lex
./c-tds -debug programa.ctds           # además imprime el código intermedio
```

Sin `-debug`, una compilación exitosa no imprime nada. Los errores léxicos, sintácticos, semánticos y de generación de código se
informan por `stderr` como `archivo:línea:columna: ...` y, si los hay, no se genera el archivo de salida. El análisis
semántico solo corre si el programa no tiene errores léxicos ni sintácticos.

Códigos de salida: `0` correcto, `1` errores en el programa fuente, `2` error de uso o de archivos.

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
| `tests/unit/` | tests unitarios en C de la tabla de símbolos, los tipos y el AST | (se ejecutan; no tienen `.expected`) |

Los tests del scanner usan `-target scan`, los del parser `-target parse` y los semánticos `-target sem`. Los tests de la interfaz de línea de comandos están en `tests/run_tests.sh`. Los tests unitarios los arma y corre `make test` (el script solo los ejecuta si recibe `UNIT_DIR`).
