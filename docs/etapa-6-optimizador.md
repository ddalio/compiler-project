# Etapa 6 – Optimizador: documentación

Compilador **c-tds** para C-TDS. Implementación en C con **flex**, **bison** y **make**.
Checklist de la etapa: [etapa-6-optimizador-checklist.md](etapa-6-optimizador-checklist.md).
Explicación de cada archivo y función: [guia-del-codigo.md](guia-del-codigo.md).

## 1. Decisiones de diseño, asunciones y aclaraciones

La cátedra define esta etapa así: *"las extensiones, optimizaciones y/o análisis a realizar son determinadas de común acuerdo con
cada grupo"*, *"por defecto, ninguna optimización es realizada"* y `-opt [optimización]` *"realiza la lista de optimizaciones; `all`
realiza todas las soportadas"*. La lista de la sección siguiente se acordó con el docente. Los números reales (`float`) quedan
**fuera de alcance**, por indicación del docente.

| # | Tema | Decisión |
|---|------|----------|
| 1 | Qué se optimiza | Se optimiza el **código intermedio** (el de tres direcciones de la etapa 4), no el assembly. Es independiente de la máquina, ya tiene un intérprete que permite comprobar que el programa optimizado hace lo mismo y lo que se mejora se refleja en el assembly sin trabajo extra (menos instrucciones, menos temporales y un marco de pila más chico). |
| 2 | Lista de optimizaciones | Cinco, cada una con un nombre para `-opt`: **`fold`** (plegado de constantes y simplificaciones algebraicas), **`propagate`** (propagación de constantes y de copias), **`dce`** (eliminación de código muerto), **`jumps`** (optimización de saltos y etiquetas) y **`temps`** (compactación de temporales). **`all`** las activa todas. |
| 3 | La opción `-opt` | **`-opt <nombre>`**: lleva siempre un argumento (la cátedra lo escribe entre corchetes, pero un argumento opcional sería ambiguo con el nombre del archivo fuente). Se puede repetir (`-opt fold -opt dce`) y las optimizaciones se suman. Un nombre desconocido o la falta del argumento es un error de uso (código 2) que lista los nombres válidos. **Sin `-opt` no se optimiza nada**: todos los targets producen exactamente la misma salida que antes. |
| 4 | Con qué targets vale | `-opt` optimiza el código intermedio, por lo que vale con `codinter`, `opt`, `assembly`, con el ejecutable (sin `-target`) y con `-run`: el código se optimiza **antes** de escribir el `.ci`, generar el assembly o interpretarlo. Con `scan`, `parse` o `sem` es un error de uso (no hay código intermedio que optimizar). |
| 5 | El archivo `.opt` **(extensión)** | La cátedra reserva la extensión `.opt` para la salida de esta etapa pero no tiene un target para ella. Se agrega **`-target opt`**, que escribe en `<fuente>.opt` el código intermedio optimizado, en el mismo formato que el `.ci`. Sin `-opt`, `-target opt` aplica **todas** las optimizaciones; si se eligen algunas con `-opt`, solo esas. Conviene confirmarlo con la cátedra. |
| 6 | Reglas de seguridad | El programa optimizado tiene que hacer lo mismo que el original: (a) la aritmética es de **32 bits con signo y da la vuelta**, igual que el intérprete y el código nativo; (b) una **división o un resto por cero nunca se pliega ni se elimina**, aunque su resultado no se use (el programa debe seguir fallando); `INT_MIN / -1` sí se pliega, con el resultado que da la vuelta; (c) **las llamadas nunca se eliminan ni se mueven** (pueden tener efectos) y solo se les puede quitar el resultado si nadie lo usa; (d) una llamada invalida todo lo que se sabe de las **variables globales**; (e) lo que se sabe de las variables se descarta en cada **bloque básico** nuevo, así el cortocircuito y los bucles nunca se rompen; (f) las escrituras a variables globales nunca se eliminan (son observables); (g) los valores iniciales en 0 de las locales no cambian. |
| 7 | Orden y punto fijo | Para cada método se aplican, en este orden, `fold`, `propagate`, `jumps` y `dce`, y se **repite hasta que ninguna cambie nada** (con un tope de seguridad de 50 vueltas más una por cada instrucción del método). Cada una abre posibilidades a las otras: al propagar una constante, `fold` puede calcular la operación; al plegar una condición, `jumps` resuelve el salto; al resolver el salto, `dce` elimina el código inalcanzable. Después, una sola vez, `temps` renumera los temporales. Cuando `fold` está activada, `propagate` además **pliega en el momento** cada operación que se vuelve constante al reemplazar un operando; así una cadena de constantes (`1+2+3+...`) se resuelve en una pasada y no en una vuelta por cada eslabón (ver el hallazgo de la sección 5). |
| 8 | Alcance de la propagación | La propagación es **local a cada bloque básico**. Es más simple y segura que un análisis de todo el método y alcanza para el código que genera el compilador, porque cada expresión se calcula y se usa dentro del mismo bloque. |
| 9 | Estadísticas | Con `-debug`, el optimizador informa por `stderr` una línea por método (`[opt] f: 22 -> 2 instructions, 8 -> 0 temporaries`) y un total con las operaciones plegadas, los operandos reemplazados, las instrucciones eliminadas, los saltos y etiquetas resueltos y la cantidad de vueltas. Sin `-debug` no se imprime nada. |
| 10 | Lo que no se hace | No se eliminan las declaraciones de variables locales aunque queden sin uso (siguen reservando su posición del marco y su inicialización en 0), no se propaga entre bloques ni se hace análisis de vida de variables, no se reduce la fuerza de operaciones (`x * 2` queda como está) ni se optimiza el assembly. |
| 11 | Reales | Fuera de alcance por indicación del docente: un programa con `float` se sigue rechazando al generar el código intermedio. |

