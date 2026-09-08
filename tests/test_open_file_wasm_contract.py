"""Static acceptance contracts for WASM open_file dispatch (sem goldens).

These tests intentionally do not use screenshots or golden files.  They check
the small, safety-critical control-flow obligations of the approved plan for
"open_file via WASM stable export" that can be verified from the source before
the runtime implementation is changed:

1. Despacho via export estável  — abertura de arquivo em app WASM despacha
   através de tab5_wasm_call_function (nome primário "tab5_app_on_open_file"),
   nunca invocando o ponteiro de callback registrado no lifecycle.
2. Caminho sandbox seguro  — o despacho acontece somente depois da resolução
   sandbox (tab5_storage_sandbox_resolve_path) e transporta o safe_path, não a
   string bruta recebida do sistema de arquivos.
3. Ponteiro liberado  — se o caminho for copiado para a memória WASM, o
   buffer alocado é liberado após o retorno do despacho.
4. Fallback alias somente em NOT_FOUND  — o alias "on_open_file" só é
   tentado quando o export primário falta (mesmo contrato já validado no
   helper call_wasm_callback do ui_host).
5. Nenhuma chamada direta a ponteiro de callback WASM  — em lifecycle_host,
   package_mgr e ui_host todo ponteiro lifecycle.on_* só pode ser invocado sob
   guarda !ctx->is_wasm (ou numa ramificação estruturalmente exclusiva de
   wasm); dispatch WASM passa obrigatoriamente pelo dispatcher do runtime.

A execução dinâmica WAMR não está disponível no build host (HAVE_WAMR=0); o
comportamento é adicionalmente exercitado por tests/host/src/
test_open_file_wasm_contract.cpp.  Nenhuma verificação golden é usada.
"""

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
LIFE = (ROOT / "components/os/runtime/tab5_lifecycle_host.cpp").read_text(encoding="utf-8")
UI = (ROOT / "components/os/runtime/tab5_ui_host.cpp").read_text(encoding="utf-8")
PKG = (ROOT / "components/os/runtime/tab5_package_mgr.cpp").read_text(encoding="utf-8")
WASM = (ROOT / "components/os/runtime/tab5_wasm_runtime.cpp").read_text(encoding="utf-8")

OPEN_FILE_PRIMARY = "tab5_app_on_open_file"
OPEN_FILE_ALIAS = "on_open_file"

# Primários já estabelecidos para ui_event/theme_changed pelo mesmo helper.
KNOWN_PRIMARIES = ("tab5_app_on_ui_event", "tab5_app_on_theme_changed", OPEN_FILE_PRIMARY)
KNOWN_ALIASES = ("on_ui_event", "on_theme_changed")

LIFECYCLE_CBS = ("on_init", "on_resume", "on_pause", "on_destroy", "on_open_file")

ALLOC_PRIMITIVES = ("wasm_runtime_module_malloc", "wasm_runtime_malloc", "wasm_runtime_malloc_with_allocator")
RELEASE_PRIMITIVES = ("wasm_runtime_module_free", "wasm_runtime_free")


def function_body(source: str, signature: str) -> str:
    """Return one C++ function body, respecting nested braces."""
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
    raise AssertionError(f"unclosed function body: {signature}")


def open_file_body() -> str:
    return function_body(LIFE, "tab5_err_t tab5_lifecycle_host_open_file")


def wasm_dispatch_region() -> str:
    """Região da função open_file após a resolução sandbox (o despacho WASM
    precisa estar aqui).  Retorna vazio se ainda não há despacho."""
    body = open_file_body()
    if OPEN_FILE_PRIMARY not in body:
        return ""
    idx = body.index("tab5_storage_sandbox_resolve_path")
    if idx == -1:
        return ""
    return body[idx:]


def string_dispatch_body() -> str:
    return function_body(WASM, "tab5_err_t tab5_wasm_call_string_function")


