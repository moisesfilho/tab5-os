"""Static contract checks for candidate-app rollback."""

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
UI = (ROOT / "components/os/runtime/tab5_ui_host.cpp").read_text(encoding="utf-8")
WASM = (ROOT / "components/os/runtime/tab5_wasm_runtime.cpp").read_text(encoding="utf-8")
PKG = (ROOT / "components/os/runtime/tab5_package_mgr.cpp").read_text(encoding="utf-8")


class RuntimeRollbackContract(unittest.TestCase):
    def test_pthread_failure_does_not_unload_owned_instance(self):
        block = re.search(r"if \(rc != 0\) \{(?P<body>.*?)\n    \}", WASM, re.DOTALL)
        self.assertIsNotNone(block)
        self.assertNotIn("tab5_wasm_unload", block.group("body"))

    def test_candidate_failures_use_abort_path(self):
        self.assertGreaterEqual(PKG.count("tab5_lifecycle_host_abort_app"), 3)
        self.assertIn("tab5_ui_host_abort_app_screen", UI)

    def test_abort_restores_handles_before_future_events(self):
        abort = UI.split("tab5_err_t tab5_ui_host_abort_app_screen", 1)[1]
        self.assertIn("memcpy(s_handle_table, s_previous_handle_table", abort)
        self.assertIn("lv_obj_delete(scr)", abort)

    def test_wasm_callbacks_fallback_only_when_primary_is_missing(self):
        disp = (ROOT / "components/os/runtime/tab5_wasm_dispatcher.cpp").read_text(encoding="utf-8")
        self.assertIn('if (err == TAB5_ERR_NOT_FOUND', disp)
        self.assertIn('tab5_wasm_dispatch_post_call', UI)
        self.assertNotIn('tab5_wasm_call_function(wasm_inst, "tab5_app_on_ui_event", 3, argv);\n            tab5_wasm_call_function', UI)
        self.assertNotIn('tab5_wasm_call_function(wasm_inst, "tab5_app_on_theme_changed", argc, argv);\n', UI)

    def test_launch_uses_active_context_and_short_circuits_same_entry(self):
        self.assertIn('tab5_app_context_t *active_ctx = tab5_host_get_active_app();', PKG)
        self.assertIn('if (s_running_dynamic_app == entry || active_ctx == &entry->host_ctx)', PKG)
        self.assertIn('tab5_host_set_active_app(previous_ctx);', PKG)

    def test_successful_replacement_does_not_clear_new_app_ui_state(self):
        destroy = UI.split("tab5_err_t tab5_ui_host_destroy_app_screen", 1)[1].split(
            "tab5_err_t tab5_ui_host_abort_app_screen", 1
        )[0]
        self.assertIn("const bool destroying_active = tab5_host_get_active_app() == ctx;", destroy)
        active_cleanup = destroy.split("if (destroying_active)", 1)[1]
        self.assertIn("tab5_ui_host_clear_handles();", active_cleanup)
        self.assertEqual(1, destroy.count("tab5_ui_host_clear_handles();"))
        self.assertIn("if (destroying_active) {\n            ui_keyboard_hide();", destroy)
        self.assertIn("if (s_previous_screen == scr || destroying_active)", destroy)

    def test_successful_replacement_closes_old_app_after_candidate_activation(self):
        launch = PKG.split("tab5_err_t tab5_package_mgr_launch", 1)[1]
        self.assertIn("// O candidato agora está validado; somente neste ponto fecha o anterior.", launch)
        self.assertIn("tab5_package_mgr_close_active();", launch)
        self.assertLess(launch.index("tab5_host_set_active_app"), launch.index("tab5_package_mgr_close_active();"))

    def test_replacement_removes_only_old_screen_handles_before_delete(self):
        destroy = UI.split("tab5_err_t tab5_ui_host_destroy_app_screen", 1)[1].split(
            "tab5_err_t tab5_ui_host_abort_app_screen", 1
        )[0]
        self.assertIn("static void clear_handles_for_screen", UI)
        self.assertIn("is_descendant_of(s_handle_table[i], screen)", UI)
        self.assertIn("clear_handles_for_screen(scr);", destroy)
        self.assertLess(destroy.index("clear_handles_for_screen(scr);"), destroy.index("lv_obj_delete(scr);"))

    def test_views_are_owned_and_candidate_abort_restores_previous_views(self):
        self.assertIn("s_active_camera_owner", UI)
        self.assertIn("s_active_gallery_owner", UI)
        self.assertIn("destroy_owned_views(ctx);", UI)
        abort = UI.split("tab5_err_t tab5_ui_host_abort_app_screen", 1)[1]
        self.assertIn("s_active_camera_view = s_previous_camera_view;", abort)
        self.assertIn("s_active_gallery_view = s_previous_gallery_view;", abort)

    def test_normal_destroy_cleans_snapshot_views_owned_by_old_context(self):
        destroy = UI.split("tab5_err_t tab5_ui_host_destroy_app_screen", 1)[1].split(
            "tab5_err_t tab5_ui_host_abort_app_screen", 1
        )[0]
        self.assertIn("static void destroy_snapshot_owned_views", UI)
        self.assertIn("destroy_snapshot_owned_views(ctx);", destroy)
        self.assertLess(destroy.index("destroy_snapshot_owned_views(ctx);"), destroy.index("lv_obj_delete(scr);"))

    def test_snapshot_cleanup_precedes_active_owner_cleanup_to_avoid_double_destroy(self):
        destroy = UI.split("tab5_err_t tab5_ui_host_destroy_app_screen", 1)[1].split(
            "tab5_err_t tab5_ui_host_abort_app_screen", 1
        )[0]
        inactive_cleanup = destroy.split("if (!destroying_active)", 1)[1]
        self.assertLess(
            inactive_cleanup.index("destroy_snapshot_owned_views(ctx);"),
            inactive_cleanup.index("destroy_owned_views(ctx);"),
        )

    def test_snapshot_cleanup_does_not_destroy_replacement_view(self):
        cleanup = UI.split("static void destroy_snapshot_owned_views", 1)[1].split(
            "void tab5_ui_host_generic_widget_event_cb", 1
        )[0]
        self.assertIn("s_previous_camera_view != s_active_camera_view", cleanup)
        self.assertIn("s_previous_gallery_view != s_active_gallery_view", cleanup)

    def test_snapshot_cleanup_clears_previous_camera_ownership(self):
        cleanup = UI.split("static void destroy_snapshot_owned_views", 1)[1].split(
            "void tab5_ui_host_generic_widget_event_cb", 1
        )[0]
        self.assertIn("s_previous_camera_view = nullptr;", cleanup)
        self.assertIn("s_previous_camera_owner = nullptr;", cleanup)

    def test_snapshot_cleanup_clears_previous_gallery_ownership(self):
        cleanup = UI.split("static void destroy_snapshot_owned_views", 1)[1].split(
            "void tab5_ui_host_generic_widget_event_cb", 1
        )[0]
        self.assertIn("s_previous_gallery_view = nullptr;", cleanup)
        self.assertIn("s_previous_gallery_owner = nullptr;", cleanup)

    def test_replacement_transfers_or_clears_single_wasm_poll_timer(self):
        destroy = UI.split("tab5_err_t tab5_ui_host_destroy_app_screen", 1)[1].split(
            "tab5_err_t tab5_ui_host_abort_app_screen", 1
        )[0]
        sync = UI.split("static void sync_wasm_poll_timer_to_active_app", 1)[1].split(
            "#endif", 1
        )[0]
        self.assertIn("sync_wasm_poll_timer_to_active_app();", destroy)
        self.assertIn("tab5_host_get_active_app()", sync)
        self.assertIn("s_wasm_poll_owner = s_wasm_poll_timer != nullptr ? active_ctx : nullptr;", sync)
        self.assertIn("delete_wasm_poll_timer();", sync)
        self.assertIn("if (s_wasm_poll_timer == nullptr)", sync)
        self.assertNotIn("lv_timer_reset", sync)
        self.assertNotIn("lv_timer_set_period", sync)

    def test_final_destroy_removes_wasm_poll_timer(self):
        destroy = UI.split("tab5_err_t tab5_ui_host_destroy_app_screen", 1)[1].split(
            "tab5_err_t tab5_ui_host_abort_app_screen", 1
        )[0]
        self.assertIn("if (destroying_active)", destroy)
        self.assertIn("delete_wasm_poll_timer();", destroy)

    def test_wasm_poll_rejects_non_owner_before_dispatch(self):
        poll = UI.split("static void on_wasm_poll_timer", 1)[1].split(
            "static void delete_wasm_poll_timer", 1
        )[0]
        self.assertIn("if (app_ctx != s_wasm_poll_owner)", poll)
        self.assertIn("tab5_wasm_dispatch_post_call(wasm_inst", poll)
        self.assertLess(
            poll.index("if (app_ctx != s_wasm_poll_owner)"),
            poll.index("tab5_wasm_dispatch_post_call(wasm_inst"),
        )


if __name__ == "__main__":
    unittest.main()
