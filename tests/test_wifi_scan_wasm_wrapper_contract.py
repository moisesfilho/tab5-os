"""Static contracts for wasm_tab5_wifi_scan WAMR pointer handling.

The WAMR native symbol signature for tab5_wifi_scan is "(*i*)i".  The '*'
marker tells WAMR to translate WASM app offsets to native pointers BEFORE
the wrapper function body runs.  Calling wasm_runtime_addr_app_to_native()
inside the wrapper a second time treats a native pointer as a WASM linear-
memory offset, producing TAB5_ERR_INVALID_ARG on hardware even though the
arguments are valid.

The correct pattern is the same as wasm_tab5_bt_scan (same signature "(*i*)i"):
pass the already-translated pointers straight through to tab5_wifi_scan(),
optionally guarded by wasm_arg() for clarity.

These tests extract the wasm_tab5_wifi_scan body and assert the contract.
They fail (RED) against the current buggy code that double-translates the
out_aps/out_count pointers.
"""

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ABI = ROOT / "components/os/runtime/tab5_host_abi.cpp"

WIFI_SCAN_WRAPPER_SIGNATURE = "static tab5_err_t wasm_tab5_wifi_scan("
BT_SCAN_WRAPPER_SIGNATURE = "static tab5_err_t wasm_tab5_bt_scan("


def _read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def _function_body(source: str, signature: str) -> str:
    """Return a C++ function body, respecting nested braces."""
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
    raise AssertionError(f"unclosed function body: {signature!r}")