## 2. Diseño y decisiones clave

El optimizador está en dos módulos: **`flow`** (análisis: qué lee y escribe cada instrucción, bloques básicos y grafo de flujo de control)
y **`opt`** (las cinco optimizaciones y el punto fijo). Cada optimización es una función que recorre el código de **un método**, lo
modifica en el lugar y devuelve si cambió algo.

### Análisis del flujo (`flow.h`, `flow.c`)

- **Qué lee y qué escribe cada instrucción.** `instr_reads` devuelve **punteros** a los operandos que lee una instrucción (así una
  optimización puede reemplazarlos) e `instr_write` el que escribe. Con ellas, ninguna optimización necesita saber cómo es cada
  instrucción: una operación aritmética lee dos operandos y escribe un resultado, un `jz` lee uno y no escribe, un `call` escribe su
  resultado (si lo tiene) y no lee operandos. `is_location` y `same_location` distinguen una variable o temporal de una constante.
- **Bloques básicos.** Un bloque empieza en la primera instrucción, en cada etiqueta y en la instrucción que sigue a un salto o a un
  `ret`. Dentro de un bloque la ejecución es siempre de arriba hacia abajo.
- **Grafo de flujo de control.** `cfg_build` une los bloques: el sucesor de un `jmp` es su destino; el de un salto condicional, su destino
  y el bloque siguiente; un `ret` no tiene sucesores; los demás siguen al bloque siguiente. El grafo permite saber qué bloques son
  alcanzables desde el principio del método.

### Las optimizaciones (`opt.h`, `opt.c`)

**`fold`: plegado de constantes y simplificaciones algebraicas.** Una operación entre dos constantes se reemplaza por su resultado,
calculado con aritmética de 32 bits que da la vuelta. Además, `x + 0`, `x - 0`, `x * 1`, `x / 1` se reemplazan por `x`; `x * 0`, `x - x`,
`x % 1` por 0; `x == x` por 1; y `x < x`, `x > x` por 0. Una división o un resto por cero no se pliega (decisión 6). Ejemplo:

```
t1 = 3 * 4      →  t1 = 12
t2 = x + 0      →  t2 = x
t3 = y - y      →  t3 = 0
```

**`propagate`: propagación de constantes y de copias.** Dentro de cada bloque se recuerda qué se sabe de cada variable: que vale una
constante (`a = 5`) o que es copia de otra (`b = a`). Cada lectura posterior se reemplaza por lo que se sabe. Lo que se sabe se descarta
al empezar un bloque, al escribir la variable (y las que eran copia de ella) y, para las globales, en cada llamada. Ejemplo:

```
a = 5                a = 5
b = a          →     b = 5
t1 = b + a           t1 = 5 + 5        (después, `fold` lo calcula)
```

Si `fold` también está activada, `propagate` lo calcula en el momento: `t1 = 5 + 5` pasa a `t1 = 10` y la constante se sigue propagando en el mismo recorrido.
Con `-opt propagate` solo no se pliega nada.

