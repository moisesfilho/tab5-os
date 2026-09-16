#!/usr/bin/env bash
# Fase 30: testes unitarios em host nativo com gate de cobertura >= 80%.
# Configura, compila, roda o GoogleTest via ctest e valida a cobertura
# de linhas (gcov/lcov) sobre SOMENTE os .cpp de producao sob teste.
#
# Uso: tools/ci/run_host_tests.sh [--suite SUITE]
# Suites: all (padrao), core, runtime, terminal, wifi, bluetooth, files,
# integration. O gate de cobertura roda somente na suite all.
# Variaveis: BUILD_DIR, COVERAGE_MIN (padrao 80)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SUITE="all"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --suite)
            [[ $# -ge 2 ]] || { echo "erro: --suite exige um valor" >&2; exit 2; }
            SUITE="$2"
            shift 2
            ;;
        --help|-h)
            sed -n '2,8p' "$0"
            exit 0
            ;;
        *)
            echo "uso: $0 [--suite all|core|runtime|terminal|wifi|bluetooth|files|integration]" >&2
            exit 2
            ;;
    esac
done

case "$SUITE" in
    all|core|runtime|terminal|wifi|bluetooth|files|integration) ;;
    *)
        echo "erro: suite desconhecida: $SUITE" >&2
        exit 2
        ;;
esac

if [[ -n "${BUILD_DIR:-}" ]]; then
    BUILD_DIR="$BUILD_DIR"
elif [[ "$SUITE" == "all" ]]; then
    BUILD_DIR="$ROOT/build/host"
else
    BUILD_DIR="$ROOT/build/host-$SUITE"
fi

COVERAGE_MIN="${COVERAGE_MIN:-80}"

case "$SUITE" in
    all) CTEST_REGEX="" ;;
    core) CTEST_REGEX='(InfraTest|OrientationTest|AppRegistryTest|FileAssocTest|TimezoneMgrTest|AiStorageTest|DisplayStorageTest|CalendarLogicTest|StorageSandboxTest|StorageMgrTest)' ;;
    runtime) CTEST_REGEX='(HostAbiTest|WasmRuntimeTest|WasmUnloadContract|AsyncDispatcherContract|NvsWorkerTest)' ;;
    terminal) CTEST_REGEX='TerminalMicroShellTest' ;;
    wifi) CTEST_REGEX='WifiStorageTest' ;;
    bluetooth) CTEST_REGEX='(BtStorageTest|BtReportMapTest)' ;;
    files) CTEST_REGEX='(FileAssocTest|HostAbiTest.*FileAssoc)' ;;
    integration) CTEST_REGEX='(ManifestTest|PackageMgrTest|SdkIntegrationTest|SerialBridgeDispatchContract|ScreenShotStateContract)' ;;
esac

echo "==> Configurando suite '$SUITE' ($BUILD_DIR)"
# Never reuse instrumented objects or gcda files from another build. Stale
# counters are instrumentation errors, not coverage results.
rm -rf "$BUILD_DIR"
BUILD_MARKER="$ROOT/.host-coverage-build-start"
touch "$BUILD_MARKER"
trap 'rm -f "$BUILD_MARKER"' EXIT
CONFIG_ARGS=(
    -DCMAKE_BUILD_TYPE=Debug
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    -DHOST_TESTS_COVERAGE=$([[ "$SUITE" == "all" ]] && echo ON || echo OFF)
)
if [[ "$SUITE" == "all" ]]; then
    CONFIG_ARGS+=(-DCMAKE_CXX_FLAGS=--coverage)
fi
cmake -S "$ROOT/tests/host" -B "$BUILD_DIR" "${CONFIG_ARGS[@]}" >/dev/null

echo "==> Compilando"
cmake --build "$BUILD_DIR" -j "$(nproc)"

echo "==> Executando testes"
if [[ -n "$CTEST_REGEX" ]]; then
    ctest --test-dir "$BUILD_DIR" --output-on-failure -R "$CTEST_REGEX"
else
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

if [[ "$SUITE" != "all" ]]; then
    echo "==> Suite '$SUITE' concluida sem gate de cobertura (use --suite all para o gate)"
    exit 0
fi

# lcov must see only data emitted by this build.  Refuse an accidentally
# checked-in/generated gcda outside BUILD_DIR and files older than our clean
# build marker instead of converting them into a plausible percentage.
shopt -s globstar nullglob
for gcda in "$ROOT"/**/*.gcda; do
    [[ -f "$gcda" ]] || continue
    if [[ "$gcda" != "$BUILD_DIR"/* || ! "$gcda" -nt "$BUILD_MARKER" ]]; then
        echo "erro: GCDA fora do build atual: $gcda" >&2
        exit 1
    fi
done
shopt -u globstar nullglob

if ! command -v lcov >/dev/null 2>&1; then
    echo "erro: lcov nao encontrado no PATH (apt-get install lcov)" >&2
    exit 1
fi

TRACE="$BUILD_DIR/coverage.info"
echo "==> Capturando cobertura (gcov/lcov)"
RAW="$BUILD_DIR/coverage-raw.info"
lcov --config-file "$ROOT/tools/ci/lcovrc" \
    --capture \
    --directory "$BUILD_DIR/CMakeFiles/tab5_host_tests.dir$ROOT/components/os/core" \
    --directory "$BUILD_DIR/CMakeFiles/tab5_host_tests.dir$ROOT/components/os/runtime" \
    --output-file "$RAW" --quiet

# A metrica cobre apenas os modulos sob teste; stubs/, mocks/ e tests/
# ficam fora do calculo por construcao.
lcov --config-file "$ROOT/tools/ci/lcovrc" --extract "$RAW" \
    "$ROOT/components/os/core/*.cpp" "$ROOT/components/os/runtime/*.cpp" \
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
