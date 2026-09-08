"""Static acceptance contracts for unload-during-WAMR-callback (sem goldens).

These tests intentionally do not use screenshots or golden files.  They check
the small, safety-critical control-flow obligations of the approved fix for
"unload during WAMR callback" that can be verified from the source before the
runtime implementation is changed:

1. call_depth/inflight por instância  — the instance struct carries its own
   active-call counter and a pending-close flag (never global/static).
2. unload/close pendente  — calling unload while a call is active defers the
   teardown instead of freeing the module/exec_env in use.
3. processamento após join  — the deferred teardown runs after the WAMR call
   thread has been joined (i.e. the callback returned).
4. Files->Music sem destruir Files antes do retorno  — the package manager
   routes all previous-app teardown through the deferred-capable
   tab5_wasm_unload and never tears the runtime down inline.
5. chamadas aninhadas  — depth counter is symmetric around the WAMR dispatch,
   so nested calls defer until the whole stack unwinds (depth == 0).
6. falha pthread sem pendência  — pthread_create failure leaves no pending
   flag: the caller keeps ownership and unloads exactly once.

Dynamic WAMR execution is not available in the host build (HAVE_WAMR=0), so
the deferral is additionally exercised at struct level by the C++ host tests
(test_wasm_unload_contract.cpp); only static verification is possible here.
"""

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
HDR = (ROOT / "components/os/runtime/tab5_wasm_runtime.h").read_text(encoding="utf-8")
PKG = (ROOT / "components/os/runtime/tab5_package_mgr.cpp").read_text(encoding="utf-8")
WASM = (ROOT / "components/os/runtime/tab5_wasm_runtime.cpp").read_text(encoding="utf-8")
ABI = (ROOT / "components/os/runtime/tab5_host_abi.cpp").read_text(encoding="utf-8")

COUNTER_NAMES = ("call_depth", "inflight_calls", "active_calls")
PENDING_NAMES = ("unload_pending", "close_pending")


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


def struct_field(candidates) -> str | None:
    """Return the first candidate name present in the instance struct."""
    struct = function_body(HDR, "typedef struct {")
    for name in candidates:
        if re.search(rf"\b{re.escape(name)}\b", struct):
            return name
    return None


def find_wasm_dispatch_body():
    """Body of the function that actually executes wasm_runtime_call_wasm."""
    for signature in ("static tab5_err_t tab5_wasm_call_function_direct",
                      "static void *wasm_call_pthread_worker",
                      "tab5_err_t tab5_wasm_call_function("):
        try:
            body = function_body(WASM, signature)
        except ValueError:
            continue
        if "wasm_runtime_call_wasm(" in body:
            return body
    raise AssertionError("no function containing wasm_runtime_call_wasm found")


class PerInstanceDepthContract(unittest.TestCase):
    """O contador de chamadas e a pendência vivem na instância, não no runtime."""

    def test_runtime_struct_has_per_instance_call_depth_field(self):
        self.assertIsNotNone(
            struct_field(COUNTER_NAMES),
            "tab5_wasm_app_instance_t precisa de um contador de chamadas ativas por instância",
        )

    def test_runtime_struct_has_pending_unload_flag(self):
        self.assertIsNotNone(
            struct_field(PENDING_NAMES),
            "tab5_wasm_app_instance_t precisa de uma flag de unload/close pendente",
        )

    def test_no_global_or_static_counter_in_runtime_implementation(self):
        # O contador tem que ser POR INSTÂNCIA; um contador global/static no
        # .cpp violaria o isolamento entre apps (Files->Music).
        self.assertNotRegex(
            WASM,
            r"static\s+(?:uint32_t|int|unsigned)\s+(?:call_depth|inflight_calls|active_calls)\b",
            "contador de chamadas não pode ser global/static no runtime",
        )


