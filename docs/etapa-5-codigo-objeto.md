# Etapa 5 – Generador de Código Objeto: documentación

Compilador **c-tds** para C-TDS. Implementación en C con **flex**, **bison** y **make**.
Checklist de la etapa: [etapa-5-codigo-objeto-checklist.md](etapa-5-codigo-objeto-checklist.md).
Explicación de cada archivo y función: [guia-del-codigo.md](guia-del-codigo.md).

## 1. Decisiones de diseño, asunciones y aclaraciones

La especificación (`00-TDS-proyecto` y el tutorial de generación de assembly de la cátedra, `x86-64-architecture-guide`)
deja abiertos varios detalles. Estas son las decisiones tomadas **antes** de escribir código. Las que interpretan algo que la
cátedra no define están marcadas con **(interpretación)**. Varias se comprobaron antes de decidir, escribiendo a mano pequeños
programas en assembly y ensamblándolos en la máquina de desarrollo (Linux x86-64, `gcc` 13.3 y GNU `as` 2.42): el pasaje de
más de 6 argumentos, la división con negativos, el resultado de `INT_MIN / -1` y la división por cero.

### Herramientas y línea de comandos

| # | Tema | Decisión |
|---|------|----------|
| 1 | Plataforma y sintaxis | El assembly es **x86-64 de Linux en sintaxis AT&T**, la que acepta el ensamblador de GNU y la que usa el tutorial (`mov origen, destino`, registros con `%`, inmediatos con `$`). El archivo de salida tiene la extensión `.ass`, que define la cátedra para esta etapa. |
| 2 | Cómo se obtiene el ejecutable | Se invoca **`gcc -x assembler - -o <ejecutable>`** y el assembly se le pasa **por su entrada estándar** (una tubería), sin archivos temporales. `gcc` ensambla y enlaza con la biblioteca de C, cuyo código de arranque es el que llama a `main`. Se lanza con `fork` y `execvp` pasando los argumentos como arreglo, **sin intérprete de comandos**, así ningún nombre de archivo necesita comillas. `-x assembler` es necesario porque `gcc` no reconoce la extensión `.ass` (sin él la trata como un script del enlazador). Si `gcc` no se puede ejecutar, o falla, el compilador informa el motivo por `stderr` y termina con código 2 (error de herramientas, no del programa fuente). |
| 3 | Qué hace el compilador **sin `-target`** | La cátedra dice que el comportamiento por defecto es realizar la compilación completa y generar el ejecutable `nombreArchivo.out`, y que `-target` detiene la compilación en una etapa. Por eso, **sin `-target`, el compilador genera el ejecutable** (`<fuente>.out`, o el archivo de `-o`): corre todo el front end, genera el código intermedio, el assembly y lo ensambla y enlaza. El assembly no se guarda. `-target assembly` se detiene antes y escribe el **`.ass`** (en el archivo de `-o`, si se lo da). Los demás targets (`scan`, `parse`, `sem`, `codinter`) siguen funcionando igual. Hasta la etapa 4 el target por defecto era `codinter`; esto lo cambia (los tests que lo usaban pasan a indicar `-target codinter`). |
| 4 | `-o` y `-debug` | `-o` nombra el **ejecutable** (como dice la cátedra: "renombra el archivo ejecutable") o, con `-target assembly`, el `.ass`. Con `-debug` el assembly también sale por `stdout`. |
| 5 | Errores | Un programa con errores léxicos, sintácticos, semánticos o de generación (por ejemplo, un `float`) no genera ni `.ass` ni ejecutable, y termina con código 1. Un programa correcto genera siempre assembly válido: si `gcc` lo rechazara, es un error del compilador y se informa como error de herramientas (código 2). |
| 23 | `-o` y el archivo fuente | Si `-o` apunta al **propio archivo fuente** (con el mismo nombre o con otro nombre del mismo archivo, por ejemplo un enlace simbólico), el compilador lo rechaza (`output file '...' would overwrite the input file`, código 2) antes de hacer nada. Con `-o` el ejecutable o el archivo de la etapa se escribe donde se indique, y sin esta comprobación `c-tds -o programa.ctds programa.ctds` reemplazaba el fuente por el ejecutable. Se detectó al preparar los problemas conocidos de esta etapa y afectaba a todos los targets. |

