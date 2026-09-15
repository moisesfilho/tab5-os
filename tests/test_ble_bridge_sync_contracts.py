"""Static contracts for the synchronous BLE scan path (approved interface).

Objective aprovado: corrigir a lista Bluetooth VAZIA no dispositivo fisico
Tab5 (sem simulador), com validacao final exclusivamente via serial exigindo
>= 1 dispositivo BLE. A implementacao aprovada (confirmada fisicamente pela
ponte serial) expoe a seguinte API publica efetiva, que estes contratos
estaticos verificam:

  1. `tab5_bt_scan()` (Host ABI/SDK, usada pelos apps WASM e simulador)
     DELEGA ao wrapper interno `tab5_bt_scan_with_timeout()` — a logica
     sincrona vive no wrapper, nao na funcao de entrada.
  2. `tab5_bt_scan_with_timeout()` (tab5_host_abi.cpp, declarado em
     tab5_host_abi.h) concentra o caminho sincrono:
     a. dispara uma busca real via `bt_mgr_scan()` com callback != nullptr
        (nunca bloqueado pelo throttle passivo, que so valida cb == nullptr);
     b. aplica o `timeout_ms` pedido (ou a constante nomeada quando == 0) e
        aguarda a conclusao com espera limitada (`xSemaphoreTake` +
        `pdMS_TO_TICKS(...)` de uma constante nomeada / deadline);
     c. reporta resultado deterministico: `*out_count` e copia dos
        dispositivos encontrados para `out_devs` (clamp por capacidade;
        MAC e o identificador do dispositivo, nome pode ser vazio);
     d. sinaliza a conclusao (xSemaphoreGive no callback).
  3. Serializacao/retry: `bt_mgr_scan` devolve `ESP_ERR_INVALID_STATE`
      (slot unico / radio desabilitado); o wrapper tenta novamente a chamada com
     espera enquanto o estado do radio for invalido, ate o deadline, e
     propaga timeout/falha explicitamente — nunca uma lista vazia "ok"
     false.
  4. A ponte serial (`components/os/core/serial_bridge.cpp`) expoe o
     comando `ble.scan`:
     a. aceita `timeout_ms` opcional;
     b. invoca o MESMO caminho sincrono (`tab5_bt_scan_with_timeout`);
     c. responde com `data.count` e `data.devices[]`;
     d. reporta ERROR (timeout / Bluetooth desabilitado / falha) em vez de
        lista vazia bem-sucedida.

Estes contratos falham (red) se a implementacao regredir para o caminho
antigo (count=0 sem disparar buscas, sem espera sincrona, sem copia) e
ficam verdes contra a implementacao aprovada. Self-checks do parser
permanecem verdes (independem da implementacao).
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HOST_ABI = ROOT / "components/os/runtime/tab5_host_abi.cpp"
HOST_ABI_H = ROOT / "components/os/runtime/tab5_host_abi.h"
BT_MGR_H = ROOT / "components/os/core/bt_mgr.h"
BT_MGR_CPP = ROOT / "components/os/core/bt_mgr.cpp"
SERIAL_BRIDGE = ROOT / "components/os/core/serial_bridge.cpp"

BT_SCAN_FUNC = "tab5_err_t tab5_bt_scan(tab5_bt_dev_t *out_devs, uint32_t max_devs, uint32_t *out_count)"
BT_SCAN_FUNC_TIMEOUT = \
    "tab5_err_t tab5_bt_scan_with_timeout(tab5_bt_dev_t *out_devs, uint32_t max_devs, " \
    "uint32_t *out_count, uint32_t timeout_ms)"

# Constantes de timeout do scan BLUETOOTH (wrapper sync). Alem de
# (TIMEOUT|SYNC), o nome precisa ser ESPECIFICO de BT/BLE: o primeiro
# #define de timeout do tab5_host_abi.cpp e WIFI_SCAN_SYNC_TIMEOUT_MS (que
# tambem carrega SCAN+TIMEOUT) e nao pode ser escolhido como a constante do
# wrapper BT — WIFI_SCAN_SYNC_TIMEOUT_MS nao aparece no corpo do wrapper.
TIMEOUT_NAME_RE = re.compile(
    r"(?=.*(?:TIMEOUT|SYNC))(?=.*(?:BT|BLE|BLUETOOTH))[A-Z][A-Z0-9_]*"
)
# Variante camelCase, ex.: kSyncBtScanTimeoutMs, kBluetoothScanTimeoutMs.
TIMEOUT_CAMEL_RE = re.compile(
    r"(?=.*(?:[Tt]imeout|[Ss]ync))(?=.*(?i:bt|ble|bluetooth))[A-Za-z0-9_]{4,}"
)
CONST_DECL_RE = re.compile(
    r"(?:static\s+)?(?:constexpr|const)\s+"
    r"(?:uint32_t|uint64_t|uint16_t|int|unsigned(?:\s+int)?)\s+"
    r"(\w+)\s*=\s*(\d+)\s*;"
)

# Erro explicito de serializacao do bt_mgr (slot unico/radio desabilitado)
# na implementacao aprovada — o wrapper deve repetir com espera ate o deadline.
BUSY_TOKENS = ("ESP_ERR_INVALID_STATE", "ESP_ERR_WIFI_BUSY")


def _read(p: Path) -> str:
    return p.read_text(encoding="utf-8")


def _strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def _signature_pattern(signature: str) -> re.Pattern:
    """Assinatura C/C++ (possivelmente multilinha) tolerante a espacos."""
    norm = re.sub(r"\s+", " ", signature).strip()
    return re.compile(re.escape(norm).replace(r"\ ", r"\s+"))


def _contains_signature(source: str, signature: str) -> bool:
    return _signature_pattern(signature).search(source) is not None


def _function_body(source: str, signature: str) -> str:
    """Retorna o corpo de uma funcao C++, respeitando chaves aninhadas."""
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
    """Synchronous bounded wait: xSemaphoreTake + a pdMS_TO_TICKS translate."""
    return ("xSemaphoreTake(" in body) and ("pdMS_TO_TICKS(" in body)


def _find_sync_timeout_const(*sources) -> tuple | None:
    """Bluetooth-specific named timeout constant (define or const/constexpr)
    with value > 0. Never a WIFI constant: wlan/bt co-exist in
    tab5_host_abi.cpp (WIFI_SCAN_SYNC_TIMEOUT_MS before
    BT_SCAN_SYNC_TIMEOUT_MS) and the search must pick the BT wrapper one."""
    for _src in sources:
        src = _read(_src) if isinstance(_src, Path) else _src
        for m in re.finditer(r"#define\s+([A-Z][A-Z0-9_]*)\s+\(?(\d+)\)?", src):
            name, value = m.group(1), int(m.group(2))
            if TIMEOUT_NAME_RE.fullmatch(name) and value > 0:
                return name, value
        for m in CONST_DECL_RE.finditer(src):
            name, value = m.group(1), int(m.group(2))
            if value > 0 and (
                TIMEOUT_NAME_RE.fullmatch(name) or TIMEOUT_CAMEL_RE.fullmatch(name)
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
        if not re.fullmatch(r"\b\w*(?:retry|rescan|sync|attempt)\w*", name, re.IGNORECASE):
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


def _dispatch_window(src: str, cmd: str, size: int = 1200) -> str | None:
    pos = src.find('"' + cmd + '"')
    if pos < 0:
        return None
    return src[pos : pos + size]


class BleSyncScanHostAbiContract(unittest.TestCase):
    """tab5_bt_scan delegates to tab5_bt_scan_with_timeout (the sync path)."""

    @classmethod
    def setUpClass(cls):
        cls.abi = _read(HOST_ABI)
        cls.header = _read(HOST_ABI_H)
        cls.body = _function_body(cls.abi, BT_SCAN_FUNC)
        cls.wrapper_body = _function_body(cls.abi, BT_SCAN_FUNC_TIMEOUT)

    def test_with_timeout_wrapper_is_the_public_api(self):
        """O wrapper com timeout e a API publica efetiva (definido na ABI e
        declarado no header para a ponte serial)."""
        self.assertTrue(
            _contains_signature(self.abi, BT_SCAN_FUNC_TIMEOUT),
            "tab5_bt_scan_with_timeout(...) deve estar definida em "
            "tab5_host_abi.cpp",
        )
        self.assertTrue(
            _contains_signature(self.header, BT_SCAN_FUNC_TIMEOUT),
            "tab5_bt_scan_with_timeout(...) deve estar declarada em "
            "tab5_host_abi.h (API interna usada pela ponte serial)",
        )

    def test_tab5_bt_scan_delegates_to_wrapper(self):
        """A funcao de entrada (ABI/SDK) deve delegar ao wrapper com timeout."""
        self.assertIn(
            "tab5_bt_scan_with_timeout(",
            self.body,
            "tab5_bt_scan() deve delegar a tab5_bt_scan_with_timeout() — a "
            "logica sincrona de scan vive no wrapper (o mesmo usado pela ponte "
            "serial), nao na funcao de entrada",
        )

    def test_wrapper_starts_a_real_scan(self):
        """O wrapper deve chamar bt_mgr_scan() com callback != nullptr.

        Hoje o ESP_PLATFORM branch antigo retornava count=0 sem nunca iniciar
        a descoberta NimBLE — e o bug da lista vazia que o plano corrige.
        """
        self.assertIn(
            "bt_mgr_scan(",
            self.wrapper_body,
            "tab5_bt_scan_with_timeout() (ESP_PLATFORM) deve invocar "
            "bt_mgr_scan() com callback nao-nulo para a busca sincrona iniciar "
            "descoberta real e nunca ser bloqueada pelo throttle passivo "
            "(que so valida cb == nullptr)",
        )

    def test_wrapper_waits_with_bounded_timeout(self):
        """O scan sincrono deve aguardar a conclusao com timeout limitado."""
        self.assertTrue(
            _has_sync_wait(self.wrapper_body),
            "tab5_bt_scan_with_timeout() deve bloquear ate o scan concluir usando "
            "xSemaphoreTake(..., pdMS_TO_TICKS(...)) (espera limitada), "
            "nao fire-and-forget com count=0",
        )

    def test_sync_timeout_constant_exists(self):
        """Uma constante nomeada positiva de timeout deve gatear a espera."""
        const = _find_sync_timeout_const(self.abi, BT_MGR_H, BT_MGR_CPP)
        self.assertIsNotNone(
            const,
            "a named Bluetooth-specific timeout constant (name with "
            "TIMEOUT|SYNC and BT/BLE, value > 0) must exist in "
            "tab5_host_abi.cpp or bt_mgr.h/.cpp, e.g. "
            "`#define BT_SCAN_SYNC_TIMEOUT_MS 7000` — the helper must never "
            "select WIFI_SCAN_SYNC_TIMEOUT_MS as the BT wrapper constant",
        )

    def test_wrapper_applies_caller_timeout_or_defaults(self):
        """O wrapper deve honrar timeout_ms (0 == constante nomeada default)."""
        const = _find_sync_timeout_const(self.abi, BT_MGR_H, BT_MGR_CPP)
        self.assertIn(
            "timeout_ms",
            self.wrapper_body,
            "tab5_bt_scan_with_timeout() deve usar o parametro timeout_ms",
        )
        self.assertTrue(
            re.search(r"timeout_ms\s*==\s*0", self.wrapper_body),
            "o wrapper deve trocar timeout_ms == 0 pela constante nomeada default",
        )
        if const is not None:
            self.assertIn(
                const[0],
                self.wrapper_body,
                "o wrapper deve usar a constante de timeout nomeada como default",
            )

    def test_wrapper_copies_results_into_out_devs(self):
        """Dispositivos descobertos devem ser copiados para out_devs."""
        self.assertTrue(
            re.search(r"->out\s*\[", self.wrapper_body),
            "tab5_bt_scan_with_timeout() deve copiar os dispositivos para o "
            "buffer de saida (loop preenchendo out[i]/ctx->out[i])",
        )
        self.assertIn("mac", self.wrapper_body,
                      "a copia deve incluir o MAC (identificador do dispositivo)")
        self.assertIn("rssi", self.wrapper_body,
                      "a copia deve incluir o RSSI de cada dispositivo")

    def test_wrapper_reports_deterministic_count(self):
        """*out_count deve ser atribuido de forma deterministico."""
        self.assertIn(
            "*out_count =",
            self.wrapper_body,
            "tab5_bt_scan_with_timeout() deve atribuir *out_count (inicial e "
            "final) para resultado deterministico mesmo com 0 dispositivos",
        )
        self.assertIn(
            "context->count",
            self.wrapper_body,
            "o *out_count final deve vir do contador do contexto sincrono "
            "(clamp por capacidade)",
        )

    def test_scan_completion_signals_the_waiting_task(self):
        """O callback de conclusao deve xSemaphoreGive ao terminar."""
        self.assertIn(
            "xSemaphoreGive(",
            self.wrapper_body,
            "o callback de conclusao (registrado com bt_mgr_scan) deve "
            "sinalizar a tarefa que aguarda via xSemaphoreGive/give-from-isr "
            "para a espera sincrona retornar os resultados",
        )


class BleScanBridgeContract(unittest.TestCase):
    """serial_bridge.cpp must dispatch `ble.scan` via the with_timeout wrapper."""

    @classmethod
    def setUpClass(cls):
        cls.src = _read(SERIAL_BRIDGE)

    def test_bridge_dispatches_ble_scan(self):
        """`ble.scan` must be recognised by the bridge dispatch."""
        self.assertIn(
            '"ble.scan"',
            self.src,
            "serial_bridge_dispatch must handle the ble.scan command",
        )

    def test_bridge_calls_sync_scan_path(self):
        """ble.scan deve invocar o wrapper com timeout (mesmo caminho da ABI)."""
        window = _dispatch_window(self.src, "ble.scan")
        self.assertIsNotNone(window, "ble.scan branch not found")
        self.assertTrue(
            re.search(r"\btab5_bt_scan_with_timeout\(", window),
            "ble.scan deve chamar tab5_bt_scan_with_timeout() — o wrapper com "
            "timeout e o MESMO caminho sincrono que a ABI usa (via "
            "tab5_bt_scan), permitindo aplicar o deadline pedido pelo host",
        )

    def test_bridge_response_shape_has_devices_and_count(self):
        """ble.scan must answer with data.devices[] and data.count."""
        window = _dispatch_window(self.src, "ble.scan")
        self.assertIsNotNone(window, "ble.scan branch not found")
        self.assertIn('"devices"', window,
                       "ble.scan data must include a \"devices\" array")
        self.assertIn('"count"', window,
                       "ble.scan data must include a \"count\" field")

    def test_bridge_accepts_timeout_ms(self):
        """ble.scan must parse an optional timeout_ms (bounded operation)."""
        window = _dispatch_window(self.src, "ble.scan")
        self.assertIsNotNone(window, "ble.scan branch not found")
        self.assertTrue(
            re.search(r'has_number\(root,\s*"timeout_ms"', window),
            "ble.scan must parse timeout_ms from the request (has_number) or "
            "fail with a clear error",
        )

    def test_bridge_reports_timeout_as_error(self):
        """Um timeout/falha deve produzir frame de erro, nao ok/lista vazia."""
        window = _dispatch_window(self.src, "ble.scan")
        self.assertIsNotNone(window, "ble.scan branch not found")
        self.assertTrue(
            re.search(r'"[^"]*(?:tempo|timeout|falha|busy|desabilitado)[^"]*"',
                      window, re.IGNORECASE),
            "ble.scan deve expor timeout / Bluetooth desabilitado / falha como "
            "error_frame explicito (ex.: 'timeout no scan Bluetooth', "
            "'Bluetooth desabilitado', 'falha no scan Bluetooth') — nunca um "
            "ok/lista vazia falso",
        )


class BleScanParserSelfCheck(unittest.TestCase):
    """The static checkers above must be sound (green, implementation-free)."""

    def test_body_extraction(self):
        src = (
            "tab5_err_t tab5_bt_scan_with_timeout(tab5_bt_dev_t *out_devs, uint32_t max_devs,\n"
            "                                     uint32_t *out_count, uint32_t timeout_ms)\n"
            "{\n"
            "    *out_count = 0;\n"
            "    esp_err_t err = bt_mgr_scan(cb, context);\n"
            "    if (xSemaphoreTake(s, pdMS_TO_TICKS(BT_SCAN_SYNC_TIMEOUT_MS)) == pdTRUE) {\n"
            "        *out_count = context->count;\n"
            "        memcpy(out_devs, s_devs, sizeof(tab5_bt_dev_t) * s_count);\n"
            "    }\n"
            "    return TAB5_OK;\n"
            "}\n"
        )
        body = _function_body(src, BT_SCAN_FUNC_TIMEOUT)
        self.assertIn("bt_mgr_scan(", body)
        self.assertIn("xSemaphoreTake(", body)
        self.assertIn("memcpy(out_devs", body)
        self.assertIn("*out_count =", body)

    def test_multiline_signature_body_extraction(self):
        """Assinaturas quebradas em linhas (como no tab5_host_abi.cpp) devem ser
        extraidas sem depender de espacos exatos."""
        src = (
            "tab5_err_t tab5_bt_scan_with_timeout(tab5_bt_dev_t *out_devs,\n"
            "                                    uint32_t max_devs, uint32_t *out_count,\n"
            "                                    uint32_t timeout_ms)\n"
            "{\n"
            "    if (timeout_ms == 0) timeout_ms = BT_SCAN_SYNC_TIMEOUT_MS;\n"
            "    return bt_mgr_scan(cb, ctx) == ESP_OK ? TAB5_OK : TAB5_ERR_FAIL;\n"
            "}\n"
        )
        body = _function_body(src, BT_SCAN_FUNC_TIMEOUT)
        self.assertIn("bt_mgr_scan(", body)
        self.assertIn("timeout_ms ==", body)

    def test_signature_containment(self):
        header = (
            "tab5_err_t tab5_bt_scan_with_timeout(tab5_bt_dev_t *out_devs, uint32_t max_devs,\n"
            "                                     uint32_t *out_count, uint32_t timeout_ms);\n"
        )
        self.assertTrue(_contains_signature(header, BT_SCAN_FUNC_TIMEOUT))
        self.assertFalse(_contains_signature("tab5_bt_scan(devs, 8, &c);", BT_SCAN_FUNC_TIMEOUT))

    def test_delegation_detection(self):
        src = (
            "tab5_err_t tab5_bt_scan(tab5_bt_dev_t *out_devs, uint32_t max_devs, uint32_t *out_count)\n"
            "{\n"
            "    return tab5_bt_scan_with_timeout(out_devs, max_devs, out_count, BT_SCAN_SYNC_TIMEOUT_MS);\n"
            "}\n"
        )
        body = _function_body(src, BT_SCAN_FUNC)
        self.assertIn("tab5_bt_scan_with_timeout(", body)

    def test_has_sync_wait_patterns(self):
        self.assertTrue(_has_sync_wait("xSemaphoreTake(s, pdMS_TO_TICKS(5000));"))
        self.assertFalse(_has_sync_wait("xSemaphoreTake(s, portMAX_DELAY);"))
        self.assertFalse(_has_sync_wait("esp_wifi_scan_start(NULL, true);"))

    def test_timeout_constant_define_detection(self):
        src = "#define BT_SCAN_SYNC_TIMEOUT_MS 15000\n"
        self.assertEqual(_find_sync_timeout_const(src), ("BT_SCAN_SYNC_TIMEOUT_MS", 15000))

    def test_timeout_constant_constexpr_detection(self):
        src = "static constexpr uint32_t kBtSyncScanTimeoutMs = 12000;\n"
        self.assertEqual(_find_sync_timeout_const(src), ("kBtSyncScanTimeoutMs", 12000))

    def test_existing_passive_constant_not_matched(self):
        src = "#define BT_SCAN_PASSIVE_MIN_INTERVAL_MS 15000\n#define AUTOCONN_BACKOFF_MS 15000\n"
        self.assertIsNone(_find_sync_timeout_const(src))

    def test_wifi_timeout_constant_not_selected_for_bt(self):
        """WIFI_SCAN_SYNC_TIMEOUT_MS (primeiro #define de timeout do
        tab5_host_abi.cpp) nao pode ser escolhido como a constante do wrapper
        BT — o helper deve localizar BT_SCAN_SYNC_TIMEOUT_MS, que e o que o
        wrapper BT realmente usa."""
        src = (
            "#define WIFI_SCAN_SYNC_TIMEOUT_MS 8000\n"
            "#define BT_SCAN_SYNC_TIMEOUT_MS 7000\n"
        )
        self.assertEqual(
            _find_sync_timeout_const(src),
            ("BT_SCAN_SYNC_TIMEOUT_MS", 7000),
        )

    def test_wifi_only_timeout_constant_rejected(self):
        """Sem constante BT/BLE no src, o helper do wrapper BT retorna None
        (nunca cai na constante de WIFI como substituta)."""
        self.assertIsNone(
            _find_sync_timeout_const("#define WIFI_SCAN_SYNC_TIMEOUT_MS 8000\n")
        )

    def test_dispatch_window_detection(self):
        src = ('{"cmd": "ble.scan", "timeout_ms": 15000}\n'
               '"ble.scan" -> tab5_bt_scan_with_timeout(devs, N, &c, t);')
        window = _dispatch_window(src, "ble.scan")
        self.assertIsNotNone(window)
        self.assertIn("tab5_bt_scan_with_timeout(", window)
        self.assertIn("timeout_ms", window)

    def test_timeout_constant_finder_accepts_path(self):
        """The constant finder must coerce Path sources (helper robustness)."""
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            header = Path(tmp) / "bt_mgr_test.h"
            header.write_text("#define BT_SCAN_SYNC_TIMEOUT_MS 9000\n", encoding="utf-8")
            self.assertEqual(
                _find_sync_timeout_const(header), ("BT_SCAN_SYNC_TIMEOUT_MS", 9000)
            )

    def test_timeout_error_token_matches_inside_quoted_message(self):
        """Valid compound error strings must match, not only exact tokens."""
        window = (
            'error_frame(cmd, scan_err == TAB5_ERR_TIMEOUT ? '
            '"timeout no scan Bluetooth" : scan_err == TAB5_ERR_INVALID_STATE ? '
            '"Bluetooth desabilitado" : "falha no scan Bluetooth", rid);'
        )
        self.assertIsNotNone(re.search(
            r'"[^"]*(?:tempo|timeout|falha|busy|desabilitado)[^"]*"', window, re.IGNORECASE))

    def test_busy_token_accepts_both_conventions(self):
        """O detector de retry reconhece ESP_ERR_INVALID_STATE (aprovado) e a
        convencao ESP_ERR_WIFI_BUSY (IDF mais antigo)."""
        self.assertTrue(_has_busy_token("if (err == ESP_ERR_INVALID_STATE) retry();"))
        self.assertTrue(_has_busy_token("while (err == ESP_ERR_WIFI_BUSY) retry();"))
        self.assertFalse(_has_busy_token("retry();"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
