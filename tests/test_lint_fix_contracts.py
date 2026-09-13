#!/usr/bin/env python3
"""Contratos estáticos das correções de lint CI planejadas (fase vermelha).

O CI/`pre-commit` do tab5-os roda, hoje, três lints que falham por motivos
reais:

1. `.clang-tidy` não tem exclusões justificadas para dois checkers cuja
   decisão de projeto é aceitar: `bugprone-assignment-in-if-condition` (padrão
   `if ((err = api(...)) != OK)` usado no firmware) e
   `readability-use-concise-preprocessor-directives` (`#if defined(X)`
   explícito preferido a `#ifdef X` em código multi-target).
2. `.codespellrc` não consolida os falsos positivos `assertIn`, `excede` e
   `saem`; hoje essas palavras só são ignoradas pelo override `-L` inline do
   hook codespell no `.pre-commit-config.yaml`, o que esconde os typos de
   qualquer execução do codespell que não passe pelo pre-commit.
3. `main/CMakeLists.txt` e `tests/simulator/CMakeLists.txt` têm linhas acima
   do limite do `cmake-lint` (default 80; respeita `line_width` de um
   eventual `.cmake-format*` na raiz).
4. Warnings reais de clang-tidy em `tab5_package_mgr.cpp`,
   `tab5_ui_host.cpp` e `serial_bridge.cpp` (confirmados localmente: 23
   warnings) devem ser corrigidos; os testes abaixo protegem as propriedades
   subjacentes de cada correção, sem exigir formatação específica.

Os contratos abaixo impedem regressões nas correções de produção/configuração.

Rastreabilidade:
  REQ-CONFIG-001 -> TEST-CONFIG-001..003 (clang-tidy, CMake e codespell)
  REQ-LINT-003   -> TEST-LINT-003 (offset de screen.dump)
"""

import json
import pathlib
import re
import shutil
import subprocess
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]

CLANG_TIDY_SRC = (ROOT / ".clang-tidy").read_text(encoding="utf-8")
CODESPELLRC_SRC = (ROOT / ".codespellrc").read_text(encoding="utf-8")
PRE_COMMIT_SRC = (ROOT / ".pre-commit-config.yaml").read_text(encoding="utf-8")
PKG_MGR_SRC = (ROOT / "components/os/runtime/tab5_package_mgr.cpp").read_text(encoding="utf-8")
UI_HOST_SRC = (ROOT / "components/os/runtime/tab5_ui_host.cpp").read_text(encoding="utf-8")
SERIAL_BRIDGE_SRC = (ROOT / "components/os/core/serial_bridge.cpp").read_text(encoding="utf-8")
HOST_ABI_SRC = (ROOT / "components/os/runtime/tab5_host_abi.cpp").read_text(encoding="utf-8")
WASM_RUNTIME_SRC = (ROOT / "components/os/runtime/tab5_wasm_runtime.cpp").read_text(encoding="utf-8")
WASM_RUNTIME_HDR = (ROOT / "components/os/runtime/tab5_wasm_runtime.h").read_text(encoding="utf-8")
MANIFEST_SRC = (ROOT / "components/os/runtime/tab5_manifest.cpp").read_text(encoding="utf-8")
STORAGE_MGR_SRC = (ROOT / "components/os/core/storage_mgr.cpp").read_text(encoding="utf-8")

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

# Replica a regex `exclude` do top-level do `.pre-commit-config.yaml`.
_PRE_COMMIT_EXCLUDE = re.compile(
    r"(?:^|/)("
    r"components/m5stack_tab5/"
    r"|components/rtc_rx8130/"
    r"|components/os/fonts/"
    r"|components/os/core/minimp3\.h"
    r"|components/os/core/tjpgd\.(c|h)"
    r"|managed_components/"
    r")"
)

_BUILD_DIRS = {"build", "build-sim", ".git", ".pytest_cache", ".opencode"}