### Modelo de ejecución

| # | Tema | Decisión |
|---|------|----------|
| 6 | Enteros de 32 bits | Se usan **instrucciones de 32 bits** (`movl`, `addl`, `subl`, `imull`, `negl`, `cmpl`, `idivl` y los registros `%r10d`, `%r11d`, `%eax`), no las de 64 bits del tutorial. Así un `int` es de 32 bits con signo como pide la spec y el desborde **da la vuelta** (decisión 19 de la etapa 4): `2147483647 + 1` vale `-2147483648`. Las constantes del código intermedio caben siempre en 32 bits. |
| 7 | Sin asignación de registros | Como en el tutorial, **cada parámetro, variable local y temporal vive en una posición del marco de pila** y cada operación carga sus operandos en los registros temporales **`%r10`** y **`%r11`**, opera y guarda el resultado en la pila. Se usan además `%rax` y `%rdx` (división y valor devuelto) y los registros de argumentos. No se usan los registros que el llamado debe preservar (`%rbx`, `%r12`–`%r15`), por lo que no hay que guardarlos. El código resulta lento a propósito: la cátedra pide concentrarse en la corrección. |
| 8 | Marco de pila | Cada método tiene un marco con **una posición de 8 bytes** por parámetro, local y temporal (se usan los 4 bytes de abajo; es lo más simple y deja todo alineado). La posición `k` (empezando en 0) está en **`-8*(k+1)(%rbp)`**. El orden es: primero los parámetros, después las locales y al final los temporales (`t1` primero). El prólogo es `pushq %rbp`, `movq %rsp, %rbp` y `subq $N, %rsp`, y el epílogo `leave` y `ret` (equivalente a `enter`, pero más legible). `N` es `8 * (nparams + nlocals + ntemps)` **redondeado hacia arriba a un múltiplo de 16**: como al entrar al método `%rsp` vale 8 módulo 16 (la dirección de retorno) y `pushq %rbp` lo deja en 0, con `N` múltiplo de 16 la pila queda alineada a 16 bytes. |
| 9 | Variables locales | **Se inicializan en 0 en el prólogo** (`movl $0, ...`). La spec no define su valor; así el programa es determinista y no depende de la basura que haya en la pila. Los temporales no hace falta inicializarlos. |
| 10 | Variables globales | Van en la sección de datos (`.data`) como `ctds_<nombre>: .long 0`, o sea **inicializadas en 0**, y se acceden con direccionamiento relativo a `%rip` (`ctds_g(%rip)`), que sirve también para ejecutables independientes de la posición. |
| 11 | Nombres de símbolos | Todo método y toda global llevan el prefijo **`ctds_`** (`ctds_fact`, `ctds_g`), **salvo `main`**, que tiene que llamarse así porque lo llama la biblioteca de C. El prefijo evita choques con los símbolos de la biblioteca: un método llamado `printf`, `exit` o `puts` es válido en C-TDS y, sin prefijo, fallaría al enlazar o reemplazaría a la función de la biblioteca. `main` se declara `.globl`. |
| 12 | Etiquetas | La etiqueta `L<n>` del código intermedio pasa a **`.L<n>`**: el prefijo `.L` hace que el ensamblador la trate como local (no aparece en la tabla de símbolos) y, como los números son únicos en todo el programa, no chocan entre métodos. Las etiquetas que necesita el propio generador para la división (decisión 18) se llaman `.Ldiv<n>` y `.Ldiv<n>e`, que no pueden confundirse con las anteriores (después de `.L` siguen letras y no solo dígitos). |

### Llamadas y valor devuelto

