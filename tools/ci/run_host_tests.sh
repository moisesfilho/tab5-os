#!/usr/bin/env bash
# Fase 30: testes unitarios em host nativo com gate de cobertura >= 80%.
# Configura, compila, roda o GoogleTest via ctest e valida a cobertura
# de linhas (gcov/lcov) sobre SOMENTE os .cpp de producao sob teste.
#
# Uso: tools/ci/run_host_tests.sh
# Variaveis: BUILD_DIR, COVERAGE_MIN (padrao 80)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/host}"
COVERAGE_MIN="${COVERAGE_MIN:-80}"

echo "==> Configurando ($BUILD_DIR)"
cmake -S "$ROOT/tests/host" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_CXX_FLAGS=--coverage >/dev/null

echo "==> Compilando"
cmake --build "$BUILD_DIR" -j "$(nproc)"

echo "==> Executando testes"
ctest --test-dir "$BUILD_DIR" --output-on-failure

if ! command -v lcov >/dev/null 2>&1; then
    echo "erro: lcov nao encontrado no PATH (apt-get install lcov)" >&2
    exit 1
fi

TRACE="$BUILD_DIR/coverage.info"
echo "==> Capturando cobertura (gcov/lcov)"
# inconsistent/mismatch: alarme do par GCC/lcov em TU de teste;
# irrelevante aqui pois o filtro abaixo reduz aos .cpp de producao.
RAW="$BUILD_DIR/coverage-raw.info"
lcov --config-file "$ROOT/tools/ci/lcovrc" \
    --ignore-errors inconsistent --ignore-errors mismatch \
    --capture --directory "$BUILD_DIR" --output-file "$RAW" --quiet

# A metrica cobre apenas os modulos sob teste; stubs/, mocks/ e tests/
# ficam fora do calculo por construcao.
lcov --config-file "$ROOT/tools/ci/lcovrc" --extract "$RAW" \
    "$ROOT/components/os/core/*.cpp" "$ROOT/components/apps/*/*.cpp" \
    --output-file "$TRACE" --quiet
rm -f "$RAW"

SUMMARY="$(lcov --config-file "$ROOT/tools/ci/lcovrc" --summary "$TRACE" 2>/dev/null || true)"
echo "$SUMMARY"

PCT="$(awk '/lines\.*:/ {
    if (match($0, /[0-9]+([.][0-9]+)?%/)) {
        print substr($0, RSTART, RLENGTH - 1)
    }
}' <<<"$SUMMARY" | tail -n1)"

if [[ -z "$PCT" ]]; then
    echo "erro: nao foi possivel calcular a cobertura de linhas" >&2
    exit 1
fi

if ! awk -v pct="$PCT" -v min="$COVERAGE_MIN" 'BEGIN { exit !(pct + 0 >= min + 0) }'; then
    echo "erro: cobertura de linhas ${PCT}% < gate de ${COVERAGE_MIN}%" >&2
    exit 1
fi
echo "==> Gate de cobertura atendido (${PCT}% >= ${COVERAGE_MIN}%)"

if command -v genhtml >/dev/null 2>&1; then
    HTML="$BUILD_DIR/coverage-html"
    genhtml --config-file "$ROOT/tools/ci/lcovrc" "$TRACE" --output-directory "$HTML" --quiet || true
    echo "==> Relatorio HTML em $HTML"
fi
