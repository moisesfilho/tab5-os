"""Static acceptance contracts for LVGL serialization pthread→UI (sem goldens).

These tests intentionally do not use screenshots or golden files.  They check
the small, safety-critical obligations of the approved plan for serializing
LVGL access on operations that arrive from pthread (WAMR dispatch) and for
running destroy/switch in the LVGL context:

1. LV_LOCK/LV_UNLOCK são primitivas reais  — no build com LVGL, o par precisa
   adquirir/liberar um primitivo de lock (bsp_display_lock / lvgl_port_lock /
   pthread_mutex/lv_display_lock), e a forma (void)0 atual não é aceitável.
2. Criação/destruição/abort de tela pegam o lock  — create_app_screen,
   destroy_app_screen e abort_app_screen (os três caminhos de switch de app)
   executam corpo todo sob LV_LOCK()/LV_UNLOCK().
3. O dispatcher pthread não toca LVGL diretamente  — tab5_wasm_call_function
   e o processamento pós-join (process_pending_close) só alcançam LVGL via
   funções registradas do ui_host que tomam o lock; nunca lv_* inline.
4. Pending-close herda a serialização  — o teardown de app anterior
   (Files->Music) roda sob o lock de destroy_app_screen/abort (chamado pelo
   lifecycle), mantendo o contexto LVGL serializado mesmo vindo de pthread.

Nenhuma verificação golden é usada; a execução dinâmica é coberta pelos tests
host em C++ (test_open_file_wasm_contract.cpp).
"""

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
UI = (ROOT / "components/os/runtime/tab5_ui_host.cpp").read_text(encoding="utf-8")
WASM = (ROOT / "components/os/runtime/tab5_wasm_runtime.cpp").read_text(encoding="utf-8")
PKG = (ROOT / "components/os/runtime/tab5_package_mgr.cpp").read_text(encoding="utf-8")

LOCK_PRIMITIVES = (
    "bsp_display_lock",
    "lvgl_port_lock",
    "lv_display_lock",
    "pthread_mutex_lock",
    "lv_lock",
    "s_lvgl_mutex",
)


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


def lock_pairs_in(body: str) -> int:
    """Menor contagem de pares LV_LOCK/LV_UNLOCK presentes no corpo."""
    return min(body.count("LV_LOCK()"), body.count("LV_UNLOCK();"))


def lvgl_call(body: str) -> bool:
    """True se o corpo faz qualquer chamada lv_* (indício de acesso direto a LVGL)."""
    return bool(re.search(r"(?<![A-Za-z_0-9])lv_[a-z_0-9]+\(", body))


class LvglLockPrimitiveContract(unittest.TestCase):
    """O par de lock não pode continuar sendo no-op no build com LVGL."""

    def test_lv_lock_acquires_a_real_primitive(self):
        found_real = False
        for m in re.finditer(r"#define\s+LV_LOCK\(\)\s*(?P<body>[^\n]*(?:\\\n[^\n]*)*)", UI):
            if any(p in m.group("body") for p in LOCK_PRIMITIVES):
                found_real = True
                break
        self.assertTrue(
            found_real,
            "nenhuma definição de LV_LOCK adquire um primitivo de lock "
            f"({', '.join(LOCK_PRIMITIVES)}); no build LVGL a serialização "
            "é obrigatória",
        )

    def test_lv_unlock_releases_after_lock(self):
        found_release = False
        for m in re.finditer(r"#define\s+LV_UNLOCK\(\)\s*(?P<body>[^\n]*(?:\\\n[^\n]*)*)", UI):
            if any(p in m.group("body") for p in ("bsp_display_unlock", "lvgl_port_unlock",
                                                  "lv_display_unlock", "pthread_mutex_unlock",
                                                  "lv_unlock", "s_lvgl_mutex")):
                found_release = True
                break
        self.assertTrue(
            found_release,
            "nenhuma definição de LV_UNLOCK libera o lock correspondente",
        )

    def test_noop_fallback_is_conditional(self):
        # O fallback sem LVGL continua no-op, mas nunca pode ser a definição
        # usada pelo build que inclui LVGL.
        self.assertIn("#if HAVE_LVGL\n#define LV_LOCK() bsp_display_lock", UI)
        self.assertIn("#else\n#define LV_LOCK() (void)0", UI)


