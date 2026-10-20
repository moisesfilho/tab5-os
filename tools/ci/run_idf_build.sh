#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LOCK_FILE="${REPO_ROOT}/dependencies.lock"

echo "🔨 [pre-push] Verificando compilação completa do firmware com ESP-IDF..."

if [[ ! -f "${LOCK_FILE}" ]]; then
    echo "⚠️  [pre-push] dependencies.lock ausente; build local ignorado. CI valida as dependências resolvidas."
    exit 0
fi

EXPECTED_IDF="$(awk '
    /^  idf:$/ { in_idf = 1; next }
    in_idf && /^    version:/ { print $2; exit }
    in_idf && /^  [^ ]/ { exit }
' "${LOCK_FILE}")"

if [[ -z "${EXPECTED_IDF}" ]]; then
    echo "❌ [pre-push] Não foi possível determinar a versão do ESP-IDF em dependencies.lock."
    exit 1
fi

IDF_ROOT="${IDF_PATH:-}"
if [[ -z "${IDF_ROOT}" ]] && command -v idf.py >/dev/null 2>&1; then
    IDF_ROOT="$(dirname "$(dirname "$(readlink -f "$(command -v idf.py)")")")"
fi
if [[ -z "${IDF_ROOT}" && -f "${HOME}/esp/esp-idf/tools/idf.py" ]]; then
    IDF_ROOT="${HOME}/esp/esp-idf"
fi

if [[ -z "${IDF_ROOT}" || ! -f "${IDF_ROOT}/tools/idf.py" ]]; then
    echo "⚠️  [pre-push] ESP-IDF não detectado. CI valida a versão ${EXPECTED_IDF}; build local ignorado."
    exit 0
fi

ACTUAL_IDF="$(awk '
    /set\(IDF_VERSION_MAJOR / { major = $2; sub(/\).*/, "", major) }
    /set\(IDF_VERSION_MINOR / { minor = $2; sub(/\).*/, "", minor) }
    /set\(IDF_VERSION_PATCH / { patch = $2; sub(/\).*/, "", patch) }
    END { if (major != "" && minor != "" && patch != "") print major "." minor "." patch }
' "${IDF_ROOT}/tools/cmake/version.cmake")"
if [[ "${ACTUAL_IDF}" != "${EXPECTED_IDF}" ]]; then
    echo "⚠️  [pre-push] IDF local ${ACTUAL_IDF:-desconhecido}; o projeto exige ${EXPECTED_IDF}. Build local ignorado para não reescrever dependencies.lock."
    exit 0
fi

if [[ -z "${IDF_PYTHON_ENV_PATH:-}" && -z "${IDF_PYTHON:-}" ]]; then
    ACTIVE_IDF_PY="$(command -v idf.py || true)"
    if [[ -z "${ACTIVE_IDF_PY}" || "$(readlink -f "${ACTIVE_IDF_PY}")" != "$(readlink -f "${IDF_ROOT}/tools/idf.py")" ]]; then
        if [[ -f "${IDF_ROOT}/export.sh" ]]; then
            if ! source "${IDF_ROOT}/export.sh" >/dev/null 2>&1; then
                echo "⚠️  [pre-push] Não foi possível ativar o ambiente ESP-IDF ${EXPECTED_IDF}; build local ignorado."
                exit 0
            fi
        fi
    fi
fi

PYTHON="${IDF_PYTHON:-}"
if [[ -z "${PYTHON}" && -n "${IDF_PYTHON_ENV_PATH:-}" && -x "${IDF_PYTHON_ENV_PATH}/bin/python" ]]; then
    PYTHON="${IDF_PYTHON_ENV_PATH}/bin/python"
fi
if [[ -n "${PYTHON}" ]]; then
    IDF_COMMAND=("${PYTHON}" "${IDF_ROOT}/tools/idf.py")
else
    IDF_COMMAND=("${IDF_ROOT}/tools/idf.py")
fi

# Component Manager serializes local component paths as absolute paths and may
# rewrite the lock during configure. Keep the developer's tracked lock intact.
LOCK_BACKUP="$(mktemp)"
LOCK_EXISTED=true
cp "${LOCK_FILE}" "${LOCK_BACKUP}"
restore_lock() {
    if [[ "${LOCK_EXISTED}" == true ]]; then
        cp "${LOCK_BACKUP}" "${LOCK_FILE}"
    fi
    rm -f "${LOCK_BACKUP}"
}
trap restore_lock EXIT

BUILD_DIR="${IDF_BUILD_DIR:-${REPO_ROOT}/build/pre-push-${EXPECTED_IDF}}"
cd "${REPO_ROOT}"
"${IDF_COMMAND[@]}" -B "${BUILD_DIR}" build