**`dce`: eliminación de código muerto.** Elimina (1) el **código inalcanzable**: los bloques a los que no se llega desde el principio del
método, por ejemplo lo que sigue a un `ret`; (2) las copias de una variable a sí misma (`x = x`); y (3) las instrucciones cuyo resultado nadie
lee, repitiendo hasta que no quede ninguna (eliminar una puede dejar sin uso a las que la alimentaban). Un `call` cuyo resultado no se usa
se conserva, pero deja de guardarlo. Las escrituras a globales y las divisiones o restos que podrían fallar nunca se eliminan. Ejemplo:

```
t1 = 7 * 3                      param 2
u = t1                    →     call f, 1
param 2                         ret 1
t2 = call f, 1
ret 1
ret 2
```

**`jumps`: optimización de saltos y etiquetas.** Un salto condicional con condición constante se resuelve: `jz 0, L` siempre salta (pasa a
ser un `jmp`) y `jz 1, L` nunca salta (desaparece); `jnz` al revés. Un salto a un salto va directo al destino final. Un salto a la instrucción
siguiente se elimina. Las etiquetas que ningún salto usa se eliminan. Ejemplo (el `while (false)` y el `if (true)` de un programa):

```
x = 1                   x = 1
jz 1, L1                x = 2
x = 2             →     jmp L3      (el cuerpo del while queda inalcanzable:
L1:                     x = 3        `dce` lo elimina)
L2:                     L3:
jz 0, L3                ret x
x = 3
jmp L2
L3:
ret x
```

**`temps`: compactación de temporales.** Después de las otras optimizaciones, muchos temporales dejaron de usarse. Esta optimización
renumera los que quedan como `t1`, `t2`, ... sin huecos y baja la cantidad de temporales del método. Como el marco de pila tiene una posición
por temporal, el marco se achica. Ejemplo (`u = 1 + 2; v = 7 * 3; return u * 2;` con `dce`): `3 -> 2 temporaries`.

### Un ejemplo completo

Con `-opt all`, este programa se reduce a dos instrucciones (de 22 a 2) y de 8 temporales a 0, porque todo se puede calcular en la compilación:

```
int g;                                      global @g
int main() {                                func main()
  int x; int y;                               local x
  x = 2 + 3 * 4;                              local y
  y = x;                              →       @g = 14
  if (y == 14) { g = y + 0; }                 ret 14
  else { g = 99; }                          end
  while (false) { g = g + 1; }
  return g * 1 + (x - x);
}
```

El programa devuelve 14 igual que antes. Las declaraciones de `x` y `y` se conservan (decisión 10).

## 3. Detalles de implementación

### Integración en el compilador (`main.c`)

- **La opción `-opt`** se lee en `parse_args`: el argumento se convierte en un bit con `opt_from_name` y los bits se suman en
  `Options.opt_mask`. Si el nombre no existe o falta, se informa con `opt_names()` la lista de nombres válidos. Si hay `-opt` con
  `scan`, `parse` o `sem` es un error de uso.
- **Dónde se optimiza.** `build_ir` (el que genera el código intermedio y lo usan `codinter`, `opt`, `assembly` y el ejecutable) llama a
  `opt_run` justo después de `irgen_generate`. Así **todas** las salidas (el `.ci`, el assembly, el ejecutable y `-run`) reflejan las
  optimizaciones pedidas, y sin `-opt` el camino no cambia.
- **`-target opt`** reutiliza `run_codinter` pasándole la extensión `.opt`. Si el usuario no eligió optimizaciones, `parse_args` activa todas.
- **`-debug`** pasa `stderr` a `opt_run` y se imprime el total, de modo que el código (por `stdout`) y las estadísticas no se mezclan.

### Verificación

Se verificó que **optimizar no cambia el comportamiento**, con dos pruebas sobre todos los programas de prueba del proyecto:

- **Con el intérprete:** cada programa de `tests/codinter/run/`, `run_errors/`, `native/`, `codinter/valid/` y `semantic/valid/` se ejecutó con `-run`
  sin optimizar y con cada una de las optimizaciones por separado, con algunas combinaciones y con `all` (9 configuraciones): la salida y el código de
  salida fueron idénticos en las **747 ejecuciones**. Esto incluye los programas con división por cero, recursión infinita y desborde de 32 bits, que
  siguen fallando de la misma manera.
