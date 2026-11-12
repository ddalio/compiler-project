# Etapa 1 – Análisis Léxico: documentación

Compilador **c-tds** para C-TDS. Implementación en C con **flex** y **make**.


## 1. División del trabajo

El proyecto lo realiza una sola persona, **Damián Dalio**, que se encargó de todas las
tareas de la etapa: especificación léxica, implementación del scanner y del CLI, tests y documentación.

## 2. Decisiones de diseño, asunciones y aclaraciones

La especificación (`01-TDS-spec-lenguaje`) no define todos los detalles. Estas son las
decisiones tomadas:

| # | Tema | Decisión |
|---|------|----------|
| 1 | Alcance | Solo se implementa el scanner. En esta etapa `-target scan` era el único target disponible; los demás se rechazaban con un mensaje. `-opt` todavía no existe. _(Actualización etapas 2 a 5: se agregaron los targets `parse`, `sem`, `codinter` y `assembly`; sin `-target` se genera hoy el ejecutable. `-target scan` sigue funcionando igual.)_ _(Actualización etapa 6: existe `-opt <nombre>` y el target `opt`; `-opt` con `scan` es un error de uso porque no hay código intermedio que optimizar, y sin `-opt` nada cambia.)_ |
| 2 | Identificadores | Exactamente la gramática: `alpha (alpha \| digit \| '_')*`. Un identificador **no puede empezar con `_`**; el `_` suelto es un error léxico. |
| 3 | Literales reales | Exactamente la gramática: `digit+ '.' digit+`. Ni `1.` ni `.5` son literales: el `.` suelto es un error léxico. |
| 4 | Literales enteros | Solo dígitos, sin signo: `-5` son dos tokens (`MINUS`, `INT_LITERAL`), como pide la gramática (`- <expr>`). Un entero mayor a 2147483647 es un error léxico. Los ceros a la izquierda se aceptan y valen en decimal (`007` = 7). |
| 5 | `true` / `false` | Son palabras reservadas (`KW_TRUE`, `KW_FALSE`), no un token de literal booleano. |
| 6 | Espacios | Se descartan espacio, tab, `\r` y `\n` (el `\r` permite archivos con fin de línea CRLF). |
| 7 | Comentarios | `// ...` hasta fin de línea y `/* ... */` multilínea, **sin anidar** (como en C). Un `/*` sin cerrar es un error léxico ubicado en el comienzo del comentario. |
| 8 | Errores | Un error léxico no detiene el análisis: se informa y se sigue, así una corrida reporta todos los errores. Los errores van a `stderr` con el formato `archivo:línea:columna: lexical error: mensaje`. |
| 9 | Posición | Líneas y columnas desde 1. Un tab ocupa una columna. La columna cuenta bytes, no caracteres UTF-8. |
| 10 | Salida | El archivo `.lex` tiene un token por línea: `línea:columna NOMBRE lexema`. Si hay errores léxicos **no se genera** el archivo. |
| 11 | Nombre de salida | Por defecto es el nombre del archivo fuente con extensión `.lex`, en la misma carpeta que el archivo fuente. `-o <salida>` lo reemplaza. |
| 12 | Sin `-debug` | Una corrida exitosa no imprime nada. Con `-debug` los tokens también se imprimen por `stdout`. |
| 13 | Código de salida | `0` correcto, `1` errores léxicos, `2` error de uso o de archivos. |
| 14 | Nombre de archivo | Debe terminar en `.ctds`. Un argumento que empieza con `-` se interpreta como opción, por lo que ningún archivo de entrada puede empezar con `-`. |

## 3. Diseño y decisiones clave

**Módulos**

| Archivo | Responsabilidad |
|---------|-----------------|
| `src/scanner.l` | Especificación flex: expresiones regulares, posición de los tokens y errores léxicos. |
| `src/scanner.h` | Interfaz del scanner (`scanner_init`, `yylex`, posición y cantidad de errores). |
| `src/tokens.h`, `src/tokens.c` | Nombre imprimible de cada token. _(Actualización etapa 2: los códigos de los tokens dejaron de ser un `enum TokenType` escrito a mano; ahora los define bison, ver la documentación de la etapa 2.)_ |
| `src/main.c` | Línea de comandos, validación de argumentos y escritura del `.lex`. _(Actualización etapa 2: también ejecuta el parser.)_ |

## 4. Detalles de implementación

- **Palabras reservadas vs. identificadores.** Flex elige el lexema más largo y, ante empate, la
  primera regla. Las palabras reservadas están antes que `ID`: `while` es `KW_WHILE`, pero
  `whiletrue` es un `ID` porque es un lexema más largo.
- **Seguimiento de posición.** La macro `YY_USER_ACTION` guarda donde empieza cada token y
  avanza la posición actual sobre el lexema reconocido, contando los `\n`. No se usa `yylineno`,
  porque no da columnas.
- **Comentarios de bloque.** Se resuelven con una *start condition* exclusiva (`BLOCK_COMMENT`),
  porque la expresión regular de `/* ... */` sería más difícil de leer y de mantener. Al llegar a EOF
  dentro del comentario se informa el error con la posición donde empezó.
- **Validación de enteros.** El patrón `digit+` acepta cualquier cantidad de dígitos; el rango se
  verifica con `strtoll` (`ERANGE` o valor mayor a 2147483647).
- **`&` y `|` sueltos.** Tienen una regla propia que sugiere `&&` / `||` en el mensaje.
- **Tests.** Los `.expected` están escritos a mano a partir de la especificación, no generados por el
  scanner. Ver `tests/run_tests.sh` y la sección de comandos del `README.md`. Desde la etapa 2 estos tests se ejecutan con `-target scan`.

## 5. Problemas conocidos

- `-2147483648` no se puede escribir: el literal `2147483648` se rechaza y el signo es un token aparte.
- Un literal real se valida solo contra el rango de `double`; no hay chequeo del rango de `float`.
- `123abc` se reconoce como `INT_LITERAL 123` seguido de `ID abc`. El scanner no lo marca como error;
  corresponde a la etapa siguiente rechazar esa secuencia. _(Resuelto en la etapa 2: el parser la rechaza
  con `unexpected identifier`.)_
- Los caracteres no ASCII (por ejemplo `ñ`) se reportan byte por byte, una vez por byte.
- El ejemplo de programa de la especificación tiene una llave `}` de más al final. El test
  `program` usa el ejemplo sin esa llave.
- Los comentarios de bloque no se pueden anidar.