| # | Tema | Decisión |
|---|------|----------|
| 13 | Convención de llamada | La estándar de Linux (System V AMD64): los **primeros 6 argumentos** van en `%rdi`, `%rsi`, `%rdx`, `%rcx`, `%r8` y `%r9` (en sus partes de 32 bits: `%edi`, `%esi`, `%edx`, `%ecx`, `%r8d`, `%r9d`), y el **resto en la pila**, apilados de derecha a izquierda (el 7.º argumento queda en `16(%rbp)` del llamado, el 8.º en `24(%rbp)`, ...). El llamador retira los argumentos de la pila con `addq` después del `call`. El valor devuelto vuelve en **`%eax`**. Se probó a mano con 8 argumentos. |
| 14 | Argumentos y `PARAM` | Una instrucción `param` **no genera código por sí sola** (solo un comentario): el generador junta los `param` y, al llegar al `call`, carga los argumentos en los registros o los apila. Es equivalente porque entre los `param` y su `call` no hay otras instrucciones (decisión 10 de la etapa 4), y las globales que podrían cambiar ya se copiaron a temporales (decisión 9). En el llamado, los parámetros que llegan en registros se guardan en sus posiciones del marco en el prólogo, y los que llegan por pila se **copian** a sus posiciones (así todos los parámetros se acceden igual). |
| 15 | Alineación de la pila | Antes de cada `call`, `%rsp` tiene que estar alineado a 16 bytes. Dentro de un método ya lo está (decisión 8); si hay una cantidad **impar** de argumentos en la pila, el llamador agrega `subq $8, %rsp` de relleno antes de apilarlos y lo retira junto con ellos. |
| 16 | `main` y el valor de salida | El **código de salida del ejecutable** es lo que devuelve `main` (sus 8 bits bajos: el sistema operativo solo conserva 8 bits). Es el único resultado observable, porque C-TDS no tiene entrada ni salida. Todo `ret` de `main` **sin valor** (un `main` `void`) lleva antes `movl $0, %eax`, como en el tutorial, para que el programa termine con código 0; con valor, `movl <valor>, %eax`. En los demás métodos, un `ret` sin valor no toca `%eax`. Un `boolean main` devuelve 0 o 1. |

### Operaciones

| # | Tema | Decisión |
|---|------|----------|
| 17 | Comparaciones y booleanos **(interpretación)** | `t = a < b` se traduce con `cmpl` y una instrucción **`set*`** (`setl`, `setg`, `sete`) seguida de `movzbl`, que deja 0 o 1 en el registro. El tutorial usa `cmov*` con dos registros cargados con 0 y 1; `set*` hace lo mismo con menos instrucciones. `t = ! a` es `cmpl $0` y `sete`. Los saltos `jz` y `jnz` cargan el operando en `%r10d` (puede ser una constante, y `cmpl` no admite dos inmediatos), hacen `testl` y saltan con `je` o `jne`. |
| 18 | División y resto | `a / b` y `a % b` cargan `a` en `%eax`, extienden el signo a `%edx` con `cltd`, cargan `b` en `%r10d` y usan `idivl`: el cociente queda en `%eax` y el resto en `%edx`, truncados hacia cero como en C. Para que el resultado sea el de la aritmética de 32 bits que da la vuelta (decisión 19 de la etapa 4), antes de dividir se **compara el divisor con -1**: si lo es, el cociente es `-a` (`negl`) y el resto 0, sin ejecutar `idivl`, porque en x86 `INT_MIN / -1` provoca una excepción de hardware aunque en la aritmética de 32 bits que da la vuelta su resultado es `INT_MIN` (se comprobó a mano). |
| 19 | División por cero | **No se intercepta**: `idivl` con divisor 0 es una excepción de hardware y el sistema operativo termina el programa con la señal `SIGFPE` (código de salida 136 en `bash`). La spec no define el comportamiento, y comprobarlo en cada división agregaría código a todas las operaciones. Queda documentado como comportamiento del ejecutable. |
| 20 | Números reales | Siguen sin soportarse: un programa con `float` se rechaza al generar el código intermedio (decisión 12 de la etapa 4), antes de llegar a esta etapa. La cátedra pide primero los enteros. |

### Formato del `.ass` y testing

