"""Static acceptance contracts for runtime entrypoints and UI cleanup.

These tests intentionally do not use screenshots or golden files.  They check
the small, safety-critical control-flow obligations that can be verified from
the source before the runtime implementation is changed.
"""

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PKG = (ROOT / "components/os/runtime/tab5_package_mgr.cpp").read_text(encoding="utf-8")
WASM = (ROOT / "components/os/runtime/tab5_wasm_runtime.cpp").read_text(encoding="utf-8")
UI = (ROOT / "components/os/runtime/tab5_ui_host.cpp").read_text(encoding="utf-8")
GALLERY = (ROOT / "components/os/shell/ui_gallery_view.cpp").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
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


class EntrypointContracts(unittest.TestCase):
    def test_package_manager_never_uses_start_as_fallback(self):
        launch = function_body(PKG, "tab5_err_t tab5_package_mgr_launch")
        self.assertNotIn('"_start"', launch)

    def test_entrypoint_precedence_and_missing_entrypoint_error(self):
        launch = function_body(PKG, "tab5_err_t tab5_package_mgr_launch")
        self.assertIn("tab5_wasm_select_entrypoint(&entry->wasm_inst, &entrypoint)", launch)
        self.assertLess(launch.index("tab5_wasm_select_entrypoint"), launch.index("tab5_lifecycle_host_init_app"))
        self.assertEqual(launch.count("tab5_wasm_call_function(&entry->wasm_inst"), 1)
        self.assertIn("if (entry_err != TAB5_OK)", launch)
        self.assertIn("return entry_err;", launch)

    def test_entrypoint_and_wasm_worker_are_each_attempted_once(self):
        launch = function_body(PKG, "tab5_err_t tab5_package_mgr_launch")
        self.assertEqual(launch.count("tab5_wasm_call_function(&entry->wasm_inst"), 1)
        self.assertNotIn('"_start"', launch)
        worker = function_body(WASM, "static void *wasm_load_pthread_worker")
        self.assertEqual(worker.count("tab5_wasm_load_from_bytes_direct"), 1)
        # Include the opening parenthesis so this does not match the
        # tab5_wasm_load_from_bytes_direct helper.
        load = function_body(WASM, "tab5_err_t tab5_wasm_load_from_bytes(")
        self.assertEqual(load.count("pthread_create("), 1)

    def test_preflight_rejects_without_lifecycle_rollback(self):
        launch = function_body(PKG, "tab5_err_t tab5_package_mgr_launch")
        preflight = launch.split("tab5_wasm_select_entrypoint", 1)[1].split(
            "tab5_lifecycle_host_init_app", 1
        )[0]
        self.assertIn("tab5_wasm_unload(&entry->wasm_inst);", preflight)
        self.assertNotIn("tab5_lifecycle_host_abort_app", preflight)
        self.assertIn("TAB5_ERR_NOT_FOUND", preflight)


class GalleryCleanupContracts(unittest.TestCase):
    def test_gallery_objects_are_invalidated_and_removed_before_buffer_release(self):
        destroy = function_body(GALLERY, "void ui_gallery_view_destroy")
        free_pos = destroy.index("free(view->gallery_canvas_buf)")
        invalidation = re.search(r"lv_obj_invalidate\([^;]+\);", destroy)
        removal = re.search(r"lv_obj_(?:delete|delete_async)\([^;]+\);", destroy)
        self.assertIsNotNone(invalidation)
        self.assertIsNotNone(removal)
        self.assertLess(invalidation.start(), free_pos)
        self.assertLess(removal.start(), free_pos)


class AbortContracts(unittest.TestCase):
    def test_abort_restores_context_handles_and_has_no_pending_callback_owner(self):
        abort = function_body(UI, "tab5_err_t tab5_ui_host_abort_app_screen")
        self.assertIn("delete_wasm_poll_timer();", abort)
        self.assertIn("memcpy(s_handle_table, s_previous_handle_table", abort)
        self.assertIn("s_next_handle = s_previous_next_handle;", abort)
        self.assertIn("s_active_camera_view = s_previous_camera_view;", abort)
        self.assertIn("s_active_gallery_view = s_previous_gallery_view;", abort)
        self.assertLess(abort.index("delete_wasm_poll_timer();"), abort.index("lv_obj_delete(scr);"))
        self.assertIn("s_wasm_poll_owner = nullptr;", UI)

    def test_poll_callback_rejects_stale_context_before_wasm_dispatch(self):
        poll = function_body(UI, "static void on_wasm_poll_timer")
        self.assertIn("if (app_ctx != s_wasm_poll_owner)", poll)
        self.assertIn("tab5_wasm_dispatch_post_call", poll)
        self.assertLess(poll.index("if (app_ctx != s_wasm_poll_owner)"), poll.index("tab5_wasm_dispatch_post_call"))


if __name__ == "__main__":
    unittest.main()