# Diretórios de *build* do CMake detectados por marcador (CMakeCache.txt).
# O `run_host_tests.sh` aceita BUILD_DIR arbitrário (build-cov, host-asan,
# host-review, ...); o marcador cobre qualquer nome sem depender de lista. Os
# artefatos gerados (ex.: tab5_host_tests[1]_tests.cmake com linhas de milhares
# de caracteres) ficam fora do escopo do cmake-lint; o teste continua cobrindo
# apenas CMakeLists.txt/.cmake versionados no repositório.
_CMAKE_BUILD_DIRS = frozenset(
    cache.parent.relative_to(ROOT).as_posix()
    for cache in ROOT.glob("**/CMakeCache.txt")
    if not any(part.startswith(".") for part in cache.parts)
)


def _is_excluded(rel_path: str) -> bool:
    """True se o arquivo é gerado/vendido/ignorado ou vive em um build dir.

    Ignora arquivos sob diretórios de build do CMake (marcados por
    `CMakeCache.txt` em qualquer ancestral) além da política do pre-commit;
    assim, um BUILD_DIR local com nome arbitrário não vira falso positivo.
    """
    pure = pathlib.PurePosixPath(rel_path)
    parts = set(pure.parts)
    if parts & _BUILD_DIRS:
        return True
    ancestor = pure.parent
    while ancestor.as_posix() != ".":
        if ancestor.as_posix() in _CMAKE_BUILD_DIRS:
            return True
        ancestor = ancestor.parent
    return bool(_PRE_COMMIT_EXCLUDE.search(rel_path))


def cmake_line_width() -> int:
    """Lê `line_width` de um eventual `.cmake-format*`; senão o default (80)."""
    candidates = (
        (ROOT / ".cmake-format"),
        (ROOT / ".cmake-format.json"),
        (ROOT / ".cmake-format.yaml"),
        (ROOT / ".cmake-format.yml"),
    )
    for path in candidates:
        if not path.exists():
            continue
        try:
            if path.suffix == ".json":
                return int(json.loads(path.read_text(encoding="utf-8")).get("line_width", 80))
            match = re.search(r"(?m)^\s*line_width\s*[:=]\s*(\d+)\s*$", path.read_text(encoding="utf-8"))
            if match:
                return int(match.group(1))
        except (ValueError, TypeError, OSError):
            continue
    return 80


def cmake_lint_files():
    """CMakeLists.txt e *.cmake relevantes (não excluídos pelos hooks)."""
    paths = list(ROOT.glob("**/CMakeLists.txt")) + list(ROOT.glob("**/*.cmake"))
    for path in sorted(paths):
        rel = str(path.relative_to(ROOT))
        if not _is_excluded(rel):
            yield path


def codespellrc_ignore_words():
    """Conjunto (lowercase) da `ignore-words-list` do `.codespellrc`."""
    match = re.search(r"(?ms)^\[codespell\][^\[]*ignore-words-list\s*=\s*([^\n#]+)", CODESPELLRC_SRC)
    if not match:
        return set()
    return {word.strip().lower() for word in match.group(1).split(",") if word.strip()}


def _codespell_hook_args():
    """Extrai ``args`` do hook codespell no ``.pre-commit-config.yaml``.

    Usa ``yaml.safe_load`` quando disponível; em caso de falha ou ausência
    cai para um parsing textual delimitado (``- id: codespell`` até o
    próximo ``- repo:`` ou fim de arquivo).
    """
    try:
        import yaml as _yaml  # noqa: delay import for portability
    except ImportError:
        _yaml = None
    if _yaml is not None:
        try:
            cfg = _yaml.safe_load(PRE_COMMIT_SRC)
            for repo in cfg.get("repos", []) if isinstance(cfg, dict) else []:
                hooks = repo.get("hooks", []) if isinstance(repo, dict) else []
                for hook in hooks:
                    if isinstance(hook, dict) and hook.get("id") == "codespell":
                        args = hook.get("args")
                        return list(args) if args else []
            return None  # hook não encontrado
        except Exception:
            pass
    # --- fallback: parsing textual delimitado ---
    hook = re.search(
        r"(?m)^\s*- id:\s*codespell\b(.*?)(?=^\s*- repo:|\Z)",
        PRE_COMMIT_SRC,
        re.S,
    )
    if hook is None:
        return None
    args_match = re.search(r"^\s*args:\s*\[(.*)\]\s*$", hook.group(1), re.S | re.M)
    if args_match is None:
        return []
    return re.findall(r'"((?:[^"\\]|\\.)*)"', args_match.group(1))


