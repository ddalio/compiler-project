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

echo "== Tests unitarios (tabla de simbolos, tipos y AST) =="
if [ -n "$UNIT_DIR" ]; then
    for unit in test_symtab test_ast; do
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
cli "-target no disponible"      2 "not available yet"        -- -target assembly keywords.ctds
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

echo
echo "Resultado: $pass ok, $fail fallaron"
[ $fail -eq 0 ]
