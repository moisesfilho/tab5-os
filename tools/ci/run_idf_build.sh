#!/usr/bin/env bash
set -eo pipefail

echo "🔨 [pre-push] Verificando compilação completa do firmware com ESP-IDF..."

# Se idf.py já estiver no PATH atual
if command -v idf.py &> /dev/null; then
    idf.py build
    exit 0
fi

# Fallback: ativa ambiente do ESP-IDF padrão se existir
if [ -f "$HOME/esp/esp-idf/export.sh" ]; then
    export IDF_PYTHON_ENV_PATH="${IDF_PYTHON_ENV_PATH:-$HOME/.espressif/python_env_3.12}"
    . "$HOME/esp/esp-idf/export.sh" > /dev/null 2>&1
    idf.py build
    exit 0
fi

echo "⚠️  [pre-push] Ambiente ESP-IDF não detectado. Pulando verificação de build local."
exit 0
