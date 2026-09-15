"""Contratos de thread-safety LVGL: locks com timeout finito nas APIs de textarea.

O travamento do Terminal (`help` + Enter) expôs que os mutators de textarea do
host (`tab5_ui_host_textarea_*`) chamam `lv_textarea_*` sem serialização. Como
o despacho WAMR alcança o LVGL a partir do pthread do dispatcher, o acesso
concorrente ao mesmo widget corrompe a lista de objetos LVGL (crash/travamento).

Contratos (level: source — a execução dinâmica real exige o firmware):

1. As quatro APIs de textarea usadas pelo fluxo do Terminal — `set_text`,
   `get_text`, `set_placeholder` e `set_cursor_pos` — devem executar o corpo
   LVGL sob `LV_LOCK()`/`LV_UNLOCK()` no build com LVGL (`HAVE_LVGL`).
2. O lock deve ter **timeout finito** (nunca `portMAX_DELAY`/infinito): em caso
   de timeout o mutator retorna ANTES de tocar em LVGL (`TAB5_ERR_TIMEOUT` para
   APIs com retorno de erro; retorno seguro para `get_text`).
3. Contrato estendido: `get_cursor_pos` e `set_password_mode` também acessam o
   mesmo widget e devem seguir o mesmo padrão (proteção uniforme do textarea).
4. O macro `LV_LOCK` no build LVGL deve adquirir um primitivo real com
   conversão de tempo finita (sem `pdMS_TO_TICKS(0)`).

Antes da correção: `set_text/get_text/set_placeholder/set_cursor_pos` não têm
lock (falha esperada); `set_password_mode` já tem; `get_cursor_pos` não tem.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UI = (ROOT / "components/os/runtime/tab5_ui_host.cpp").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    """Retorna o corpo de uma função C++ respeitando chaves aninhadas."""
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
    raise AssertionError(f"função não fechada: {signature}")


def lock_pairs_in(body: str) -> int:
    return min(body.count("LV_LOCK()"), body.count("LV_UNLOCK();"))


def lvgl_call(body: str) -> bool:
    """True se o corpo toca o widget LVGL fora do lock (ordem: lock -> lv_*)."""
    return bool(re.search(r"(?<![A-Za-z_0-9])lv_[a-z_0-9]+\(", body))


def touches_lvgl(body: str) -> bool:
    return bool(re.search(r"(?<![A-Za-z_0-9])lv_[a-z_0-9]+\(", body))


# As quatro APIs do fluxo do Terminal que precisam de lock (contrato do plano).
# `copy_text` (que substitui `get_text` no ABI) também retorna cedo com
# TAB5_ERR_TIMEOUT e NUL no buffer quando o lock falha.
PRIMARY_TEXTAREA_APIS = (
    ("tab5_err_t tab5_ui_host_textarea_set_text", "err"),
    ("int32_t tab5_ui_host_textarea_copy_text", "copy"),
    ("tab5_err_t tab5_ui_host_textarea_set_placeholder", "err"),
    ("tab5_err_t tab5_ui_host_textarea_set_cursor_pos", "err"),
)

# Contrato estendido (mesmo widget, mesmo acesso concorrente).
EXTENDED_TEXTAREA_APIS = (
    ("int32_t tab5_ui_host_textarea_get_cursor_pos", "int"),
    ("tab5_err_t tab5_ui_host_textarea_set_password_mode", "err"),
)


class LvglTextareaLockPrimitiveContract(unittest.TestCase):
    """O macro LV_LOCK precisa ter timeout finito no build com LVGL."""

    def test_lv_lock_usa_timeout_finito(self):
        # A definição do bloco HAVE_LVGL (entre o #if e o #else).
        if_start = UI.index("#if HAVE_LVGL")
        else_start = UI.index("#else", if_start)
        lvgl_region = UI[if_start:else_start]
        m = re.search(r"#define\s+LV_LOCK\(\)\s*(?P<body>[^\n]*(?:\\\n[^\n]*)*)", lvgl_region)
        self.assertIsNotNone(m, "definição de LV_LOCK ausente no bloco HAVE_LVGL")
        body = m.group("body")
        self.assertFalse(
            re.search(r"portMAX_DELAY|pdMS_TO_TICKS\(\s*0\s*\)|\(\s*void\s*\)\s*0|UINT32_MAX", body),
            f"LV_LOCK não pode ter timeout infinito/imediato/no-op: {body!r}",
        )
        self.assertTrue(
            re.search(r"pdMS_TO_TICKS\(\s*[1-9][0-9]*\s*\)|lv_display_lock|pthread_mutex_timedlock|"
                      r"os_mutex_lock|\.\s*acquire\s*\(|s_lvgl_mutex|bsp_display_lock", body),
            f"LV_LOCK precisa adquirir um primitivo real com timeout finito: {body!r}",
        )

    def test_lv_unlock_libera_o_lock(self):
        if_start = UI.index("#if HAVE_LVGL")
        else_start = UI.index("#else", if_start)
        lvgl_region = UI[if_start:else_start]
        m = re.search(r"#define\s+LV_UNLOCK\(\)\s*(?P<body>[^\n]*(?:\\\n[^\n]*)*)", lvgl_region)
        self.assertIsNotNone(m, "definição de LV_UNLOCK ausente no bloco HAVE_LVGL")
        body = m.group("body")
        self.assertTrue(
            re.search(r"unlock|Unlock|UNLOCK|pthread_mutex|bsp_display", body),
            f"LV_UNLOCK precisa liberar o lock: {body!r}",
        )


class LvglTextareaLockContract(unittest.TestCase):
    """As APIs de textarea executam sob lock e respeitam a falha de lock."""

    def _check_locked(self, signature, kind):
        body = function_body(UI, signature)
        with self.subTest(signature=signature, kind=kind, phase="corpo sob lock"):
            self.assertGreaterEqual(
                lock_pairs_in(body), 1,
                f"{signature} precisa rodar sob LV_LOCK()/LV_UNLOCK()",
            )
        with self.subTest(signature=signature, kind=kind, phase="primeiro acesso lv_* após lock"):
            # A primeira chamada lv_* no corpo precisa vir DEPOIS de um LV_LOCK().
            first_lv = body.find("lv_")
            first_lock = body.find("LV_LOCK()")
            self.assertGreaterEqual(
                first_lv, first_lock,
                f"{signature} toca lv_* antes de adquirir o lock",
            )
        if kind == "err":
            with self.subTest(signature=signature, phase="falha de lock retorna antes do LVGL"):
                self.assertRegex(
                    body,
                    r"if\s*\(\s*!LV_LOCK\(\)\s*\)\s*\{\s*return\s+(TAB5_ERR_TIMEOUT|TAB5_ERR_FAIL)\s*;",
                    f"{signature} precisa retornar TAB5_ERR_TIMEOUT quando o lock falhar",
                )
        elif kind == "copy":
            with self.subTest(signature=signature, phase="falha de lock retorna antes do LVGL"):
                # copy_text retorna int32_t e usa TAB5_ERR_TIMEOUT (negativo);
                # o corpo do path de falha inclui NUL-terminação do buffer antes
                # do return — o regex permite linhas intermediárias.
                self.assertRegex(
                    body,
                    r"if\s*\(\s*!LV_LOCK\(\)\s*\)\s*\{[\s\S]*?return\s+(TAB5_ERR_TIMEOUT|TAB5_ERR_FAIL)\s*;",
                    f"{signature} precisa retornar TAB5_ERR_TIMEOUT quando o lock falhar",
                )
            with self.subTest(signature=signature, phase="buffer NUL-terminado no path de timeout"):
                # No path de timeout, o buffer deve ser NUL-terminado ANTES de
                # retornar (contrato de copy_text: caller nunca recebe lixo).
                self.assertIn(
                    "buffer[0] = '\\0';", body,
                    f"{signature} deve NUL-terminar buffer[0] antes de retornar "
                    "TAB5_ERR_TIMEOUT no path de lock failure",
                )
        elif kind == "ptr":
            with self.subTest(signature=signature, phase="falha de lock retorna antes do LVGL"):
                self.assertRegex(
                    body,
                    r"if\s*\(\s*!LV_LOCK\(\)\s*\)\s*\{\s*return[^;]*;",
                    f"{signature} precisa retornar antecipadamente quando o lock falhar",
                )

    def test_primary_four_textarea_apis_have_locks(self):
        for signature, kind in PRIMARY_TEXTAREA_APIS:
            # As quatro APIs primárias precisam estar presentes E com lock.
            self.assertIn(signature, UI, f"assinatura ausente: {signature}")
            self._check_locked(signature, kind)

    def test_extended_textarea_apis_have_locks(self):
        for signature, kind in EXTENDED_TEXTAREA_APIS:
            self.assertIn(signature, UI, f"assinatura ausente: {signature}")
            self._check_locked(signature, kind)

    def test_apis_aceitam_argumentos_invalidos_antes_do_lock(self):
        # Validação de argumento barata deve acontecer antes do lock (deadlock-free).
        for signature in ("tab5_err_t tab5_ui_host_textarea_set_text",
                          "tab5_err_t tab5_ui_host_textarea_set_placeholder",
                          "tab5_err_t tab5_ui_host_textarea_set_cursor_pos"):
            body = function_body(UI, signature)
            first_lv = body.find("lv_")
            guard = body.find("return TAB5_ERR_INVALID_ARG;")
            self.assertGreater(guard, -1, f"{signature} valida args")
            self.assertLess(guard, first_lv, f"{signature} valida args antes do lv_*")


if __name__ == "__main__":
    unittest.main()
