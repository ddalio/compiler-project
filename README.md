# c-tds - Compilador para el lenguaje C-TDS

Taller de Diseño de Software (Cod. 3306) - DC - FCEFQyN - UNRC - 2026

**Estado:** Etapa 2 (análisis sintáctico) en desarrollo. Etapa 1 (léxico) terminada.

Documentación: [etapa 1](docs/etapa-1-lexico.md), [etapa 2](docs/etapa-2-sintactico.md). Para estudiar el código (qué hace cada archivo y cada función): [guía del código](docs/guia-del-codigo.md).

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
| `-target <etapa>` | etapa hasta la que se compila: `scan` o `parse` (por defecto `parse`, la etapa actual) |
| `-debug` | imprime información de depuración |

Salida de cada etapa:

| Etapa | Archivo | Contenido |
|-------|---------|-----------|
| `scan` | `<nombre>.lex` | un token por línea: `línea:columna NOMBRE lexema` |
| `parse` | `<nombre>.sint` | árbol de parsing, un nodo por línea con 2 espacios de sangría por nivel |

Con `-debug` se imprime ese mismo contenido por pantalla; además, en `parse`, la traza del parser sale por `stderr`.

Ejemplos:

```
./c-tds programa.ctds                  # genera programa.sint
./c-tds -o arbol.sint programa.ctds    # genera arbol.sint
./c-tds -target scan programa.ctds     # genera programa.lex
./c-tds -debug programa.ctds           # además imprime el árbol
```

Sin `-debug`, una compilación exitosa no imprime nada. Los errores léxicos y sintácticos se informan por
`stderr` como `archivo:línea:columna: ...` y, si los hay, no se genera el archivo de salida.

Códigos de salida: `0` correcto, `1` errores en el programa fuente, `2` error de uso o de archivos.

## Tests

```
make test                          # compila y corre todos los tests
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

Los tests del scanner usan `-target scan`. Los tests de la interfaz de línea de comandos están en `tests/run_tests.sh`.