class LvglScreenSwitchLockContract(unittest.TestCase):
    """Criação/destruição/abort de tela executam sob o lock."""

    def test_create_app_screen_pairs_lock(self):
        body = function_body(UI, "tab5_err_t tab5_ui_host_create_app_screen")
        self.assertGreaterEqual(
            lock_pairs_in(body), 1,
            "create_app_screen precisa rodar sob LV_LOCK()/LV_UNLOCK()",
        )

    def test_destroy_app_screen_pairs_lock(self):
        body = function_body(UI, "tab5_err_t tab5_ui_host_destroy_app_screen")
        self.assertGreaterEqual(
            lock_pairs_in(body), 1,
            "destroy_app_screen precisa rodar sob LV_LOCK()/LV_UNLOCK()",
        )

    def test_abort_app_screen_pairs_lock(self):
        body = function_body(UI, "tab5_err_t tab5_ui_host_abort_app_screen")
        self.assertGreaterEqual(
            lock_pairs_in(body), 1,
            "abort_app_screen precisa rodar sob LV_LOCK()/LV_UNLOCK()",
        )

    def test_direct_layout_entrypoints_are_serialized(self):
        for signature in (
            "void tab5_ui_host_apply_layout(void)",
            "void tab5_ui_host_resume_app(tab5_app_context_t *ctx)",
            "void tab5_ui_host_open_file(tab5_app_context_t *ctx, const char *path)",
        ):
            body = function_body(UI, signature)
            self.assertGreaterEqual(lock_pairs_in(body), 1, signature)

    def test_screen_paths_return_before_lvgl_on_lock_failure(self):
        for signature in (
            "tab5_err_t tab5_ui_host_create_app_screen",
            "tab5_err_t tab5_ui_host_destroy_app_screen",
            "tab5_err_t tab5_ui_host_abort_app_screen",
        ):
            body = function_body(UI, signature)
            self.assertRegex(body, r"if \(!LV_LOCK\(\)\) \{\s*return")

    def test_wasm_callbacks_are_not_nested_under_host_lock(self):
        for signature in (
            "void tab5_ui_host_resume_app(tab5_app_context_t *ctx)",
            "void tab5_ui_host_open_file(tab5_app_context_t *ctx, const char *path)",
        ):
            body = function_body(UI, signature)
            self.assertNotIn("call_wasm_callback", body)


class LvglThreadSerializationContract(unittest.TestCase):
    """O caminho pthread (WAMR dispatch) nunca toca LVGL direto."""

    def test_wasm_call_function_does_not_touch_lvgl_directly(self):
        call = function_body(WASM, "tab5_err_t tab5_wasm_call_function(")
        self.assertFalse(
            lvgl_call(call),
            "o dispatcher pthread não pode chamar lv_* diretamente; acessos "
            "LVGL da app passam pelos mutators host (que tomam o lock)",
        )
        self.assertIn("tab5_package_mgr_process_pending_close();", call,
                      "o processamento pós-join precisa acontecer no chamador "
                      "(contexto LVGL), seriado pelos locks dos mutators")

    def test_pending_close_does_not_touch_lvgl_directly(self):
        close = function_body(PKG, 'extern "C" void tab5_package_mgr_process_pending_close')
        self.assertFalse(
            lvgl_call(close),
            "o pós-join não pode tocar LVGL inline; o teardown da app anterior "
            "passa por destroy/abort do ui_host, que tomam o lock",
        )
        self.assertIn("tab5_lifecycle_host_destroy_app(&entry->host_ctx);", close)

    def test_pthread_worker_is_lvgl_free(self):
        # O worker roda fora do contexto LVGL: qualquer lv_* ali estaria
        # descoberto do lock (violação de serialização).
        worker = function_body(WASM, "static void *wasm_call_pthread_worker")
        self.assertFalse(
            lvgl_call(worker),
            "o worker pthread não pode acessar LVGL: seria corrida com o "
            "contexto LVGL principal",
        )


if __name__ == "__main__":
    unittest.main()