def function_body(source: str, signature: str) -> str:
    """Extrai o corpo de uma função C++ respeitando chaves aninhadas."""
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for pos in range(opening, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : pos]
    raise AssertionError(f"corpo de função não fechado: {signature}")


def single_define_preprocessor_lines(source: str):
    """Linhas `#if defined(X)` com um único identificador (convertíveis em #ifdef)."""
    for num, line in enumerate(source.splitlines(), 1):
        stripped = line.strip()
        match = re.match(r"#\s*if\s+defined\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*\)\s*$", stripped)
        if match:
            yield num, line


def _has_justifying_comment(source: str, position: int) -> bool:
    """Verdadeiro se há um comentário não trivial nas 12 linhas antes de `position`."""
    prefix = source[:position].splitlines()[-12:]
    return any(
        line.lstrip().startswith("#") and len(line.strip()) > 15 for line in prefix
    )


# ---------------------------------------------------------------------------
# R1 — .clang-tidy: exclusões justificadas
# ---------------------------------------------------------------------------

class TestClangTidyExclusions(unittest.TestCase):
    """R1: `.clang-tidy` contém exclusões justificadas para determinados checkers."""

    TOKENS = (
        "-bugprone-assignment-in-if-condition",
        "-readability-use-concise-preprocessor-directives",
    )

    def _dumped_checks(self):
        """REQ-CONFIG-001: lê a lista efetiva, não só o texto YAML."""
        if shutil.which("clang-tidy"):
            result = subprocess.run(
                ["clang-tidy", "--dump-config", "--config-file=.clang-tidy"],
                cwd=ROOT,
                check=True,
                capture_output=True,
                text=True,
            )
            return next(line.split(":", 1)[1] for line in result.stdout.splitlines()
                        if line.startswith("Checks:"))
        return CLANG_TIDY_SRC

    def _assert_exclusion_justified(self, token: str):
        # A justificativa fica fora do scalar; comentários dentro dele viram
        # tokens e tornam a configuração ambígua para clang-tidy.
        self.assertIn(token, self._dumped_checks(), f"{token} não é efetivo no dump-config")

    def test_assignment_in_if_condition_exclusion_justified(self):
        """`bugprone-assignment-in-if-condition` excluído com justificativa."""
        self._assert_exclusion_justified(self.TOKENS[0])

    def test_concise_preprocessor_exclusion_justified(self):
        """`readability-use-concise-preprocessor-directives` excluído com justificativa."""
        self._assert_exclusion_justified(self.TOKENS[1])

    def test_exclusions_hold_format_prefix(self):
        """TEST-CONFIG-001: o dump preserva o prefixo de negação."""
        for token in self.TOKENS:
            self.assertIn(token, self._dumped_checks())
        checks_scalar = CLANG_TIDY_SRC.split("Checks:", 1)[1].split("WarningsAsErrors:", 1)[0]
        self.assertNotIn("#", checks_scalar, "Checks não pode conter comentários no scalar")


# ---------------------------------------------------------------------------
# R2 — .codespellrc e override do pre-commit
# ---------------------------------------------------------------------------

