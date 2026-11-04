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
cli "-target no disponible"      2 "not available yet"        -- -target codinter keywords.ctds
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

echo
echo "Resultado: $pass ok, $fail fallaron"
[ $fail -eq 0 ]