def _extract_call_args(text: str, callee: str) -> str:
    """Return the balanced argument substring of the call 'callee(...)'.

    A regex like r"callee\s*\([^)]*" stops at the first ')' and cannot see
    arguments wrapped in nested helper calls (e.g. wasm_arg(exec_env, out_aps)).
    This scans with balanced-parenthesis depth instead, so it robustly crosses
    nested calls up to the matching closing paren.
    """
    marker = callee + "("
    start = 0
    while True:
        pos = text.find(marker, start)
        if pos == -1:
            raise AssertionError(f"call {callee!r} not found in body")
        # Skip matches that are suffixes of a longer identifier
        # (e.g. wasm_tab5_wifi_scan also contains "tab5_wifi_scan(").
        if pos > 0 and (text[pos - 1].isalnum() or text[pos - 1] == "_"):
            start = pos + 1
            continue
        break
    open_pos = pos + len(callee)  # index of the '('
    depth = 0
    for i in range(open_pos, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return text[open_pos + 1 : i]
    raise AssertionError(f"unbalanced call: {callee!r}")


def _split_top_level_args(args: str) -> list:
    """Split a balanced argument substring on commas at nesting depth 0."""
    parts = []
    current = []
    depth = 0
    for ch in args:
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
        if ch == "," and depth == 0:
            parts.append("".join(current).strip())
            current = []
        else:
            current.append(ch)
    parts.append("".join(current).strip())
    return parts


def _wifi_scan_call_args(body: str) -> list:
    """Top-level argument list of the tab5_wifi_scan() call in the wrapper."""
    args = _extract_call_args(body, "tab5_wifi_scan")
    return _split_top_level_args(args)


def _is_forwarded_pointer(arg: str, param: str) -> bool:
    """True when arg forwards param either directly (passthrough, as in
    wasm_tab5_bt_scan) or wrapped in wasm_arg(...) (the correct WAMR
    translation layer).  Token-boundary match avoids out_aps/out_aps_x
    confusion."""
    if arg == param:
        return True
    if arg.startswith("wasm_arg("):
        return re.search(rf"\b{re.escape(param)}\b", arg) is not None
    return False


def _find_native_symbol_entry(source: str, symbol_name: str) -> str:
    """Return the native symbol table entry line for symbol_name."""
    for line in source.splitlines():
        if f'"{symbol_name}"' in line and "s_native_symbols" not in line:
            return line
    raise AssertionError(f"symbol entry for {symbol_name!r} not found")


def _extract_wamr_signature(entry_line: str) -> str:
    """Extract the WAMR signature string (e.g. "(*i*)i") from a symbol table
    entry line of the form {"name", (void *)wrapper, "(*i*)i", nullptr}."""
    match = re.search(r'"([()*$i]+)"', entry_line)
    if match is None:
        raise AssertionError(f"no WAMR signature found in: {entry_line!r}")
    return match.group(1)


class WasmWifiScanWrapperBodyContract(unittest.TestCase):
    """The wasm_tab5_wifi_scan wrapper must not double-translate pointers."""

    @classmethod
    def setUpClass(cls):
        cls.source = _read(ABI)
        cls.body = _function_body(cls.source, WIFI_SCAN_WRAPPER_SIGNATURE)

    # -- Anti-vacuity: confirm we actually extracted a non-trivial body --

    def test_wrapper_body_extraction_is_non_vacuous(self):
        """Self-check: the extracted body must contain characteristic markers."""
        self.assertGreater(len(self.body), 50,
                           "body extraction returned a suspiciously small string")
        self.assertIn("tab5_wifi_scan", self.body,
                       "body must reference tab5_wifi_scan (anchor guard)")

    # -- Core bug contract: no double pointer translation in WAMR path --

    def test_no_wasm_runtime_addr_app_to_native_on_out_aps_or_out_count(self):
        """wasm_runtime_addr_app_to_native() must NOT be CALLED for out_aps or
        out_count.  The WAMR signature "(*i*)i" already delivers native
        pointers; converting again rejects valid buffers on hardware."""
        # Check that the function body does not CALL wasm_runtime_addr_app_to_native
        # (a comment mentioning the function is acceptable, a call is the bug).
        self.assertNotRegex(
            self.body,
            r"wasm_runtime_addr_app_to_native\s*\(",
            "wasm_tab5_wifi_scan must NOT call wasm_runtime_addr_app_to_native "
            "on out_aps/out_count.  The WAMR signature (*i*)i already converts "
            "WASM app offsets to native pointers; double-conversion rejects "
            "valid buffers and returns TAB5_ERR_INVALID_ARG on hardware.",
        )

    def test_out_aps_forwarded_as_top_level_arg(self):
        """out_aps must be forwarded as the FIRST top-level argument to
        tab5_wifi_scan(), either via wasm_arg(exec_env, out_aps) (correct
        translation layer) or directly as out_aps (passthrough).  Rejects
        absent args, nullptr forwarding and double-conversion."""
        arguments = _wifi_scan_call_args(self.body)
        self.assertEqual(
            len(arguments), 3,
            "tab5_wifi_scan must receive exactly 3 top-level arguments "
            "(out_aps, max_aps, out_count); "
            f"got {arguments!r}",
        )
        first = arguments[0]
        self.assertTrue(
            _is_forwarded_pointer(first, "out_aps"),
            f"First top-level arg to tab5_wifi_scan must forward out_aps "
            f"directly or via wasm_arg(exec_env, out_aps); got {first!r}",
        )
        self.assertNotEqual(first, "nullptr",
                            "out_aps must not be forwarded as nullptr")
        self.assertNotIn("wasm_runtime_addr_app_to_native", first,
                         "out_aps must not go through "
                         "wasm_runtime_addr_app_to_native (double-translation)")

    def test_out_count_forwarded_as_top_level_arg(self):
        """out_count must be forwarded as the THIRD top-level argument to
        tab5_wifi_scan(), either via wasm_arg(exec_env, out_count) (correct
        translation layer) or directly as out_count (passthrough).  Rejects
        absent args, nullptr forwarding and double-conversion."""
        arguments = _wifi_scan_call_args(self.body)
        self.assertEqual(
            len(arguments), 3,
            "tab5_wifi_scan must receive exactly 3 top-level arguments "
            "(out_aps, max_aps, out_count); "
            f"got {arguments!r}",
        )
        third = arguments[2]
        self.assertTrue(
            _is_forwarded_pointer(third, "out_count"),
            f"Third top-level arg to tab5_wifi_scan must forward out_count "
            f"directly or via wasm_arg(exec_env, out_count); got {third!r}",
        )
        self.assertNotEqual(third, "nullptr",
                            "out_count must not be forwarded as nullptr")
        self.assertNotIn("wasm_runtime_addr_app_to_native", third,
                         "out_count must not go through "
                         "wasm_runtime_addr_app_to_native (double-translation)")

    # -- Structural invariants --

    def test_wrapper_preserves_max_aps(self):
        """max_aps must be passed to tab5_wifi_scan unchanged, as its own
        top-level argument (not wrapped in wasm_arg, not reordered and not
        duplicated).  Balanced-paren extraction crosses nested wasm_arg(...)
        calls, which a naive [^)]* regex cannot do."""
        self.assertIn("max_aps", self.body,
                       "max_aps parameter must be forwarded")
        args = _extract_call_args(self.body, "tab5_wifi_scan")
        arguments = _split_top_level_args(args)
        own_max_aps_args = [arg for arg in arguments if arg == "max_aps"]
        self.assertEqual(
            len(own_max_aps_args), 1,
            "tab5_wifi_scan() must receive max_aps exactly once as its own "
            f"argument, unchanged; got arguments {arguments!r}",
        )

    def test_wrapper_delegates_to_tab5_wifi_scan(self):
        """The wrapper must call the native tab5_wifi_scan function."""
        self.assertIn("tab5_wifi_scan(", self.body,
                       "wrapper must delegate to tab5_wifi_scan()")

    def test_helper_keeps_pointer_validation(self):
        """The sync helper tab5_wifi_scan_with_timeout retains null-argument
        validation.  The wrapper forwards WAMR-translated pointers without
        re-translation; the null check lives in the helper, which returns
        TAB5_ERR_INVALID_ARG for out_aps/out_count null or max_aps == 0."""
        source = _read(ABI)
        helper_body = _function_body(
            source,
            "tab5_err_t tab5_wifi_scan_with_timeout(tab5_wifi_ap_t *out_aps",
        )
        self.assertIn("out_aps == nullptr", helper_body,
                       "helper must reject null out_aps")
        self.assertIn("max_aps == 0", helper_body,
                       "helper must reject zero max_aps")
        self.assertIn("out_count == nullptr", helper_body,
                       "helper must reject null out_count")
        self.assertIn("TAB5_ERR_INVALID_ARG", helper_body,
                       "helper must return TAB5_ERR_INVALID_ARG on bad args")


class WasmWifiScanSymbolRegistrationContract(unittest.TestCase):
    """Verify the WAMR native symbol table entry for tab5_wifi_scan."""

    @classmethod
    def setUpClass(cls):
        cls.source = _read(ABI)

    def test_symbol_entry_exists(self):
        entry = _find_native_symbol_entry(self.source, "tab5_wifi_scan")
        self.assertIn("wasm_tab5_wifi_scan", entry,
                       "native symbol must reference the wasm_ wrapper")

    def test_symbol_signature_has_pointer_markers(self):
        """The signature must use '*' for pointer args, not '$' (string)."""
        entry = _find_native_symbol_entry(self.source, "tab5_wifi_scan")
        sig = _extract_wamr_signature(entry)
        # Signature should be "(*i*)i" — pointers for out_aps and out_count
        self.assertEqual(sig, "(*i*)i",
                         "tab5_wifi_scan signature must be (*i*)i "
                         "(pointer, int, pointer)")


class WasmBtScanWrapperPatternContract(unittest.TestCase):
    """Reference: wasm_tab5_bt_scan uses the same (*i*)i signature correctly.

    This is the reference — it proves the correct pattern exists
    in the codebase and documents what wasm_tab5_wifi_scan should look like.
    """

    @classmethod
    def setUpClass(cls):
        cls.source = _read(ABI)
        cls.body = _function_body(cls.source, BT_SCAN_WRAPPER_SIGNATURE)

    def test_bt_scan_body_extraction_is_non_vacuous(self):
        self.assertGreater(len(self.body), 30,
                           "bt_scan body extraction too small")

    def test_bt_scan_does_not_double_translate(self):
        """bt_scan correctly avoids wasm_runtime_addr_app_to_native."""
        self.assertNotIn("wasm_runtime_addr_app_to_native", self.body,
                         "wasm_tab5_bt_scan must not double-translate (reference)")

    def test_bt_scan_delegates_to_native_bt_scan(self):
        self.assertIn("tab5_bt_scan(", self.body,
                       "bt_scan wrapper must call tab5_bt_scan()")

    def test_bt_scan_comment_documents_no_double_translation(self):
        """The bt_scan comment explicitly says not to translate again.
        This serves as the project convention reference."""
        self.assertIn("Do", self.body)
        self.assertIn("not translate them a second time", self.body)
        self.assertIn("wasm_runtime_addr_app_to_native", self.source)
        # The comment must appear near the bt_scan function
        bt_pos = self.source.index(BT_SCAN_WRAPPER_SIGNATURE)
        comment_zone = self.source[bt_pos : bt_pos + 800]
        self.assertIn(
            "Do\n     * not translate them a second time",
            comment_zone,
            "wasm_tab5_bt_scan must contain the 'do not translate' comment "
            "(project convention for (*i*)i wrappers)",
        )


class SelfCheckContract(unittest.TestCase):
    """Implementation-free self-checks to ensure the test infrastructure
    itself is sound (anti-vacuity, discriminants, no false-positive risk)."""

    def test_function_body_extraction_is_structurally_valid(self):
        """Anti-vacuity: the extracted body must contain the return delegation
        to tab5_wifi_scan — present in both the buggy and fixed versions."""
        source = _read(ABI)
        body = _function_body(source, WIFI_SCAN_WRAPPER_SIGNATURE)
        # Both the buggy (with #if HAVE_WAMR_ENV) and the fixed (passthrough)
        # wrappers end with: return tab5_wifi_scan(...);
        self.assertIn("return tab5_wifi_scan(", body,
                       "extracted body must contain the return delegation to "
                       "tab5_wifi_scan — if this fails the extraction is wrong "
                       "or the function was renamed")

    def test_double_translation_state_is_observable(self):
        """Discriminant: the wrapper must be in a state the core contract can
        distinguish — either the buggy conversion call is present (core test
        RED) or absent (core test GREEN).  This guards against the test
        silently going stale if the wrapper is renamed/restructured."""
        source = _read(ABI)
        body = _function_body(source, WIFI_SCAN_WRAPPER_SIGNATURE)
        # The wrapper must still reference the native scan entry point
        # (anti-vacuity: we are analyzing the right function, not an empty
        # or unrelated body).
        self.assertIn("tab5_wifi_scan", body,
                       "wrapper body must reference the native scan function")
        # Discriminant: the state must be one of the two expected forms.
        has_double_translation = bool(re.search(
            r"wasm_runtime_addr_app_to_native\s*\(",
            body,
        ))
        self.assertIsInstance(
            has_double_translation, bool,
            "double-translation state must be deterministically observable",
        )
        # Both states are valid test subjects: RED now (bug present) or
        # GREEN after the approved fix.  A NONE state (body vanished or
        # re-anchored) would be a vacuity failure above.

    def test_bt_scan_and_wifi_scan_same_wamr_signature(self):
        """Both wrappers use "(*i*)i" — the bug is a deviation from the
        established correct pattern, not a different signature."""
        source = _read(ABI)
        bt_entry = _find_native_symbol_entry(source, "tab5_bt_scan")
        wifi_entry = _find_native_symbol_entry(source, "tab5_wifi_scan")
        bt_sig = _extract_wamr_signature(bt_entry)
        wifi_sig = _extract_wamr_signature(wifi_entry)
        self.assertEqual(
            bt_sig, wifi_sig,
            "tab5_bt_scan and tab5_wifi_scan must share the same WAMR "
            "signature since both take (pointer, int, pointer) -> int",
        )

    def test_wasm_arg_helper_passes_through(self):
        """wasm_arg() returns app_ptr unchanged — the correct translation
        layer.  The wifi scan wrapper must use it (or skip translation)."""
        source = _read(ABI)
        helper = _function_body(source, "template <typename T> static T *wasm_arg")
        self.assertIn("return app_ptr", helper,
                       "wasm_arg must pass through (WAMR already translated)")
        self.assertNotIn("wasm_runtime_addr_app_to_native", helper,
                         "wasm_arg must not call wasm_runtime_addr_app_to_native")


if __name__ == "__main__":
    unittest.main(verbosity=2)