class TestCodespellConsolidation(unittest.TestCase):
    """R2: falsos positivos consolidados no `.codespellrc`, sem override no pre-commit."""

    FALSE_POSITIVES = {"assertin", "excede", "saem"}

    def test_codespellrc_consolidates_false_positives(self):
        """`assertIn`, `excede` e `saem` presentes na ignore-words-list do `.codespellrc`."""
        ignored = codespellrc_ignore_words()
        missing = self.FALSE_POSITIVES - ignored
        self.assertFalse(
            missing,
            f"falsos positivos ausentes do .codespellrc (ignore-words-list): {sorted(missing)}",
        )

    def test_codespellrc_has_ignore_words_list(self):
        """O arquivo `.codespellrc` define `ignore-words-list` na seção [codespell]."""
        self.assertRegex(CODESPELLRC_SRC, r"(?m)^\[codespell\]")
        self.assertRegex(CODESPELLRC_SRC, r"ignore-words-list\s*=")

    def test_pre_commit_has_no_ignore_list_override(self):
        """O hook codespell do pre-commit não usa `-L`/`--ignore-words-list` inline.

        A fonte única dos falsos positivos é o `.codespellrc`; manter um
        override `-L` no pre-commit dependeria de config duplicada e deixaria
        execuções do codespell fora do pre-commit (CLI, CI externo, IDE)
        reportarem typos falsos.
        """
        args = _codespell_hook_args()
        self.assertIsNotNone(args, "hook codespell não localizado no pre-commit")
        offenders = [
            arg
            for arg in args
            if re.match(r"^-L(?:[,= ].*)?$|^--ignore-words-list(?:[,= ]|$)", arg)
        ]
        self.assertEqual(
            offenders,
            [],
            "hook codespell não deve usar -L/--ignore-words-list; " "mova para o .codespellrc",
        )


# ---------------------------------------------------------------------------
# R3 — CMakeLists sem linhas acima do limite do cmake-lint
# ---------------------------------------------------------------------------

class TestCMakeLineLength(unittest.TestCase):
    """R3: CMakeLists relevantes não contêm linhas além do limite configurado."""

    def test_no_cmake_file_exceeds_configured_width(self):
        """Nenhuma linha de um CMake relevante ultrapassa `line_width`."""
        limit = cmake_line_width()
        offenders = []
        for path in cmake_lint_files():
            for num, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
                if len(line) > limit:
                    offenders.append(f"{path.relative_to(ROOT)}:{num}:{len(line)}>{limit}")
        self.assertFalse(
            offenders,
            "Linhas CMake acima do limite configurado (%d): %s"
            % (limit, "; ".join(offenders[:12])),
        )

    def test_configured_width_is_positive(self):
        """O limite configurado (default 80 ou `.cmake-format`) é um número válido."""
        width = cmake_line_width()
        self.assertIsInstance(width, int)
        self.assertGreater(width, 0)


# ---------------------------------------------------------------------------
# R4 — Warnings reais de package_mgr, ui_host e serial_bridge
# ---------------------------------------------------------------------------

