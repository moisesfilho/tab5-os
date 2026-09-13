"""Static acceptance contracts for the BLE passive rescan loop fix.

TDD red phase: fix the passive BLE rescan loop in
components/os/core/bt_mgr.cpp.  Today the background auto-reconnection
"listening" scan (bt_mgr_scan(nullptr, nullptr)) is restarted immediately
after every scan completes (BLE_GAP_EVENT_DISC_COMPLETE, connect failure,
disconnect), so the radio is almost never idle and the same restart messages
are logged every ~5 s (scan window 5000 ms, watchdog 5500 ms).

Approved plan (confirmed objective) for the passive rescan loop:

  1. Minimum-interval constant  — a named constant (ms) for the minimum
     interval between PASSIVE rescans whose value is strictly greater than
     the scan duration AND the scan watchdog, so the controller gets an idle
     gap between background scans.
  2. Gate applied only to cb==nullptr  — the throttle is applied ONLY to
     passive scans (callback == nullptr).  Manual scans from the Bluetooth
     app (callback != nullptr) are never rate-limited.
  3. Manual scans never blocked  — a manual scan must always reach
     ble_gap_disc() regardless of when the last passive scan happened.
  4. Repetitive logs downgraded, summary kept in INFO  — the per-restart
     messages ("Reiniciando escuta passiva ...") drop to ESP_LOGD while the
     concise per-scan summary ("Scan NimBLE concluido (N dispositivos)")
     remains at ESP_LOGI.

Static source contracts (same style as the other tests/ contracts): they fail
now (red) and must turn green with the approved implementation.  The parser
self-checks (PassiveRescanParserSelfCheck) stay green.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BT_MGR_SRC = ROOT / "components/os/core/bt_mgr.cpp"
BT_MGR_H = ROOT / "components/os/core/bt_mgr.h"

SCAN_FUNC = "esp_err_t bt_mgr_scan(bt_scan_cb_t cb, void *ctx)"

# Time sources acceptable for "elapsed since last passive scan".
TIME_FUNCS = ("xTaskGetTickCount", "xTaskGetTickCountFromISR", "esp_timer_get_time", "millis")

# Names that suggest passive-scan throttling (gate helper / condition).
HELPER_NAME_RE = re.compile(r"\b\w*(?:passive|throttle|rescan|rate_?limit|gate)\w*", re.IGNORECASE)

CB_NULL_RE = re.compile(r"\bcb\s*==\s*(?:nullptr|NULL)\b")
CB_NOT_NULL_RE = re.compile(r"\bcb\s*!=\s*(?:nullptr|NULL)\b")

SCAN_ISH = ("SCAN", "PASSIVE", "RESCAN", "LISTEN")
MIN_ISH = ("INTERVAL", "MIN", "PERIOD", "COOLDOWN")


def _read(p: Path) -> str:
    return p.read_text(encoding="utf-8")


def _strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return text


def _function_body(source: str, signature: str) -> str:
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


def _if_conditions_with_pos(body: str) -> list[tuple[str, int, int]]:
    """Every `if (...)` condition with its (start, end) offsets in body."""
    body = _strip_comments(body)
    out: list[tuple[str, int, int]] = []
    for m in re.finditer(r"\bif\s*\(", body):
        depth = 0
        for i in range(m.end() - 1, len(body)):
            ch = body[i]
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
                if depth == 0:
                    out.append((body[m.end() : i], m.start(), i))
                    break
    return out


def _looks_like_min_interval_name(name: str) -> bool:
    upper = name.upper()
    return any(k in upper for k in SCAN_ISH) and any(k in upper for k in MIN_ISH)


def _find_min_interval(src: str) -> tuple[str, int] | None:
    """Returns (name, ms) of the passive-rescan min-interval constant."""
    for m in re.finditer(r"#define\s+([A-Z][A-Z0-9_]*)\s+\(?(\d+)\)?", src):
        name, val = m.group(1), int(m.group(2))
        if _looks_like_min_interval_name(name) and val > 0:
            return name, val
    for m in re.finditer(
        r"(?:static\s+)?(?:constexpr|const)\s+"
        r"(?:uint32_t|uint64_t|uint16_t|int|unsigned(?:\s+int)?)\s+"
        r"(\w+)\s*=\s*(\d+)\s*;",
        src,
    ):
        name, val = m.group(1), int(m.group(2))
        if _looks_like_min_interval_name(name) and val > 0:
            return name, val
    return None


def _find_watchdog_ms(src: str) -> int | None:
    m = re.search(r'xTimerCreate\(\s*"bt[^"]*",\s*pdMS_TO_TICKS\((\d+)\)', src)
    if m:
        return int(m.group(1))
    m = re.search(r"xTimerCreate\([^,]+,\s*pdMS_TO_TICKS\((\d+)\)", src)
    return int(m.group(1)) if m else None


def _find_scan_duration_ms(src: str) -> int | None:
    m = re.search(r"ble_gap_disc\(\s*[^,]+,\s*(\d+)", src)
    return int(m.group(1)) if m else None


def _strictly_greater(min_ms: int, watchdog_ms: int, scan_ms: int) -> bool:
    """The passive minimum interval must beat BOTH scan window and watchdog."""
    return min_ms > max(watchdog_ms, scan_ms)


def _gate_form1(scan_body: str, const_name: str | None) -> bool:
    """Throttle as `if (cb == nullptr && <elapsed> < CONST) return;` (or a
    precomputed `bool due = ...` in the statement right before the gate)."""
    for cond, cstart, _ in _if_conditions_with_pos(scan_body):
        has_cb = bool(CB_NULL_RE.search(cond) or CB_NOT_NULL_RE.search(cond))
        if not has_cb:
            continue
        has_time = any(fn in cond for fn in TIME_FUNCS)
        has_ing = (const_name is not None and const_name in cond) or bool(HELPER_NAME_RE.search(cond))
        if has_time and has_ing:
            return True
        window = _strip_comments(scan_body[max(0, cstart - 240) : cstart])
        win_time = any(fn in window for fn in TIME_FUNCS)
        win_ing = (const_name is not None and const_name in window) or bool(HELPER_NAME_RE.search(window))
        if win_time and win_ing:
            return True
    return False


def _gate_form3(src: str, scan_body: str, const_name: str | None) -> bool:
    """Throttle via a static helper that is invoked with `cb` and exempts
    manual scans (cb != nullptr returns false before the time check)."""
    for m in re.finditer(r"static\s+(?:inline\s+)?bool\s+(\w{2,})\s*\(", src):
        name = m.group(1)
        if not HELPER_NAME_RE.fullmatch(name):
            continue
        sig = src[m.start() : m.end() - 1]
        body = _function_body(src, sig)
        exempts_manual = bool(CB_NULL_RE.search(body) or CB_NOT_NULL_RE.search(body))
        uses_time = any(fn in body for fn in TIME_FUNCS)
        uses_const = const_name is not None and const_name in body
        called_with_cb = re.search(rf"\b{re.escape(name)}\s*\(\s*cb\b", scan_body) is not None
        if exempts_manual and uses_time and uses_const and called_with_cb:
            return True
    return False


def _unconditional_time_gates(scan_body: str, const_name: str | None) -> list[str]:
    """Time/constant-based conditions in bt_mgr_scan that do NOT involve the
    callback.  An unconditional throttle would block manual scans too, which
    the plan forbids."""
    bad: list[str] = []
    for cond, _, _ in _if_conditions_with_pos(scan_body):
        has_time = any(fn in cond for fn in TIME_FUNCS)
        has_const = const_name is not None and const_name in cond
        if not (has_time or has_const):
            continue
        mentions_cb = re.search(r"\bcb\b", cond) is not None
        calls_helper = bool(HELPER_NAME_RE.search(cond))
        if not mentions_cb and not calls_helper:
            bad.append(cond)
    return bad


def _count_passive_rescan_sites(src: str) -> int:
    return len(re.findall(r"bt_mgr_scan\(\s*(?:nullptr|NULL)\s*,\s*(?:nullptr|NULL)\s*\)", src))


def _log_macro_before(src: str, pos: int, back: int = 120) -> str | None:
    window = src[max(0, pos - back) : pos]
    hits = re.findall(r"\b(ESP_LOG[A-Z])\s*\(", window)
    return hits[-1] if hits else None


def _log_levels_for_message(src: str, pattern: str) -> list[str]:
    return [_log_macro_before(src, m.start()) for m in re.finditer(pattern, src)]


class PassiveRescanMinIntervalContract(unittest.TestCase):
    """The passive rescan must be rate-limited above the scan/watchdog window."""

    @classmethod
    def setUpClass(cls):
        cls.src = _read(BT_MGR_SRC)
        cls.header = _read(BT_MGR_H)
        cls.const = _find_min_interval(cls.header) or _find_min_interval(cls.src)
        cls.const_name = cls.const[0] if cls.const else None
        cls.const_ms = cls.const[1] if cls.const else None
        cls.scan_body = _function_body(cls.src, SCAN_FUNC)

    def test_bt_mgr_scan_exists(self):
        """Anchor: bt_mgr_scan must exist (non-vacuous guard)."""
        self.assertIn(SCAN_FUNC, self.src, "bt_mgr_scan must exist")

    def test_min_interval_constant_exists(self):
        """There must be a named constant for the passive-rescan minimum."""
        self.assertIsNotNone(
            self.const,
            "bt_mgr (h/cpp) must define a passive-rescan minimum interval "
            "constant, e.g. `#define BT_SCAN_PASSIVE_MIN_INTERVAL_MS <ms>` or "
            "a `constexpr` with SCAN/PASSIVE/RESCAN + INTERVAL/MIN/PERIOD in "
            "the name",
        )

    def test_constant_strictly_greater_than_scan_and_watchdog(self):
        """The interval constant must be > scan duration AND > watchdog.

        A passive scan runs ble_gap_disc() for 5000 ms with a 5500 ms
        watchdog; restarting before that window the controller is never idle.
        """
        watchdog_ms = _find_watchdog_ms(self.src)
        scan_ms = _find_scan_duration_ms(self.src)
        self.assertIsNotNone(watchdog_ms, "scan watchdog duration must be readable")
        self.assertIsNotNone(scan_ms, "ble_gap_disc duration must be readable")
        self.assertIsNotNone(self.const_ms, "min-interval constant must exist")
        self.assertTrue(
            _strictly_greater(self.const_ms, watchdog_ms, scan_ms),
            f"min-interval {self.const_ms} ms must be strictly greater than "
            f"max(scan={scan_ms} ms, watchdog={watchdog_ms} ms) so the radio "
            "gets an idle gap between passive rescans",
        )

    def test_gate_guards_only_null_callback_scans(self):
        """The throttle must be keyed to cb == nullptr (passive scans only).

        Accepted forms: an inline `if (cb == nullptr && <elapsed> < CONST)`
        early return inside bt_mgr_scan, or a static bool helper invoked with
        ``cb`` that exempts cb != nullptr before the time check.
        """
        form1 = _gate_form1(self.scan_body, self.const_name)
        form3 = _gate_form3(self.src, self.scan_body, self.const_name)
        self.assertTrue(
            form1 or form3,
            "bt_mgr_scan must throttle ONLY passive scans (cb == nullptr): "
            "an early-return gate must pair a `cb == nullptr` (or cb != "
            "nullptr exemption) with the min-interval constant/time check, "
            "either inline or through a static helper that receives cb",
        )

    def test_no_unconditional_time_throttle(self):
        """No time/constant-based early return may run without the cb guard.

        An unconditional time gate would block manual scans (cb != nullptr),
        which the plan explicitly forbids.
        """
        bad = _unconditional_time_gates(self.scan_body, self.const_name)
        self.assertEqual(
            bad, [],
            "time/interval throttling without a callback guard would block "
            "manual scans; every throttle must be conditioned on "
            "cb == nullptr (or exempt cb != nullptr)",
        )

    def test_manual_scan_still_starts_discovery(self):
        """Manual scans must still reach the discovery start (never dropped)."""
        self.assertIn(
            "ble_gap_disc(", self.scan_body,
            "bt_mgr_scan must still start ble_gap_disc() after the gate",
        )

    def test_passive_rescan_sites_preserved(self):
        """The auto-restart sites must remain (auto-reconnection preserved).

        The fix rate-limits the rescan loop; it must not delete the passive
        listening entirely, or auto-reconnection after disconnect/scan-fail
        would stop working.
        """
        sites = _count_passive_rescan_sites(self.src)
        self.assertGreaterEqual(
            sites, 3,
            "expected >= 3 `bt_mgr_scan(nullptr, nullptr)` auto-restart sites "
            f"(DISC_COMPLETE, connect failure, disconnect); found {sites}",
        )


class PassiveRescanLogContract(unittest.TestCase):
    """Repetitive restart logs drop to DEBUG; the scan summary stays INFO."""

    @classmethod
    def setUpClass(cls):
        cls.src = _read(BT_MGR_SRC)

    def test_passive_restart_logs_downgraded_to_debug(self):
        """'Reiniciando escuta passiva ...' must be ESP_LOGD, not ESP_LOGI."""
        levels = _log_levels_for_message(self.src, r"Reiniciando escuta passiva")
        self.assertTrue(
            levels,
            "expected 'Reiniciando escuta passiva' log messages to exist",
        )
        offenders = [lv for lv in levels if lv != "ESP_LOGD"]
        self.assertEqual(
            offenders, [],
            "every 'Reiniciando escuta passiva' message must be ESP_LOGD "
            "(downgraded from ESP_LOGI): these repeat on every rescan and "
            "must not spam the INFO level",
        )

    def test_scan_completed_summary_stays_info(self):
        """'Scan NimBLE concluido (N dispositivos)' must remain at ESP_LOGI.

        This is the concise per-scan summary the plan keeps visible in INFO.
        """
        levels = _log_levels_for_message(self.src, r"Scan NimBLE concluido")
        self.assertTrue(levels, "missing 'Scan NimBLE concluido' summary log")
        offenders = [lv for lv in levels if lv != "ESP_LOGI"]
        self.assertEqual(
            offenders, [],
            "'Scan NimBLE concluido (N dispositivos)' must remain ESP_LOGI "
            "as the per-scan summary (only the repetitive restart messages "
            "are downgraded)",
        )


class PassiveRescanParserSelfCheck(unittest.TestCase):
    """The static checker itself must be sound (green, implementation-free)."""

    def test_watchdog_duration_detection(self):
        src = 's_wd = xTimerCreate("bt_sc_wd", pdMS_TO_TICKS(5500), pdFALSE, nullptr, cb);'
        self.assertEqual(_find_watchdog_ms(src), 5500)

    def test_scan_duration_detection(self):
        src = "ble_gap_disc(own_addr_type, 5000, &p, cb, nullptr);"
        self.assertEqual(_find_scan_duration_ms(src), 5000)

    def test_min_interval_define_detection(self):
        src = "#define BT_SCAN_PASSIVE_MIN_INTERVAL_MS 15000\n"
        self.assertEqual(_find_min_interval(src), ("BT_SCAN_PASSIVE_MIN_INTERVAL_MS", 15000))

    def test_min_interval_constexpr_detection(self):
        src = "static constexpr uint32_t kPassiveRescanMinMs = 10000;\n"
        self.assertEqual(_find_min_interval(src), ("kPassiveRescanMinMs", 10000))

    def test_strict_greater_than_both_values(self):
        self.assertTrue(_strictly_greater(10000, 5000, 5500))
        self.assertFalse(_strictly_greater(5500, 5000, 5500))
        self.assertFalse(_strictly_greater(5000, 5000, 5500))

    def test_if_conditions_detection(self):
        body = (
            "if (cb == nullptr && (xTaskGetTickCount() - s_last) < "
            "pdMS_TO_TICKS(BT_SCAN_PASSIVE_MIN_INTERVAL_MS)) { return ESP_OK; }"
        )
        conds = _if_conditions_with_pos(body)
        self.assertEqual(len(conds), 1)
        self.assertIn("cb == nullptr", conds[0][0])
        self.assertIn("xTaskGetTickCount", conds[0][0])

    def test_gate_form1_detection(self):
        body = (
            "if (cb == nullptr && s_last != 0 && (xTaskGetTickCount() - s_last) < "
            "pdMS_TO_TICKS(BT_SCAN_PASSIVE_MIN_INTERVAL_MS)) { return ESP_OK; }\n"
            "ble_gap_disc(own_addr_type, 5000, &p, cb, nullptr);"
        )
        self.assertTrue(_gate_form1(body, "BT_SCAN_PASSIVE_MIN_INTERVAL_MS"))

    def test_gate_form3_detection(self):
        src = (
            "static bool passive_rescan_throttled(bt_scan_cb_t cb)\n{\n"
            "    if (cb != nullptr) { return false; }\n"
            "    return (xTaskGetTickCount() - s_last) < "
            "pdMS_TO_TICKS(BT_SCAN_PASSIVE_MIN_INTERVAL_MS);\n}\n"
            "esp_err_t bt_mgr_scan(bt_scan_cb_t cb, void *ctx)\n{\n"
            "    if (passive_rescan_throttled(cb)) { return ESP_OK; }\n"
            "    ble_gap_disc(own_addr_type, 5000, &p, cb, nullptr);\n}"
        )
        scan_body = _function_body(src, SCAN_FUNC)
        self.assertTrue(_gate_form3(src, scan_body, "BT_SCAN_PASSIVE_MIN_INTERVAL_MS"))
        self.assertFalse(_gate_form3(src, scan_body, None))

    def test_log_level_detection(self):
        src = (
            'ESP_LOGI(TAG, "Scan NimBLE concluido (%d)", n);\n'
            'ESP_LOGD(TAG, "Reiniciando escuta passiva apos desconexao...");\n'
        )
        self.assertEqual(_log_levels_for_message(src, r"Scan NimBLE concluido"), ["ESP_LOGI"])
        self.assertEqual(_log_levels_for_message(src, r"Reiniciando escuta passiva"), ["ESP_LOGD"])

    def test_restart_log_downgrade_detection(self):
        src = (
            'ESP_LOGI(TAG, "Reiniciando escuta passiva em segundo plano...");\n'
        )
        self.assertEqual(_log_levels_for_message(src, r"Reiniciando escuta passiva"), ["ESP_LOGI"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
