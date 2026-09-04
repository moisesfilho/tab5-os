#!/usr/bin/env bash
# build_embedded_apps.sh - Empacota as aplicações padrão para inclusão no bundle do firmware
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
PACK_TOOL="${REPO_ROOT}/sdk/tab5-app-sdk/tools/pack.py"
PKG_OUTPUT_DIR="${REPO_ROOT}/embedded_apps_pkg"
WASI_CLANG="${WASI_SDK_PATH:-/home/moises/.wasi-sdk}/bin/clang"
SDK_INC="${REPO_ROOT}/sdk/tab5-app-sdk/include"

mkdir -p "${PKG_OUTPUT_DIR}"

compile_app() {
    local app_dir="$1"
    if [ -f "${app_dir}/src/main.c" ] && [ -x "${WASI_CLANG}" ]; then
        echo "[INFO] Compilando ${app_dir}/src/main.c para ${app_dir}/app.wasm..."
        "${WASI_CLANG}" -O2 -I"${SDK_INC}" \
            -Wl,--export=main -Wl,--export=app_main -Wl,--export=tab5_app_on_ui_event -Wl,--export=on_ui_event -Wl,--export=tab5_app_on_theme_changed -Wl,--export=on_theme_changed -Wl,--allow-undefined \
            -o "${app_dir}/app.wasm" "${app_dir}/src/main.c"
    elif [ ! -f "${app_dir}/app.wasm" ]; then
        echo "[WARN] Gerando dummy wasm para ${app_dir}..."
        printf '\x00\x61\x73\x6d\x01\x00\x00\x00' > "${app_dir}/app.wasm"
    fi
}

echo "[INFO] Empacotando aplicacoes do sistema Tab5 OS para o bundle do firmware..."

# Lista de todos os apps embutidos (desacoplados em repositórios independentes)
EMBEDDED_APPS=(
    "tab5-app-wifi"
    "tab5-app-bluetooth"
    "tab5-app-terminal"
    "tab5-app-fileserver"
    "tab5-app-recorder"
    "tab5-app-music"
    "tab5-app-chat"
    "tab5-app-notas"
    "tab5-app-calendar"
    "tab5-app-files"
    "tab5-app-camera"
    "tab5-app-gallery"
)

for app_name in "${EMBEDDED_APPS[@]}"; do
    app_path="${REPO_ROOT}/../${app_name}"
    if [ -d "${app_path}" ]; then
        compile_app "${app_path}"
        python3 "${PACK_TOOL}" "${app_path}" -o "${PKG_OUTPUT_DIR}"
    fi
done

# Empacota apps de exemplo se presentes
if [ -d "${REPO_ROOT}/sdk/tab5-app-sdk/examples/hello_wasm" ]; then
    compile_app "${REPO_ROOT}/sdk/tab5-app-sdk/examples/hello_wasm"
    python3 "${PACK_TOOL}" "${REPO_ROOT}/sdk/tab5-app-sdk/examples/hello_wasm" -o "${PKG_OUTPUT_DIR}"
fi

if [ -d "${REPO_ROOT}/sdk/tab5-app-sdk/examples/widgets_demo" ]; then
    compile_app "${REPO_ROOT}/sdk/tab5-app-sdk/examples/widgets_demo"
    python3 "${PACK_TOOL}" "${REPO_ROOT}/sdk/tab5-app-sdk/examples/widgets_demo" -o "${PKG_OUTPUT_DIR}"
fi

# Varre submodulos em embedded_apps/ se existirem (suporte a CI/produção)
if [ -d "${REPO_ROOT}/embedded_apps" ]; then
    for app_dir in "${REPO_ROOT}/embedded_apps"/*; do
        if [ -d "${app_dir}" ] && [ -f "${app_dir}/manifest.json" ]; then
            compile_app "${app_dir}"
            python3 "${PACK_TOOL}" "${app_dir}" -o "${PKG_OUTPUT_DIR}"
        fi
    done
fi

echo "[INFO] Total de pacotes gerados em ${PKG_OUTPUT_DIR}:"
ls -la "${PKG_OUTPUT_DIR}"
