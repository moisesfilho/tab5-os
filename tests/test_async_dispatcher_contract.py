"""Static acceptance contracts for the async WASM dispatch queue (sem goldens).

Contrato aprovado: os callbacks LVGL (touch/timer/theme) e o launch do launcher
NÃO podem mais aguardar pthread_join com o mutex de display detido; a única
forma de alcançar o WASM a partir desses caminhos de UI é postar um job numa
fila limitada executada por um worker em background.

Obrigações estáticas verificadas aqui (fonte, sem goldens):

1. Sem join síncrono no caminho de UI — pthread_join jamais aparece em
   tab5_ui_host.cpp, tab5_package_mgr.cpp nem tab5_lifecycle_host.cpp; o join
   só existe no primitivo síncrono do runtime (tab5_wasm_runtime.cpp).
2. Os callbacks LVGL (generic_widget_event_cb, on_wasm_poll_timer,
   refresh_theme) e tab5_package_mgr_launch não chamam mais
   call_wasm_callback / tab5_wasm_call_function / tab5_wasm_load_from_*
   inline — em vez disso POSTAM um job ao dispatcher.
3. Fila limitada (constante de capacidade) cujo job copia argumentos por valor:
   nunca lv_event_t* (evento do display é inválido após o retorno do callback)
   nem uint32_t*/int* (argv vive no stack do callback); strings vão para
   storage próprio (char[N]/std::string), permanecendo válidas até o worker
   rodar o module_free.
4. A instância carrega um marcador de geração/época; o dispatcher compara a
   geração antes de executar (rejeita eventos de instância fechada).
5. Fila cheia não pode bloquear a UI: nunca portMAX_DELAY nos fontes de UI/
   package/lifecycle e o enqueue trata a capacidade com retorno.
6. Teardown acontece uma única vez: free(inst->wasm_buf) existe apenas dentro
   de tab5_wasm_unload (uma vez por braço #if HAVE_WAMR/#else), nunca no
   caminho de dispatch/teardown paralelo.

Limites de verificação (doc no plano): o build host não instancia WAMR
(HAVE_WAMR=0) nem LVGL (HAVE_LVGL=0) — comportamento real de join/worker/fila
exige alvo ESP-IDF. A semântica de teardown/geração a nível de struct é
exercitada em test_async_dispatcher_contract.cpp (C++ host, com auto-SKIP até
os campos novos existirem). Aqui só há verificação estática do fonte.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "components/os/runtime"

UI = (RUNTIME / "tab5_ui_host.cpp").read_text(encoding="utf-8")
PKG = (RUNTIME / "tab5_package_mgr.cpp").read_text(encoding="utf-8")
LIFE = (RUNTIME / "tab5_lifecycle_host.cpp").read_text(encoding="utf-8")
WASM = (RUNTIME / "tab5_wasm_runtime.cpp").read_text(encoding="utf-8")
HDR = (RUNTIME / "tab5_wasm_runtime.h").read_text(encoding="utf-8")

# Arquivos novos que o coder pode criar para o dispatcher; se existirem são
# incorporados às buscas (evita forçar API fixa, como PRIMITIVES/LOCK de
# revisões anteriores).
DISPATCHER_GLOBS = (
    "tab5_*dispatch*.h", "tab5_*dispatch*.cpp",
    "tab5_*dispatcher*.h", "tab5_*dispatcher*.cpp",
    "tab5_*async*.h", "tab5_*async*.cpp",
    "*dispatcher*.h", "*dispatcher*.cpp",
    "*dispatch_queue*.h", "*dispatch_queue*.cpp",
)


def _dispatcher_extra_texts() -> list[str]:
    found: list[str] = []
    for pattern in DISPATCHER_GLOBS:
        for path in sorted(RUNTIME.glob(pattern)):
            if path.is_file() and path.suffix in (".h", ".cpp"):
                found.append(path.read_text(encoding="utf-8"))
    return found


_DISPATCHER_EXTRA = _dispatcher_extra_texts()
ALL = "\n".join(_DISPATCHER_EXTRA) + "\n" + UI + "\n" + PKG + "\n" + LIFE + "\n" + WASM + "\n" + HDR

GENERATION_NAMES = ("generation", "dispatch_generation", "state_epoch",
                    "instance_epoch", "active_epoch", "epoch")

DISPATCH_TOKENS = (r"\bdispatch\w*", r"\benqueue\w*", r"\bqueue_\w+",
                   r"\bpost_\w+", r"\basync_\w+")

CAPACITY_PATTERNS = (
    r"#define\s+(\w*(?:DISPATCH|QUEUE|ASYNC)\w*)\s+(\d+)",
    r"constexpr\s+(?:size_t|int|uint32_t|uint16_t)\s+(\w*(?:DISPATCH|QUEUE|ASYNC)\w*)\s*=\s*(\d+)",
    r"static\s+const\s+(?:size_t|int|uint32_t|uint16_t)\s+(\w*(?:DISPATCH|QUEUE|ASYNC)\w*)\s*=\s*(\d+)",
    r"enum\s*\{\s*(\w*(?:DISPATCH|QUEUE|ASYNC)\w*)\s*=\s*(\d+)\s*\}",
)

JOB_STRUCT_NAME_RE = re.compile(r"(?:dispatch|dispatcher|queue|job)", re.IGNORECASE)
ENQUEUE_NAME_RE = re.compile(r"\w*(?:enqueue|dispatch|dispatcher)\w*")


def function_body(source: str, signature: str) -> str:
    """Corpo (definição) de uma função/struct C++, respeitando chaves aninhadas.

    Declarações forward (assinatura terminada em ';', ex.: o prototype de
    tab5_ui_host_generic_widget_event_cb no .cpp) são puladas.
    """
    search_from = 0
    while True:
        start = source.index(signature, search_from)
        opening = source.index("{", start)
        semi = source.find(";", start)
        if semi != -1 and semi < opening:
            search_from = semi + 1  # forward declaration: procura a definição
            continue
        depth = 0
        for pos in range(opening, len(source)):
            if source[pos] == "{":
                depth += 1
            elif source[pos] == "}":
                depth -= 1
                if depth == 0:
                    return source[opening + 1 : pos]
        raise AssertionError(f"bloco sem fechamento: {signature}")


def _next_non_space(source: str, pos: int) -> int | None:
    for p in range(pos, len(source)):
        if not source[p].isspace():
            return p
    return None


def _balanced_body(source: str, opening: int) -> str:
    depth = 0
    for pos in range(opening, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : pos]
    raise AssertionError("bloco sem fechamento")


def function_bodies_named(source: str, name_pattern: re.Pattern) -> list[str]:
    """Corpos (definições) de funções cujo nome casa com name_pattern."""
    bodies: list[str] = []
    for match in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", source):
        if not name_pattern.fullmatch(match.group(1)):
            continue
        opening = _next_non_space(source, match.end())
        if opening is None or source[opening] != "{":
            continue
        bodies.append(_balanced_body(source, opening))
    return bodies


def struct_field(candidates) -> str | None:
    """Retorna o primeiro candidato presente no struct da instância."""
    struct = function_body(HDR, "typedef struct {")
    for name in candidates:
        if re.search(rf"\b{re.escape(name)}\b", struct):
            return name
    return None


def find_capacity_definition() -> str | None:
    """Constante de capacidade da fila (bounded), se existir."""
    for pattern in CAPACITY_PATTERNS:
        match = re.search(pattern, ALL, re.IGNORECASE)
        if match and match.group(2).isdigit() and int(match.group(2)) > 0:
            return match.group(0)
    return None


def find_job_struct_body() -> str | None:
    """Body do struct de job da fila (nome contém dispatch/dispatcher/queue/job)."""
    for match in re.finditer(r"typedef\s+struct\s*\{([^{}]*?)\}\s*([A-Za-z_]\w*)\s*;", ALL, re.DOTALL):
        if JOB_STRUCT_NAME_RE.search(match.group(2)):
            return match.group(1)
    for match in re.finditer(r"struct\s+([A-Za-z_]\w*(?:dispatch|job|queue)\w*)\s*\{([^{}]*?)\}",
                             ALL, re.DOTALL):
        return match.group(2)
    return None


class NoSynchronousWasmCallInUiPath(unittest.TestCase):
    """Requisito 1: sem join no caminho de UI e sem chamada WASM inline."""

    def test_pthread_join_absent_in_ui_pkg_lifecycle(self):
        for filename, src in (("tab5_ui_host.cpp", UI),
                              ("tab5_package_mgr.cpp", PKG),
                              ("tab5_lifecycle_host.cpp", LIFE)):
            self.assertNotIn("pthread_join", src,
                             f"{filename} não pode aguardar pthread_join inline")

    def test_widget_event_cb_has_no_sync_wasm_call(self):
        body = function_body(UI, "void tab5_ui_host_generic_widget_event_cb(lv_event_t *e)")
        self.assertNotIn("call_wasm_callback(", body,
                         "generic_widget_event_cb não pode chamar o síncrono call_wasm_callback")
        self.assertNotIn("tab5_wasm_call_function(", body,
                         "generic_widget_event_cb não pode chamar tab5_wasm_call_function")

    def test_poll_timer_has_no_sync_wasm_call(self):
        body = function_body(UI, "static void on_wasm_poll_timer(lv_timer_t *timer)")
        self.assertNotIn("call_wasm_callback(", body,
                         "on_wasm_poll_timer não pode chamar o síncrono call_wasm_callback")
        self.assertNotIn("tab5_wasm_call_function(", body,
                         "on_wasm_poll_timer não pode chamar tab5_wasm_call_function")

    def test_refresh_theme_has_no_sync_wasm_call(self):
        body = function_body(UI, "void tab5_ui_host_refresh_theme(void)")
        self.assertNotIn("call_wasm_callback(", body,
                         "refresh_theme não pode chamar o síncrono call_wasm_callback")
        self.assertNotIn("tab5_wasm_call_function(", body,
                         "refresh_theme não pode chamar tab5_wasm_call_function")

    def test_launch_has_no_sync_wasm_load_or_call(self):
        launch = function_body(
            PKG, "tab5_err_t tab5_package_mgr_launch(const char *app_id, const char *open_file_path)")
        self.assertNotIn("tab5_wasm_call_function(", launch,
                         "launch não pode executar o entrypoint WASM inline (pthread_join)")
        self.assertNotIn("tab5_wasm_load_from_", launch,
                         "launch não pode carregar o bytecode inline (pthread_join no load)")


class DispatcherPostingContract(unittest.TestCase):
    """Requisitos 1/2: callbacks LVGL e launch POSTAM o job ao dispatcher."""

    def _assert_posts_to_dispatcher(self, body, where):
        for token in DISPATCH_TOKENS:
            if re.search(token, body):
                return
        self.fail(f"{where} precisa postar o job ao dispatcher "
                  "(espera-se token dispatch/enqueue/queue_/post_/async_) no corpo")

    def test_widget_event_cb_posts_to_dispatcher(self):
        self._assert_posts_to_dispatcher(
            function_body(UI, "void tab5_ui_host_generic_widget_event_cb(lv_event_t *e)"),
            "generic_widget_event_cb")

    def test_poll_timer_posts_to_dispatcher(self):
        self._assert_posts_to_dispatcher(
            function_body(UI, "static void on_wasm_poll_timer(lv_timer_t *timer)"),
            "on_wasm_poll_timer")

    def test_refresh_theme_posts_to_dispatcher(self):
        self._assert_posts_to_dispatcher(
            function_body(UI, "void tab5_ui_host_refresh_theme(void)"),
            "refresh_theme")

    def test_launch_posts_to_dispatcher(self):
        self._assert_posts_to_dispatcher(
            function_body(PKG, "tab5_err_t tab5_package_mgr_launch(const char *app_id, "
                               "const char *open_file_path)"),
            "package_mgr_launch")


class BoundedQueueCopiesArguments(unittest.TestCase):
    """Requisitos 2 e 6: fila limitada, cópia por valor e strings donas."""

    def test_dispatch_queue_has_bounded_capacity_constant(self):
        definition = find_capacity_definition()
        self.assertIsNotNone(
            definition,
            "precisa existir uma constante de capacidade/depth para a fila do dispatcher "
            "(nome com DISPATCH/QUEUE/ASYNC e valor inteiro positivo)")

    def test_dispatch_job_struct_exists(self):
        self.assertIsNotNone(
            find_job_struct_body(),
            "precisa existir um struct de job da fila (typedef ... } NAME; com "
            "dispatch/dispatcher/queue/job no nome) definindo o payload copiado")

    def test_job_struct_copies_arguments_by_value(self):
        job = find_job_struct_body()
        self.assertIsNotNone(job, "sem struct de job não há cópia de argumentos")
        self.assertNotIn("lv_event_t", job,
                         "job não pode reter lv_event_t*: o evento do LVGL é inválido "
                         "após o retorno do callback")
        self.assertNotRegex(
            job,
            r"(?:u?int(?:8|16|32|64)_t|int(?:8|16|32)?_t)\s*\*\s*\w+",
            "argumentos não podem ser copiados por ponteiro (argv vive no stack do callback)",
        )

    def test_job_struct_owns_string_storage(self):
        job = find_job_struct_body()
        self.assertIsNotNone(job, "sem struct de job não há storage de strings")
        self.assertRegex(
            job,
            r"(?:char\s+\w+\s*\[\s*\w+\s*\]|std::string\s+\w+|char\s*\*\s+\w+)",
            "strings precisam ir para storage próprio do job (char[N]/std::string/char*), "
            "permanecendo válidas até o worker executar o module_free",
        )

    def test_enqueue_handles_full_queue_without_blocking(self):
        enqueuers = [
            body for body in function_bodies_named(ALL, ENQUEUE_NAME_RE)
            if "wasm_runtime_call_wasm(" not in body  # exclui o worker/executor
        ]
        self.assertTrue(enqueuers,
                        "falta função de enqueue que alimente a fila limitada do dispatcher")
        full_branch = re.compile(
            r"if\s*\([^)]*(?:full|count|size|len|depth|available|space)\s*(?:>=|==)\s*"
            r"(?:CAP|SIZE|MAX|capacity|\d+)[^)]*\)",
            re.IGNORECASE,
        )
        self.assertTrue(
            any(full_branch.search(body) for body in enqueuers),
            "enqueue precisa tratar fila cheia retornando sem bloquear (branch de capacidade/full)",
        )


class GenerationRejectsClosedInstance(unittest.TestCase):
    """Requisito 3: eventos de instância fechada/geração antiga são rejeitados."""

    def test_instance_struct_has_generation_marker(self):
        self.assertIsNotNone(
            struct_field(GENERATION_NAMES),
            "tab5_wasm_app_instance_t precisa de um marcador de geração/época "
            "(generation/dispatch_generation/state_epoch/instance_epoch/active_epoch/epoch)",
        )

    def test_dispatcher_checks_generation_before_execution(self):
        generation = struct_field(GENERATION_NAMES)
        self.assertIsNotNone(
            generation,
            "sem marcador de geração na instância o dispatcher não consegue "
            "rejeitar eventos antigos")
        guard = re.search(rf"if\s*\([^)]*\b{re.escape(generation)}\b[^)]*\)", ALL)
        self.assertIsNotNone(guard,
                             "o processamento do job precisa comparar a geração da instância")
        self.assertIn("is_running", ALL,
                      "o worker precisa conferir is_running da instância antes de executar")


class NonBlockingDisplayPath(unittest.TestCase):
    """Requisito 4: fila cheia/não pode travar a UI; display lock sempre com timeout finito."""

    def test_no_portMAX_DELAY_in_ui_pkg_lifecycle(self):
        for filename, src in (("tab5_ui_host.cpp", UI),
                              ("tab5_package_mgr.cpp", PKG),
                              ("tab5_lifecycle_host.cpp", LIFE)):
            self.assertNotIn("portMAX_DELAY", src,
                             f"{filename} não pode esperar bloqueio indefinido (portMAX_DELAY)")

    def test_display_lock_has_finite_timeout(self):
        self.assertRegex(
            UI,
            r"#define\s+LV_LOCK\(\)\s+bsp_display_lock\(",
            "LV_LOCK precisa ser bsp_display_lock com timeout finito "
            "(pdMS_TO_TICKS(500)), nunca portMAX_DELAY",
        )
        self.assertIn("pdMS_TO_TICKS(500)", UI,
                      "o display lock no caminho de UI precisa ter timeout finito (500ms)")


class TeardownOnce(unittest.TestCase):
    """Requisito 5: teardown uma única vez após as chamadas em voo."""

    def test_wasm_buf_freed_only_inside_unload(self):
        unload = function_body(WASM, "tab5_err_t tab5_wasm_unload")
        self.assertEqual(
            WASM.count("free(inst->wasm_buf)"), 2,
            "o bytecode é liberado apenas por tab5_wasm_unload (uma vez por braço "
            "#if HAVE_WAMR/#else); nenhum outro caminho pode duplicar o free",
        )
        self.assertEqual(
            unload.count("free(inst->wasm_buf)"), 2,
            "todo free(inst->wasm_buf) precisa viver dentro de tab5_wasm_unload — "
            "nunca no caminho de dispatch/teardown paralelo do dispatcher",
        )


if __name__ == "__main__":
    unittest.main()