| # | Tema | Decisión |
|---|------|----------|
| 21 | Contenido del `.ass` | Un comentario con el nombre del fuente, la sección `.data` con las globales, la sección `.text` con los métodos en el orden del fuente, y al final **`.section .note.GNU-stack,"",@progbits`**, que le dice al enlazador que el programa no necesita una pila ejecutable (sin ella, `ld` emite una advertencia en cada compilación, como se comprobó). Cada instrucción del código intermedio va precedida de un **comentario con su texto** (`# t1 = a + b`) y cada método lleva un comentario con su encabezado: el assembly queda legible y se puede relacionar línea a línea con el `.ci`. |
| 22 | Testing de extremo a extremo | Como el único resultado observable de un ejecutable es su código de salida, los programas de `tests/codinter/programs/` (cada `.expected` trae, calculado a mano, el valor que devuelve `main`) **se compilan, se ejecutan y su código de salida se compara con ese valor, módulo 256**. Esos tests requieren Linux x86-64 y `gcc`; si no están, se **saltean** con un aviso (no fallan). La división por cero se prueba aparte: debe terminar con código 136. |

Ejemplo del formato y de las decisiones 6 a 16. El programa:

```
int g;
int add(int a, int b) { return a + b; }
void main() { g = add(3, 4); }
```

tiene este código intermedio:

```
global @g

func add(a, b)
  t1 = a + b
  ret t1
end

func main()
  param 3
  param 4
  t1 = call add, 2
  @g = t1
  ret
end
```

y se traduce a este assembly:

```
# c-tds: programa.ctds
        .data
ctds_g: .long   0

        .text
ctds_add:                       # func add(a, b)
        pushq   %rbp
        movq    %rsp, %rbp
        subq    $32, %rsp       # 3 posiciones = 24 bytes -> 32
        movl    %edi, -8(%rbp)  # a
        movl    %esi, -16(%rbp) # b
        # t1 = a + b
        movl    -8(%rbp), %r10d
        addl    -16(%rbp), %r10d
        movl    %r10d, -24(%rbp)
        # ret t1
        movl    -24(%rbp), %eax
        leave
        ret

        .globl  main
main:                           # func main()
        pushq   %rbp
        movq    %rsp, %rbp
        subq    $16, %rsp       # 1 posicion = 8 bytes -> 16
        # param 3
        # param 4
        # t1 = call add, 2
        movl    $3, %edi
        movl    $4, %esi
        call    ctds_add
        movl    %eax, -8(%rbp)
        # @g = t1
        movl    -8(%rbp), %r10d
        movl    %r10d, ctds_g(%rip)
        # ret
        movl    $0, %eax
        leave
        ret

        .section .note.GNU-stack,"",@progbits
```

Este es el **resultado real** del generador para ese programa (se verificó que se ensambla y que el ejecutable termina con
código 0). Los argumentos se cargan en `%edi` y `%esi` recién en el `call` (decisión 14). En `ctds_add` las posiciones del marco son
`a` en `-8(%rbp)`, `b` en `-16(%rbp)` y `t1` en `-24(%rbp)`; el marco de 24 bytes se redondea a 32. `main`, que es `void`, termina con
`movl $0, %eax` (decisión 16).

## 2. Diseño y decisiones clave

### Generador de assembly (`x86gen.h`, `x86gen.c`)

`x86_generate(out, prog, source_name)` recorre el programa en código intermedio y escribe el assembly en un `FILE *`. Es una
traducción **instrucción por instrucción**: cada instrucción del código intermedio se traduce a una secuencia fija de instrucciones
x86-64, sin mirar las vecinas. Eso hace al generador simple y fácil de verificar, a costa de código lento (decisión 7).

- **El patrón de casi toda operación** es: cargar el primer operando en `%r10d`, combinar con el segundo (que puede ser una
  constante, una posición del marco o una global, porque x86 permite que el segundo operando de `addl`, `subl`, `imull` y `cmpl` esté en
  memoria) y guardar el resultado en su posición. Por ejemplo, `t1 = a + b` es `movl -8(%rbp), %r10d`, `addl -16(%rbp), %r10d` y
  `movl %r10d, -24(%rbp)`.
