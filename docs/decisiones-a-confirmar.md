# Decisiones a confirmar con la cátedra

Compilador **c-tds** para C-TDS (Taller de Diseño de Software, UNRC, 2026). Este documento reúne en un solo lugar las decisiones que la spec o el
enunciado no definen y que se resolvieron por criterio propio. Cada una indica qué se hizo, dónde está explicada (`E3 #7` = decisión 7 de
`etapa-3-semantico.md`) y qué cambiaría si la cátedra prefiere otra cosa. Están ordenadas por lo que más puede afectar a quien pruebe el
compilador: primero la interfaz, después el lenguaje y por último los formatos.

## Ya confirmadas por el docente

| Tema | Respuesta |
|------|-----------|
| Números reales (`float`) | No hace falta implementarlos en el código intermedio ni en el assembly (E4 #12, E5 #20, E6 #11). El análisis semántico sí los reconoce; un programa con `float` pasa `-target sem` y se rechaza al generar código. |
| Optimizaciones de la etapa 6 | La lista (plegado de constantes, propagación, código muerto, saltos y compactación de temporales) y que se apliquen al código intermedio (E6 #1 y #2). |

## 1. Interfaz de línea de comandos

La cátedra define `-o`, `-target` (`scan`, `parse`, `codinter`, `assembly`), `-opt` y `-debug`, y las extensiones `.lex`, `.sint`, `.sem`, `.ci`,
`.opt` y `.ass`. Lo que sigue es lo que se agregó o se interpretó.

| # | Decisión | Dónde | Qué pasaría si la cátedra prefiere otra cosa |
|---|----------|-------|----------------------------------------------|
| 1 | **`-target sem`**: la cátedra define la extensión `.sem` pero su tabla de targets no tiene uno para el análisis semántico. Se agregó. | E3 #7 | Es una opción más: quitarla no afecta a los demás targets (el análisis semántico corre igual antes del código intermedio). |
| 2 | **`-target opt`** escribe el código intermedio optimizado en `<fuente>.opt` (la cátedra reserva la extensión pero no un target). Sin `-opt`, aplica todas las optimizaciones. | E6 #5 | Igual que `-target sem`: aislado en `main.c`. |
| 3 | **`-opt <nombre>` siempre lleva un argumento** (la cátedra lo escribe entre corchetes). Un argumento opcional sería ambiguo con el nombre del archivo fuente. Se puede repetir. `-opt` con `scan`, `parse` o `sem` es un error de uso. | E6 #3, #4 | Si debe ser opcional, `-opt` sin nombre podría equivaler a `-opt all`, pero habría que fijar una regla para distinguirlo del archivo fuente. |
| 4 | **Sin `-target` se genera el ejecutable** `<fuente>.out`, como dice la cátedra. El ejecutable se arma con `gcc` (Linux x86-64) y el assembly no se guarda; `-target assembly` lo deja en `<fuente>.ass`. | E5 #2, #3 | Si la cátedra quiere otro ensamblador o enlazador, está aislado en `build_executable` (`main.c`). |
| 5 | **`-o` no puede apuntar al propio archivo fuente** (con el mismo nombre ni con un enlace): se rechaza con código 2. | E5 #23 | Es una protección; no cambia el comportamiento en el uso normal. |

## 2. El lenguaje

| # | Decisión | Dónde |
|---|----------|-------|
| 6 | **Coerciones `int`/`float`.** Las reglas 4, 7, 11 y 13 piden "el mismo tipo" y la regla 14 dice que se permiten coerciones o truncamientos; se interpreta que la 14 relaja a las demás (asignación, `return`, argumentos y `==`). `boolean` no es compatible con ellos. | E3 #14 |
| 7 | **Operador `%`**: sus operandos pueden ser `int` o `float` (regla 10), aunque la prosa de la spec dice que es el resto entre enteros. No se agregó una restricción que la lista de reglas no pide. | E3 #15 |
| 8 | **El programa de ejemplo de la spec** trae una llave `}` de más (se trata como errata) y su `main` es `void` pero devuelve valores, lo que viola la regla 6: el compilador lo informa como error semántico y no se relajó la regla. En los tests se usa como caso inválido y hay una versión corregida. | E2 #14, E3 #22 |
| 9 | **Las variables locales valen 0 al empezar.** La spec no define su valor inicial; así el programa es determinista y no depende de lo que haya en la pila. | E5 #9 |
| 10 | **Sin entrada ni salida.** La gramática no las tiene; el tutorial de assembly usa `extern int get_int();` y `extern void printInt(int x);`, pero `extern` no está en la gramática y el compilador lo rechaza. El único resultado observable es el valor que devuelve `main` (con 8 bits en el ejecutable: un 300 sale como 44). | E4 #18 |
| 11 | **Aritmética de 32 bits que da la vuelta** (`2147483647 + 1` vale `-2147483648`), igual en el código nativo y en el optimizador. | E4 #19, E5 #6, E6 #6 |
| 12 | **División por cero.** No se comprueba cada división: el ejecutable termina con `SIGFPE` (código 136). El optimizador nunca elimina ni pliega una división por cero, para que el programa siga fallando. | E5 #19, E6 #6 |
| 13 | **Literales.** Un real es exactamente `digit+ '.' digit+` (`1.` y `.5` no lo son). Los enteros no llevan signo (`-5` son dos tokens), por lo que `-2147483648` no se puede escribir como literal. | E1 #3, #4 |

## 3. Formatos de salida

La cátedra define las extensiones pero no el contenido de los archivos intermedios. Son formatos propios, descritos en el README y en la
documentación de cada etapa:

| Archivo | Contenido | Dónde |
|---------|-----------|-------|
| `.lex` | un token por línea: `línea:columna NOMBRE lexema` | E1 #10 |
| `.sint` | árbol de parsing con 2 espacios de sangría por nivel; no muestra los signos de puntuación | E2 #3 |
| `.sem` | AST anotado con el tipo de cada expresión y la clase de cada identificador | E3 #8 |
| `.ci` y `.opt` | código de tres direcciones (`t1 = a + b`, `jz t1, L1`, `param`, `call`, `ret`) | E4 #1 y siguientes, E6 #5 |
| `.ass` | assembly x86-64 de Linux en sintaxis AT&T, con cada instrucción intermedia como comentario | E5 #1, #21 |