class TestSourceCodeWarningProtections(unittest.TestCase):
    """R4: warnings reais de clang-tidy corrigidos e protegidos por contrato."""

    # --- serial_bridge.cpp -------------------------------------------------

    def test_serial_bridge_fseek_validates_offset_before_conversion(self):
        """REQ-LINT-003 / TEST-LINT-003: offset só é convertido após validação.

        Warning real: `bugprone-misplaced-widening-cast` em serial_bridge.cpp:349
        (`fseek(file, static_cast<long>(from_chunk * chunk_size), SEEK_SET)`).
        O contrato semântico é rejeitar o índice que não cabe em `long` antes da
        multiplicação e da conversão, não apenas mover o cast.
        """
        start = SERIAL_BRIDGE_SRC.index("constexpr size_t max_seek_offset")
        body = SERIAL_BRIDGE_SRC[start : SERIAL_BRIDGE_SRC.index("for (size_t index", start)]
        self.assertIn("from_chunk > max_seek_offset / chunk_size", body)
        self.assertIn("const size_t offset_value = from_chunk * chunk_size", body)
        self.assertIn("const long offset = static_cast<long>(offset_value)", body)
        self.assertLess(
            body.index("from_chunk > max_seek_offset / chunk_size"),
            body.index("from_chunk * chunk_size"),
            "a multiplicação deve ocorrer somente depois do limite validado",
        )

    # --- tab5_ui_host.cpp --------------------------------------------------

    def test_ui_host_theme_switch_has_no_duplicate_consecutive_cases(self):
        """`tab5_ui_host_theme_get_color` sem cases consecutivos com corpo idêntico.

        Warning real: `bugprone-branch-clone` em tab5_ui_host.cpp:1699 (`case 0` e
        `case 1` retornam o mesmo `pal->accent`). A correção deve consolidar os
        cases (fall-through) ou diferenciar os retornos.
        """
        body = function_body(UI_HOST_SRC, "uint32_t tab5_ui_host_theme_get_color")
        cases = re.findall(r"case\s+\d+\s*:\s*return\s+([^;]+);", body)
        duplicates = [
            (a, b)
            for a, b in zip(cases, cases[1:])
            if a.strip() == b.strip()
        ]
        self.assertEqual(
            duplicates,
            [],
            "cases consecutivos com o mesmo corpo no switch de cores: %s"
            % "; ".join(f"{a} == {b}" for a, b in duplicates[:4]),
        )

    # --- tab5_package_mgr.cpp ----------------------------------------------

    def test_package_mgr_single_defined_uses_ifdef_or_comment(self):
        """`#if defined(MACRO)` simples em package_mgr é `#ifdef` ou justificado.

        Warning real: `readability-use-concise-preprocessor-directives` em
        tab5_package_mgr.cpp:50 (`#if defined(TAB5_SIM)`). Diretiva de um único
        identificador é equivalente a `#ifdef`; o contrato aceita a forma
        `#ifdef` ou a forma `defined(X)` acompanhada de comentário justificando
        a escolha (ex.: simetria com builds multi-macro).
        """
        offenders = []
        for num, line in single_define_preprocessor_lines(PKG_MGR_SRC):
            previous = PKG_MGR_SRC.splitlines()[num - 2] if num >= 2 else ""
            has_inline_comment = "//" in line or "/*" in line
            has_prev_comment = previous.lstrip().startswith("//") or previous.lstrip().startswith("/*")
            if not (has_inline_comment or has_prev_comment):
                offenders.append(f"linha {num}: {line.strip()}")
        self.assertEqual(
            offenders,
            [],
            "Altere para #ifdef ou justifique com comentário (%d): %s"
            % (len(offenders), "; ".join(offenders[:4])),
        )

    # --- warning residuals from the expanded clang-tidy pass ----------------

    def test_host_abi_non_lvgl_parameters_are_intentionally_consumed(self):
        """Parâmetros válidos da ABI não podem virar unused no build host."""
        body = function_body(HOST_ABI_SRC, "tab5_ui_app_bar_add_action_button")
        self.assertIn("#if !HAVE_LVGL", body)
        self.assertIn("(void)on_click", body)
        self.assertIn("(void)user_data", body)
        body = function_body(HOST_ABI_SRC, "tab5_nvs_set_u8")
        self.assertIn("(void)val", body)

    def test_host_abi_wasm_module_helper_is_targeted(self):
        """Helper usado só com WAMR não deve existir como função morta no host."""
        self.assertRegex(HOST_ABI_SRC, r"#if HAVE_WAMR_ENV\s+static wasm_module_inst_t wasm_module")

    def test_storage_membership_uses_cxx20_contains(self):
        """O lookup de diretórios deve usar a API de membership semântica."""
        self.assertIn("inspected_dirs.contains(pkg_name)", STORAGE_MGR_SRC)
        self.assertNotIn("inspected_dirs.count(pkg_name) > 0", STORAGE_MGR_SRC)

    def test_widening_multiplications_are_explicit(self):
        """Limites derivados de ftell devem ser calculados em tipo long."""
        self.assertIn("8L * 1024L * 1024L", WASM_RUNTIME_SRC)
        self.assertIn("64L * 1024L", MANIFEST_SRC)

    def test_wasm_query_definitions_match_public_parameter_names(self):
        """Definições das queries devem preservar o contrato nominal do header."""
        for name in (
            "tab5_wasm_instance_snapshot",
            "tab5_wasm_instance_call_depth",
            "tab5_wasm_instance_unload_pending",
        ):
            self.assertIn(f"const tab5_wasm_app_instance_t *inst", WASM_RUNTIME_HDR)
            self.assertRegex(WASM_RUNTIME_SRC, rf"{name}\(.*?const tab5_wasm_app_instance_t \*inst", re.S)


if __name__ == "__main__":
    unittest.main(verbosity=2)