- **`param` no genera código**: el generador guarda los argumentos pendientes (`pending`) y en el `call` los carga en los registros
  o los apila. Como entre los `param` y su `call` no hay otras instrucciones, cargar los argumentos recién en el `call` es equivalente a hacerlo
  en cada `param`.
- **Las posiciones del marco se calculan con `ir_slot`** (en `ir.c`), que da la posición de cada parámetro, local y temporal
  dentro del marco del método.
- **Cada método termina con un retorno.** Si su código no termina en un `ret` (por ejemplo, un método con tipo que termina en una
  etiqueta, como `spec_example_fixed`), el generador agrega un `leave` y `ret` final; sin él, la ejecución caería en el código del
  método siguiente. Para `main` ese retorno devuelve 0.
- **Los comentarios explican el código**: cada instrucción del código intermedio aparece como comentario antes de su
  traducción (se imprime con `ir_print_instr`, que se separó de `ir_print` para esto), y los comentarios al final de línea están
  alineados en la columna 32. Es lo que permite leer el assembly junto con el `.ci`.
- **La división se traduce a una secuencia con un salto** (decisión 18): se compara el divisor con -1 para evitar la excepción de
  `INT_MIN / -1`. Cada división usa un par de etiquetas `.Ldiv<n>` y `.Ldiv<n>e` con un contador propio.
- **La alineación de la pila** para los `call` con una cantidad impar de argumentos en la pila se resuelve con un relleno de 8 bytes
  (decisión 15), que se quita junto con los argumentos.

### Verificación del generador

Se probó con un programa temporal (que no se versiona) que compila a assembly, lo ensambla con `gcc`, ejecuta el resultado y
compara su código de salida. Con los **26 programas** de `tests/codinter/programs/` el código de salida coincide con el valor que
devuelve `main` según su `.expected` (módulo 256), y el ensamblado no emite ninguna advertencia. Además se probaron casos propios de esta etapa:
funciones de 7, 8 y 9 argumentos (por pila, con y sin relleno), una llamada de 7 argumentos como argumento de otra, una recursión con
9 argumentos, un método llamado `printf` y otro `exit` y globales llamadas `puts` y `malloc` (el prefijo `ctds_` evita el choque
con la biblioteca de C), la división y el resto por cero (el ejecutable termina con `SIGFPE`, código 136, como se documentó en la
decisión 19), `INT_MIN / -1`, un resultado negativo (`-5` sale como 251) y uno mayor que 255 (300 sale como 44), una variable local
sin inicializar (vale 0) y el orden de evaluación con una global modificada por un método. Sin errores de memoria del generador con
AddressSanitizer y UBSan. Los tests permanentes se agregan en la sección de testing.

## 3. Detalles de implementación

### Integración en el compilador (`main.c`)

- **Sin `-target` hay una compilación completa.** `Options.target` vale `NULL` cuando no se da `-target`, y `main` despacha
  a `run_executable`. Con `-target`, las cinco etapas (`scan`, `parse`, `sem`, `codinter`, `assembly`) son las únicas válidas;
  cualquier otro nombre es `unknown target`. Como ya no queda ninguna etapa "todavía no disponible", se eliminó ese mensaje.
- **El generador de código intermedio se factorizó en `build_ir`**: corre el front end y `irgen_generate` y devuelve el programa
  en código intermedio solo si no hubo ningún error. Lo usan las tres etapas que lo necesitan: `run_codinter` (escribe el `.ci`),
  `run_assembly` (escribe el `.ass`) y `run_executable` (arma el ejecutable). La tabla de símbolos y el AST siguen vivos hasta que
  esas funciones terminan, como pide la etapa 4.
- **El assembly se genera siempre en una cadena en memoria** (`make_assembly`, con `open_memstream`) y de ahí va al archivo, al
  `gcc` o a `stdout` si hay `-debug`. Así `-debug` no genera el assembly dos veces y el assembly que se imprime es exactamente el
  que se ensambla.
