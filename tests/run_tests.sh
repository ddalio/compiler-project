#!/usr/bin/env bash
#
# run_tests.sh - Tests del compilador c-tds.
#
# Uso:  bash tests/run_tests.sh [ruta/al/c-tds]      (o simplemente: make test)
#
# Etapa 1 (scanner):
#   tests/scanner/valid/*.ctds    -> <caso>.expected = contenido esperado del .lex
#                                    (codigo de salida 0, nada por stdout/stderr)
#   tests/scanner/errors/*.ctds   -> <caso>.expected = stderr esperado
#                                    (codigo de salida 1, sin archivo de salida)
# Etapa 5 (codigo objeto):
#   tests/assembly/*.ctds   -> <caso>.expected = assembly esperado (-target assembly)
#   tests/native/*.ctds     -> <caso>.expected = codigo de salida del ejecutable
#   tests/codinter/run/*.ctds tambien se compilan a ejecutable: su codigo de
#       salida debe ser el valor que devuelve el interprete, modulo 256.
#   Los tests que arman un ejecutable necesitan Linux x86-64 y gcc; si no estan
#   se omiten con un aviso.
#   tests/unit/test_x86gen.c -> unitario del generador de assembly.
#
# Etapa 4 (codigo intermedio):
#   tests/codinter/valid/*.ctds     -> <caso>.expected = contenido esperado del .ci
#   tests/codinter/errors/*.ctds    -> <caso>.expected = stderr esperado (codigo 1,
#                                      sin archivo de salida)
#   tests/codinter/run/*.ctds       -> <caso>.expected = stdout de `-run` (el valor
#                                      que devuelve main); codigo de salida 0
#   tests/codinter/run_errors/*.ctds -> <caso>.expected = stderr de `-run` (error
#                                      de ejecucion); codigo 1, nada por stdout
#   tests/unit/test_ir.c, test_interp.c -> unitarios del codigo intermedio y del
#       interprete.
#
# Etapa 3 (semantico):
#   tests/semantic/valid/*.ctds   -> <caso>.expected = contenido esperado del .sem
#                                    (AST anotado; codigo de salida 0)
#   tests/semantic/errors/*.ctds  -> <caso>.expected = stderr esperado
#                                    (codigo de salida 1, sin archivo de salida)
#   tests/unit/test_symtab.c, test_ast.c -> tests unitarios de la tabla de
#       simbolos, los tipos y el AST (ejecutables que arma `make test` en
#       $UNIT_DIR; si UNIT_DIR no esta definido se omiten).
#
# Etapa 2 (parser):
#   tests/parser/valid/*.ctds     -> <caso>.expected = contenido esperado del .sint
#                                    (arbol de parsing; codigo de salida 0)
#   tests/parser/errors/*.ctds    -> <caso>.expected = stderr esperado
#                                    (codigo de salida 1, sin archivo de salida)
#   Interfaz de linea de comandos: opciones, nombres de salida y errores de uso.
#
# Los .expected estan escritos a mano a partir de la especificacion del lenguaje.

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$(cd "$(dirname "${1:-$ROOT/c-tds}")" && pwd)/$(basename "${1:-$ROOT/c-tds}")"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0

# Los tests de extremo a extremo (compilar a ejecutable y correrlo) necesitan
# Linux x86-64 y gcc.
NATIVE=0
if [ "$(uname -s)" = "Linux" ] && [ "$(uname -m)" = "x86_64" ] && command -v gcc > /dev/null; then
    NATIVE=1
fi

ok()  { pass=$((pass + 1)); echo "  ok    $1"; }
bad() { fail=$((fail + 1)); echo "  FAIL  $1"; [ -n "$2" ] && echo "$2" | sed 's/^/        /'; }

if [ ! -x "$BIN" ]; then
    echo "No se encuentra el ejecutable: $BIN (correr 'make' primero)"
    exit 2
fi

# Corre c-tds dentro de <dir> (asi los mensajes llevan solo el nombre del
# archivo). Deja stdout en $TMP/stdout, stderr en $TMP/stderr y el codigo de
# salida en $status.
run() {
    local dir="$1"; shift
    (cd "$dir" && "$BIN" "$@") > "$TMP/stdout" 2> "$TMP/stderr"
    status=$?
}

