"""Static contracts (REQ-001..REQ-005) — runtime tab5-os para a correção do
reboot ao alternar "Mostrar ocultos" no app Arquivos.

O plano aprovado atribui ao runtime a causa raiz primária: `nvs_commit`
executado na pthread do call WASM sem stack SRAM interna suficiente.  A
implementação adotada move TODA a persistência NVS para um worker nativo
dedicado (tab5_nvs_worker.cpp): a API host tab5_nvs_get_u8/set_u8 delega ao
worker, e a pthread do call WASM nunca executa nvs_open/nvs_set_u8/nvs_commit.
Este módulo verifica, estaticamente (fonte, sem goldens):

  REQ-002 -> a pthread do call WASM NÃO pode executar persistência NVS
             (nvs_open/nvs_set_u8/nvs_commit) — a operação de flash acontece
             apenas na task nativa "tab5_nvs", criada com xTaskCreateStatic,
             stack estática de 2048 palavras (8 KiB) marcada DRAM_ATTR (SRAM
             interna) e fila estática de 1 slot.  O worker do dispatcher
             mantém a stack explícita (24 KiB) e nome "wasm_disp" (guarda).
  REQ-004 -> todos os mutators de UI usados pelo render_content do Arquivos
             (create/clean/label/list/style/scroll/size/align/pad/gap) tomam
             LV_LOCK/LV_UNLOCK no ui_host — hoje em dia ok (guarda contra
             regressão).
  REQ-005 -> wasm_tab5_nvs_get_u8 NÃO re-traduz out_val com
             wasm_runtime_addr_app_to_native (assinatura WAMR "($$*)" já
             entrega o ponteiro nativo): usa o caminho canônico
             wasm_string/wasm_arg (mesmo helper dos demais imports).
             Guarda: wasm_tab5_nvs_set_u8 permanece wrapper fino que propaga
             o resultado do worker (sem engolir erro de persistência).

A documentação da hipótese de crash foi atualizada: o teste que exigia
nvs_commit DENTRO de tab5_nvs_set_u8 (host) foi substituído pelo contrato de
delegação ao worker, que é mais forte (a persistência nem chega à thread do
chamador).
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "components/os/runtime"

WASM = (RUNTIME / "tab5_wasm_runtime.cpp").read_text(encoding="utf-8")
DISPATCHER = (RUNTIME / "tab5_wasm_dispatcher.cpp").read_text(encoding="utf-8")
HOST_ABI = (RUNTIME / "tab5_host_abi.cpp").read_text(encoding="utf-8")
UI = (RUNTIME / "tab5_ui_host.cpp").read_text(encoding="utf-8")
NVS_WORKER = (RUNTIME / "tab5_nvs_worker.cpp").read_text(encoding="utf-8")

DISPATCHER_WORKER_MIN_STACK = 24 * 1024
NVS_WORKER_MIN_STACK_WORDS = 2048


def function_body(source: str, signature: str) -> str:
    """Corpo da função C/C++, respeitando chaves aninhadas.

    Localiza a DEFINIÇÃO (não a declaração forward): a partir de uma
    ocorrência da assinatura, avança até o primeiro delimitador `{` ou `;`
    (uma declaração forward termina em ';' antes de qualquer '{').  Se for
    `;`, procura a próxima ocorrência; se for `{`, é a abertura do corpo.
    """
    search_from = 0
    while True:
        start = source.find(signature, search_from)
        if start < 0:
            raise AssertionError(f"definição não encontrada: {signature}")
        pos = start + len(signature)
        while pos < len(source) and source[pos] not in "{;":
            pos += 1
        if pos < len(source) and source[pos] == "{":
            opening = pos
            break
        # era declaração (`;`) ou fim de arquivo: tenta a próxima ocorrência
        search_from = start + 1
    depth = 0
    for pos in range(opening, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : pos]
    raise AssertionError(f"bloco sem fechamento: {signature}")


def lock_pairs_in(body: str) -> int:
    return min(body.count("LV_LOCK()"), body.count("LV_UNLOCK();"))


class WasmCallNvsIsolationContract(unittest.TestCase):
    """REQ-002 (atualizado): a pthread do call WASM não executa persistência
    NVS; a operação de flash vive só na task nativa "tab5_nvs" (stack estática
    em SRAM).  O antigo contrato (pthread_attr_setstacksize >= 24 KiB na call)
    deixou de se aplicar porque a causa-raiz foi removida do caminho WASM — o
    requisito original (nenhum nvs_commit em stack de pthread WASM sem SRAM) é
    preservado, de forma mais forte: nem chega à pthread."""

    NVS_FLASH_OPS = ("nvs_open(", "nvs_set_u8(", "nvs_set_i8(", "nvs_commit(", "nvs_get_u8(")

    def test_wasm_call_pthread_worker_has_no_nvs_flash_operations(self):
        body = function_body(WASM, "static void *wasm_call_pthread_worker(")
        for op in self.NVS_FLASH_OPS:
            self.assertNotIn(op, body,
                             f"{op} não pode rodar na pthread do call WASM")

    def test_wasm_call_function_has_no_nvs_flash_operations(self):
        body = function_body(WASM, "tab5_err_t tab5_wasm_call_function(")
        for op in self.NVS_FLASH_OPS:
            self.assertNotIn(op, body,
                             f"{op} não pode rodar na pthread do call WASM")

    def test_wasm_runtime_never_links_persistence_directly(self):
        self.assertNotIn("nvs_commit(", WASM)
        self.assertNotIn("nvs_open(", WASM)
        self.assertNotIn("tab5_nvs_worker", WASM)


class NvsWorkerBacksWasmPersistenceContract(unittest.TestCase):
    """REQ-002 (novo mecanismo): a persistência NVS roda na task nativa
    "tab5_nvs", estática, em SRAM interna, com fila de 1 slot."""

    def test_nvs_worker_task_is_created_static_with_sram_stack(self):
        self.assertIn(
            f"WORKER_STACK_WORDS = {NVS_WORKER_MIN_STACK_WORDS}", NVS_WORKER,
            "o worker precisa declarar 2048 palavras de stack (8 KiB)",
        )
        self.assertIn("DRAM_ATTR", NVS_WORKER,
                      "a stack do worker precisa ser marcada DRAM_ATTR (SRAM "
                      "interna, não heap nem PSRAM)")
        self.assertIn("xTaskCreateStatic(", NVS_WORKER)
        self.assertIn('"tab5_nvs"', NVS_WORKER)
        self.assertIn("xQueueCreateStatic(1,", NVS_WORKER)

    def test_nvs_commit_appears_only_inside_worker_task(self):
        occurrences = re.findall(r"nvs_commit\s*\(", NVS_WORKER)
        self.assertEqual(len(occurrences), 1,
                         "nvs_commit precisa existir uma única vez, dentro da "
                         "task do worker (fila síncrona), nunca na thread "
                         "WASM")


class DispatcherWorkerStackGuard(unittest.TestCase):
    """Guarda REQ-002: o worker do dispatcher não pode regredir de 24 KiB."""

    def test_dispatcher_worker_keeps_explicit_stack(self):
        init = function_body(DISPATCHER,
                             'extern "C" tab5_err_t tab5_wasm_dispatcher_init(void)')
        self.assertIn("pthread_attr_setstacksize(&attr,", init)
        match = re.search(
            r"pthread_attr_setstacksize\(&attr,\s*(\d+)\s*\*\s*(\d+)\)", init)
        self.assertIsNotNone(match, "stack do worker do dispatcher não encontrada")
        value = int(match.group(1)) * int(match.group(2))
        self.assertGreaterEqual(
            value, DISPATCHER_WORKER_MIN_STACK,
            "worker do dispatcher não pode regredir abaixo de 24 KiB",
        )

    def test_dispatcher_worker_has_thread_name(self):
        init = function_body(DISPATCHER,
                             'extern "C" tab5_err_t tab5_wasm_dispatcher_init(void)')
        self.assertIn("pcfg.thread_name", init)


class NvsAbiPointerContract(unittest.TestCase):
    """REQ-005: ponteiros NVS não podem ser re-traduzidos no ABI WASM."""

    def test_nvs_get_u8_wamr_branch_does_not_retranslate_out_val(self):
        body = function_body(HOST_ABI,
                             "static tab5_err_t wasm_tab5_nvs_get_u8")
        # FALHA ESPERADA hoje: o branch HAVE_WAMR_ENV aplica
        # wasm_runtime_addr_app_to_native em out_val que já é nativo
        # (assinatura "($$*)"): double translation -> endereço inválido,
        # files_hidden nunca restaurado (e possível store em memória errada).
        self.assertNotIn(
            "wasm_runtime_addr_app_to_native", body,
            "REQ-005: out_val de tab5_nvs_get_u8 chega já nativo do WAMR "
            "(\"($$*)\"); re-traduzir com addr_app_to_native corrompe o "
            "endereço e quebra a restauração de files_hidden.",
        )
        self.assertNotIn(
            "(uint32_t)(uintptr_t)", body,
            "REQ-005: proibido re-empacotar ponteiro nativo como offset WASM",
        )

    def test_nvs_get_u8_uses_established_wasm_arg_helper(self):
        body = function_body(HOST_ABI,
                             "static tab5_err_t wasm_tab5_nvs_get_u8")
        # O restante da ABI usa wasm_arg(exec_env, ptr) (test_wasm_abi_pointer_
        # contract.py): o wrapper NVS deve seguir o mesmo padrão.
        self.assertIn(
            "wasm_arg(exec_env, out_val)", body,
            "REQ-005: wasm_tab5_nvs_get_u8 deve desreferenciar out_val com o "
            "helper wasm_arg (sem re-tradução), como os demais wrappers.",
        )

    def test_nvs_get_u8_out_val_still_fed_to_host_api(self):
        body = function_body(HOST_ABI,
                             "static tab5_err_t wasm_tab5_nvs_get_u8")
        self.assertIn(
            "tab5_nvs_get_u8(wasm_string(exec_env, ns), "
            "wasm_string(exec_env, key), wasm_arg(exec_env, out_val))",
            body,
            "REQ-005: o valor lido precisa chegar à API host tab5_nvs_get_u8 "
            "pelo caminho canônico wasm_string/wasm_arg (sem re-tradução de "
            "out_val).",
        )

    def test_nvs_set_u8_returns_commit_result_without_swallowing(self):
        body = function_body(HOST_ABI,
                             "static tab5_err_t wasm_tab5_nvs_set_u8")
        self.assertIn("return tab5_nvs_set_u8(ns, key, val);", body,
                      "REQ-005: o wrapper set_u8 deve propagar o resultado "
                      "do commit (nunca engolir falha de persistência).")


class FilesRenderMutatorsLockedContract(unittest.TestCase):
    """REQ-004: mutators usados pelo render_content do Arquivos sob LV_LOCK."""

    RENDER_MUTATORS = (
        "tab5_err_t tab5_ui_host_obj_clean_deferred",
        "tab5_err_t tab5_ui_host_obj_scroll_to_top",
        "tab5_err_t tab5_ui_host_obj_set_flex_flow",
        "tab5_err_t tab5_ui_host_obj_set_scrollable",
        "tab5_err_t tab5_ui_host_obj_set_pad",
        "tab5_err_t tab5_ui_host_obj_set_gap",
        "tab5_err_t tab5_ui_host_obj_set_size",
        "tab5_ui_obj_t tab5_ui_host_container_create",
        "tab5_ui_obj_t tab5_ui_host_label_create",
        "tab5_err_t tab5_ui_host_obj_set_align",
        "tab5_err_t tab5_ui_host_obj_set_style_bg",
        "tab5_err_t tab5_ui_host_obj_set_style_border",
        "tab5_err_t tab5_ui_host_obj_set_style_radius",
        "tab5_err_t tab5_ui_host_obj_set_style_text_color",
        "tab5_err_t tab5_ui_host_obj_set_style_text_size",
        "tab5_err_t tab5_ui_host_obj_set_clickable",
        "tab5_ui_obj_t tab5_ui_host_list_create",
        "tab5_ui_obj_t tab5_ui_host_list_add_btn",
        "tab5_err_t tab5_ui_host_label_set_text",
        "tab5_err_t tab5_ui_host_obj_clean",
    )

    def test_all_render_mutators_are_display_locked(self):
        unlocked = []
        for sig in self.RENDER_MUTATORS:
            body = function_body(UI, sig)
            if lock_pairs_in(body) < 1:
                unlocked.append(sig)
        self.assertEqual(
            unlocked, [],
            "REQ-004: os mutators abaixo precisam rodar sob "
            "LV_LOCK()/LV_UNLOCK() para que o re-render do Arquivos (executado "
            "pelo worker do dispatcher) nunca toque LVGL fora da thread "
            "principal.",
        )

    def test_theme_getter_is_pure(self):
        # theme_get_color é getter puro (sem mutação LVGL): pode seguir sem
        # lock, mas nunca pode chamar lv_* que escreve.
        body = function_body(UI, "uint32_t tab5_ui_host_theme_get_color(")
        self.assertNotIn("lv_obj_set_", body)
        self.assertNotIn("lv_label_set_", body)


class NvsCommitMovedToDedicatedWorkerContract(unittest.TestCase):
    """A hipótese de crash do plano (REQ-002) foi resolvida movendo o
    nvs_commit para a task nativa "tab5_nvs".  Os contratos abaixo substituem
    o antigo "tab5_nvs_set_u8 faz nvs_commit no corpo" (que não é mais
    aplicável) por uma guarda MAIS FORTE: a ABI host delega ao worker e nunca
    toca no NVS diretamente — a persistência não chega à thread do chamador."""

    def test_host_abi_set_delegates_to_worker_without_commit(self):
        body = function_body(HOST_ABI, "tab5_err_t tab5_nvs_set_u8(")
        self.assertIn("tab5_nvs_worker_set_u8(ns, key, val)", body,
                      "a ABI set precisa delegar ao worker")
        self.assertNotIn("nvs_commit(", body)
        self.assertNotIn("nvs_open(", body)
        self.assertNotIn("nvs_set_u8(", body)

    def test_host_abi_get_delegates_to_worker_read(self):
        body = function_body(HOST_ABI, "tab5_err_t tab5_nvs_get_u8(")
        self.assertIn("tab5_nvs_worker_get_u8(ns, key, out_val)", body,
                      "a ABI get precisa delegar ao worker")
        self.assertNotIn("nvs_open(", body)
        self.assertNotIn("nvs_get_u8(", body)


if __name__ == "__main__":
    unittest.main(verbosity=2)