- **`build_executable` lanza `gcc` con `fork` y `execlp`** y le pasa el assembly por una tubería conectada a su entrada estándar.
  Detalles:
  - se usa `gcc -x assembler - -o <ejecutable>`: `-x assembler` porque `gcc` no reconoce la extensión `.ass`, y `-` para leer
    de la entrada estándar; no se usa un intérprete de comandos, por lo que el nombre del ejecutable no necesita comillas;
  - antes de escribir se ignora `SIGPIPE`: si `gcc` termina antes de leer todo (porque falló), el compilador no muere por
    la señal sino que informa el error;
  - `write_all` repite la escritura si la interrumpe una señal y devuelve error si falla;
  - el estado de `gcc` se espera con `waitpid`: si el hijo terminó con 127 (no se pudo ejecutar `gcc`) se informa
    `cannot run 'gcc': is it installed? ...` con la sugerencia de usar `-target assembly`; si terminó con otro estado distinto de 0
    se informa `gcc failed to assemble and link the program`. En ambos casos el código de salida es 2 (error de herramientas).
- **Errores**: igual que en las etapas anteriores, si el programa tiene errores léxicos, sintácticos, semánticos o de generación
  (un `float`), no se genera nada y el código de salida es 1.

### Cambios en las etapas anteriores

- **El target por defecto** dejó de ser `codinter`: sin `-target` se genera el ejecutable. Los tests que verificaban el `.ci` por
  defecto ahora pasan `-target codinter`. El test que comprobaba que `-target assembly` se rechazaba ("not available yet") se
  reemplazó por tests de que lo genera. Los casos anteriores siguen pasando (con esos cambios).
- Se agregaron `x86gen.o` al Makefile (sección 3 de esta etapa) y las llamadas al sistema `fork`, `pipe`, `execlp`, `waitpid` y
  `signal` a `main.c`; el programa sigue sin necesitar nada fuera de POSIX y de `gcc` para el ejecutable.

### Verificación de la integración

Se probó el compilador completo con un programa que calcula el factorial de 5: sin `-target` genera `p.out` (y no deja ningún
archivo intermedio) y el ejecutable termina con código 120; con `-o` lo nombra; con `-target assembly -o salida.s` escribe el
assembly, que ensamblado aparte con `gcc -x assembler` da el mismo ejecutable (código 120); con `-debug` imprime el assembly por
`stdout` y además arma el ejecutable; un error semántico y un programa con `float` no generan ejecutable (código 1); con `PATH` vacío (sin `gcc`)
informa `cannot run 'gcc'` con código 2. Con AddressSanitizer y UBSan no hay errores de memoria sobre los programas de prueba de
las etapas 1 a 4 por el camino del ejecutable y por el del assembly. Los tests permanentes se agregan en la sección de testing.

## 4. Testing

Los casos están en `tests/assembly/`, `tests/native/` y `tests/unit/` (y se reutilizan los de `tests/codinter/programs/`), y los
ejecuta `tests/run_tests.sh` (`make test`). Los `.expected` están escritos a mano: el assembly se derivó aplicando las decisiones de
la sección 2 (incluidos el marco de pila, el relleno de alineación y las posiciones de cada valor) y los códigos de salida se
calcularon a mano, antes de correr nada. Los tests que arman un ejecutable necesitan **Linux x86-64 y `gcc`**; si no están, se omiten
con un aviso y no cuentan como fallas.

**Unitario del generador (`tests/unit/test_x86gen.c`).** Arma programas con la API del código intermedio y compara el assembly
generado con el escrito a mano, **una operación por vez**: `MOVE`, `ADD`, `SUB`, `MUL`, `NEG`, `NOT`, `LT`, `GT`, `EQ`, `DIV`, `MOD`,
`JMP`, `JZ`, `JNZ`, etiquetas, `PARAM`, `CALL` y `RET`. También prueba el marco de pila (tamaños para 0 a 9 posiciones), los
parámetros (los 6 de los registros y el 7.º y 8.º de la pila), las locales en 0, las llamadas con 0, 1, 6, 7, 8 y 9 argumentos (incluido
el relleno de alineación), los nombres de símbolos y la estructura del archivo. Son 76 comprobaciones. Las explicaciones de cada función
están en `guia-del-codigo.md`, sección 18.