- **Con código nativo:** los 52 programas de `tests/codinter/run/` y `tests/native/` se compilaron a ejecutable con cada optimización y se
  compararon sus códigos de salida con el del ejecutable sin optimizar: idénticos en las **312 compilaciones**.

Efecto medido sobre esos 52 programas: de **603 a 420 instrucciones** de código intermedio con `-opt all` (un 30% menos; eran 429 antes de que `propagate`
pliegue en el momento, ver el hallazgo de la sección 5); en el programa de ejemplo de la sección 3, el assembly pasa de 83 a 21 líneas. Sin errores de memoria
con AddressSanitizer y UBSan. Los tests permanentes del optimizador están en la sección 5.

## 4. Testing

Hay dos clases de tests del optimizador, y las dos comparan con resultados **escritos a mano antes de correr nada**, no copiados de la salida del compilador:

1. **Tests unitarios** (`tests/unit/test_flow.c` y `test_opt.c`): prueban el análisis y cada optimización directamente sobre código intermedio armado con la API (la explicación
   de cada función de prueba está en `guia-del-codigo.md`, sección 22).
2. **Tests de programas completos** (`tests/optimizer/` y los grupos del optimizador de `tests/run_tests.sh`): pasan un programa C-TDS por todo el compilador, y comprueban el
   código optimizado esperado, que el programa optimizado hace lo mismo que el original y la opción `-opt` de la línea de comandos.

### Tests unitarios

**Análisis (`tests/unit/test_flow.c`, 70 comprobaciones).** Qué operandos lee y escribe cada instrucción (y que `instr_reads` devuelve punteros a los campos reales, para poder
reemplazarlos), las funciones auxiliares, y los bloques básicos y el grafo de flujo en cuatro formas típicas: código recto, un `if/else`, un `while` y código inalcanzable; más
dos bordes: etiquetas consecutivas y un método que termina en una etiqueta.

**Optimizaciones (`tests/unit/test_opt.c`, 90 comprobaciones).** Para cada una de las cinco optimizaciones hay tres tipos de caso:

- **Lo que debe optimizar**, con una optimización a la vez (así se ve qué hace cada una sin que las otras la tapen).
- **Lo que no debe tocar**, que son las reglas de seguridad de la decisión 6: una división o un resto por cero no se pliega ni se elimina, las llamadas y las escrituras a globales
  no se eliminan, una llamada invalida las globales pero no las locales, y una etiqueta descarta lo que se sabe (el resultado de un cortocircuito que se une desde dos caminos no
  se propaga por la etiqueta de unión).
- **Bordes**: aritmética de 32 bits que da la vuelta (`INT_MAX + 1`, `INT_MIN / -1`), un salto a sí mismo, métodos sin temporales.

Además, un programa que usa las cinco juntas (que el punto fijo reduce de 12 instrucciones a 2), la comprobación de que cada optimización por separado hace menos que todas juntas y que
**optimizar dos veces da lo mismo que optimizar una** (idempotencia, en 7 programas).

**Los tests encontraron tres errores míos, no del optimizador.** Al escribir las expectativas a mano olvidé que, dentro de un bloque, después de `t1 = x` la propagación de copias
reemplaza también un `ret t1` posterior por `ret x`; los tres textos esperados (`redefinition`, `block_boundary` y `short_circuit`) estaban mal y se corrigieron. En esos tres casos lo
que importa verificar sigue cumpliéndose: nada se propaga a través de una etiqueta ni a través de una unión de caminos. Un cuarto error, también de un test, fue esperar que `x = x` quedara
igual cuando antes se sabía que `x` es copia de `t1` (la propagación lo reescribe como `x = t1`, que es correcto).

**Comprobación de que los tests detectan fallas.** Se rompió el optimizador a propósito de seis maneras, una por cada regla de seguridad, y en cada una el test unitario falló:
plegar también la división por cero (2 fallas), no invalidar las globales en una llamada (2), eliminar una división que puede fallar (1), no reiniciar la propagación en cada bloque (3),
eliminar las llamadas sin uso (1) y no dar la vuelta en `INT_MIN / -1` (1). Con el código restaurado vuelven a pasar todos. Sin errores de memoria con AddressSanitizer y UBSan.

### Tests de programas completos

