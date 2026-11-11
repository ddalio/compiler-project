# Etapa 6 – Optimizador: documentación

Compilador **c-tds** para C-TDS. Implementación en C con **flex**, **bison** y **make**.
Checklist de la etapa: [etapa-6-optimizador-checklist.md](etapa-6-optimizador-checklist.md).
Explicación de cada archivo y función: [guia-del-codigo.md](guia-del-codigo.md).

## 1. División del trabajo

El proyecto lo realiza una sola persona, **Damián Dalio**, que se encargó de todas las
tareas de la etapa.

## 2. Decisiones de diseño, asunciones y aclaraciones

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
| 7 | Orden y punto fijo | Para cada método se aplican, en este orden, `fold`, `propagate`, `jumps` y `dce`, y se **repite hasta que ninguna cambie nada** (con un tope de 50 vueltas por seguridad). Cada una abre posibilidades a las otras: al propagar una constante, `fold` puede calcular la operación; al plegar una condición, `jumps` resuelve el salto; al resolver el salto, `dce` elimina el código inalcanzable. Después, una sola vez, `temps` renumera los temporales. |
| 8 | Alcance de la propagación | La propagación es **local a cada bloque básico**. Es más simple y segura que un análisis de todo el método y alcanza para el código que genera el compilador, porque cada expresión se calcula y se usa dentro del mismo bloque. |
| 9 | Estadísticas | Con `-debug`, el optimizador informa por `stderr` una línea por método (`[opt] f: 22 -> 2 instructions, 8 -> 0 temporaries`) y un total con las operaciones plegadas, los operandos reemplazados, las instrucciones eliminadas, los saltos y etiquetas resueltos y la cantidad de vueltas. Sin `-debug` no se imprime nada. |
| 10 | Lo que no se hace | No se eliminan las declaraciones de variables locales aunque queden sin uso (siguen reservando su posición del marco y su inicialización en 0), no se propaga entre bloques ni se hace análisis de vida de variables, no se reduce la fuerza de operaciones (`x * 2` queda como está) ni se optimiza el assembly. |
| 11 | Reales | Fuera de alcance por indicación del docente: un programa con `float` se sigue rechazando al generar el código intermedio. |

## 3. Diseño y decisiones clave

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

## 4. Detalles de implementación

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

Efecto medido sobre esos 52 programas: de **603 a 429 instrucciones** de código intermedio con `-opt all` (un 29% menos); en el programa de ejemplo de la
sección 3, el assembly pasa de 83 a 21 líneas. Sin errores de memoria con AddressSanitizer y UBSan. Los tests permanentes del optimizador se agregan en la
sección de testing.