**Assembly de programas completos (`tests/assembly/`, el `.expected` es el `.ass`):**

| Caso | Qué cubre |
|------|-----------|
| `add` | El ejemplo de la sección 3: una global, un método con dos parámetros y un `main` `void`. |
| `loop` | Un `while` con locales y temporales: marco de 5 posiciones redondeado a 48, etiquetas `.L1` y `.L2`, comparación, `jz`, `jmp` y `ret` con valor. |
| `seven_args` | Un método de 7 parámetros (el 7.º se copia desde la pila) y una llamada con 7 argumentos con relleno y `addq $16`. |

**Ejecutables (extremo a extremo):**

- **Los 26 programas de `tests/codinter/programs/`** se compilan a ejecutable y se ejecutan. Su código de salida debe ser el valor que
  devuelve `main` según el `.expected` (calculado a mano) **módulo 256**. Es el test más importante de la etapa: verifica que el
  assembly hace lo mismo que el código intermedio (factorial, Fibonacci, Ackermann, Collatz, cortocircuito, orden de evaluación,
  aritmética que da la vuelta, recursión de 50000 niveles, etc.).
- **26 programas propios (`tests/native/`)** con el código de salida esperado calculado a mano: funciones de 7, 8 y 9 argumentos y
  una recursión con 9; una llamada de 7 argumentos como argumento de otra y dentro de una expresión (la pila alineada); métodos y
  globales con nombres de la biblioteca de C (`printf`, `exit`, `puts`, `malloc`); `INT_MIN / -1` y `INT_MIN % -1`; la división y el resto
  con negativos; un resultado negativo (`-5` sale como 251) y uno mayor que 255 (300 sale como 44); un `boolean main`; un `main` `void`;
  una expresión de 60 términos (muchos temporales); un método sin variables; locales que arrancan en 0; el orden de evaluación con una
  global modificada por un método; globales como argumentos; variables con el mismo nombre en bloques distintos; llamadas encadenadas; y
  el desborde de 32 bits.
- **División y resto por cero:** el ejecutable termina con la señal `SIGFPE` y el código de salida es **136** (decisión 19).
- **Un programa grande**, generado por el script (3000 sentencias, unos 600 KB de assembly: 21017 líneas): como no entra de una vez en la tubería
  que lee `gcc`, comprueba que el compilador escribe todo el assembly; el ejecutable debe salir con 3000 módulo 256 = 184.

**Errores de herramientas.** Se simulan con un `PATH` modificado: sin `gcc` el compilador informa `cannot run 'gcc'` y sale con código 2
sin dejar un ejecutable; con un `gcc` falso que siempre falla informa `gcc failed` con código 2; y con un `gcc` falso que termina **sin leer**
el assembly de unos 600 KB (la tubería guarda 64 KB) comprueba que escribir en la tubería no mata al compilador con `SIGPIPE` (código 141) sino que informa el
error y sale con código 2. Además, `-target assembly` funciona sin `gcc`.

**Línea de comandos:** `-target assembly` genera el `.ass` y no el ejecutable; `-o` con `-target assembly`; sin `-target` se genera el
ejecutable (y ningún archivo intermedio) y termina con el código esperado.

**Regresión y memoria:** los casos de las etapas 1 a 4 siguen pasando y `make test-asan` corre los
mismos con AddressSanitizer y UBSan, sin errores de memoria. Se comprobó además que los tests detectan fallas: al cambiar a propósito la
instrucción de la multiplicación por una suma en el generador, y un código de salida y una etiqueta en dos `.expected`, fallaron el unitario,
el caso `loop`, y los programas nativos que multiplican; al restaurar todo volvieron a pasar.