**Código optimizado esperado (`tests/optimizer/`, 13 casos).** Nueve programas chicos, cada uno pensado para mostrar una cosa, con el código que debe quedar escrito a mano
(`<caso>.expected`, con todas las optimizaciones). Algunos tienen además un `<caso>.<optimización>.expected` con lo que deja **solo esa** optimización, para ver qué hace cada
una sin que las otras la tapen. El script corre `-target opt` (o `-opt <nombre> -target opt`), comprueba que no imprime nada y compara el `.opt` con lo esperado; después repite con
`-debug` y comprueba que el mismo código sale por `stdout`.

| Programa | Qué muestra |
|----------|-------------|
| `constant_folding` | `2 + 3 * 4` y las variables que lo reciben se reducen a `ret 24`; con `fold` solo se pliega `3 * 4` y con `propagate` solo se propagan las copias. |
| `dead_code` | Una llamada cuyo resultado no se usa **se conserva** (sin el resultado), el `return 4` inalcanzable desaparece (`dce` solo da lo mismo que `all`). |
| `constant_branches` | `if (true)` y `while (false)` se resuelven: queda `ret 2`; con `jumps` solo, se ven los saltos redirigidos pero las asignaciones no se tocan. |
| `loop_kept` | Un bucle con condición variable **no cambia**: no hay nada que plegar ni eliminar. |
| `division_by_zero_kept` | `10 / z` con `z = 0` se propaga a `10 / 0` pero **no se pliega ni se elimina** aunque nadie use el resultado, porque tiene que fallar al ejecutarse. |
| `globals_and_calls` | La global `g` vale 3 antes de la llamada y la llamada la cambia: se conserva la lectura `ret @g` y la llamada sin uso. |
| `short_circuit_constants` | `true && false` y `r \|\| true` se reducen a una constante (`ret 1`). |
| `constant_chain` | Una suma de 60 constantes (`1+2+...+60`) se pliega entera a `ret 1830` (= 60·61/2): el caso que mostró el defecto del tope de vueltas. |
| `fully_constant` | Un programa donde todo es constante: se reduce a `@g = 14` y `ret 14`. |

**Equivalencia (la prueba de que no cambia el comportamiento).** Los programas de `tests/codinter/run/`, `run_errors/`, `native/` y `optimizer/` se ejecutan con `-run` sin
optimizar y con `-opt all` y con cada una de las cinco optimizaciones sola (6 variantes por programa): el `stdout`, el mensaje de error de ejecución y el código de salida tienen que
ser **idénticos** (65 programas). Con gcc disponible, los programas de `tests/codinter/run/`, `tests/native/` y `tests/optimizer/` se compilan también a ejecutable, sin optimizar y con
`-opt all` (en `optimizer/` también con cada una por separado), y se comparan el código de salida y la salida (61 programas). Esto incluye los que **fallan a propósito**: una división
o un resto por cero tiene que seguir terminando con la misma señal, no desaparecer.

**Línea de comandos (27 casos).** `-opt` sin argumento, con un nombre desconocido (y que lista los nombres válidos), con la opción siguiente en lugar de un nombre, y con
`-target scan`, `parse` y `sem` (todos código 2); cada nombre válido (`fold`, `propagate`, `dce`, `jumps`, `temps`, `all`) escribe el `.opt` sin imprimir nada;
`-target opt` sin `-opt` es lo mismo que `-opt all`; repetir un nombre, cambiar el orden y escribir las cinco juntas equivale a `all` (importa el conjunto, no el orden);
sin `-opt` el `.ci` no se optimiza y `-debug` no menciona `[opt]`; `-opt all -target codinter` escribe el mismo código que el `.opt`; `-debug` da las estadísticas por `stderr` y el
código por `stdout`; `-run` con `-opt` da el mismo resultado y escribe el `.ci` optimizado; el nombre por defecto es `<fuente>.opt` junto al fuente y `-o` no puede pisar el fuente;
con `-target assembly` el assembly sale más corto y sin la multiplicación; un error semántico o un `float` se informan igual que sin `-opt` y no generan salida; y el ejecutable
optimizado sale con el valor esperado.

**Comprobación de que los tests detectan fallas.** Con los tests de programas se repitió la prueba de romper el optimizador a propósito, ahora con cuatro mutaciones del `opt.c`:

| Mutación | Tests que fallan |
|----------|------------------|
| No invalidar las globales en una llamada | `globals_and_calls` (esperado y equivalencia) y los viejos `eval_order_args`, `eval_order_binary` y `native/evaluation_order` |
| Eliminar una división que puede fallar | `division_by_zero_kept` (esperado y equivalencia) |
| Eliminar la llamada cuyo resultado no se usa | `dead_code` (con `all` y con `dce`) y `short_circuit` (interprete y nativo) |
| Plegar la división por cero | `division_by_zero_kept`, `run_errors/division_by_zero`, `run_errors/remainder_by_zero`, `native/division_by_zero` y `native/remainder_by_zero` |

Con el código restaurado (igual a `HEAD`) vuelven a pasar todos. Para el defecto del tope de vueltas (hallazgo 3) se hizo lo mismo: sin el plegado inmediato falla `test_opt`
(`st.rounds <= 3`), y con el comportamiento viejo completo (sin plegado inmediato y con tope fijo de 50) fallan además `constant_chain` y la cadena de 100 constantes.

**Hallazgos de los tests.** (1) La primera versión de `globals_and_calls` **no detectaba** la mutación "no invalidar globales": en `g = g + h()` la global se lee *antes* de la llamada,
así que su valor conocido sigue siendo válido aunque el optimizador se equivoque. Lo descubrió la prueba de mutaciones (solo fallaban tests viejos); se cambió el programa para
que también lea `g` **después** de otra llamada, que es donde el error se vería. (2) Un error del script, no del compilador: una variable nueva llamada `pass` pisaba el contador de
casos aprobados, y el resumen final decía 187 ok cuando las líneas `ok` eran 412; se renombró.
(3) **Un defecto real del optimizador, encontrado al preparar los problemas conocidos.** Al medir cuántas vueltas del punto fijo hacía cada programa de prueba, `native/many_temporaries`
(una suma de 60 constantes) llegaba al **tope de 50**: cada vuelta plegaba un solo eslabón de la cadena (`1+2`, después `3+3`, ...) y el código quedaba a medias
(`t1 = 1326 + 52`, `t2 = t1 + 53`, ... en lugar de `ret 1830`). El resultado seguía siendo correcto, por eso ningún test de equivalencia lo detectaba: solo se perdía optimización.
Se corrigió de dos maneras: el tope pasó a ser 50 más una vuelta por instrucción del método, y `propagate` pliega en el momento (si `fold` está activada), con lo que la cadena de 60
constantes pasó de 60 vueltas a 2 y una suma de 5000 constantes de 3,6 s a 0,4 s. Quedó un test unitario (cadena de 100 constantes, resultado 5050, a lo sumo 3 vueltas) y el programa
`constant_chain`.

**Efecto medido** (`-opt all`, instrucciones y temporales antes y después, sumados sobre cada carpeta; salen de `-debug`):

| Carpeta | Programas | Instrucciones | Temporales |
|---------|-----------|---------------|------------|
| `tests/codinter/run/` | 26 | 309 → 243 (21% menos) | 115 → 79 |
| `tests/native/` | 26 | 294 → 177 (39% menos) | 150 → 60 |
| `tests/optimizer/` | 9 | 147 → 35 (76% menos) | 85 → 7 |

**Resultado.** `make test` ejecuta **415 casos** y todos pasan (los 249 anteriores —etapas 1 a 5 y tests unitarios del optimizador— más 166 de programas completos del optimizador: 13 de código esperado, 65 de
equivalencia con el intérprete, 61 de equivalencia nativa y 27 de línea de comandos); `make test-asan` también, sin errores de memoria ni de comportamiento indefinido.

## 6. Problemas conocidos

Todo lo que sigue se probó con programas generados o escritos para esto; los tiempos son de la máquina de desarrollo y sirven para comparar.

**Lo que el optimizador no hace** (decisiones 8 y 10; el resultado es siempre correcto, solo queda código sin mejorar):

- **La propagación es local a cada bloque básico.** Un valor conocido no pasa una etiqueta ni un salto. En `x = 5; if (p == 1) { p = 2; } return x + 1;` (con `p` un
  parámetro) queda `x = 5` y, después del `if`, `t2 = x + 1`: no llega a `ret 6`. Tampoco saca del bucle lo que no cambia: en `k = 3; while (i < 5) { s = s + k * 4; ... }` el `k * 4`
  se sigue calculando en cada vuelta aunque `k` valga siempre 3. (Cuando la condición del `if` sí es constante, `jumps` resuelve el salto y entonces todo queda en un bloque y sí se calcula.)
