#!/usr/bin/env bash
set -eo pipefail

if ! command -v cppcheck &> /dev/null; then
    echo "⚠️  [pre-commit] cppcheck não instalado no host. (Para instalar: sudo apt install cppcheck)"
    exit 0
fi

FILES=()
for f in "$@"; do
    if [[ "$f" =~ ^(main/|components/os/|components/apps/).*\.(cpp|c)$ ]] && [[ ! "$f" =~ ^components/os/fonts/ ]]; then
        FILES+=("$f")
    fi
done

if [ ${#FILES[@]} -eq 0 ]; then
    exit 0
fi

cppcheck \
    --enable=warning,performance,portability \
    --suppress=missingIncludeSystem \
    --suppress=syntaxError \
    --suppress=incorrectStringBooleanError \
    --error-exitcode=1 \
    --inline-suppr \
    "${FILES[@]}"

echo "✅ [pre-commit] cppcheck: nenhum problema encontrado."
exit 0