**Hallazgos del testing:** los `.expected` de assembly calculados a mano coincidieron con el generador sin cambios, y los 52 programas
nativos dieron el resultado esperado. Al preparar los problemas conocidos se encontró un defecto de la línea de comandos que afectaba a
todos los targets: `-o` podía apuntar al propio fuente y reemplazarlo (decisión 23); se corrigió y se agregó un test. El único defecto encontrado fue un error del propio test unitario (verificaba que no hubiera `pushq`
en una llamada de 6 argumentos, pero el prólogo tiene `pushq %rbp`), corregido en el test.

## 5. Problemas conocidos

- **Solo Linux x86-64 con `gcc`.** El assembly es de Linux (sintaxis AT&T de GNU, `%rip`, `.note.GNU-stack`) y el ejecutable se arma con
  `gcc`, que tiene que estar en el `PATH`; no hay una opción para elegir otra herramienta. En macOS o Windows solo sirve
  `-target assembly`, que no necesita `gcc`. Cualquier error de `gcc` se informa como `gcc failed to assemble and link the program` (sus
  propios mensajes salen por `stderr` tal cual), y un nombre de salida que `gcc` no pueda escribir produce ese mismo error.
- **El único resultado observable es de 8 bits.** El sistema operativo conserva solo el byte bajo del valor que devuelve `main`: un
  resultado de 1000 sale como 232. Por eso los tests comparan módulo 256 y no se puede distinguir, por ejemplo, 5 de 261.
- **División por cero.** El ejecutable termina con `SIGFPE` (código de salida 136) en vez de informar un error (decisión 19).
- **Recursión profunda.** El ejecutable usa la pila del sistema (8 MB por defecto) y el código sin optimizar gasta mucha: cada llamada
  ocupa su marco completo más 16 bytes. Con un método recursivo de 5 posiciones por marco (64 bytes por nivel), 100000 niveles
  funcionan y 150000 terminan con `SIGSEGV` (código 139). Se midió con programas generados.
- **El código no está optimizado** (a propósito: la cátedra pide corrección y la optimización es la etapa siguiente). Todos los valores
  van a memoria y **cada temporal tiene su propia posición**: un método de 3000 asignaciones `x = x + 1` genera un marco de 3001
  posiciones (24016 bytes) y 21017 líneas de assembly (unos 600 KB). Cada división incluye la comprobación del divisor -1 y cada
  variable local se inicializa en 0 aunque el programa la asigne enseguida. _(Actualización etapa 6: con `-opt all` el mismo
  programa de 3000 asignaciones pasa de 21017 a 14 líneas de assembly, porque el optimizador trabaja sobre el código intermedio
  antes de generar el assembly; sin `-opt` el assembly es el de esta etapa. Lo que sigue igual: cada valor que queda va a memoria,
  cada temporal que queda tiene su posición y cada división incluye la comprobación del divisor -1.)_
- **Expresiones larguísimas.** El compilador recorre las expresiones con recursión; una suma `a + a + ... + a` de 20000 términos se compila
  bien y con 30000 el compilador se cae por desborde de su pila (código 139), igual que el límite de la etapa 4. No ocurre con programas
  reales.
- **Las variables locales valen 0 al empezar** (decisión 9). Es determinista, pero un programa equivalente
  en C tendría valores indefinidos; los resultados no son comparables con los de un compilador de C para programas que lean locales sin
  asignar.
- **Los números reales no están soportados** (decisión 20): un programa con `float` se rechaza al generar el código intermedio.
- **Heredados de las etapas anteriores:** el lenguaje no tiene entrada ni salida; `-2147483648` no se puede escribir como literal; los
  literales reales solo se validan contra el rango de `double`; los caracteres no ASCII se informan byte por byte; los comentarios de bloque
  no se anidan; el ejemplo de la spec trae una llave `}` de más y su `main` `void` devuelve valores (rechazado por la regla 6); un solo
  error sintáctico por corrida; el análisis semántico solo corre si no hay errores léxicos ni sintácticos; la búsqueda en la tabla de
  símbolos es lineal.

