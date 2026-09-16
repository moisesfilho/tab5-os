#!/usr/bin/env bash
# Build and package one Tab5 WASM application.
set -euo pipefail

APP_DIR=""
DIST_DIR=""
WRAPPER_FILE=""
ENTRYPOINT_WRAPPER=""
REQUIRED_EXPORTS=()
EXPORTS=()

while (($#)); do
    case "$1" in
        --app-dir) APP_DIR="$2"; shift 2 ;;
        --dist-dir) DIST_DIR="$2"; shift 2 ;;
        --wrapper-file) WRAPPER_FILE="$2"; shift 2 ;;
        --entrypoint-wrapper) ENTRYPOINT_WRAPPER="$2"; shift 2 ;;
        --required-export) REQUIRED_EXPORTS+=("$2"); shift 2 ;;
        --exports)
            shift
            while (($#)) && [[ "$1" != --* ]]; do EXPORTS+=("$1"); shift; done
            ;;
    *) echo "[ERROR] Argumento desconhecido: $1" >&2; exit 2 ;;
    esac
done

[[ -n "${APP_DIR}" ]] || { echo "[ERROR] --app-dir e obrigatorio" >&2; exit 2; }
APP_DIR="$(cd "${APP_DIR}" && pwd)"
SDK_DIR="${TAB5_SDK_PATH:-${APP_DIR}/../tab5-os/sdk/tab5-app-sdk}"
DIST_DIR="${DIST_DIR:-${APP_DIR}/dist}"
WASI_CLANG="${WASI_SDK_PATH:-/home/moises/.wasi-sdk}/bin/clang"
PACK_TOOL="${SDK_DIR}/tools/pack.py"
VALIDATOR="${SDK_DIR}/../../tools/ci/validate_wasm_entrypoint.py"

[[ -x "${WASI_CLANG}" ]] || { echo "[ERROR] Compilador WASI nao encontrado: ${WASI_CLANG}" >&2; exit 1; }
[[ -f "${APP_DIR}/manifest.json" ]] || { echo "[ERROR] manifest.json ausente em ${APP_DIR}" >&2; exit 1; }
[[ -f "${APP_DIR}/src/main.c" ]] || { echo "[ERROR] src/main.c ausente em ${APP_DIR}" >&2; exit 1; }
[[ -f "${PACK_TOOL}" ]] || { echo "[ERROR] pack.py ausente em ${SDK_DIR}" >&2; exit 1; }
[[ -f "${VALIDATOR}" ]] || { echo "[ERROR] validador WASM ausente: ${VALIDATOR}" >&2; exit 1; }

rm -rf "${DIST_DIR}"
mkdir -p "${DIST_DIR}"

SOURCES=("${APP_DIR}/src/main.c")
TEMP_WRAPPER=""
cleanup() { [[ -z "${TEMP_WRAPPER}" ]] || rm -f "${TEMP_WRAPPER}"; }
trap cleanup EXIT

if [[ "${ENTRYPOINT_WRAPPER}" == auto ]]; then
    if grep -Eq '[[:space:]]app_main[[:space:]]*\(' "${APP_DIR}/src/main.c"; then
        ENTRYPOINT_WRAPPER=""
    else
        ENTRYPOINT_WRAPPER="main"
    fi
fi

if [[ -n "${WRAPPER_FILE}" ]]; then
    [[ -f "${WRAPPER_FILE}" ]] || { echo "[ERROR] wrapper ausente: ${WRAPPER_FILE}" >&2; exit 1; }
    SOURCES+=("${WRAPPER_FILE}")
elif [[ "${ENTRYPOINT_WRAPPER}" == main ]]; then
    if grep -Eq '[[:space:]]app_main[[:space:]]*\(' "${APP_DIR}/src/main.c"; then
        echo "[ERROR] --entrypoint-wrapper main conflita com app_main definido em ${APP_DIR}/src/main.c" >&2
        exit 1
    fi
    TEMP_WRAPPER="$(mktemp "${TMPDIR:-/tmp}/tab5-app-entrypoint.XXXXXX.c")"
    cat >"${TEMP_WRAPPER}" <<'EOF'
#include "tab5_sdk.h"

extern int main(int, char **);

TAB5_APP_ENTRYPOINT_EXPORT int tab5_wasm_app_main(void)
{
    return main(0, NULL);
}
EOF
    SOURCES+=("${TEMP_WRAPPER}")
elif [[ -n "${ENTRYPOINT_WRAPPER}" ]]; then
    echo "[ERROR] --entrypoint-wrapper desconhecido: ${ENTRYPOINT_WRAPPER}" >&2
    exit 2
fi

CLANG_ARGS=(-O2 "-I${SDK_DIR}/include")
for export_name in "${EXPORTS[@]}"; do
    CLANG_ARGS+=("-Wl,--export=${export_name}")
done
CLANG_ARGS+=("-Wl,--allow-undefined" -o "${APP_DIR}/app.wasm")
echo "[INFO] Compilando ${APP_DIR}..."
"${WASI_CLANG}" "${CLANG_ARGS[@]}" "${SOURCES[@]}"

VALIDATE_ARGS=("${APP_DIR}/app.wasm")
for required_export in "${REQUIRED_EXPORTS[@]}"; do
    VALIDATE_ARGS+=(--require-export "${required_export}")
done
python3 "${VALIDATOR}" "${VALIDATE_ARGS[@]}"
python3 "${PACK_TOOL}" "${APP_DIR}" -o "${DIST_DIR}"
APP_ID="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8"))["id"])' "${APP_DIR}/manifest.json")"
PACKAGE="${DIST_DIR}/${APP_ID}.tab5pkg"
[[ -f "${PACKAGE}" ]] || { echo "[ERROR] pacote esperado ausente: ${PACKAGE}" >&2; exit 1; }
echo "[OK] Pacote gerado: ${PACKAGE}"