class OpenFileWasmDispatchContract(unittest.TestCase):
    """open_file de app WASM despacha via export estável, com caminho seguro."""

    def test_wasm_dispatches_via_stable_export_after_sandbox(self):
        body = open_file_body()
        self.assertIn(OPEN_FILE_PRIMARY, body,
                      "despacho open_file WASM precisa tentar o export primário "
                      f"{OPEN_FILE_PRIMARY}")
        self.assertIn("tab5_storage_sandbox_resolve_path", body)
        self.assertLess(
            body.index("tab5_storage_sandbox_resolve_path"),
            body.index(OPEN_FILE_PRIMARY),
            "o despacho só pode ocorrer após a resolução sandbox",
        )

    def test_wasm_dispatch_runs_on_runtime_dispatcher(self):
        region = wasm_dispatch_region()
        self.assertIn(OPEN_FILE_PRIMARY, region)
        self.assertTrue(
            "tab5_wasm_call_function(" in region or "tab5_wasm_call_string_function(" in region or
            "call_wasm_callback(" in region,
            "o despacho WASM precisa passar pelo dispatcher do runtime "
            "(direto ou via call_wasm_callback), nunca pelo ponteiro lifecycle",
        )

    def test_lifecycle_uses_string_dispatch_api(self):
        body = open_file_body()
        self.assertIn("tab5_wasm_call_string_function", body)
        self.assertIn('"tab5_app_on_open_file"', body)

    def test_dispatch_transports_sandboxed_path(self):
        region = wasm_dispatch_region()
        self.assertIn(OPEN_FILE_PRIMARY, region)
        self.assertIn("safe_path", region,
                      "o WASM precisa receber o caminho já resolvido na sandbox, "
                      "nunca a string bruta de entrada")

    def test_shuttled_path_pointer_is_released_after_dispatch(self):
        body = string_dispatch_body()
        has_alloc = any(p in body for p in ALLOC_PRIMITIVES)
        has_release = any(p in body for p in RELEASE_PRIMITIVES)
        self.assertTrue(
            has_alloc,
            "levar o caminho à memória WASM exige alocação do ponteiro seguro "
            f"({', '.join(ALLOC_PRIMITIVES)})",
        )
        self.assertTrue(
            has_release,
            "o ponteiro de caminho alocado na memória WASM precisa ser liberado "
            f"após o despacho ({', '.join(RELEASE_PRIMITIVES)})",
        )

    def test_alias_is_attempted_only_on_not_found(self):
        region = wasm_dispatch_region()
        self.assertIn(OPEN_FILE_PRIMARY, region)
        self.assertIn(OPEN_FILE_ALIAS, region,
                      "o alias simples do export primário precisa existir no despacho")
        if "call_wasm_callback(" in region:
            # O helper centraliza o fallback; o contrato NOT_FOUND-only é
            # verificado nativamente pelo teste abaixo.
            return
        guard = re.search(r"if\s*\(\s*err\s*==\s*TAB5_ERR_NOT_FOUND\s*\)", region)
        self.assertIsNotNone(
            guard,
            "o retry via alias precisa ser condicionado a TAB5_ERR_NOT_FOUND",
        )
        self.assertLess(
            guard.start(), region.index(OPEN_FILE_ALIAS, guard.start()),
            "a condição NOT_FOUND precisa preceder a chamada do alias",
        )
        self.assertNotRegex(
            region,
            r"else\s*\{\s*\w+\s*=\s*tab5_wasm_call_function\(",
            "não pode haver retry incondicional/else do alias",
        )


class CallbackHelperFallbackContract(unittest.TestCase):
    """O helper existente call_wasm_callback já implementa fallback NOT_FOUND-only;
    este teste trava essa regressão para os canais já despachados."""

    def test_ui_host_helper_falls_back_only_on_not_found(self):
        helper = function_body(UI, "static tab5_err_t call_wasm_callback")
        self.assertIn("TAB5_ERR_NOT_FOUND", helper)
        self.assertRegex(helper, r"if\s*\(\s*err\s*==\s*TAB5_ERR_NOT_FOUND\s*\)")
        self.assertNotRegex(
            helper,
            r"if\s*\(\s*err\s*==\s*TAB5_ERR_FAIL\s*\)",
            "fallback de alias só pode ocorrer em NOT_FOUND, nunca em FAIL",
        )

    def test_all_known_dispatches_carry_primary_and_alias(self):
        # Todos os canais WASM continuam despachando com primário + alias.
        for primary, alias in ((KNOWN_PRIMARIES[0], KNOWN_ALIASES[0]),
                               (KNOWN_PRIMARIES[1], KNOWN_ALIASES[1]),
                               (OPEN_FILE_PRIMARY, OPEN_FILE_ALIAS)):
            self.assertIn(primary, UI + LIFE, f"primário {primary} ausente")
            self.assertIn(alias, UI + LIFE, f"alias {alias} ausente")


class NoDirectWasmCallbackPointerContract(unittest.TestCase):
    """Ponteiros de callback registrados (lifecycle.on_*) nunca são invocados
    para contexto WASM: o despacho passa pelo runtime dispatcher."""

    def test_every_lifecycle_pointer_call_is_wasm_guarded(self):
        for cb in LIFECYCLE_CBS:
            hits = list(re.finditer(rf"ctx\s*->\s*lifecycle\.{cb}\s*\(", LIFE))
            self.assertTrue(hits, f"{cb}: nenhuma invocação encontrada para verificar")
            for m in hits:
                window = LIFE[max(0, m.start() - 140) : m.start()]
                self.assertIn(
                    "!ctx->is_wasm", window,
                    f"{cb} invocado sem guarda is_wasm: chamada direta a ponteiro WASM",
                )

    def test_package_manager_never_calls_lifecycle_pointers(self):
        self.assertNotIn(
            "->lifecycle.on_", PKG,
            "o pkg_mgr não pode invocar ponteiros lifecycle; teardown passa "
            "por tab5_wasm_unload / lifecycle_host",
        )

    def test_ui_event_native_pointer_kept_outside_wasm_dispatch_branch(self):
        idx = UI.rindex("app_ctx->lifecycle.on_ui_event(handle, event_type, event_val);")
        window = UI[max(0, idx - 500) : idx]
        self.assertIn("is_wasm", window)
        self.assertIn("wasm_instance", window)
        self.assertIn(
            "} else if (app_ctx->lifecycle.on_ui_event != nullptr) {", window,
            "o ponteiro nativo on_ui_event só pode ser alcançado na ramificação "
            "structuralmente exclusiva de wasm (else do branch is_wasm)",
        )

    def test_open_file_native_callback_is_wasm_guarded(self):
        body = open_file_body()
        self.assertIn("!ctx->is_wasm && ctx->lifecycle.on_open_file", body)


if __name__ == "__main__":
    unittest.main()
