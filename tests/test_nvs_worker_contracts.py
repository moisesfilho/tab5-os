"""Static contracts for the NVS worker (components/os/runtime/tab5_nvs_worker.cpp/h).

Verifica, direto no fonte de producao (sem goldens):

  C1 - a task do worker e ESTATICA em SRAM interna: StackType_t + DRAM_ATTR,
       StaticTask_t/StaticQueue_t/StaticSemaphore_t e apenas funcoes
       x*Create*Static (sem malloc/heap) no TU.
  C2 - a fila e sincrona e de 1 slot (1 byte); o submit espera o s_done da
       task antes de devolver o resultado.
  C3 - string bounds: ns/key de ate 15 chars sao copiados com strncpy para
       buffers de 16; >= 16 chars -> TAB5_ERR_INVALID_ARG antes da copia.
  C4 - nvs_commit existe UMA unica vez no TU, dentro da task do worker
       (nunca na thread do chamador / WASM).
  C5 - a ABI host (tab5_host_abi.cpp) delega get/set ao worker e nao toca
       nvs_open/nvs_set_u8/nvs_commit diretamente.
  C6 - o build ESP-IDF realmente compila o worker (components/os/CMakeLists.txt).

Complementado pelos testes comportamentais tests/host/src/test_nvs_worker.cpp,
que rodam o codigo real compilado com -DESP_PLATFORM contra um kernel FreeRTOS
mock (fila/semaforos/task reais em std::thread).
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORKER = ROOT / "components/os/runtime/tab5_nvs_worker.cpp"
WORKER_H = ROOT / "components/os/runtime/tab5_nvs_worker.h"
HOST_ABI = ROOT / "components/os/runtime/tab5_host_abi.cpp"
SDK_H = ROOT / "components/os/runtime/include/tab5_sdk.h"
OS_CMAKE = ROOT / "components/os/CMakeLists.txt"

WORKER_SRC = WORKER.read_text(encoding="utf-8")
WORKER_HEADER = WORKER_H.read_text(encoding="utf-8")
HOST_ABI_SRC = HOST_ABI.read_text(encoding="utf-8")
SDK_SRC = SDK_H.read_text(encoding="utf-8")
OS_CMAKE_SRC = OS_CMAKE.read_text(encoding="utf-8")

NVS_NAME_SIZE = 16        # 15 chars + NUL (limite do NVS)
WORKER_STACK_WORDS = 2048 # 8 KiB em SRAM interna
REQUEST_TIMEOUT_MS = 2000


def function_body(source: str, signature: str) -> str:
    """Corpo da funcao C/C++ respeitando chaves aninhadas (mesma logica dos
    demais contratos estaticos do repositorio)."""
    search_from = 0
    while True:
        start = source.find(signature, search_from)
        if start < 0:
            raise AssertionError(f"definicao nao encontrada: {signature}")
        pos = start + len(signature)
        while pos < len(source) and source[pos] not in "{;":
            pos += 1
        if pos < len(source) and source[pos] == "{":
            opening = pos
            break
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


class WorkerStaticStorageContract(unittest.TestCase):
    """C1: task estatica em SRAM interna, sem heap."""

    def test_task_stack_is_static_array_in_internal_sram(self):
        self.assertIn(f"WORKER_STACK_WORDS = {WORKER_STACK_WORDS}", WORKER_SRC)
        self.assertRegex(
            WORKER_SRC,
            rf"StackType_t\s+s_task_stack\[WORKER_STACK_WORDS\]\s+DRAM_ATTR;",
            "a stack da task precisa ser um array estatico marcado DRAM_ATTR "
            "(SRAM interna, nao heap, nao PSRAM)",
        )
        self.assertIn("StaticTask_t s_task_storage;", WORKER_SRC)

    def test_queue_and_semaphores_are_static_objects(self):
        self.assertIn("StaticQueue_t s_queue_storage;", WORKER_SRC)
        self.assertIn("StaticSemaphore_t s_available_storage;", WORKER_SRC)
        self.assertIn("StaticSemaphore_t s_done_storage;", WORKER_SRC)
        self.assertIn("StaticSemaphore_t s_state_mutex_storage;", WORKER_SRC)

    def test_only_static_creation_apis_are_used(self):
        self.assertIn("xTaskCreateStatic(", WORKER_SRC)
        self.assertIn("xQueueCreateStatic(", WORKER_SRC)
        self.assertIn("xSemaphoreCreateBinaryStatic(", WORKER_SRC)
        self.assertIn("xSemaphoreCreateMutexStatic(", WORKER_SRC)

    def test_no_heap_allocation_in_worker_source(self):
        for forbidden in (
            "malloc(", "calloc(", "realloc(", "free(",
            "pvPortMalloc", "heap_caps_malloc", "heap_caps_calloc", " new ",
        ):
            self.assertNotIn(forbidden, WORKER_SRC,
                             f"o worker nao pode alocar no heap ({forbidden})")

    def test_worker_registered_in_os_component_build(self):
        self.assertIn('"runtime/tab5_nvs_worker.cpp"', OS_CMAKE_SRC)


class WorkerSynchronousQueueContract(unittest.TestCase):
    """C2: fila sincrona de 1 slot."""

    def test_queue_is_single_slot_byte(self):
        self.assertRegex(
            WORKER_SRC,
            r"xQueueCreateStatic\(1,\s*sizeof\(uint8_t\),\s*s_queue_items,\s*&s_queue_storage\)",
            "a fila precisa ter exatamente 1 slot de 1 byte (uma requisicao "
            "por vez)",
        )
        self.assertIn("uint8_t s_queue_items[1];", WORKER_SRC)

    def test_request_is_copied_and_waited_synchronously(self):
        submit = function_body(WORKER_SRC, "tab5_err_t submit(")
        self.assertIn("xQueueSend(s_queue, &slot, timeout)", submit)
        self.assertIn("xSemaphoreTake(s_done, timeout)", submit)
        self.assertIn("strncpy(s_request.ns, ns, sizeof(s_request.ns));", submit)
        self.assertIn("strncpy(s_request.key, key, sizeof(s_request.key));", submit)

    def test_worker_signals_completion_after_nvs_operation(self):
        task = function_body(WORKER_SRC, "void nvs_worker_task(")
        self.assertIn("xSemaphoreGive(s_done);", task)


class WorkerStringBoundsContract(unittest.TestCase):
    """C3: copia de strings com limites de 15 chars."""

    def test_lengths_are_validated_before_copy(self):
        submit = function_body(WORKER_SRC, "tab5_err_t submit(")
        self.assertIn(f"NVS_NAME_SIZE = {NVS_NAME_SIZE}", WORKER_SRC)
        self.assertRegex(
            submit,
            r"strnlen\(ns,\s*NVS_NAME_SIZE\)\s*>=\s*NVS_NAME_SIZE",
            "ns >= 16 chars deve ser rejeitado",
        )
        self.assertRegex(
            submit,
            r"strnlen\(key,\s*NVS_NAME_SIZE\)\s*>=\s*NVS_NAME_SIZE",
            "key >= 16 chars deve ser rejeitado",
        )
        self.assertIn("return TAB5_ERR_INVALID_ARG;", submit)

    def test_null_arguments_rejected(self):
        submit = function_body(WORKER_SRC, "tab5_err_t submit(")
        self.assertIn("ns == nullptr || key == nullptr", submit)
        self.assertIn("(!is_set && out_value == nullptr)", submit)

    def test_get_and_set_forward_to_submit(self):
        body = function_body(WORKER_SRC, "tab5_err_t tab5_nvs_worker_get_u8(")
        self.assertIn("submit(false, ns, key, 0, out_val)", body)
        body = function_body(WORKER_SRC, "tab5_err_t tab5_nvs_worker_set_u8(")
        self.assertIn("submit(true, ns, key, val, nullptr)", body)


class NvsCommitOnlyOnWorkerContract(unittest.TestCase):
    """C4: nvs_commit ocorre uma unica vez, dentro da task do worker."""

    def test_nvs_commit_appears_exactly_once(self):
        occurrences = [m.start() for m in re.finditer(r"nvs_commit\s*\(", WORKER_SRC)]
        self.assertEqual(
            len(occurrences), 1,
            "nvs_commit precisa existir uma unica vez no TU do worker (dentro "
            "da sua task); o chamador/WASM nunca persiste direto",
        )

    def test_nvs_commit_is_inside_worker_task_and_set_path(self):
        task = function_body(WORKER_SRC, "void nvs_worker_task(")
        self.assertIn("nvs_commit(handle)", task)
        self.assertIn("nvs_open(s_request.ns, NVS_READWRITE, &handle)", task)
        self.assertIn("nvs_set_u8(handle, s_request.key, s_request.value)", task)
        self.assertIn("nvs_close(handle)", task)

    def test_read_path_never_commits(self):
        task = function_body(WORKER_SRC, "void nvs_worker_task(")
        self.assertIn("nvs_open(s_request.ns, NVS_READONLY, &handle)", task)
        self.assertIn("nvs_get_u8(handle, s_request.key, &result_value)", task)


class WorkerAbandonAndTimeoutContract(unittest.TestCase):
    """Estado ABANDONED e timeout do contrato de fila."""

    def test_request_state_machine_has_abandoned(self):
        self.assertIn("FREE, IN_FLIGHT, COMPLETED, ABANDONED", WORKER_SRC)

    def test_timeout_constant_matches_contract(self):
        self.assertIn(f"REQUEST_TIMEOUT_MS = {REQUEST_TIMEOUT_MS}", WORKER_SRC)
        self.assertIn("TAB5_ERR_TIMEOUT", WORKER_SRC)

    def test_submit_marks_abandoned_when_worker_unresponsive(self):
        submit = function_body(WORKER_SRC, "tab5_err_t submit(")
        self.assertIn("s_request.state = Request::ABANDONED;", submit)
        task = function_body(WORKER_SRC, "void nvs_worker_task(")
        self.assertIn("if (s_request.state == Request::ABANDONED)", task)
        self.assertIn("release_request_locked();", task)


class WorkerHostAbiDelegationContract(unittest.TestCase):
    """C5: a ABI host delega ao worker; nunca toca no NVS diretamente."""

    def test_host_abi_get_delegates_to_worker(self):
        body = function_body(HOST_ABI_SRC, "tab5_err_t tab5_nvs_get_u8(")
        self.assertIn("tab5_nvs_worker_get_u8(ns, key, out_val)", body)

    def test_host_abi_set_delegates_to_worker(self):
        body = function_body(HOST_ABI_SRC, "tab5_err_t tab5_nvs_set_u8(")
        self.assertIn("tab5_nvs_worker_set_u8(ns, key, val)", body)

    def test_host_abi_functions_do_not_touch_nvs_directly(self):
        for signature in ("tab5_err_t tab5_nvs_get_u8(", "tab5_err_t tab5_nvs_set_u8("):
            body = function_body(HOST_ABI_SRC, signature)
            self.assertNotIn("nvs_open(", body)
            self.assertNotIn("nvs_set_u8(", body)
            self.assertNotIn("nvs_commit(", body)

    def test_public_api_declared_in_header(self):
        self.assertIn(
            "tab5_err_t tab5_nvs_worker_get_u8(const char *ns, const char *key, uint8_t *out_val);",
            WORKER_HEADER,
        )
        self.assertIn(
            "tab5_err_t tab5_nvs_worker_set_u8(const char *ns, const char *key, uint8_t val);",
            WORKER_HEADER,
        )
        self.assertIn("tab5_err_t tab5_nvs_get_u8(const char *ns, const char *key, uint8_t *out_val);",
                      SDK_SRC)
        self.assertIn("tab5_err_t tab5_nvs_set_u8(const char *ns, const char *key, uint8_t val);",
                      SDK_SRC)


if __name__ == "__main__":
    unittest.main(verbosity=2)
