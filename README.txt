c-tds - Compilador para el lenguaje C-TDS
Taller de Diseño de Software (Cod. 3306) - DC - FCEFQyN - UNRC - 2026

Estado: Etapa 1 (analisis lexico) en desarrollo.
Documentacion de la etapa: docs/etapa-1-lexico.md
Checklist de la etapa:     docs/etapa-1-lexico-checklist.md


REQUISITOS
----------
  gcc, make, flex (probado con gcc 13, GNU Make 4.3, flex 2.6.4), bash.
  En Ubuntu/Debian:  sudo apt install build-essential flex


COMPILAR
--------
  make              genera el ejecutable ./c-tds
  make clean        borra el ejecutable y la carpeta build/


EJECUTAR
--------
  ./c-tds [opcion] nombreArchivo.ctds

  Opciones (etapa 1):
    -o <salida>      nombre del archivo de salida
    -target <etapa>  etapa hasta la que se compila (disponible: scan)
    -debug           imprime los tokens por pantalla

  Ejemplos:
    ./c-tds programa.ctds                  genera programa.lex
    ./c-tds -o tokens.lex programa.ctds    genera tokens.lex
    ./c-tds -debug programa.ctds           ademas imprime los tokens

  Sin -debug, una compilacion exitosa no imprime nada.
  El archivo .lex tiene un token por linea:  linea:columna NOMBRE lexema
  Codigos de salida: 0 = ok, 1 = errores lexicos, 2 = error de uso o de archivos.


TESTS
-----
  make test                          compila y corre todos los tests
  bash tests/run_tests.sh ./c-tds    corre los tests con un ejecutable ya compilado

  Casos de test (cada .ctds tiene un .expected con la salida esperada):
    tests/scanner/valid/     programas validos  -> .expected = contenido del .lex
    tests/scanner/errors/    errores lexicos    -> .expected = mensajes de stderr
  Los tests de la interfaz de linea de comandos estan en tests/run_tests.sh.
