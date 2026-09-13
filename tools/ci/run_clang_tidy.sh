#!/usr/bin/env bash
set -Eeuo pipefail

# clang-tidy must consume the same compilation options as the build.  In
# particular, silently continuing without the database (or after removing
# RISC-V diagnostics from the output) turns an invalid analysis into a green
# gate.  Keep the pre-commit entry point and the CI entry point identical.
ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$ROOT_DIR"

CLANG_TIDY_BIN=${CLANG_TIDY_BIN:-clang-tidy}
BUILD_DIR=${CLANG_TIDY_BUILD_DIR:-build/host}
COMPILE_DB="$BUILD_DIR/compile_commands.json"

if ! command -v "$CLANG_TIDY_BIN" >/dev/null 2>&1; then
    echo "❌ [clang-tidy] ferramenta não encontrada: $CLANG_TIDY_BIN" >&2
    exit 127
fi

if [ ! -f "$COMPILE_DB" ] || [ ! -s "$COMPILE_DB" ]; then
    echo "❌ [clang-tidy] compile database ausente ou vazio: $COMPILE_DB" >&2
    echo "   Gere-a com o CMake host e o mesmo clang usado pelo CI." >&2
    exit 2
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "❌ [clang-tidy] python3 não encontrado; não é possível validar o compile database." >&2
    exit 127
fi

REQUESTED=$(mktemp)
SELECTED_FILES=$(mktemp)
trap 'rm -f "$REQUESTED" "$SELECTED_FILES"' EXIT
printf '%s\n' "$@" > "$REQUESTED"

# Do not recreate compiler arguments here: -p makes clang-tidy consume the
# complete command recorded by CMake.  This small resolver only selects files
# that really have an entry in that database, preventing an accidental lint
# of a file with different/default arguments.
if ! python3 - "$ROOT_DIR" "$COMPILE_DB" "$REQUESTED" "$SELECTED_FILES" <<'PY'
import json
import pathlib
import sys

root = pathlib.Path(sys.argv[1]).resolve()
database = pathlib.Path(sys.argv[2])
requested_file = pathlib.Path(sys.argv[3])
selected_file = pathlib.Path(sys.argv[4])

entries = json.loads(database.read_text(encoding="utf-8"))
available = set()
for entry in entries:
    source = pathlib.Path(entry["file"])
    if not source.is_absolute():
        source = pathlib.Path(entry.get("directory", ".")) / source
    source = source.resolve()
    try:
        relative = source.relative_to(root)
    except ValueError:
        continue
    name = relative.as_posix()
    if (name.startswith(("main/", "components/os/"))
            and not name.startswith("components/os/fonts/")
            and source.suffix in {".c", ".cpp"}):
        available.add((source, name))

selected = []
for raw in requested_file.read_text(encoding="utf-8").splitlines():
    if not raw:
        continue
    candidate = pathlib.Path(raw)
    if not candidate.is_absolute():
        candidate = root / candidate
    candidate = candidate.resolve()
    for source, name in available:
        if source == candidate:
            selected.append(name)
            break
selected_file.write_text("".join(f"{name}\n" for name in selected), encoding="utf-8")
PY
then
    echo "❌ [clang-tidy] não foi possível ler o compile database: $COMPILE_DB" >&2
    exit 2
fi
mapfile -t FILES < "$SELECTED_FILES"

if [ ${#FILES[@]} -eq 0 ]; then
    exit 0
fi

TMP_LOG=$(mktemp)
trap 'rm -f "$REQUESTED" "$SELECTED_FILES" "$TMP_LOG"' EXIT

set +e
"$CLANG_TIDY_BIN" -p "$BUILD_DIR" \
    --config-file=.clang-tidy \
    --header-filter='components/(os|apps)/(?!.*minimp3).*' \
    "${FILES[@]}" 2>&1 | tee "$TMP_LOG"
TIDY_STATUS=${PIPESTATUS[0]}
set -e

# These diagnostics mean that clang (not the source) rejected a compiler
# option from the target toolchain.  They are reported separately, never
# deleted.  An environmental error invalidates the analysis and is therefore
# still a hard failure (status 2 when clang-tidy itself returned success).
ENVIRONMENTAL_DIAGNOSTICS=0
SOURCE_DIAGNOSTICS=0
while IFS= read -r diagnostic_line; do
    if [[ "$diagnostic_line" =~ (fatal\ error:|file\ not\ found|cannot\ find|error:\ (unknown\ argument|invalid\ arch\ name|unknown\ target\ CPU|unsupported\ option|unsupported\ argument)) ]]; then
        ENVIRONMENTAL_DIAGNOSTICS=$((ENVIRONMENTAL_DIAGNOSTICS + 1))
    elif [[ "$diagnostic_line" =~ (error|warning): ]]; then
        SOURCE_DIAGNOSTICS=$((SOURCE_DIAGNOSTICS + 1))
    fi
done < "$TMP_LOG"

if [ "$ENVIRONMENTAL_DIAGNOSTICS" -gt 0 ]; then
    echo "❌ [clang-tidy] análise inválida: incompatibilidade entre compile database/toolchain e clang-tidy." >&2
    echo "   Diagnósticos ambientais: $ENVIRONMENTAL_DIAGNOSTICS (não foram suprimidos)." >&2
fi

if [ "$SOURCE_DIAGNOSTICS" -gt 0 ]; then
    echo "❌ [clang-tidy] warnings/errors reais encontrados: $SOURCE_DIAGNOSTICS." >&2
fi

if [ "$TIDY_STATUS" -ne 0 ]; then
    echo "❌ [clang-tidy] falhou com exit code $TIDY_STATUS." >&2
    exit "$TIDY_STATUS"
fi

if [ "$ENVIRONMENTAL_DIAGNOSTICS" -gt 0 ]; then
    exit 2
fi

if [ "$SOURCE_DIAGNOSTICS" -gt 0 ]; then
    exit 1
fi

echo "✅ [clang-tidy] análise válida: nenhum problema encontrado."
