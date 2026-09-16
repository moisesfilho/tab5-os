#!/usr/bin/env bash
# Build selected independent Tab5 applications into the firmware bundle.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SDK_DIR="${REPO_ROOT}/sdk/tab5-app-sdk"
HELPER="${SDK_DIR}/tools/build_wasm_app.sh"
PKG_OUTPUT_DIR="${REPO_ROOT}/embedded_apps_pkg"

EMBEDDED_APPS=(
    "tab5-app-wifi" "tab5-app-bluetooth" "tab5-app-terminal" "tab5-app-fileserver"
    "tab5-app-recorder" "tab5-app-music" "tab5-app-chat" "tab5-app-notas"
    "tab5-app-calendar" "tab5-app-files" "tab5-app-camera" "tab5-app-gallery"
)
SELECTED=all
if (($#)); then
    [[ "$#" == 2 && "$1" == --app ]] || { echo "Uso: $0 [--app all|short-name|repo-dir]" >&2; exit 2; }
    SELECTED="$2"
fi

is_standard_app() {
    local candidate="$1" app
    for app in "${EMBEDDED_APPS[@]}"; do [[ "$app" == "$candidate" ]] && return 0; done
    return 1
}

SELECTED_PATH=""
if [[ "${SELECTED}" != all ]]; then
    if [[ -d "${SELECTED}" ]]; then
        SELECTED_PATH="$(cd "${SELECTED}" && pwd)"
    else
        app_name="${SELECTED}"
        [[ "${app_name}" == tab5-app-* ]] || app_name="tab5-app-${app_name}"
        is_standard_app "${app_name}" || { echo "[ERROR] App selecionado ausente ou invalido: ${SELECTED}" >&2; exit 1; }
        SELECTED_PATH="${REPO_ROOT}/../${app_name}"
    fi
    [[ -d "${SELECTED_PATH}" ]] || { echo "[ERROR] App ausente: ${SELECTED_PATH}" >&2; exit 1; }
fi

rm -rf "${PKG_OUTPUT_DIR}"
mkdir -p "${PKG_OUTPUT_DIR}"

build_standard_app() {
    local app_dir="$1" app_id package
    [[ -d "${app_dir}" ]] || { echo "[ERROR] App ausente: ${app_dir}" >&2; exit 1; }
    [[ -x "${app_dir}/tools/build.sh" ]] || { echo "[ERROR] build.sh ausente: ${app_dir}" >&2; exit 1; }
    "${app_dir}/tools/build.sh"
    app_id="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8"))["id"])' "${app_dir}/manifest.json")"
    package="${app_dir}/dist/${app_id}.tab5pkg"
    [[ -f "${package}" ]] || { echo "[ERROR] Pacote esperado ausente: ${package}" >&2; exit 1; }
    cp "${package}" "${PKG_OUTPUT_DIR}/"
}

if [[ "${SELECTED}" == all ]]; then
    for app_name in "${EMBEDDED_APPS[@]}"; do build_standard_app "${REPO_ROOT}/../${app_name}"; done
elif [[ -n "${SELECTED_PATH}" ]]; then
    build_standard_app "${SELECTED_PATH}"
else
    echo "[ERROR] App selecionado ausente ou invalido: ${SELECTED}" >&2
    exit 1
fi

# Applications supplied as embedded_apps/ remain supported and use the same helper.
if [[ -d "${REPO_ROOT}/embedded_apps" && "${SELECTED}" == all ]]; then
    for app_dir in "${REPO_ROOT}/embedded_apps"/*; do
        [[ -d "${app_dir}" && -f "${app_dir}/manifest.json" ]] || continue
        TAB5_SDK_PATH="${SDK_DIR}" "${HELPER}" --app-dir "${app_dir}" \
            --entrypoint-wrapper auto --exports main app_main \
            tab5_app_on_ui_event on_ui_event tab5_app_on_theme_changed \
            on_theme_changed tab5_app_on_open_file on_open_file
        app_id="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1], encoding="utf-8"))["id"])' "${app_dir}/manifest.json")"
        package="${app_dir}/dist/${app_id}.tab5pkg"
        [[ -f "${package}" ]] || { echo "[ERROR] Pacote embedded ausente: ${package}" >&2; exit 1; }
        cp "${package}" "${PKG_OUTPUT_DIR}/"
    done
fi

if [[ "${SELECTED}" != all ]]; then
    echo "[WARN] Bundle parcial: somente '${SELECTED}' foi gerado; nao execute idf.py build como firmware completo." >&2
fi

echo "[INFO] Pacotes gerados em ${PKG_OUTPUT_DIR}:"
ls -la "${PKG_OUTPUT_DIR}"
