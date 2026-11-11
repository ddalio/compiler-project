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
# Etapa 6 (optimizador):
#   tests/optimizer/*.ctds  -> <caso>.expected = codigo optimizado esperado
#       (-target opt, todas las optimizaciones) y <caso>.<optimizacion>.expected
#       = el esperado con solo esa optimizacion. Escritos a mano.
#   Equivalencia: los programas de tests/codinter/run, run_errors, native y
#       optimizer dan lo mismo sin optimizar y con cada -opt (interprete y, si
#       hay gcc, ejecutable nativo).
#   Opcion -opt de la linea de comandos y tests/unit/test_flow.c, test_opt.c.
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
        { "$TMP/native/n_$name" > /dev/null 2>&1; } 2>/dev/null
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

echo "== Optimizador: codigo optimizado esperado (-target opt) =="
# tests/optimizer/<caso>.ctds con <caso>.expected (todas las optimizaciones) y,
# si hay, <caso>.<optimizacion>.expected (solo esa). Escritos a mano.
for src in "$ROOT"/tests/optimizer/*.ctds; do
    name="$(basename "$src" .ctds)"
    for expected in "$ROOT/tests/optimizer/$name.expected" "$ROOT"/tests/optimizer/"$name".*.expected; do
        [ -f "$expected" ] || continue
        which="$(basename "$expected" .expected)"; which="${which#"$name"}"; which="${which#.}"
        if [ -z "$which" ]; then label="$name"; args=(-target opt); else label="$name ($which)"; args=(-opt "$which" -target opt); fi
        run "$ROOT/tests/optimizer" "${args[@]}" -o "$TMP/o.opt" "$name.ctds"
        if [ $status -ne 0 ] || [ -s "$TMP/stdout" ] || [ -s "$TMP/stderr" ]; then
            bad "$label" "exit $status; stdout/stderr deberian estar vacios: $(cat "$TMP/stderr")"
        elif ! diff -u "$expected" "$TMP/o.opt" > "$TMP/diff"; then
            bad "$label" "$(cat "$TMP/diff")"
        else
            # Con -debug el mismo codigo sale por stdout y las estadisticas por stderr.
            run "$ROOT/tests/optimizer" "${args[@]}" -debug -o "$TMP/o2.opt" "$name.ctds"
            if [ $status -eq 0 ] && diff -q "$expected" "$TMP/stdout" > /dev/null && diff -q "$expected" "$TMP/o2.opt" > /dev/null; then
                ok "$label"
            else
                bad "$label (-debug)" "exit $status"
            fi
        fi
    done
done

echo "== Optimizador: el programa optimizado hace lo mismo (interprete) =="
# Cada programa de las carpetas de abajo se interpreta sin optimizar y con cada
# optimizacion (sola y con all): debe dar la misma salida, el mismo mensaje de
# error de ejecucion y el mismo codigo de salida.
opt_variants="all fold propagate dce jumps temps"
for dir in codinter/run codinter/run_errors native optimizer; do
    for src in "$ROOT/tests/$dir"/*.ctds; do
        name="$(basename "$src" .ctds)"
        run "$ROOT/tests/$dir" -run -o "$TMP/eq.ci" "$name.ctds"
        base_status=$status
        cp "$TMP/stdout" "$TMP/base_stdout"; cp "$TMP/stderr" "$TMP/base_stderr"
        diffs=""
        for v in $opt_variants; do
            run "$ROOT/tests/$dir" -run -opt "$v" -o "$TMP/eq.ci" "$name.ctds"
            if [ $status -ne $base_status ] || ! cmp -s "$TMP/stdout" "$TMP/base_stdout" ||
               ! cmp -s "$TMP/stderr" "$TMP/base_stderr"; then
                diffs="$diffs $v"
            fi
        done
        if [ -z "$diffs" ]; then
            ok "$dir/$name (6 variantes iguales a sin optimizar)"
        else
            bad "$dir/$name" "distinto con -opt:$diffs (sin optimizar: exit $base_status, stdout $(cat "$TMP/base_stdout"))"
        fi
    done
done

echo "== Optimizador: el ejecutable optimizado hace lo mismo (codigo nativo) =="
if [ $NATIVE -eq 1 ]; then
    mkdir -p "$TMP/eqnat"
    for dir in codinter/run native optimizer; do
        for src in "$ROOT/tests/$dir"/*.ctds; do
            name="$(basename "$src" .ctds)"
            # En optimizer/ se prueban tambien las optimizaciones de a una.
            if [ "$dir" = optimizer ]; then variants="all fold propagate dce jumps temps"; else variants="all"; fi
            run "$ROOT/tests/$dir" -o "$TMP/eqnat/base" "$name.ctds"
            if [ $status -ne 0 ]; then
                bad "$dir/$name" "no se pudo compilar sin optimizar (exit $status): $(cat "$TMP/stderr")"
                continue
            fi
            { "$TMP/eqnat/base" > "$TMP/eqnat/base.out" 2>&1; } 2>/dev/null; base_got=$?
            diffs=""
            for v in $variants; do
                run "$ROOT/tests/$dir" -opt "$v" -o "$TMP/eqnat/opt" "$name.ctds"
                if [ $status -ne 0 ]; then diffs="$diffs $v(no compila)"; continue; fi
                { "$TMP/eqnat/opt" > "$TMP/eqnat/opt.out" 2>&1; } 2>/dev/null; got=$?
                if [ $got -ne $base_got ] || ! cmp -s "$TMP/eqnat/opt.out" "$TMP/eqnat/base.out"; then
                    diffs="$diffs $v(salio con $got)"
                fi
            done
            if [ -z "$diffs" ]; then
                ok "$dir/$name (sale con $base_got, igual optimizado)"
            else
                bad "$dir/$name" "sin optimizar sale con $base_got; distinto con -opt:$diffs"
            fi
        done
    done
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

echo "== Optimizador: opcion -opt de la linea de comandos =="
OPTDIR="$ROOT/tests/optimizer"
# cliopt <descripcion> <exit esperado> <texto esperado en stderr> -- <args...>
# (corre sobre un programa valido de tests/optimizer)
cliopt() {
    local desc="$1" want_status="$2" want_text="$3"; shift 4
    run "$OPTDIR" "$@"
    if [ $status -ne "$want_status" ]; then
        bad "$desc" "exit code $status (se esperaba $want_status): $(head -n 1 "$TMP/stderr")"
    elif ! grep -qF -- "$want_text" "$TMP/stderr"; then
        bad "$desc" "stderr no contiene '$want_text': $(head -n 1 "$TMP/stderr")"
    else
        ok "$desc"
    fi
}

cliopt "-opt sin argumento"             2 "requires an argument"       -- constant_folding.ctds -opt
cliopt "-opt con un nombre desconocido" 2 "unknown optimization 'foo'" -- -opt foo constant_folding.ctds
cliopt "-opt no toma la opcion siguiente como nombre" 2 "unknown optimization '-target'" -- -opt -target opt constant_folding.ctds
cliopt "-opt con -target scan"          2 "does not apply to -target scan"  -- -opt all -target scan constant_folding.ctds
cliopt "-opt con -target parse"         2 "does not apply to -target parse" -- -opt all -target parse constant_folding.ctds
cliopt "-opt con -target sem"           2 "does not apply to -target sem"   -- -opt all -target sem constant_folding.ctds
cliopt "la lista de nombres validos sale en el error" 2 "fold, propagate, dce, jumps, temps, all" -- -opt foo constant_folding.ctds

# Cada nombre es valido, no imprime nada y escribe el .opt pedido.
for name in fold propagate dce jumps temps all; do
    rm -f "$TMP/n.opt"
    run "$OPTDIR" -opt "$name" -target opt -o "$TMP/n.opt" constant_folding.ctds
    if [ $status -eq 0 ] && [ ! -s "$TMP/stdout" ] && [ ! -s "$TMP/stderr" ] && [ -s "$TMP/n.opt" ]; then
        ok "-opt $name -target opt"
    else
        bad "-opt $name -target opt" "exit $status: $(cat "$TMP/stderr")"
    fi
done

# -target opt sin -opt equivale a -opt all.
run "$OPTDIR" -target opt -o "$TMP/default.opt" constant_folding.ctds
run "$OPTDIR" -opt all -target opt -o "$TMP/all.opt" constant_folding.ctds
if cmp -s "$TMP/default.opt" "$TMP/all.opt" && [ -s "$TMP/all.opt" ]; then
    ok "-target opt sin -opt = -opt all"
else
    bad "-target opt sin -opt = -opt all" "las salidas difieren"
fi

# -opt repetido y en cualquier orden: lo que importa es el conjunto, no el orden.
run "$OPTDIR" -opt fold -target opt -o "$TMP/f1.opt" dead_code.ctds
run "$OPTDIR" -opt fold -opt fold -target opt -o "$TMP/f2.opt" dead_code.ctds
if [ $status -eq 0 ] && cmp -s "$TMP/f1.opt" "$TMP/f2.opt"; then
    ok "-opt repetido (fold fold) = fold"
else
    bad "-opt repetido (fold fold) = fold" "exit $status"
fi
run "$OPTDIR" -opt dce -opt fold -opt propagate -target opt -o "$TMP/o1.opt" constant_folding.ctds
run "$OPTDIR" -opt propagate -opt fold -opt dce -target opt -o "$TMP/o2.opt" constant_folding.ctds
if [ $status -eq 0 ] && cmp -s "$TMP/o1.opt" "$TMP/o2.opt"; then
    ok "-opt en distinto orden: mismo resultado"
else
    bad "-opt en distinto orden: mismo resultado" "las salidas difieren"
fi
run "$OPTDIR" -opt fold -opt propagate -opt dce -opt jumps -opt temps -target opt -o "$TMP/five.opt" constant_folding.ctds
if cmp -s "$TMP/five.opt" "$TMP/all.opt"; then
    ok "las cinco optimizaciones juntas = all"
else
    bad "las cinco optimizaciones juntas = all" "las salidas difieren"
fi

# Sin -opt no se optimiza: el .ci es el de siempre y -debug no habla del optimizador.
run "$OPTDIR" -target codinter -debug -o "$TMP/plain.ci" constant_folding.ctds
if [ $status -eq 0 ] && ! grep -qF "[opt]" "$TMP/stderr" && grep -qF "t1 = 3 * 4" "$TMP/plain.ci" &&
   [ "$(grep -c . "$TMP/plain.ci")" -gt 6 ]; then
    ok "sin -opt: el codigo no se optimiza y -debug no muestra [opt]"
else
    bad "sin -opt: el codigo no se optimiza y -debug no muestra [opt]" "exit $status: $(cat "$TMP/stderr")"
fi

# -opt all -target codinter: el .ci sale optimizado (mismo codigo que el .opt).
run "$OPTDIR" -opt all -target codinter -o "$TMP/opt.ci" constant_folding.ctds
if [ $status -eq 0 ] && cmp -s "$TMP/opt.ci" "$TMP/all.opt"; then
    ok "-opt all -target codinter: el .ci sale optimizado"
else
    bad "-opt all -target codinter: el .ci sale optimizado" "exit $status"
fi

# -debug: las estadisticas van por stderr, el codigo por stdout.
run "$OPTDIR" -opt all -target opt -debug -o "$TMP/dbg.opt" constant_folding.ctds
if [ $status -eq 0 ] && grep -qF "[opt] main: 7 -> 1 instructions, 4 -> 0 temporaries" "$TMP/stderr" &&
   grep -qF "[opt] total: 7 -> 1 instructions" "$TMP/stderr" && grep -qF "ret 24" "$TMP/stdout"; then
    ok "-opt -debug: estadisticas por stderr y codigo por stdout"
else
    bad "-opt -debug: estadisticas por stderr y codigo por stdout" "exit $status: $(cat "$TMP/stderr")"
fi

# -run con -opt: el mismo resultado y el .ci escrito es el optimizado.
run "$OPTDIR" -opt all -run -o "$TMP/run.ci" constant_folding.ctds
if [ $status -eq 0 ] && [ "$(cat "$TMP/stdout")" = "24" ] && grep -qF "ret 24" "$TMP/run.ci"; then
    ok "-opt all -run: resultado 24 y .ci optimizado"
else
    bad "-opt all -run: resultado 24 y .ci optimizado" "exit $status: $(cat "$TMP/stdout")"
fi

# Nombre de salida por defecto: <fuente>.opt junto al fuente (y no el .ci).
mkdir "$TMP/optdef"
cp "$OPTDIR/constant_folding.ctds" "$TMP/optdef/cf.ctds"
run "$TMP/optdef" -target opt cf.ctds
if [ $status -eq 0 ] && [ ! -s "$TMP/stdout" ] && [ ! -s "$TMP/stderr" ] &&
   [ "$(ls "$TMP/optdef" | tr '\n' ' ')" = "cf.ctds cf.opt " ]; then
    ok "-target opt: escribe <fuente>.opt junto al fuente"
else
    bad "-target opt: escribe <fuente>.opt junto al fuente" "exit $status; archivos: $(ls "$TMP/optdef")"
fi

# -o no puede apuntar al fuente tampoco con -target opt.
run "$TMP/optdef" -target opt -o cf.ctds cf.ctds
if [ $status -eq 2 ] && grep -qF "would overwrite the input file" "$TMP/stderr"; then
    ok "-target opt: -o no pisa el fuente"
else
    bad "-target opt: -o no pisa el fuente" "exit $status: $(cat "$TMP/stderr")"
fi

# -opt con assembly: el assembly optimizado es mas corto y sin la multiplicacion.
run "$OPTDIR" -target assembly -o "$TMP/plain.ass" constant_folding.ctds
run "$OPTDIR" -opt all -target assembly -o "$TMP/opt.ass" constant_folding.ctds
if [ $status -eq 0 ] && [ "$(wc -l < "$TMP/opt.ass")" -lt "$(wc -l < "$TMP/plain.ass")" ] &&
   grep -qF "imul" "$TMP/plain.ass" && ! grep -qF "imul" "$TMP/opt.ass"; then
    ok "-opt all -target assembly: assembly mas corto, sin imul"
else
    bad "-opt all -target assembly: assembly mas corto, sin imul" "exit $status"
fi

# Los errores del programa se informan igual con y sin -opt y no generan salida.
run "$ROOT/tests/semantic/errors" -opt all -target opt -o "$TMP/never.opt" rule2_undeclared.ctds
opt_err=$status; cp "$TMP/stderr" "$TMP/opt_stderr"
run "$ROOT/tests/semantic/errors" -target codinter -o "$TMP/never.ci" rule2_undeclared.ctds
if [ $opt_err -eq 1 ] && [ $status -eq 1 ] && cmp -s "$TMP/opt_stderr" "$TMP/stderr" && [ ! -e "$TMP/never.opt" ]; then
    ok "-opt con error semantico: mismo mensaje, sin salida"
else
    bad "-opt con error semantico: mismo mensaje, sin salida" "exit $opt_err y $status"
fi
run "$ROOT/tests/codinter/errors" -opt all -target opt -o "$TMP/never.opt" float_global.ctds
if [ $status -eq 1 ] && grep -qF "type 'float' is not supported" "$TMP/stderr" && [ ! -e "$TMP/never.opt" ]; then
    ok "-opt con un float: se rechaza como sin -opt"
else
    bad "-opt con un float: se rechaza como sin -opt" "exit $status: $(cat "$TMP/stderr")"
fi

# Ejecutable optimizado: sale con el valor del programa.
if [ $NATIVE -eq 1 ]; then
    run "$OPTDIR" -opt all -o "$TMP/cf_exe" constant_folding.ctds
    "$TMP/cf_exe" > /dev/null 2>&1; got=$?
    if [ $status -eq 0 ] && [ $got -eq 24 ] && [ ! -s "$TMP/stderr" ]; then
        ok "-opt all (ejecutable): sale con 24"
    else
        bad "-opt all (ejecutable): sale con 24" "exit $status, ejecutable $got"
    fi
fi

echo
echo "Resultado: $pass ok, $fail fallaron"
[ $fail -eq 0 ]
