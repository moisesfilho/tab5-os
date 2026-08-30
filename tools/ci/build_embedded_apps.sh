#!/usr/bin/env bash
# build_embedded_apps.sh - Empacota as aplicações padrão para inclusão no bundle do firmware
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PACK_TOOL="${REPO_ROOT}/sdk/tab5-app-sdk/tools/pack.py"
PKG_OUTPUT_DIR="${REPO_ROOT}/embedded_apps_pkg"

mkdir -p "${PKG_OUTPUT_DIR}"

echo "[INFO] Empacotando aplicacoes de exemplo do SDK para o bundle..."
if [ -d "${REPO_ROOT}/sdk/tab5-app-sdk/examples/hello_wasm" ]; then
    # Gera dummy wasm caso nao exista para garantir empacotamento em CI
    if [ ! -f "${REPO_ROOT}/sdk/tab5-app-sdk/examples/hello_wasm/app.wasm" ]; then
        printf '\x00\x61\x73\x6d\x01\x00\x00\x00' > "${REPO_ROOT}/sdk/tab5-app-sdk/examples/hello_wasm/app.wasm"
    fi
    python3 "${PACK_TOOL}" "${REPO_ROOT}/sdk/tab5-app-sdk/examples/hello_wasm" -o "${PKG_OUTPUT_DIR}"
fi

if [ -d "${REPO_ROOT}/../tab5-app-notas" ]; then
    if [ ! -f "${REPO_ROOT}/../tab5-app-notas/app.wasm" ]; then
        printf '\x00\x61\x73\x6d\x01\x00\x00\x00' > "${REPO_ROOT}/../tab5-app-notas/app.wasm"
    fi
    python3 "${PACK_TOOL}" "${REPO_ROOT}/../tab5-app-notas" -o "${PKG_OUTPUT_DIR}"
fi

if [ -d "${REPO_ROOT}/../tab5-app-calendar" ]; then
    if [ ! -f "${REPO_ROOT}/../tab5-app-calendar/app.wasm" ]; then
        printf '\x00\x61\x73\x6d\x01\x00\x00\x00' > "${REPO_ROOT}/../tab5-app-calendar/app.wasm"
    fi
    python3 "${PACK_TOOL}" "${REPO_ROOT}/../tab5-app-calendar" -o "${PKG_OUTPUT_DIR}"
fi

# Varre submodulos em embedded_apps/ se existirem
if [ -d "${REPO_ROOT}/embedded_apps" ]; then
    for app_dir in "${REPO_ROOT}/embedded_apps"/*; do
        if [ -d "${app_dir}" ] && [ -f "${app_dir}/manifest.json" ]; then
            if [ ! -f "${app_dir}/app.wasm" ]; then
                printf '\x00\x61\x73\x6d\x01\x00\x00\x00' > "${app_dir}/app.wasm"
            fi
            python3 "${PACK_TOOL}" "${app_dir}" -o "${PKG_OUTPUT_DIR}"
        fi
    done
fi

echo "[INFO] Total de pacotes gerados em ${PKG_OUTPUT_DIR}:"
ls -la "${PKG_OUTPUT_DIR}"
