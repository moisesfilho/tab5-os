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

EXTRA_ARGS=()
EXTRA_ARGS+=("--extra-arg=-Wno-unknown-warning-option")
EXTRA_ARGS+=("--extra-arg=-Wno-extern-c-compat")
EXTRA_ARGS+=("--extra-arg=-Wno-unknown-pragmas")

# Auto-detecta headers do toolchain riscv32 em ~/.espressif se existirem
TOOLCHAIN_GCC_INCLUDE=$(find "${HOME}/.espressif/tools/riscv32-esp-elf" -type d -name "14.2.0" 2>/dev/null | head -n 1)
if [ -n "$TOOLCHAIN_GCC_INCLUDE" ]; then
    EXTRA_ARGS+=("--extra-arg=-isystem${TOOLCHAIN_GCC_INCLUDE}/../../../../riscv32-esp-elf/include/c++/14.2.0")
    EXTRA_ARGS+=("--extra-arg=-isystem${TOOLCHAIN_GCC_INCLUDE}/../../../../riscv32-esp-elf/include/c++/14.2.0/riscv32-esp-elf")
    EXTRA_ARGS+=("--extra-arg=-isystem${TOOLCHAIN_GCC_INCLUDE}/../../../../riscv32-esp-elf/include")
    EXTRA_ARGS+=("--extra-arg=-isystem${TOOLCHAIN_GCC_INCLUDE}/include")
fi

TMP_LOG=$(mktemp)
trap 'rm -f "$TMP_LOG" "${TMP_LOG}.clean"' EXIT

set +e
clang-tidy -p build \
    --config-file=.clang-tidy \
    "${EXTRA_ARGS[@]}" \
    --header-filter='components/(os|apps)/(?!.*minimp3).*' \
    "${FILES[@]}" 2>&1 | tee "$TMP_LOG"

grep -vE "error: (unknown argument|invalid arch name)" "$TMP_LOG" > "${TMP_LOG}.clean" || true

if grep -qE 'error:|warning:' "${TMP_LOG}.clean"; then
    echo "❌ [pre-commit] clang-tidy encontrou problemas nos arquivos modificados."
    cat "${TMP_LOG}.clean"
    exit 1
fi

echo "✅ [pre-commit] clang-tidy: nenhum problema encontrado."
exit 0