- **No hay análisis de vida de variables.** Una escritura que otra escritura pisa se elimina solo si la variable nunca se lee en el método (`x = 1; x = 2; return x;` queda `ret 2`);
  si se lee en otro bloque, las dos escrituras se quedan (`x = 1; x = 2; if (...) {...} return x;`). Las declaraciones de variables locales sin uso se conservan.
- **Sin reducción de fuerza ni subexpresiones comunes.** `a * 2` queda como multiplicación y `(a - b) + (a - b)` calcula `a - b` dos veces. Sí se simplifican `x + 0`, `x * 1`, `x * 0`,
  `x - x`, `x / 1`, `x % 1`, `x == x` y `x < x`.
- **No se eliminan, mueven ni repiten llamadas**, aunque el método llamado no tenga efectos (decisión 6), y no se hace inlining. Una llamada cuyo resultado no se usa se queda sin el resultado.
- **`temps` solo renumera.** Compacta los temporales que sobreviven pero no comparte la misma posición entre temporales que no se usan a la vez, así que el marco de pila sigue teniendo
  una posición por temporal que queda.
- **No se optimiza el assembly** (decisión 1): el código nativo se mejora solo por lo que se mejora en el código intermedio (el programa de 3000 asignaciones `x = x + 1` pasa de 21017 a 14 líneas de assembly).

**Tiempo.** Hasta miles de sentencias el optimizador es rápido, pero crece de forma cuadrática en un **único bloque muy largo**:

| Programa generado | Sin `-opt` | Con `-opt all` |
|-------------------|------------|----------------|
| 3000 sentencias `x = x + 1` en un método | 0,03 s | 0,3 s |
| 20000 sentencias `x = x + 1` en un método | 0,13 s | **12,8 s** |
| 2000 métodos chicos | 0,19 s | 0,15 s |
| 400 `if/else` encadenados (3605 instrucciones, nada que optimizar) | 0,01 s | 0,02 s |

La causa está medida: con cada optimización sola el programa de 20000 sentencias tarda 0,1 s, y solo la combinación `fold` + `propagate` es lenta, porque con el plegado cada temporal queda
registrado como constante en la tabla de hechos del bloque, que es una lista con búsqueda lineal. Un programa real no tiene bloques de ese tamaño y no se cambió; se arreglaría con una tabla de hash.

**El tope de vueltas puede dejar un método a medias, sin avisar.** El punto fijo se corta a las 50 vueltas más una por instrucción del método (decisión 7). Con el plegado inmediato de `propagate`
los programas de prueba terminan en pocas vueltas (el máximo medido sobre todo el corpus de pruebas es de 5, en `constant_branches`, donde `jumps` y `dce` se ayudan entre sí), así que el tope es solo una protección; si alguna vez se alcanzara, el código sigue siendo correcto
pero no completamente optimizado, y `-debug` lo muestra en el total (`N rounds`). Antes de corregirlo (sección 5, hallazgo 3) una suma de 60 constantes alcanzaba el tope fijo de 50.

**La idempotencia solo se prueba sobre la API.** No hay forma de leer un `.opt` o un `.ci` de vuelta (el compilador no tiene un lector del código intermedio), así que "optimizar dos veces da lo
mismo" se verifica en los tests unitarios con 7 programas armados a mano y no sobre todos los programas de prueba desde la línea de comandos.

**Interfaz.**

- **`-opt` siempre lleva un argumento.** La cátedra lo escribe entre corchetes, pero un argumento opcional sería ambiguo con el nombre del archivo fuente (decisión 3). `-opt` sin nombre es un error de uso.
- **`-target opt` es una extensión propia.** La cátedra reserva `.opt` pero no tiene un target con ese nombre (decisión 5); conviene confirmarlo.
- **`-debug` informa solo los totales por método y de todo el programa**, no lo que hizo cada optimización en cada vuelta.
- **Los números reales** no están soportados (decisión 11): un programa con `float` se rechaza en el generador de código intermedio, con y sin `-opt`.

**Heredados de las etapas anteriores** (ver los problemas conocidos de cada una): el lenguaje no tiene entrada ni salida, el único resultado observable de un ejecutable es de 8 bits (por eso los tests
comparan módulo 256), el ejecutable termina con `SIGFPE` en una división por cero mientras el intérprete informa un error (los tests de equivalencia comparan cada uno contra su propia versión sin
optimizar), y la recursión muy profunda depende de la pila del sistema.

