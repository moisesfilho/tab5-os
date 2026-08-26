#!/usr/bin/env bash
# Fase 40+: regressao visual do tab5_os no simulador SDL.
# Compila o sim, roda os cenarios (capturas em tests/simulator/out) e
# compara contra os goldens (tests/simulator/goldens) via compare_images.py.
#
# Uso:
#   tools/ci/run_sim_tests.sh                 roda todos os cenarios e compara
#   tools/ci/run_sim_tests.sh --scenario NOME roda apenas um cenario
#   tools/ci/run_sim_tests.sh --update-goldens regenera os goldens (sem comparar)
# Variaveis: BUILD_DIR, SIM_BIN
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build-sim}"
SIM_BIN="${SIM_BIN:-$BUILD_DIR/tab5_sim}"
GOLDENS="$ROOT/tests/simulator/goldens"
OUT="$ROOT/tests/simulator/out"
COMPARE="$SCRIPT_DIR/compare_images.py"

SCENARIO=""
UPDATE_GOLDENS=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --scenario)
            SCENARIO="$2"
            shift 2
            ;;
        --update-goldens)
            UPDATE_GOLDENS=1
            shift
            ;;
        *)
            echo "uso: $0 [--scenario NOME] [--update-goldens]" >&2
            exit 1
            ;;
    esac
done

echo "==> Compilando simulador ($BUILD_DIR)"
cmake -S "$ROOT/tests/simulator" -B "$BUILD_DIR" >/dev/null
cmake --build "$BUILD_DIR" -j "$(nproc)"

# O simulador abre uma janela real (driver SDL) e nao responde a SIGTERM
# em loop de render; usa gnutimeout com SIGKILL e limpa o processo no final.
if [[ "$SCENARIO" != "" ]]; then
    SCENARIOS=("$SCENARIO")
else
    mapfile -t SCENARIOS < <("$SIM_BIN" --list 2>/dev/null | awk '/^  [a-z_]/ {print $1}' || true)
    if [[ ${#SCENARIOS[@]} -eq 0 ]]; then
        SCENARIOS=(shell_desktop shell_power shell_settings app_teclado app_notas app_files \
                   app_wifi app_bluetooth app_terminal app_gallery app_music app_chat \
                   app_recorder app_camera app_fileserver)
    fi
fi

if [[ "$UPDATE_GOLDENS" -eq 1 ]]; then
    echo "==> Regenerando goldens ($GOLDENS)"
    for sc in "${SCENARIOS[@]}"; do
        echo "    $sc"
        /usr/bin/gnutimeout -k 2 120 "$SIM_BIN" --scenario "$sc" --update-goldens >/dev/null
        pkill -9 -x tab5_sim 2>/dev/null || true
    done
    echo "==> Goldens atualizados. Verifique visualmente antes de commitar."
    exit 0
fi

echo "==> Capturando cenarios ($OUT)"
rm -rf "$OUT"
mkdir -p "$OUT"
for sc in "${SCENARIOS[@]}"; do
    echo "    $sc"
    /usr/bin/gnutimeout -k 2 120 "$SIM_BIN" --scenario "$sc" >/dev/null
    pkill -9 -x tab5_sim 2>/dev/null || true
done

echo "==> Comparando contra goldens"
if [[ "$SCENARIO" != "" ]]; then
    "$COMPARE" --goldens "$GOLDENS" --out "$OUT" --scenario "$SCENARIO"
else
    "$COMPARE" --goldens "$GOLDENS" --out "$OUT"
fi