class DeferredUnloadContract(unittest.TestCase):
    """Unload/close durante callback não pode liberar recursos WAMR em uso."""

    def test_call_depth_wraps_the_wasm_dispatch_symmetrically(self):
        counter = struct_field(COUNTER_NAMES)
        self.assertIsNotNone(counter, "sem campo de profundidade por instância não há deferimento")
        dispatch = find_wasm_dispatch_body()
        call_pos = dispatch.index("wasm_runtime_call_wasm(")
        inc = re.search(rf"(?:\+\+\s*{counter}|{counter}\s*\+\+)", dispatch)
        dec = re.search(rf"(?:\-\-\s*{counter}|{counter}\s*\-\-)", dispatch)
        self.assertIsNotNone(inc, "profundidade precisa ser incrementada antes do dispatch")
        self.assertIsNotNone(dec, "profundidade precisa ser decrementada após o dispatch")
        self.assertLess(inc.start(), call_pos, "incremento deve preceder wasm_runtime_call_wasm")
        self.assertGreater(dec.start(), call_pos, "decremento deve suceder wasm_runtime_call_wasm")

    def test_unload_defers_teardown_while_call_active(self):
        counter = struct_field(COUNTER_NAMES)
        pending = struct_field(PENDING_NAMES)
        self.assertIsNotNone(counter)
        self.assertIsNotNone(pending)
        unload = function_body(WASM, "tab5_err_t tab5_wasm_unload")
        guard = re.search(rf"if\s*\(\s*{counter}\s*>\s*0\s*\)", unload)
        self.assertIsNotNone(guard, "unload deve checar a profundidade ativa antes de teardown")
        self.assertRegex(unload, rf"{pending}\s*=\s*true", "unload ativo deve marcar close pendente")
        first_teardown = min(
            pos
            for pos in (
                unload.find("wasm_runtime_destroy_exec_env("),
                unload.find("wasm_runtime_deinstantiate("),
                unload.find("wasm_runtime_unload("),
                unload.find("free(inst->wasm_buf)"),
            )
            if pos != -1
        )
        self.assertLess(guard.start(), first_teardown,
                        "o guard de profundidade precisa preceder toda liberação de recursos")

    def test_unload_guard_is_platform_independent(self):
        # O guard precisa estar fora do #if HAVE_WAMR para valer tanto no
        # dispositivo quanto no build de testes host (que executa a semântica
        # de deferimento via test_wasm_unload_contract.cpp).
        counter = struct_field(COUNTER_NAMES)
        self.assertIsNotNone(counter)
        unload = function_body(WASM, "tab5_err_t tab5_wasm_unload")
        guard = re.search(rf"if\s*\(\s*{counter}\s*>\s*0\s*\)", unload)
        self.assertIsNotNone(guard)
        ifdef_pos = unload.find("#if HAVE_WAMR")
        self.assertGreater(ifdef_pos, 0, "unload precisa conter o bloco #if HAVE_WAMR")
        self.assertLess(guard.start(), ifdef_pos,
                        "o guard precisa ser comum às duas plataformas (antes do #if HAVE_WAMR)")

    def test_pending_unload_is_processed_after_join(self):
        pending = struct_field(PENDING_NAMES)
        self.assertIsNotNone(pending)
        call = function_body(WASM, "tab5_err_t tab5_wasm_call_function(")
        join_pos = call.index("pthread_join(thread, nullptr);")
        proc = re.search(rf"if\s*\([^)]*\b{pending}\b[^)]*\)", call[join_pos:])
        self.assertIsNotNone(proc, "após o join o runtime precisa processar o unload pendente")
        tail = call[join_pos:]
        self.assertTrue(
            "tab5_wasm_unload(" in tail or "wasm_runtime_deinstantiate(" in tail,
            "o processamento pós-join precisa efetivamente descarregar a instância",
        )

    def test_deferred_processing_requires_empty_call_stack(self):
        counter = struct_field(COUNTER_NAMES)
        pending = struct_field(PENDING_NAMES)
        self.assertIsNotNone(counter)
        self.assertIsNotNone(pending)
        call = function_body(WASM, "tab5_err_t tab5_wasm_call_function(")
        join_pos = call.index("pthread_join(thread, nullptr);")
        cond = re.search(rf"if\s*\([^)]*\)", call[join_pos:])
        self.assertIsNotNone(cond)
        window = call[join_pos + cond.start() : join_pos + cond.end()]
        self.assertIn(pending, window)
        self.assertRegex(window, rf"\b{re.escape(counter)}\b\s*==\s*0",
                         "chamadas aninhadas devem manter o unload pendente até a profundidade zerar")

    def test_pthread_failure_leaves_no_pending(self):
        call = function_body(WASM, "tab5_err_t tab5_wasm_call_function(")
        for block in re.finditer(r"if \(rc != 0\) \{(?P<body>.*?)\n    \}", call, re.DOTALL):
            self.assertNotIn("tab5_wasm_unload", block.group("body"))
            self.assertNotIn("free(", block.group("body"))
            for name in PENDING_NAMES:
                self.assertNotIn(name, block.group("body"),
                                 "pthread_create falho não pode deixar estado pendente")


class LaunchCloseOrderingContract(unittest.TestCase):
    """Files->Music: a app anterior (Files) não é destruída antes do retorno."""

    def test_close_active_routes_teardown_through_runtime_unload_only(self):
        close = function_body(PKG, "tab5_err_t tab5_package_mgr_close_active")
        self.assertIn("tab5_wasm_unload(&entry->wasm_inst);", close)
        self.assertNotIn("wasm_runtime_destroy_exec_env", close)
        self.assertNotIn("wasm_runtime_deinstantiate", close)
        self.assertNotIn("free(", close)

    def test_launch_closes_previous_app_only_after_candidate_resumed(self):
        # A troca Files->Music só fecha Files depois do candidato validado; a
        # destruição real fica sob responsabilidade do unload deferido.
        launch = function_body(PKG, "tab5_err_t tab5_package_mgr_launch")
        self.assertIn("tab5_lifecycle_host_resume_app(&entry->host_ctx);", launch)
        self.assertLess(
            launch.index("tab5_lifecycle_host_resume_app(&entry->host_ctx);"),
            launch.index("tab5_package_mgr_close_active();"),
            "o close do app anterior precisa ocorrer somente após o candidato ativo",
        )

    def test_package_manager_never_tears_down_runtime_directly(self):
        # Se o pkg_mgr nunca acessa wasm_runtime_*, a teardown da app anterior
        # passa obrigatoriamente por tab5_wasm_unload (que defere quando ativo).
        self.assertNotIn("wasm_runtime_", PKG)

    def test_file_assoc_navigation_chain_is_registered(self):
        # Cadeia Files->Music: import WASM -> tab5_file_assoc_open -> handler.
        self.assertIn("wasm_tab5_file_assoc_open(", ABI)
        self.assertIn('"tab5_file_assoc_open", (void *)wasm_tab5_file_assoc_open', ABI)


if __name__ == "__main__":
    unittest.main()
