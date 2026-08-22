#!/usr/bin/env bash
set -eo pipefail

if ! command -v clang-tidy &> /dev/null; then
    echo "⚠️  [pre-commit] clang-tidy não instalado no host. (Para instalar: sudo apt install clang-tidy)"
    exit 0
fi

if [ ! -f "build/compile_commands.json" ]; then
    echo "⚠️  [pre-commit] build/compile_commands.json não encontrado. Execute 'idf.py build' ou 'idf.py reconfigure'."
    exit 0
fi

FILES=()
for f in "$@"; do
    # Apenas arquivos de projeto em main/, components/os/ ou components/apps/
    if [[ "$f" =~ ^(main/|components/os/|components/apps/).*\.(cpp|c)$ ]] && [[ ! "$f" =~ ^components/os/fonts/ ]]; then
        FILES+=("$f")
    fi
done

if [ ${#FILES[@]} -eq 0 ]; then
    exit 0
fi

TMP_LOG=$(mktemp)
trap 'rm -f "$TMP_LOG"' EXIT

set +e
clang-tidy -p build \
    --config-file=.clang-tidy \
    --extra-arg="-Wno-unknown-warning-option" \
    --extra-arg="-Wno-extern-c-compat" \
    --extra-arg="-Wno-unknown-pragmas" \
    --header-filter='components/(os|apps)/(?!.*minimp3).*' \
    "${FILES[@]}" 2>&1 | tee "$TMP_LOG"

grep -vE "error: unknown argument:" "$TMP_LOG" > "${TMP_LOG}.clean" || true

if grep -qE 'error:|warning:' "${TMP_LOG}.clean"; then
    echo "❌ [pre-commit] clang-tidy encontrou problemas nos arquivos modificados."
    cat "${TMP_LOG}.clean"
    exit 1
fi

echo "✅ [pre-commit] clang-tidy: nenhum problema encontrado."
exit 0
