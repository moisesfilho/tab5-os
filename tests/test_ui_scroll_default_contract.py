#!/usr/bin/env python3
"""Contrato estático de rolagem padrão dos componentes genéricos do SDK/ABI.

O LVGL 9 liga ``LV_OBJ_FLAG_SCROLLABLE`` por padrão no criador de
``lv_obj`` — ou seja, salvo intervenção explícita do host, todo objeto novo
nasce rolável. Para o SDK do Tab5 o contrato aprovado é:

1. Componentes genéricos (contêiner, label, botão, switch, slider) NÃO são
   roláveis por padrão. A rolagem só pode ser ligada explicitamente por
   ``tab5_ui_obj_set_scrollable(true)``.
2. Listas e textareas preservam a rolagem padrão (são roláveis por natureza
   e o host não pode desligá-la).
3. ``tab5_ui_host_obj_set_scrollable`` é o único portão que ADICIONA
   ``LV_OBJ_FLAG_SCROLLABLE`` em ``tab5_ui_host.cpp``.
4. ``set_scrollable(true)`` liga a flag + ``LV_SCROLLBAR_MODE_AUTO``;
   ``set_scrollable(false)`` limpa a flag + ``LV_SCROLLBAR_MODE_OFF``.

Fase vermelha (TDD): hoje ``container_create`` (lv_obj_create) e
``label_create`` (lv_label_create) herdam a flag SCROLLABLE do LVGL sem o
factory do host limpá-la — os widgets nascem roláveis por padrão, violando o
item 1. Os testes desse item falham até o firmware implementar a limpeza
explícita nos factories. Os demais itens já valem hoje.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UI = (ROOT / "components/os/runtime/tab5_ui_host.cpp").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    """Extrai o corpo de uma função C++, respeitando chaves aninhadas."""
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


def function_span(source: str, signature: str) -> tuple:
    """Devolve (ini, fim) do corpo de uma função — offsets em ``source``."""
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for pos in range(opening, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return (opening + 1, pos)
    raise AssertionError(f"corpo de função não fechado: {signature}")


DISABLE_SCROLL_CLEAR = (
    r"(?:lv_obj_clear_flag|lv_obj_remove_flag)\s*\([^;()]*,"
    r"\s*LV_OBJ_FLAG_SCROLLABLE\s*\)"
)
DISABLE_SCROLL_SETTER = (
    r"tab5_ui_host_obj_set_scrollable\s*\([^;()]*,\s*false\s*\)"
)
ENABLE_SCROLL = (
    r"lv_obj_add_flag\s*\([^;()]*LV_OBJ_FLAG_SCROLLABLE"
)


def disables_scroll_by_default(body: str) -> bool:
    """True se o corpo desliga a rolagem padrão do LVGL no widget criado."""
    if re.search(DISABLE_SCROLL_CLEAR, body):
        return True
    if re.search(DISABLE_SCROLL_SETTER, body):
        return True
    return False


class UiGenericFactoryScrollDefaultContract(unittest.TestCase):
    """Fábricas genéricas do SDK criam widgets SEM rolagem por padrão."""

    def test_container_factory_disables_scroll_by_default(self):
        body = function_body(UI, "tab5_ui_obj_t tab5_ui_host_container_create")
        self.assertTrue(
            disables_scroll_by_default(body),
            "tab5_ui_host_container_create usa lv_obj_create (que nasce com "
            "LV_OBJ_FLAG_SCROLLABLE) sem limpar a flag; o contêiner precisa "
            "ser criado não-rolável (scroll só via tab5_ui_obj_set_scrollable"
            "(true))",
        )

    def test_label_factory_disables_scroll_by_default(self):
        body = function_body(UI, "tab5_ui_obj_t tab5_ui_host_label_create")
        self.assertTrue(
            disables_scroll_by_default(body),
            "tab5_ui_host_label_create usa lv_label_create (que herda "
            "LV_OBJ_FLAG_SCROLLABLE de lv_obj) sem limpar a flag; o label "
            "precisa ser criado não-rolável por padrão",
        )

    def test_btn_factory_does_not_reenable_scroll(self):
        body = function_body(UI, "tab5_ui_obj_t tab5_ui_host_btn_create")
        self.assertFalse(
            re.search(ENABLE_SCROLL, body),
            "tab5_ui_host_btn_create não pode religar a rolagem no botão; "
            "se a rolagem for necessária, o app chama "
            "tab5_ui_obj_set_scrollable(true)",
        )

    def test_switch_factory_does_not_reenable_scroll(self):
        body = function_body(UI, "tab5_ui_obj_t tab5_ui_host_switch_create")
        self.assertFalse(
            re.search(ENABLE_SCROLL, body),
            "tab5_ui_host_switch_create não pode religar a rolagem no "
            "switch; rolagem é opt-in via tab5_ui_obj_set_scrollable(true)",
        )

    def test_slider_factory_does_not_reenable_scroll(self):
        body = function_body(UI, "tab5_ui_obj_t tab5_ui_host_slider_create")
        self.assertFalse(
            re.search(ENABLE_SCROLL, body),
            "tab5_ui_host_slider_create não pode religar a rolagem no "
            "slider; rolagem é opt-in via tab5_ui_obj_set_scrollable(true)",
        )


class UiScrollReservedWidgetsContract(unittest.TestCase):
    """Listas e textareas preservam a rolagem padrão (intocado)."""

    def test_list_factory_preserves_scroll(self):
        body = function_body(UI, "tab5_ui_obj_t tab5_ui_host_list_create")
        self.assertFalse(
            re.search(DISABLE_SCROLL_CLEAR, body),
            "tab5_ui_host_list_create desligou a rolagem da lista; listas "
            "precisam continuar roláveis por padrão",
        )

    def test_textarea_factory_preserves_scroll(self):
        body = function_body(UI, "tab5_ui_obj_t tab5_ui_host_textarea_create")
        self.assertFalse(
            re.search(DISABLE_SCROLL_CLEAR, body),
            "tab5_ui_host_textarea_create desligou a rolagem do textarea; "
            "textareas precisam continuar roláveis por padrão",
        )

    def test_main_content_textarea_preserves_scroll(self):
        body = function_body(UI, "tab5_err_t tab5_ui_host_create_app_screen")
        self.assertFalse(
            re.search(DISABLE_SCROLL_CLEAR, body),
            "create_app_screen desligou a rolagem do textarea padrão do app; "
            "a área de conteúdo precisa continuar rolável",
        )


class UiScrollableToggleContract(unittest.TestCase):
    """tab5_ui_host_obj_set_scrollable é a única porta de rolagem."""

    def test_only_set_scrollable_adds_scrollable_flag(self):
        lo, hi = function_span(UI, "tab5_err_t tab5_ui_host_obj_set_scrollable")
        for m in re.finditer(ENABLE_SCROLL, UI):
            self.assertTrue(
                lo <= m.start() < hi,
                f"LV_OBJ_FLAG_SCROLLABLE foi adicionado fora de "
                f"tab5_ui_host_obj_set_scrollable (offset {m.start()}); "
                f"a rolagem só pode ser ligada por esse portão",
            )

    def test_enable_sets_flag_and_auto_scrollbar(self):
        body = function_body(UI, "tab5_err_t tab5_ui_host_obj_set_scrollable")
        self.assertTrue("lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLLABLE);" in body)
        self.assertTrue("lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_AUTO);" in body)

    def test_disable_clears_flag_and_off_scrollbar(self):
        body = function_body(UI, "tab5_err_t tab5_ui_host_obj_set_scrollable")
        self.assertTrue("lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);" in body)
        self.assertTrue("lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);" in body)


if __name__ == "__main__":
    unittest.main(verbosity=2)