echo "== Scanner: casos validos =="
for src in "$ROOT"/tests/scanner/valid/*.ctds; do
    name="$(basename "$src" .ctds)"
    expected="${src%.ctds}.expected"
    rm -f "$TMP/out.lex"
    run "$(dirname "$src")" -target scan -o "$TMP/out.lex" "$name.ctds"
    if [ $status -ne 0 ]; then
        bad "$name" "exit code $status (se esperaba 0): $(cat "$TMP/stderr")"
    elif [ -s "$TMP/stdout" ] || [ -s "$TMP/stderr" ]; then
        bad "$name" "sin -debug no debe imprimirse nada"
    elif ! diff -u "$expected" "$TMP/out.lex" > "$TMP/diff"; then
        bad "$name" "$(cat "$TMP/diff")"
    else
        # Con -debug los mismos tokens salen por stdout.
        run "$(dirname "$src")" -target scan -debug -o "$TMP/out2.lex" "$name.ctds"
        if diff -u "$expected" "$TMP/stdout" > "$TMP/diff"; then
            ok "$name"
        else
            bad "$name (-debug)" "$(cat "$TMP/diff")"
        fi
    fi
done

echo "== Scanner: errores lexicos =="
for src in "$ROOT"/tests/scanner/errors/*.ctds; do
    name="$(basename "$src" .ctds)"
    expected="${src%.ctds}.expected"
    rm -f "$TMP/out.lex"
    run "$(dirname "$src")" -target scan -o "$TMP/out.lex" "$name.ctds"
    if [ $status -ne 1 ]; then
        bad "$name" "exit code $status (se esperaba 1)"
    elif [ -e "$TMP/out.lex" ]; then
        bad "$name" "con errores lexicos no debe generarse el archivo de salida"
    elif ! diff -u "$expected" "$TMP/stderr" > "$TMP/diff"; then
        bad "$name" "$(cat "$TMP/diff")"
    else
        ok "$name"
    fi
done

echo "== Parser: casos validos =="
for src in "$ROOT"/tests/parser/valid/*.ctds; do
    name="$(basename "$src" .ctds)"
    expected="${src%.ctds}.expected"
    rm -f "$TMP/out.sint"
    run "$(dirname "$src")" -target parse -o "$TMP/out.sint" "$name.ctds"
    if [ $status -ne 0 ]; then
        bad "$name" "exit code $status (se esperaba 0): $(cat "$TMP/stderr")"
    elif [ -s "$TMP/stdout" ] || [ -s "$TMP/stderr" ]; then
        bad "$name" "sin -debug no debe imprimirse nada"
    elif ! diff -u "$expected" "$TMP/out.sint" > "$TMP/diff"; then
        bad "$name" "$(cat "$TMP/diff")"
    else
        # Con -debug el mismo arbol sale por stdout (la traza va por stderr).
        run "$(dirname "$src")" -target parse -debug -o "$TMP/out2.sint" "$name.ctds"
        if [ $status -eq 0 ] && diff -u "$expected" "$TMP/stdout" > "$TMP/diff"; then
            ok "$name"
        else
            bad "$name (-debug)" "exit $status $(cat "$TMP/diff")"
        fi
    fi
done

echo "== Parser: errores lexicos y sintacticos =="
for src in "$ROOT"/tests/parser/errors/*.ctds; do
    name="$(basename "$src" .ctds)"
    expected="${src%.ctds}.expected"
    rm -f "$TMP/out.sint"
    run "$(dirname "$src")" -target parse -o "$TMP/out.sint" "$name.ctds"
    if [ $status -ne 1 ]; then
        bad "$name" "exit code $status (se esperaba 1)"
    elif [ -e "$TMP/out.sint" ]; then
        bad "$name" "con errores no debe generarse el archivo de salida"
    elif ! diff -u "$expected" "$TMP/stderr" > "$TMP/diff"; then
        bad "$name" "$(cat "$TMP/diff")"
    else
        ok "$name"
    fi
done

echo "== Tests unitarios (tabla de simbolos, tipos, AST, codigo intermedio e interprete) =="
if [ -n "$UNIT_DIR" ]; then
    for unit in test_symtab test_ast test_ir test_interp test_x86gen test_flow test_opt; do
        if [ ! -x "$UNIT_DIR/$unit" ]; then
            bad "$unit" "no se encuentra $UNIT_DIR/$unit (correr 'make test')"
        elif "$UNIT_DIR/$unit" > "$TMP/unit.out" 2>&1; then
            ok "$unit ($(tail -n 1 "$TMP/unit.out"))"
        else
            bad "$unit" "$(cat "$TMP/unit.out")"
        fi
    done
else
    echo "  (omitidos: los arma y corre 'make test')"
fi

echo "== Semantico: casos validos =="
for src in "$ROOT"/tests/semantic/valid/*.ctds; do
    name="$(basename "$src" .ctds)"
    expected="${src%.ctds}.expected"
    rm -f "$TMP/out.sem"
    run "$(dirname "$src")" -target sem -o "$TMP/out.sem" "$name.ctds"
    if [ $status -ne 0 ]; then
        bad "$name" "exit code $status (se esperaba 0): $(cat "$TMP/stderr")"
    elif [ -s "$TMP/stdout" ] || [ -s "$TMP/stderr" ]; then
        bad "$name" "sin -debug no debe imprimirse nada"
    elif ! diff -u "$expected" "$TMP/out.sem" > "$TMP/diff"; then
        bad "$name" "$(cat "$TMP/diff")"
    else
        # Con -debug el mismo AST sale por stdout (la traza va por stderr).
        run "$(dirname "$src")" -target sem -debug -o "$TMP/out2.sem" "$name.ctds"
        if [ $status -eq 0 ] && diff -u "$expected" "$TMP/stdout" > "$TMP/diff"; then
            ok "$name"
        else
            bad "$name (-debug)" "exit $status $(cat "$TMP/diff")"
        fi
    fi
done

echo "== Semantico: errores =="
for src in "$ROOT"/tests/semantic/errors/*.ctds; do
    name="$(basename "$src" .ctds)"
    expected="${src%.ctds}.expected"
    rm -f "$TMP/out.sem"
    run "$(dirname "$src")" -target sem -o "$TMP/out.sem" "$name.ctds"
    if [ $status -ne 1 ]; then
        bad "$name" "exit code $status (se esperaba 1)"
    elif [ -e "$TMP/out.sem" ]; then
        bad "$name" "con errores no debe generarse el archivo de salida"
    elif ! diff -u "$expected" "$TMP/stderr" > "$TMP/diff"; then
        bad "$name" "$(cat "$TMP/diff")"
    else
        ok "$name"
    fi
done

echo "== Codigo intermedio: casos validos =="
for src in "$ROOT"/tests/codinter/valid/*.ctds; do
    name="$(basename "$src" .ctds)"
    expected="${src%.ctds}.expected"
    rm -f "$TMP/out.ci"
    run "$(dirname "$src")" -target codinter -o "$TMP/out.ci" "$name.ctds"
    if [ $status -ne 0 ]; then
        bad "$name" "exit code $status (se esperaba 0): $(cat "$TMP/stderr")"
    elif [ -s "$TMP/stdout" ] || [ -s "$TMP/stderr" ]; then
        bad "$name" "sin -debug no debe imprimirse nada"
    elif ! diff -u "$expected" "$TMP/out.ci" > "$TMP/diff"; then
        bad "$name" "$(cat "$TMP/diff")"
    else
        # Con -debug el mismo codigo sale por stdout.
        run "$(dirname "$src")" -target codinter -debug -o "$TMP/out2.ci" "$name.ctds"
        if [ $status -eq 0 ] && diff -u "$expected" "$TMP/stdout" > "$TMP/diff"; then
            ok "$name"
        else
            bad "$name (-debug)" "exit $status $(cat "$TMP/diff")"
        fi
    fi
done

echo "== Codigo intermedio: errores de generacion =="
for src in "$ROOT"/tests/codinter/errors/*.ctds; do
    name="$(basename "$src" .ctds)"
    expected="${src%.ctds}.expected"
    rm -f "$TMP/out.ci"
    run "$(dirname "$src")" -target codinter -o "$TMP/out.ci" "$name.ctds"
    if [ $status -ne 1 ]; then
        bad "$name" "exit code $status (se esperaba 1)"
    elif [ -e "$TMP/out.ci" ]; then
        bad "$name" "con errores no debe generarse el archivo de salida"
    elif ! diff -u "$expected" "$TMP/stderr" > "$TMP/diff"; then
        bad "$name" "$(cat "$TMP/diff")"
    else
        ok "$name"
    fi
done

echo "== Interprete: programas (-run) =="
for src in "$ROOT"/tests/codinter/run/*.ctds; do
    name="$(basename "$src" .ctds)"
    expected="${src%.ctds}.expected"
    run "$(dirname "$src")" -run -o "$TMP/run.ci" "$name.ctds"
    if [ $status -ne 0 ]; then
        bad "$name" "exit code $status (se esperaba 0): $(cat "$TMP/stderr")"
    elif [ -s "$TMP/stderr" ]; then
        bad "$name" "no debe haber nada por stderr: $(cat "$TMP/stderr")"
    elif ! diff -u "$expected" "$TMP/stdout" > "$TMP/diff"; then
        bad "$name" "$(cat "$TMP/diff")"
    else
        ok "$name"
    fi
done

echo "== Interprete: errores de ejecucion =="
for src in "$ROOT"/tests/codinter/run_errors/*.ctds; do
    name="$(basename "$src" .ctds)"
    expected="${src%.ctds}.expected"
    run "$(dirname "$src")" -run -o "$TMP/run.ci" "$name.ctds"
    if [ $status -ne 1 ]; then
        bad "$name" "exit code $status (se esperaba 1)"
    elif [ -s "$TMP/stdout" ]; then
        bad "$name" "un error de ejecucion no debe imprimir resultado: $(cat "$TMP/stdout")"
    elif ! diff -u "$expected" "$TMP/stderr" > "$TMP/diff"; then
        bad "$name" "$(cat "$TMP/diff")"
    else
        ok "$name"
    fi
done

echo "== Assembly: casos con el assembly esperado (-target assembly) =="
for src in "$ROOT"/tests/assembly/*.ctds; do
    name="$(basename "$src" .ctds)"
    expected="${src%.ctds}.expected"
    rm -f "$TMP/out.ass"
    run "$(dirname "$src")" -target assembly -o "$TMP/out.ass" "$name.ctds"
    if [ $status -ne 0 ]; then
        bad "$name" "exit code $status (se esperaba 0): $(cat "$TMP/stderr")"
    elif [ -s "$TMP/stdout" ] || [ -s "$TMP/stderr" ]; then
        bad "$name" "sin -debug no debe imprimirse nada"
    elif ! diff -u "$expected" "$TMP/out.ass" > "$TMP/diff"; then
        bad "$name" "$(cat "$TMP/diff")"
    else
        # Con -debug el mismo assembly sale por stdout.
        run "$(dirname "$src")" -target assembly -debug -o "$TMP/out2.ass" "$name.ctds"
        if [ $status -eq 0 ] && diff -u "$expected" "$TMP/stdout" > "$TMP/diff"; then
            ok "$name"
        else
            bad "$name (-debug)" "exit $status $(cat "$TMP/diff")"
        fi
    fi
done

echo "== Ejecutables: los programas del interprete, compilados a codigo nativo =="
if [ $NATIVE -eq 1 ]; then
    mkdir -p "$TMP/native"
    for src in "$ROOT"/tests/codinter/run/*.ctds; do
        name="$(basename "$src" .ctds)"
        value="$(cat "${src%.ctds}.expected")"
        [ -z "$value" ] && value=0
        want=$(( ((value % 256) + 256) % 256 ))
        run "$(dirname "$src")" -o "$TMP/native/$name" "$name.ctds"
        if [ $status -ne 0 ]; then
            bad "$name" "no se pudo compilar (exit $status): $(cat "$TMP/stderr")"
            continue
        fi
        "$TMP/native/$name" > /dev/null 2>&1
        got=$?
        if [ $got -eq $want ]; then
            ok "$name (sale con $got = $value mod 256)"
        else
            bad "$name" "el ejecutable salio con $got y el interprete da $value (mod 256 = $want)"
        fi
    done

    echo "== Ejecutables: casos propios de la generacion de codigo =="
    for src in "$ROOT"/tests/native/*.ctds; do
        name="$(basename "$src" .ctds)"
        want="$(cat "${src%.ctds}.expected")"
        run "$(dirname "$src")" -o "$TMP/native/n_$name" "$name.ctds"
        if [ $status -ne 0 ]; then
            bad "$name" "no se pudo compilar (exit $status): $(cat "$TMP/stderr")"
            continue
        fi
        "$TMP/native/n_$name" > /dev/null 2>&1
        got=$?
        if [ $got -eq "$want" ]; then
            ok "$name (sale con $got)"
        else
            bad "$name" "el ejecutable salio con $got y se esperaba $want"
        fi
    done

    # Un programa grande: el assembly (unos 600 KB) no entra de una vez en la
    # tuberia que lee gcc, asi que prueba que se escribe completo.
    mkdir -p "$TMP/big"
    {
        echo "int main() {"
        echo "  int x;"
        echo "  x = 0;"
        for i in $(seq 1 3000); do echo "  x = x + 1;"; done
        echo "  return x;"
        echo "}"
    } > "$TMP/big/big.ctds"
    run "$TMP/big" big.ctds
    if [ $status -eq 0 ]; then
        "$TMP/big/big.out" > /dev/null 2>&1
        got=$?
        if [ $got -eq 184 ]; then
            ok "programa grande: 3000 sentencias (sale con 3000 mod 256 = 184)"
        else
            bad "programa grande" "el ejecutable salio con $got y se esperaba 184"
        fi
    else
        bad "programa grande" "no se pudo compilar (exit $status): $(cat "$TMP/stderr")"
    fi
else
    echo "  (omitidos: necesitan Linux x86-64 y gcc)"
fi

echo "== Errores de herramientas (gcc ausente o que falla) =="
# run_path <dir> <PATH> <args...>: como run, pero con otro PATH.
run_path() {
    local dir="$1" newpath="$2"; shift 2
    (cd "$dir" && PATH="$newpath" "$BIN" "$@") > "$TMP/stdout" 2> "$TMP/stderr"
    status=$?
}
mkdir -p "$TMP/emptybin" "$TMP/fakebin"
printf '#!/bin/sh\nexit 1\n' > "$TMP/fakebin/gcc"
chmod +x "$TMP/fakebin/gcc"

run_path "$ROOT/tests/native" "$TMP/emptybin" -o "$TMP/never.out" method_without_variables.ctds
if [ $status -eq 2 ] && grep -qF "cannot run 'gcc'" "$TMP/stderr" && [ ! -e "$TMP/never.out" ]; then
    ok "sin gcc: informa el error y sale con codigo 2"
else
    bad "sin gcc: informa el error y sale con codigo 2" "exit $status: $(cat "$TMP/stderr")"
fi

run_path "$ROOT/tests/native" "$TMP/fakebin" -o "$TMP/never.out" method_without_variables.ctds
if [ $status -eq 2 ] && grep -qF "gcc failed" "$TMP/stderr"; then
    ok "gcc que falla: informa el error y sale con codigo 2"
else
    bad "gcc que falla: informa el error y sale con codigo 2" "exit $status: $(cat "$TMP/stderr")"
fi

# Un gcc que termina sin leer el assembly (unos 600 KB): escribir en la tuberia
# daria SIGPIPE; el compilador no debe morir por esa senal (codigo 141).
{
    echo "int main() {"
    echo "  int x;"
    echo "  x = 0;"
    for i in $(seq 1 3000); do echo "  x = x + 1;"; done
    echo "  return x;"
    echo "}"
} > "$TMP/big_fake.ctds"
run_path "$TMP" "$TMP/fakebin" -o "$TMP/never.out" big_fake.ctds
if [ $status -eq 2 ] && grep -qF "gcc failed" "$TMP/stderr"; then
    ok "gcc que no lee la entrada: sin SIGPIPE, sale con codigo 2"
else
    bad "gcc que no lee la entrada: sin SIGPIPE, sale con codigo 2" "exit $status: $(cat "$TMP/stderr")"
fi

# -target assembly no necesita gcc.
run_path "$ROOT/tests/native" "$TMP/emptybin" -target assembly -o "$TMP/noexe.ass" method_without_variables.ctds
if [ $status -eq 0 ] && [ -f "$TMP/noexe.ass" ]; then
    ok "-target assembly funciona sin gcc"
else
    bad "-target assembly funciona sin gcc" "exit $status: $(cat "$TMP/stderr")"
fi

echo "== Interfaz de linea de comandos =="

# cli <descripcion> <exit esperado> <texto esperado en stderr> -- <args...>
cli() {
    local desc="$1" want_status="$2" want_text="$3"; shift 4
    run "$ROOT/tests/scanner/valid" "$@"
    if [ $status -ne "$want_status" ]; then
        bad "$desc" "exit code $status (se esperaba $want_status)"
    elif ! grep -qF -- "$want_text" "$TMP/stderr"; then
        bad "$desc" "stderr no contiene '$want_text': $(cat "$TMP/stderr")"
    else
        ok "$desc"
    fi
}

cli "sin argumentos"             2 "no input file"            --
cli "archivo inexistente"        2 "cannot open"              -- no_existe.ctds
cli "extension incorrecta"       2 "'.ctds' extension"        -- keywords.expected
cli "nombre que empieza con '-'" 2 "unknown option '-x.ctds'" -- -x.ctds
cli "opcion desconocida"         2 "unknown option '-foo'"    -- -foo keywords.ctds
cli "dos archivos de entrada"    2 "only one input file"      -- keywords.ctds literals.ctds
cli "-o sin argumento"           2 "requires an argument"     -- keywords.ctds -o
cli "-target sin argumento"      2 "requires an argument"     -- keywords.ctds -target
cli "-target desconocido"        2 "unknown target 'foo'"     -- -target foo keywords.ctds

# Nombre de salida por defecto: <fuente>.lex junto al fuente.
mkdir "$TMP/default"
cp "$ROOT/tests/scanner/valid/program.ctds" "$TMP/default/prog.ctds"
run "$TMP/default" -target scan prog.ctds
if [ $status -eq 0 ] && [ ! -s "$TMP/stdout" ] &&
   diff -q "$ROOT/tests/scanner/valid/program.expected" "$TMP/default/prog.lex" > /dev/null; then
    ok "salida por defecto: prog.lex"
else
    bad "salida por defecto: prog.lex" "exit $status; archivos: $(ls "$TMP/default")"
fi

# -o <salida> y -target scan explicito.
run "$TMP/default" -target scan -o custom.out prog.ctds
if [ $status -eq 0 ] && [ -f "$TMP/default/custom.out" ]; then
    ok "-o <salida> -target scan"
else
    bad "-o <salida> -target scan" "exit $status"
fi

# Etapa parse: -target parse genera <fuente>.sint junto al fuente.
cp "$ROOT/tests/parser/valid/spec_example.ctds" "$TMP/default/spec.ctds"
run "$TMP/default" -target parse spec.ctds
if [ $status -eq 0 ] && [ ! -s "$TMP/stdout" ] &&
   diff -q "$ROOT/tests/parser/valid/spec_example.expected" "$TMP/default/spec.sint" > /dev/null &&
   [ ! -e "$TMP/default/spec.lex" ]; then
    ok "-target parse: spec.sint"
else
    bad "-target parse: spec.sint" "exit $status; archivos: $(ls "$TMP/default")"
fi

run "$TMP/default" -target parse -o tree.out spec.ctds
if [ $status -eq 0 ] && [ -f "$TMP/default/tree.out" ]; then
    ok "-o <salida> -target parse"
else
    bad "-o <salida> -target parse" "exit $status"
fi

# Con -target scan no se parsea: un programa con error sintactico solo
# produce tokens.
run "$ROOT/tests/parser/errors" -target scan -o "$TMP/scan_only.lex" missing_semicolon.ctds
if [ $status -eq 0 ] && [ -f "$TMP/scan_only.lex" ]; then
    ok "-target scan no ejecuta el parser"
else
    bad "-target scan no ejecuta el parser" "exit $status: $(cat "$TMP/stderr")"
fi

# Un archivo inexistente es error de uso tambien con el target por defecto (codinter).
run "$TMP/default" no_existe.ctds
if [ $status -eq 2 ] && grep -qF "cannot open" "$TMP/stderr"; then
    ok "archivo inexistente (target por defecto)"
else
    bad "archivo inexistente (target por defecto)" "exit $status"
fi

# Etapa sem: -target sem genera <fuente>.sem junto al fuente (y no los archivos
# de las otras etapas).
cp "$ROOT/tests/semantic/valid/recursion.ctds" "$TMP/default/sem.ctds"
run "$TMP/default" -target sem sem.ctds
if [ $status -eq 0 ] && [ ! -s "$TMP/stdout" ] && [ ! -s "$TMP/stderr" ] &&
   diff -q "$ROOT/tests/semantic/valid/recursion.expected" "$TMP/default/sem.sem" > /dev/null &&
   [ ! -e "$TMP/default/sem.sint" ] && [ ! -e "$TMP/default/sem.lex" ]; then
    ok "-target sem: sem.sem"
else
    bad "-target sem: sem.sem" "exit $status; archivos: $(ls "$TMP/default")"
fi

run "$TMP/default" -target sem -o ast.out sem.ctds
if [ $status -eq 0 ] && [ -f "$TMP/default/ast.out" ]; then
    ok "-o <salida> -target sem"
else
    bad "-o <salida> -target sem" "exit $status"
fi

# -debug: el AST por stdout y la traza de la tabla de simbolos por stderr.
run "$TMP/default" -target sem -debug -o debug.out sem.ctds
if [ $status -eq 0 ] && grep -qF "[symtab] insert fact (method int) at level 1" "$TMP/stderr" &&
   grep -qF "[symtab] open level 2" "$TMP/stderr" &&
   diff -q "$ROOT/tests/semantic/valid/recursion.expected" "$TMP/stdout" > /dev/null; then
    ok "-debug: AST por stdout y traza de la tabla por stderr"
else
    bad "-debug: AST por stdout y traza de la tabla por stderr" "exit $status: $(head -n 3 "$TMP/stderr")"
fi

# Un programa con error semantico compila con -target parse (el parser no lo ve).
run "$ROOT/tests/semantic/errors" -target parse -o "$TMP/only_parse.sint" rule2_undeclared.ctds
if [ $status -eq 0 ] && [ -f "$TMP/only_parse.sint" ]; then
    ok "-target parse no ejecuta el analisis semantico"
else
    bad "-target parse no ejecuta el analisis semantico" "exit $status: $(cat "$TMP/stderr")"
fi

# Etapa codinter: -target codinter genera <fuente>.ci junto al fuente (y no los
# archivos de las otras etapas).
cp "$ROOT/tests/codinter/valid/factorial_and_loop.ctds" "$TMP/default/ci.ctds"
run "$TMP/default" -target codinter ci.ctds
if [ $status -eq 0 ] && [ ! -s "$TMP/stdout" ] && [ ! -s "$TMP/stderr" ] &&
   diff -q "$ROOT/tests/codinter/valid/factorial_and_loop.expected" "$TMP/default/ci.ci" > /dev/null &&
   [ ! -e "$TMP/default/ci.sem" ] && [ ! -e "$TMP/default/ci.sint" ] && [ ! -e "$TMP/default/ci.lex" ]; then
    ok "-target codinter: ci.ci"
else
    bad "-target codinter: ci.ci" "exit $status; archivos: $(ls "$TMP/default")"
fi

run "$TMP/default" -target codinter -o code.out ci.ctds
if [ $status -eq 0 ] && [ -f "$TMP/default/code.out" ]; then
    ok "-o <salida> -target codinter"
else
    bad "-o <salida> -target codinter" "exit $status"
fi

# -run: escribe el .ci y ademas ejecuta el programa.
cp "$ROOT/tests/codinter/run/factorial.ctds" "$TMP/default/fact.ctds"
run "$TMP/default" -run fact.ctds
if [ $status -eq 0 ] && [ "$(cat "$TMP/stdout")" = "3628800" ] && [ -f "$TMP/default/fact.ci" ]; then
    ok "-run: escribe el .ci y ejecuta"
else
    bad "-run: escribe el .ci y ejecuta" "exit $status; stdout: $(cat "$TMP/stdout"); archivos: $(ls "$TMP/default")"
fi

# -o no puede apuntar al propio fuente: lo pisaria con la salida (por el mismo
# nombre o por otro nombre del mismo archivo, como un enlace).
cp "$ROOT/tests/semantic/valid/recursion.ctds" "$TMP/default/own.ctds"
ln -s own.ctds "$TMP/default/link.ctds"
run "$TMP/default" -target sem -o own.ctds own.ctds
same1=$status
run "$TMP/default" -target sem -o link.ctds own.ctds
if [ $same1 -eq 2 ] && [ $status -eq 2 ] && grep -qF "would overwrite the input file" "$TMP/stderr" &&
   diff -q "$ROOT/tests/semantic/valid/recursion.ctds" "$TMP/default/own.ctds" > /dev/null; then
    ok "-o no puede pisar el archivo fuente (ni con otro nombre)"
else
    bad "-o no puede pisar el archivo fuente (ni con otro nombre)" "exit $same1 y $status: $(cat "$TMP/stderr")"
fi

# -run solo vale con -target codinter.
cli "-run con -target sem"       2 "requires -target codinter" -- -run -target sem keywords.ctds
cli "-run con -target parse"     2 "requires -target codinter" -- -run -target parse keywords.ctds

# -run con -debug: primero el codigo intermedio y despues el resultado.
run "$TMP/default" -run -debug -o debug.ci fact.ctds
if [ $status -eq 0 ] && head -n 1 "$TMP/stdout" | grep -qF "func fact(n)" &&
   [ "$(tail -n 1 "$TMP/stdout")" = "3628800" ]; then
    ok "-run -debug: codigo y despues resultado"
else
    bad "-run -debug: codigo y despues resultado" "exit $status: $(head -n 2 "$TMP/stdout")"
fi

# Con un error semantico no se genera codigo ni se ejecuta nada.
run "$ROOT/tests/semantic/errors" -run -o "$TMP/never.ci" rule2_undeclared.ctds
if [ $status -eq 1 ] && [ ! -s "$TMP/stdout" ] && [ ! -e "$TMP/never.ci" ]; then
    ok "-run con error semantico: no genera ni ejecuta"
else
    bad "-run con error semantico: no genera ni ejecuta" "exit $status"
fi

# Un programa con float es valido para -target sem pero no para codinter.
run "$ROOT/tests/codinter/errors" -target sem -o "$TMP/float.sem" float_global.ctds
if [ $status -eq 0 ] && [ -f "$TMP/float.sem" ]; then
    ok "float: valido con -target sem"
else
    bad "float: valido con -target sem" "exit $status: $(cat "$TMP/stderr")"
fi

# Etapa assembly: -target assembly genera <fuente>.ass (y no el ejecutable).
run "$TMP/default" -target assembly ci.ctds
if [ $status -eq 0 ] && [ ! -s "$TMP/stdout" ] && [ ! -s "$TMP/stderr" ] &&
   [ -f "$TMP/default/ci.ass" ] && grep -qF ".globl  main" "$TMP/default/ci.ass" &&
   [ ! -e "$TMP/default/ci.out" ]; then
    ok "-target assembly: ci.ass"
else
    bad "-target assembly: ci.ass" "exit $status; archivos: $(ls "$TMP/default")"
fi

run "$TMP/default" -target assembly -o salida.s ci.ctds
if [ $status -eq 0 ] && [ -f "$TMP/default/salida.s" ]; then
    ok "-o <salida> -target assembly"
else
    bad "-o <salida> -target assembly" "exit $status"
fi

# Sin -target se genera el ejecutable (necesita gcc y Linux x86-64).
if [ "$(uname -s)" = "Linux" ] && [ "$(uname -m)" = "x86_64" ] && command -v gcc > /dev/null; then
    cp "$ROOT/tests/codinter/run/loop_sum.ctds" "$TMP/default/exe.ctds"
    run "$TMP/default" exe.ctds
    "$TMP/default/exe.out" > /dev/null 2>&1
    exe_status=$?
    if [ $status -eq 0 ] && [ ! -s "$TMP/stdout" ] && [ ! -s "$TMP/stderr" ] &&
       [ -x "$TMP/default/exe.out" ] && [ $exe_status -eq 186 ] &&
       [ ! -e "$TMP/default/exe.ass" ] && [ ! -e "$TMP/default/exe.ci" ]; then
        ok "sin -target: genera el ejecutable exe.out (sale con 5050 mod 256)"
    else
        bad "sin -target: genera el ejecutable exe.out" "exit $status; ejecutable: $exe_status; archivos: $(ls "$TMP/default")"
    fi
else
    echo "  (omitido: el ejecutable necesita Linux x86-64 y gcc)"
fi

echo
echo "Resultado: $pass ok, $fail fallaron"
[ $fail -eq 0 ]
