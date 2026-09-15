"""Contracts for the Notes WASM app initialization and action buttons."""

from pathlib import Path


NOTAS = Path(__file__).resolve().parents[2] / "tab5-app-notas" / "src" / "main.c"
NOTAS_BUILD = NOTAS.parents[1] / "tools" / "build.sh"
DISPATCHER = Path(__file__).resolve().parents[1] / "components" / "os" / "runtime" / "tab5_wasm_dispatcher.cpp"


def test_notas_initializes_ui_from_wasm_entrypoint():
    source = NOTAS.read_text()

    assert "app_init();" in source
    assert "tab5_ui_app_bar_add_action_button(\"LV_SYMBOL_SAVE\"" in source
    assert "s_save_button =" in source


def test_notas_dispatches_wasm_action_button_events():
    source = NOTAS.read_text()

    assert "TAB5_APP_EXPORT void tab5_app_on_ui_event" in source
    assert "if (obj == s_save_button)" in source
    assert "on_save_note(NULL);" in source
    # Após a migração do ABI para tab5_ui_textarea_copy_text, o conteúdo a
    # gravar é lido com buffer cativo (nunca strlen sobre memória do app) e o
    # tamanho guardado é o retornado pela cópia (content_len).
    assert "tab5_ui_textarea_copy_text(ta, content, sizeof(content))" in source
    assert "tab5_storage_write_file(target, content, (size_t)content_len)" in source
    assert "content_len < 0 || (uint32_t)content_len >= sizeof(content)" in source
    assert 'tab5_storage_mkdir("/sdcard/notas")' in source
    assert '"/sdcard/notas/nota-%s.txt"' in source
    assert '"Nota salva: %s"' in source


def test_notas_reads_open_files_through_storage_host_api():
    source = NOTAS.read_text()

    assert "tab5_storage_read_file(" in source
    assert "fopen(" not in source


def test_notas_individual_build_exports_ui_callback():
    build = NOTAS_BUILD.read_text()

    assert "--export=tab5_app_on_ui_event" in build
    assert "--export=on_ui_event" in build
    assert "--export=on_open_file" in build


def test_notas_exports_open_file_callback():
    source = NOTAS.read_text()

    assert "TAB5_APP_EXPORT void tab5_app_on_open_file" in source
    assert "app_open_file(path);" in source


def test_wasm_dispatcher_logs_callback_failures():
    dispatcher = DISPATCHER.read_text()

    assert 'Callback Wasm %s falhou' in dispatcher
    assert 'Callback Wasm alias %s falhou' in dispatcher


def test_wasm_dispatch_jobs_retain_the_instance_for_worker_execution():
    dispatcher = DISPATCHER.read_text()

    assert "job.kind = JOB_CALL;\n    job.instance = inst;" in dispatcher
    assert "job.kind = JOB_STRING;\n    job.instance = inst;" in dispatcher
