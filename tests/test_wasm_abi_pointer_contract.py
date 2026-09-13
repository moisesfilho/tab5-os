"""Contracts for WAMR pointer and string argument handling."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]
ABI = ROOT / "components/os/runtime/tab5_host_abi.cpp"


def function_body(source, signature):
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace:index + 1]
    raise AssertionError(f"unterminated function: {signature}")


def test_wasm_pointer_helper_does_not_translate_wamr_arguments_again():
    source = ABI.read_text()
    body = function_body(source, "template <typename T> static T *wasm_arg")

    assert "wasm_runtime_addr_app_to_native" not in body
    assert "return app_ptr;" in body


def test_wasm_string_helper_forwards_native_pointer():
    source = ABI.read_text()
    body = function_body(source, "static const char *wasm_string")

    assert "wasm_runtime_addr_app_to_native" not in body
    assert re.search(r"return\s+app_ptr\s*;", body)


def test_ui_string_imports_use_wamr_string_signature():
    source = ABI.read_text()

    for symbol in (
        "tab5_ui_label_create",
        "tab5_ui_label_set_text",
        "tab5_ui_btn_create",
        "tab5_ui_list_add_btn",
    ):
        match = re.search(
            rf'\{{"{symbol}".*?,\s*"([^\"]+)"', source
        )
        assert match, f"missing import registration: {symbol}"
        assert "$" in match.group(1), f"{symbol} must declare a string argument"
