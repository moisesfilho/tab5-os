"""Static contracts for the synchronous Wi-Fi scan path (approved interface).

Objective aprovado: corrigir a lista Wi-Fi VAZIA no dispositivo fisico Tab5
(sem simulador), com >= 1 rede no ambiente de validacao. A implementacao
aprovada (confirmada fisicamente pela ponte serial) expoe a seguinte API
publica efetiva, que estes contratos estaticos verificam:

  1. `tab5_wifi_scan()` (Host ABI/SDK, usada pelos apps WASM e simulador)
     DELEGA ao wrapper interno `tab5_wifi_scan_with_timeout()` — a logica
     sincrona vive no wrapper, nao na funcao de entrada.
  2. `tab5_wifi_scan_with_timeout()` (tab5_host_abi.cpp, declarado em
     tab5_host_abi.h) concentra o caminho sincrono:
     a. passa pelo manager (`wifi_mgr_scan`) em vez de usar o SDK direto;
     b. aplica o `timeout_ms` pedido (ou a constante nomeada quando == 0)
        e aguarda a conclusao com espera limitada (semaphore + deadline);
     c. copia os APs para out_aps com `*out_count` deterministico
        (clamp por capacidade, nunca vazamento de slot);
     d. sinaliza a conclusao (xSemaphoreGive no callback).
  3. `wifi_mgr_scan()` (wifi_mgr.cpp) serializa scans reentrantes num slot
     de callback unico: enquanto houver scan pendente devolve erro explicito
      `ESP_ERR_INVALID_STATE` (convencao IDF 5.x para operacao invalida) —
     scans concorrentes nunca sobrescrevem o slot.
  4. Retry: quando o manager responde ocupado (`ESP_ERR_INVALID_STATE`, ex.:
     scan periodico do wifi_mgr ou outro scan em curso), o caminho sincrono
      tenta novamente com espera e prazo final (loop inline ou helper estatico),
     nunca desistindo no primeiro erro de serializacao.
  5. Ponte serial: comando `wifi.scan` com `timeout_ms` opcional, chamando o
     MESMO wrapper com timeout e respondendo `data.count` + `data.aps[]`,
     ou um frame de erro explicito (timeout / Wi-Fi desabilitado / falha) —
     nunca uma lista vazia "ok" falsa.

Estes contratos falham (red) se a implementacao regredir para o caminho
antigo (esp_wifi_scan_start direto, sem serializacao/retry/timeout) e ficam
verdes contra a implementacao aprovada. Self-checks do parser permanecem
verdes (independem da implementacao).
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WIFI_MGR_H = ROOT / "components/os/core/wifi_mgr.h"
WIFI_MGR_CPP = ROOT / "components/os/core/wifi_mgr.cpp"
HOST_ABI = ROOT / "components/os/runtime/tab5_host_abi.cpp"
HOST_ABI_H = ROOT / "components/os/runtime/tab5_host_abi.h"
SERIAL_BRIDGE = ROOT / "components/os/core/serial_bridge.cpp"

WIFI_SCAN_FUNC_MGR = "esp_err_t wifi_mgr_scan(wifi_scan_cb_t cb, void *ctx)"
WIFI_SCAN_FUNC_ABI = \
    "tab5_err_t tab5_wifi_scan(tab5_wifi_ap_t *out_aps, uint32_t max_aps, uint32_t *out_count)"
WIFI_SCAN_FUNC_TIMEOUT = \
    "tab5_err_t tab5_wifi_scan_with_timeout(tab5_wifi_ap_t *out_aps, uint32_t max_aps, " \
    "uint32_t *out_count, uint32_t timeout_ms)"

WIFI_TIMEOUT_NAME_RE = re.compile(r"(?=.*(?:TIMEOUT|SYNC))(?=.*SCAN)[A-Z][A-Z0-9_]*")
WIFI_TIMEOUT_CAMEL_RE = re.compile(r"(?=.*(?:[Tt]imeout|[Ss]ync))(?=.*[Ss]can)[A-Za-z0-9_]{4,}")

CONST_DECL_RE = re.compile(
    r"(?:static\s+)?(?:constexpr|const)\s+"
    r"(?:uint32_t|uint64_t|uint16_t|int|unsigned(?:\s+int)?)\s+"
    r"(\w+)\s*=\s*(\d+)\s*;"
)

RETRY_HELPER_RE = re.compile(r"\b\w*(?:retry|rescan|sync|attempt)\w*", re.IGNORECASE)

# Erro explicito que o manager usa para serializar scans reentrantes na
# implementacao aprovada (IDF 5.x) — a guarda anti-reentrancia jamais pode
# silenciosamente sobrescrever o slot unico de callback.
BUSY_TOKENS = ("ESP_ERR_INVALID_STATE", "ESP_ERR_WIFI_BUSY")


def _read(p: Path) -> str:
    return p.read_text(encoding="utf-8")


def _strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def _signature_pattern(signature: str) -> re.Pattern:
    """Converte uma assinatura C/C++ (possivelmente multilinha) em pattern
    tolerante a espacos em branco (espacos/comentarios/newlines entre tokens)."""
    norm = re.sub(r"\s+", " ", signature).strip()
    return re.compile(re.escape(norm).replace(r"\ ", r"\s+"))


def _contains_signature(source: str, signature: str) -> bool:
    return _signature_pattern(signature).search(source) is not None


def _function_body(source: str, signature: str) -> str:
    """Retorna o corpo de uma funcao C++, respeitando chaves aninhadas.

    A assinatura pode estar quebrada em varias linhas (como as definicoes
    *_with_timeout em tab5_host_abi.cpp).
    """
    m = _signature_pattern(signature).search(source)
    if m is None:
        raise AssertionError(f"function definition not found: {signature!r}")
    opening = source.index("{", m.start())
    depth = 0
    for pos in range(opening, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : pos]
    raise AssertionError(f"unclosed function body: {signature!r}")


def _has_sync_wait(body: str) -> bool:
    return ("xSemaphoreTake(" in body) and ("pdMS_TO_TICKS(" in body)


def _find_wifi_timeout_const(*sources) -> tuple | None:
    for _src in sources:
        src = _read(_src) if isinstance(_src, Path) else _src
        for m in re.finditer(r"#define\s+([A-Z][A-Z0-9_]*)\s+\(?(\d+)\)?", src):
            name, value = m.group(1), int(m.group(2))
            if WIFI_TIMEOUT_NAME_RE.fullmatch(name) and value > 0:
                return name, value
        for m in CONST_DECL_RE.finditer(src):
            name, value = m.group(1), int(m.group(2))
            if value > 0 and (
                WIFI_TIMEOUT_NAME_RE.fullmatch(name) or WIFI_TIMEOUT_CAMEL_RE.fullmatch(name)
            ):
                return name, value
    return None


def _has_busy_token(body: str) -> bool:
    return any(token in body for token in BUSY_TOKENS)


def _retry_form_inline(body: str, const_name: str | None) -> bool:
    """Inline busy-retry loop: while/for + busy-token + delay + deadline/constant."""
    if not _has_busy_token(body):
        return False
    if not re.search(r"\b(?:while|for)\s*\(", body):
        return False
    if "vTaskDelay(" not in body and "xTimerChangePeriod(" not in body:
        return False
    has_deadline = "xTaskGetTickCount" in body
    has_const = const_name is not None and const_name in body
    return has_deadline or has_const


def _retry_form_helper(src: str, body: str, const_name: str | None) -> bool:
    """Static helper (retry/rescan/sync) invoked with the deadline logic."""
    for m in re.finditer(r"static\s+(?:inline\s+)?(?:esp_err_t|bool|int)\s+(\w{2,})\s*\(", src):
        name = m.group(1)
        if not RETRY_HELPER_RE.fullmatch(name):
            continue
        try:
            helper_body = _function_body(src, src[m.start() : m.end() - 1])
        except AssertionError:
            continue
        busy = _has_busy_token(helper_body)
        delay = "vTaskDelay(" in helper_body or "xTimerChangePeriod(" in helper_body
        deadline = "xTaskGetTickCount" in helper_body
        const = const_name is not None and const_name in helper_body
        called = re.search(rf"\b{re.escape(name)}\s*\(", body) is not None
        if busy and delay and (deadline or const) and called:
            return True
    return False


def _retry_detected(src: str, *bodies: str) -> bool:
    const = _find_wifi_timeout_const(WIFI_MGR_H, WIFI_MGR_CPP, src)
    const_name = const[0] if const else None
    for body in bodies:
        if _retry_form_inline(body, const_name) or _retry_form_helper(src, body, const_name):
            return True
    return False


def _dispatch_window(src: str, cmd: str, size: int = 1200) -> str | None:
    pos = src.find('"' + cmd + '"')
    if pos < 0:
        return None
    return src[pos : pos + size]


class WifiManagerSerializationContract(unittest.TestCase):
    """wifi_mgr_scan must reject reentrant scans (single callback slot)."""

    @classmethod
    def setUpClass(cls):
        cls.src = _read(WIFI_MGR_CPP)
        cls.body = _function_body(cls.src, WIFI_SCAN_FUNC_MGR)

    def test_wifi_mgr_scan_exists(self):
        """Anchor: wifi_mgr_scan must exist (non-vacuous guard)."""
        self.assertIn(WIFI_SCAN_FUNC_MGR, self.src)

    def test_wifi_mgr_scan_has_reentrancy_guard(self):
        """Scans concorrentes devem ser rejeitados com erro explicito.

        O manager mantem UM slot de callback de scan; sem guarda, um segundo
        scan a nivel do app sobrescreve silenciosamente o primeiro. A
        implementacao aprovada devolve ESP_ERR_INVALID_STATE (convencao
         IDF 5.x para operacao invalida) enquanto houver scan pendente — nunca
        ESP_ERR_WIFI_BUSY imposto nem sobrescrita do slot.
        """
        self.assertIn(
            "ESP_ERR_INVALID_STATE",
            self.body,
            "wifi_mgr_scan deve devolver ESP_ERR_INVALID_STATE enquanto outro "
            "scan estiver pendente (serializacao explicita do slot unico)",
        )
        self.assertIn(
            "return ESP_ERR_INVALID_STATE;",
            self.body,
            "a guarda de reentrancia deve retornar explicitamente o erro — sem "
            "silenciosamente sobrescrever o slot de callback em curso",
        )
        self.assertTrue(
            re.search(r"\bs_scan_cb\b|s_scan_in_progress\b|busy|in_progress|s_scanning", self.body),
            "a guarda anti-reentrancia deve estar ligada ao estado do scan "
            "pendente (s_scan_cb slot, busy flag ou marker de scan em curso)",
        )

    def test_wifi_mgr_scan_still_starts_real_scan(self):
        """Serialization must not suppress the actual scan start."""
        self.assertIn(
            "esp_wifi_scan_start(",
            self.body,
            "wifi_mgr_scan must still start esp_wifi_scan_start() for allowed scans",
        )


class WifiSyncScanHostAbiContract(unittest.TestCase):
    """tab5_wifi_scan delegates to tab5_wifi_scan_with_timeout (the sync path).

    A logica sincrona (manager + semaphore + copia + retry) vive no wrapper
    `*_with_timeout`, que e a API efetiva usada pela ABI e pela ponte serial.
    """

    @classmethod
    def setUpClass(cls):
        cls.abi = _read(HOST_ABI)
        cls.header = _read(HOST_ABI_H)
        cls.body = _function_body(cls.abi, WIFI_SCAN_FUNC_ABI)
        cls.wrapper_body = _function_body(cls.abi, WIFI_SCAN_FUNC_TIMEOUT)

    def test_with_timeout_wrapper_is_the_public_api(self):
        """O wrapper com timeout e a API publica efetiva (definido na ABI e
        declarado no header para a ponte serial)."""
        self.assertTrue(
            _contains_signature(self.abi, WIFI_SCAN_FUNC_TIMEOUT),
            "tab5_wifi_scan_with_timeout(...) deve estar definida em "
            "tab5_host_abi.cpp",
        )
        self.assertTrue(
            _contains_signature(self.header, WIFI_SCAN_FUNC_TIMEOUT),
            "tab5_wifi_scan_with_timeout(...) deve estar declarada em "
            "tab5_host_abi.h (API interna usada pela ponte serial)",
        )

    def test_tab5_wifi_scan_delegates_to_wrapper(self):
        """A funcao de entrada (ABI/SDK) deve delegar ao wrapper com timeout."""
        self.assertIn(
            "tab5_wifi_scan_with_timeout(",
            self.body,
            "tab5_wifi_scan() deve delegar a tab5_wifi_scan_with_timeout() — a "
            "logica sincrona de scan vive no wrapper (o mesmo usado pela ponte "
            "serial), nao na funcao de entrada",
        )

    def test_wrapper_uses_manager_serialization(self):
        """O wrapper deve passar pelo wifi_mgr_scan (serializacao do slot unico)."""
        self.assertIn(
            "wifi_mgr_scan(",
            self.wrapper_body,
            "tab5_wifi_scan_with_timeout() deve invocar wifi_mgr_scan() para que "
            "o scan passe pela serializacao/guarda anti-reentrancia do manager "
            "em vez de chamar esp_wifi_scan_start diretamente",
        )

    def test_wrapper_waits_bounded_timeout(self):
        """O scan deve bloquear com timeout limitado, nunca fire-and-forget."""
        self.assertTrue(
            _has_sync_wait(self.wrapper_body),
            "tab5_wifi_scan_with_timeout() deve aguardar o callback com "
            "xSemaphoreTake(..., pdMS_TO_TICKS(...)) — espera limitada pelo "
            "prazo, nunca fire-and-forget com count=0",
        )

    def test_sync_timeout_constant_exists(self):
        """Uma constante nomeada positiva de timeout deve gatear a espera."""
        const = _find_wifi_timeout_const(WIFI_MGR_H, WIFI_MGR_CPP, self.abi)
        self.assertIsNotNone(
            const,
            "a named positive constant with TIMEOUT|SYNC and SCAN in the name "
            "(e.g. WIFI_SCAN_SYNC_TIMEOUT_MS) must exist in wifi_mgr.h/.cpp "
            "or tab5_host_abi.cpp",
        )

    def test_wrapper_applies_caller_timeout_or_defaults(self):
        """O wrapper deve honrar timeout_ms (0 == constante nomeada default)."""
        const = _find_wifi_timeout_const(WIFI_MGR_H, WIFI_MGR_CPP, self.abi)
        self.assertIn(
            "timeout_ms",
            self.wrapper_body,
            "tab5_wifi_scan_with_timeout() deve usar o parametro timeout_ms",
        )
        self.assertTrue(
            re.search(r"timeout_ms\s*==\s*0", self.wrapper_body),
            "o wrapper deve trocar timeout_ms == 0 pela constante nomeada "
            "(default com prazo determinado)",
        )
        if const is not None:
            self.assertIn(
                const[0],
                self.wrapper_body,
                "o wrapper deve usar a constante de timeout nomeada como default",
            )

    def test_wrapper_copies_aps(self):
        """APs descobertos devem ser copiados para o buffer de saida."""
        self.assertTrue(
            re.search(r"->out\s*\[", self.wrapper_body),
            "tab5_wifi_scan_with_timeout() deve copiar os APs para o buffer de "
            "saida (loop preenchendo out[i]/ctx->out[i])",
        )
        self.assertIn("ssid", self.wrapper_body,
                      "a copia deve incluir o SSID de cada AP")
        self.assertIn("rssi", self.wrapper_body,
                      "a copia deve incluir o RSSI de cada AP")

    def test_wrapper_reports_deterministic_count(self):
        """*out_count deve ser atribuido de forma deterministico."""
        self.assertIn(
            "*out_count =",
            self.wrapper_body,
            "tab5_wifi_scan_with_timeout() deve atribuir *out_count (inicial e "
            "final) para resultado deterministico",
        )
        self.assertIn(
            "context->count",
            self.wrapper_body,
            "o *out_count final deve vir do contador do contexto sincrono "
            "(clamp por capacidade)",
        )

    def test_completion_signals_waiting_task(self):
        """O callback de scan deve sinalizar a tarefa que aguarda."""
        self.assertIn(
            "xSemaphoreGive(",
            self.wrapper_body,
            "o callback de scan deve sinalizar o waiter (xSemaphoreGive) para "
            "que a espera limitada do wrapper retorne os resultados",
        )


class WifiRetryContract(unittest.TestCase):
    """A busy/disabled radio must be retried with a bounded wait, not dropped."""

    @classmethod
    def setUpClass(cls):
        cls.mgr_src = _read(WIFI_MGR_CPP)
        cls.mgr_body = _function_body(cls.mgr_src, WIFI_SCAN_FUNC_MGR)
        cls.abi = _read(HOST_ABI)
        cls.abi_body = _function_body(cls.abi, WIFI_SCAN_FUNC_ABI)
        cls.wrapper_body = _function_body(cls.abi, WIFI_SCAN_FUNC_TIMEOUT)

    def test_retry_on_busy_with_deadline(self):
        """O erro explicito de serializacao do manager deve ser tratado com retry.

        O scan periodico do wifi_mgr (ou outro scan em curso) faz o manager
        responder ESP_ERR_INVALID_STATE; o caminho sincrono deve repetir com
        espera e desistir apenas no deadline. Aceito: loop inline no wrapper
        (ou helper estatico chamado com o estado do deadline).
        """
        self.assertTrue(
            _retry_detected(self.mgr_src, self.wrapper_body, self.abi_body, self.mgr_body),
            "o caminho sincrono (tab5_wifi_scan_with_timeout) deve tratar o erro "
            "explicito de serializacao do wifi_mgr (ESP_ERR_INVALID_STATE) com "
            "loop de retry (while/for ou helper estatico) contendo espera "
            "(vTaskDelay/xTimerChangePeriod) e deadline (xTaskGetTickCount ou "
            "constante TIMEOUT/SYNC nomeada) — um radio ocupado jamais pode "
            "produzir lista vazia imediata",
        )

    def test_retry_uses_bounded_attempts(self):
        """O retry deve ser finito (deadline/limite de tentativas), nunca infinito."""
        const = _find_wifi_timeout_const(WIFI_MGR_H, WIFI_MGR_CPP, self.abi)
        const_name = const[0] if const else None
        has_deadline = any(
            "xTaskGetTickCount" in body
            for body in (self.mgr_body, self.abi_body, self.wrapper_body)
        )
        has_const = const_name is not None and any(
            const_name in body
            for body in (self.mgr_body, self.abi_body, self.wrapper_body)
        )
        self.assertTrue(
            has_deadline or has_const,
            "o loop de retry deve ser limitado por deadline ou pela constante "
            "de timeout nomeada (tentativas finitas, sem busy-spin infinito)",
        )


class WifiScanBridgeContract(unittest.TestCase):
    """serial_bridge.cpp must dispatch `wifi.scan` via the with_timeout wrapper."""

    @classmethod
    def setUpClass(cls):
        cls.src = _read(SERIAL_BRIDGE)

    def test_bridge_dispatches_wifi_scan(self):
        self.assertIn('"wifi.scan"', self.src,
                       "serial_bridge_dispatch must handle the wifi.scan command")

    def test_bridge_calls_sync_scan_path(self):
        window = _dispatch_window(self.src, "wifi.scan")
        self.assertIsNotNone(window, "wifi.scan branch not found")
        self.assertTrue(
            re.search(r"\btab5_wifi_scan_with_timeout\(", window),
            "wifi.scan deve chamar tab5_wifi_scan_with_timeout() — o wrapper com "
            "timeout e o MESMO caminho sincrono que a ABI usa (via "
            "tab5_wifi_scan), permitindo aplicar o deadline pedido pelo host",
        )

    def test_bridge_response_shape_has_aps_and_count(self):
        window = _dispatch_window(self.src, "wifi.scan")
        self.assertIsNotNone(window, "wifi.scan branch not found")
        self.assertIn('"aps"', window,
                       "wifi.scan data must include an \"aps\" array")
        self.assertIn('"count"', window,
                       "wifi.scan data must include a \"count\" field")

    def test_bridge_accepts_timeout_ms(self):
        window = _dispatch_window(self.src, "wifi.scan")
        self.assertIsNotNone(window, "wifi.scan branch not found")
        self.assertTrue(
            re.search(r'has_number\(root,\s*"timeout_ms"', window),
            "wifi.scan must parse timeout_ms from the request (has_number)",
        )

    def test_bridge_reports_failure_as_error(self):
        window = _dispatch_window(self.src, "wifi.scan")
        self.assertIsNotNone(window, "wifi.scan branch not found")
        self.assertTrue(
            re.search(r'"[^"]*(?:tempo|timeout|falha|busy|desabilitado)[^"]*"',
                      window, re.IGNORECASE),
            "wifi.scan deve expor timeout / Wi-Fi desabilitado / falha como "
            "frame de erro explicito (ex.: 'timeout no scan Wi-Fi', 'Wi-Fi "
            "desabilitado', 'falha no scan Wi-Fi') — nunca um ok/lista vazia "
            "falso",
        )


class WifiScanParserSelfCheck(unittest.TestCase):
    """Purity of the static checkers (green, implementation-free)."""

    def test_body_extraction(self):
        src = (
            "tab5_err_t tab5_wifi_scan_with_timeout(tab5_wifi_ap_t *out_aps, uint32_t max_aps,\n"
            "                                       uint32_t *out_count, uint32_t timeout_ms)\n"
            "{\n"
            "    *out_count = 0;\n"
            "    esp_err_t err = wifi_mgr_scan(cb, &ctx);\n"
            "    if (xSemaphoreTake(s, pdMS_TO_TICKS(WIFI_SCAN_SYNC_TIMEOUT_MS)) == pdTRUE) {\n"
            "        *out_count = s_count;\n"
            "        memcpy(out_aps, s_aps, sizeof(tab5_wifi_ap_t) * s_count);\n"
            "    }\n"
            "    return TAB5_OK;\n"
            "}\n"
        )
        body = _function_body(src, WIFI_SCAN_FUNC_TIMEOUT)
        self.assertIn("wifi_mgr_scan(", body)
        self.assertIn("xSemaphoreTake(", body)
        self.assertIn("memcpy(out_aps", body)
        self.assertIn("*out_count =", body)

    def test_multiline_signature_body_extraction(self):
        """Assinaturas quebradas em linhas (como no tab5_host_abi.cpp) devem ser
        extraidas sem depender de espacos exatos."""
        src = (
            "tab5_err_t tab5_wifi_scan_with_timeout(tab5_wifi_ap_t *out_aps,\n"
            "                                       uint32_t max_aps, uint32_t *out_count,\n"
            "                                       uint32_t timeout_ms)\n"
            "{\n"
            "    if (timeout_ms == 0) timeout_ms = WIFI_SCAN_SYNC_TIMEOUT_MS;\n"
            "    return wifi_mgr_scan(cb, ctx) == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;\n"
            "}\n"
        )
        body = _function_body(src, WIFI_SCAN_FUNC_TIMEOUT)
        self.assertIn("wifi_mgr_scan(", body)
        self.assertIn("timeout_ms ==", body)

    def test_signature_containment(self):
        header = (
            "tab5_err_t tab5_wifi_scan_with_timeout(tab5_wifi_ap_t *out_aps, uint32_t max_aps,\n"
            "                                       uint32_t *out_count, uint32_t timeout_ms);\n"
        )
        self.assertTrue(_contains_signature(header, WIFI_SCAN_FUNC_TIMEOUT))
        self.assertFalse(_contains_signature("tab5_wifi_scan(out_aps, 8, &c);", WIFI_SCAN_FUNC_TIMEOUT))

    def test_delegation_detection(self):
        src = (
            "tab5_err_t tab5_wifi_scan(tab5_wifi_ap_t *out_aps, uint32_t max_aps, uint32_t *out_count)\n"
            "{\n"
            "    return tab5_wifi_scan_with_timeout(out_aps, max_aps, out_count, WIFI_SCAN_SYNC_TIMEOUT_MS);\n"
            "}\n"
        )
        body = _function_body(src, WIFI_SCAN_FUNC_ABI)
        self.assertIn("tab5_wifi_scan_with_timeout(", body)

    def test_sync_wait_patterns(self):
        self.assertTrue(_has_sync_wait("xSemaphoreTake(s, pdMS_TO_TICKS(8000));"))
        self.assertFalse(_has_sync_wait("esp_wifi_scan_start(NULL, true);"))

    def test_wifi_timeout_define_detection(self):
        src = "#define WIFI_SCAN_SYNC_TIMEOUT_MS 15000\n"
        self.assertEqual(_find_wifi_timeout_const(src), ("WIFI_SCAN_SYNC_TIMEOUT_MS", 15000))

    def test_wifi_timeout_camel_detection(self):
        src = "static constexpr uint32_t kSyncScanTimeoutMs = 16000;\n"
        self.assertEqual(_find_wifi_timeout_const(src), ("kSyncScanTimeoutMs", 16000))

    def test_existing_constants_not_matched(self):
        src = "#define SCAN_PERIOD_MS 30000\n#define CONNECT_RETRY_BASE_MS 2000\n"
        self.assertIsNone(_find_wifi_timeout_const(src))

    def test_retry_inline_detection(self):
        body = (
            "for (int attempt = 0;; attempt++) {\n"
            "    if (esp_wifi_scan_start(NULL, false) != ESP_ERR_INVALID_STATE) break;\n"
            "    vTaskDelay(pdMS_TO_TICKS(200));\n"
            "    if ((xTaskGetTickCount() - t0) > pdMS_TO_TICKS(WIFI_SCAN_SYNC_TIMEOUT_MS)) break;\n"
            "}\n"
        )
        self.assertTrue(_retry_form_inline(body, "WIFI_SCAN_SYNC_TIMEOUT_MS"))
        self.assertFalse(_retry_form_inline("esp_wifi_scan_start(NULL, true);", "WIFI_SCAN_SYNC_TIMEOUT_MS"))

    def test_retry_helper_detection(self):
        src = (
            "static esp_err_t wifi_scan_retry(void)\n"
            "{\n"
            "    while (esp_wifi_scan_start(NULL, false) == ESP_ERR_INVALID_STATE) {\n"
            "        vTaskDelay(pdMS_TO_TICKS(200));\n"
            "        if ((xTaskGetTickCount() - t0) > pdMS_TO_TICKS(WIFI_SCAN_SYNC_TIMEOUT_MS)) return ESP_ERR_TIMEOUT;\n"
            "    }\n"
            "    return ESP_OK;\n"
            "}\n"
            "esp_err_t wifi_mgr_scan(wifi_scan_cb_t cb, void *ctx)\n"
            "{\n"
            "    if (s_scan_in_progress) return ESP_ERR_INVALID_STATE;\n"
            "    if (wifi_scan_retry() != ESP_OK) return ESP_ERR_TIMEOUT;\n"
            "    esp_wifi_scan_get_ap_records(&n, aps);\n"
            "}\n"
        )
        body = _function_body(src, WIFI_SCAN_FUNC_MGR)
        self.assertTrue(_retry_form_helper(src, body, "WIFI_SCAN_SYNC_TIMEOUT_MS"))

    def test_dispatch_window_detection(self):
        src = ('{"cmd": "wifi.scan", "timeout_ms": 15000}\n'
               '"wifi.scan" -> tab5_wifi_scan_with_timeout(aps, N, &c, t);')
        window = _dispatch_window(src, "wifi.scan")
        self.assertIsNotNone(window)
        self.assertIn("tab5_wifi_scan_with_timeout(", window)

    def test_timeout_constant_finder_accepts_path(self):
        """The constant finder must coerce Path sources (helper robustness)."""
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            header = Path(tmp) / "wifi_mgr_test.h"
            header.write_text("#define WIFI_SCAN_SYNC_TIMEOUT_MS 7000\n", encoding="utf-8")
            self.assertEqual(
                _find_wifi_timeout_const(header, "unused-str-source\n"),
                ("WIFI_SCAN_SYNC_TIMEOUT_MS", 7000),
            )

    def test_failure_error_token_matches_inside_quoted_message(self):
        """Valid compound error strings must match, not only exact tokens."""
        window = (
            'error_frame(cmd, scan_err == TAB5_ERR_TIMEOUT ? '
            '"timeout no scan Wi-Fi" : scan_err == TAB5_ERR_INVALID_STATE ? '
            '"Wi-Fi desabilitado" : "falha no scan Wi-Fi", rid);'
        )
        self.assertIsNotNone(re.search(
            r'"[^"]*(?:tempo|timeout|falha|busy|desabilitado)[^"]*"', window, re.IGNORECASE))

    def test_busy_token_accepts_approved_invalid_state(self):
        """O detector de retry deve reconhecer ESP_ERR_INVALID_STATE (o erro de
        serializacao aprovado) alem de ESP_ERR_WIFI_BUSY."""
        self.assertTrue(_has_busy_token("if (err == ESP_ERR_INVALID_STATE) retry();"))
        self.assertTrue(_has_busy_token("while (err == ESP_ERR_WIFI_BUSY) retry();"))
        self.assertFalse(_has_busy_token("retry();"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
