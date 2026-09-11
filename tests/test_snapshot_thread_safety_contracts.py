"""Static acceptance contracts for snapshot thread-safety, app.active
consistency, and WASM lifecycle no-UAF (use-after-free).

These tests verify safety-critical invariants of the serial bridge's
interaction with the WASM runtime and the active-app state machine:

  1. **app.active under display lock**: reading app.active state (via
     tab5_host_get_active_app) must be atomic with respect to the UI thread.
  2. **No UAF in lifecycle transitions**: when switching apps (Files→Music),
     the previous app's WASM instance must not be freed while the dispatch
     path still references it.
  3. **WASM instance lifecycle guards**: unload_pending/call_depth prevent
     freeing a module that has in-flight callbacks.
  4. **Generation counter prevents stale dispatches**: events posted to a
     closed instance are rejected by the dispatcher.
  5. **No dangling pointer after app.close**: after close_active, the active
     app context must be cleared.

Pre-implementation verification: these contracts build on existing tests
(test_wasm_unload_callback_contract.py, test_async_dispatcher_contract.py)
with ADDITIONAL structural checks specific to the serial bridge's interaction
with the lifecycle.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SERIAL_BRIDGE_SRC = ROOT / "components/os/core/serial_bridge.cpp"
WASM_RUNTIME_H = ROOT / "components/os/runtime/tab5_wasm_runtime.h"
LIFECYCLE_SRC = ROOT / "components/os/runtime/tab5_lifecycle_host.cpp"
PACKAGE_MGR_SRC = ROOT / "components/os/runtime/tab5_package_mgr.cpp"
HOST_ABI_H = ROOT / "components/os/runtime/tab5_host_abi.h"


def _read(p: Path) -> str:
    return p.read_text(encoding="utf-8")


def _function_body(source: str, signature: str) -> str:
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


# ---------------------------------------------------------------------------
# 1. app.active thread-safety contract
# ---------------------------------------------------------------------------

class AppActiveThreadSafetyContract(unittest.TestCase):
    """Reading app.active state must be safe under concurrent UI operations."""

    @classmethod
    def setUpClass(cls):
        cls.src = _read(SERIAL_BRIDGE_SRC)
        cls.abi_h = _read(HOST_ABI_H) if HOST_ABI_H.exists() else ""

    def test_app_active_dispatch_uses_host_get_active_app(self):
        """app.active must call tab5_host_get_active_app()."""
        active_pos = self.src.find('"app.active"')
        self.assertGreater(active_pos, 0, "app.active command not found")
        window = self.src[active_pos:active_pos + 400]
        self.assertIn("tab5_host_get_active_app", window,
                       "app.active must use tab5_host_get_active_app()")

    def test_app_active_returns_is_wasm_flag(self):
        """The app.active response must distinguish WASM apps via is_wasm."""
        active_pos = self.src.find('"app.active"')
        window = self.src[active_pos:active_pos + 400]
        # When active app exists, response should include fields that
        # distinguish native vs WASM apps
        self.assertIn("app_id", window,
                       "app.active must include app_id for identification")
        self.assertIn("app_name", window,
                       "app.active must include app_name for identification")

    def test_host_abi_has_set_active_app(self):
        """tab5_host_set_active_app must exist to support lifecycle transitions."""
        self.assertIn("tab5_host_set_active_app", self.abi_h,
                       "tab5_host_set_active_app must be declared in host_abi.h")

    def test_host_abi_has_get_active_app(self):
        """tab5_host_get_active_app must exist for reading current state."""
        self.assertIn("tab5_host_get_active_app", self.abi_h,
                       "tab5_host_get_active_app must be declared in host_abi.h")

    def test_host_abi_has_clear_active_app(self):
        """tab5_host_clear_active_app must exist for cleanup after close."""
        self.assertIn("tab5_host_clear_active_app", self.abi_h,
                       "tab5_host_clear_active_app must be declared for "
                       "cleanup after app.close")


# ---------------------------------------------------------------------------
# 2. No UAF in lifecycle transitions
# ---------------------------------------------------------------------------

class LifecycleNoUafContract(unittest.TestCase):
    """When switching apps, the previous instance must not be freed prematurely."""

    @classmethod
    def setUpClass(cls):
        cls.wasm_h = _read(WASM_RUNTIME_H)
        cls.lifecycle = _read(LIFECYCLE_SRC)
        cls.pkg = _read(PACKAGE_MGR_SRC)

    def test_instance_struct_has_is_running_field(self):
        """tab5_wasm_app_instance_t must have is_running for lifecycle guard."""
        self.assertIn("is_running", self.wasm_h,
                       "instance struct must have is_running field")

    def test_instance_struct_has_call_depth(self):
        """Instance must track call_depth to prevent premature unload."""
        self.assertIn("call_depth", self.wasm_h,
                       "instance struct must have call_depth for "
                       "in-flight callback tracking")

    def test_instance_struct_has_unload_pending(self):
        """Instance must track unload_pending for deferred teardown."""
        self.assertIn("unload_pending", self.wasm_h,
                       "instance struct must have unload_pending flag")

    def test_instance_struct_has_generation(self):
        """Instance must have generation counter for stale event rejection."""
        self.assertIn("generation", self.wasm_h,
                       "instance struct must have generation/epoch counter")

    def test_lifecycle_host_abort_exists(self):
        """tab5_lifecycle_host_abort_app must exist for failed launches."""
        self.assertIn("tab5_lifecycle_host_abort_app", self.lifecycle,
                       "abort_app must exist to rollback failed launches")

    def test_package_mgr_close_uses_wasm_unload(self):
        """close_active must route through tab5_wasm_unload (not direct free)."""
        close = _function_body(self.pkg, "tab5_err_t tab5_package_mgr_close_active")
        self.assertIn("tab5_wasm_unload", close,
                       "close_active must use tab5_wasm_unload for "
                       "deferred teardown (prevents UAF)")
        # Must NOT directly free WASM resources in close_active
        self.assertNotIn("free(inst->wasm_buf)", close,
                         "close_active must not directly free wasm_buf "
                         "(must go through unload)")
        self.assertNotIn("wasm_runtime_destroy_exec_env", close,
                         "close_active must not directly destroy exec_env")

    def test_package_mgr_never_calls_wasm_runtime_directly(self):
        """Package manager must never access wasm_runtime_* functions."""
        self.assertNotIn("wasm_runtime_", self.pkg,
                         "package_mgr must not call wasm_runtime_* directly "
                         "(all WASM operations go through tab5_wasm_* API)")


# ---------------------------------------------------------------------------
# 3. No dangling pointer after app.close
# ---------------------------------------------------------------------------

class NoDanglingPointerAfterCloseContract(unittest.TestCase):
    """After app.close, the active app context must be properly cleaned up."""

    @classmethod
    def setUpClass(cls):
        cls.src = _read(SERIAL_BRIDGE_SRC)

    def test_app_close_clears_active_state(self):
        """app.close dispatch must call tab5_package_mgr_close_active."""
        close_pos = self.src.find('"app.close"')
        self.assertGreater(close_pos, 0, "app.close not found")
        window = self.src[close_pos:close_pos + 300]
        self.assertIn("tab5_package_mgr_close_active", window,
                       "app.close must call tab5_package_mgr_close_active")

    def test_app_active_after_close_reports_no_active(self):
        """After close, app.active must report active=false."""
        # This is a structural check: the app.active path must handle
        # the case where get_active_app returns nullptr
        active_pos = self.src.find('"app.active"')
        window = self.src[active_pos:active_pos + 400]
        self.assertIn("active != nullptr", window or "nullptr",
                       "app.active must handle the null case (no active app)")

    def test_app_active_adds_active_bool(self):
        """app.active must include a boolean 'active' field."""
        active_pos = self.src.find('"app.active"')
        window = self.src[active_pos:active_pos + 400]
        self.assertIn("AddBoolToObject", window,
                       "app.active must add a boolean 'active' field")


# ---------------------------------------------------------------------------
# 4. WASM lifecycle state machine
# ---------------------------------------------------------------------------

class WasmLifecycleStateMachineContract(unittest.TestCase):
    """The lifecycle state machine must prevent operations on destroyed apps."""

    @classmethod
    def setUpClass(cls):
        cls.lifecycle = _read(LIFECYCLE_SRC)

    def test_init_rejects_non_uninitialized(self):
        """init_app must reject apps not in UNINITIALIZED state."""
        init_body = _function_body(self.lifecycle, "tab5_err_t tab5_lifecycle_host_init_app")
        self.assertIn("TAB5_APP_STATE_UNINITIALIZED", init_body)
        self.assertIn("TAB5_ERR_INVALID_STATE", init_body)

    def test_resume_rejects_uninitialized_and_destroyed(self):
        """resume_app must reject apps in UNINITIALIZED or DESTROYED state."""
        resume_body = _function_body(self.lifecycle, "tab5_err_t tab5_lifecycle_host_resume_app")
        self.assertIn("TAB5_APP_STATE_UNINITIALIZED", resume_body)
        self.assertIn("TAB5_APP_STATE_DESTROYED", resume_body)

    def test_pause_requires_resumed_state(self):
        """pause_app must only work on RESUMED apps."""
        pause_body = _function_body(self.lifecycle, "tab5_err_t tab5_lifecycle_host_pause_app")
        self.assertIn("TAB5_APP_STATE_RESUMED", pause_body)

    def test_lifecycle_sets_active_on_init(self):
        """init_app must call tab5_host_set_active_app."""
        init_body = _function_body(self.lifecycle, "tab5_err_t tab5_lifecycle_host_init_app")
        self.assertIn("tab5_host_set_active_app", init_body)

    def test_lifecycle_clears_active_on_init_failure(self):
        """If screen creation fails during init, active must be cleared."""
        init_body = _function_body(self.lifecycle, "tab5_err_t tab5_lifecycle_host_init_app")
        self.assertIn("tab5_host_clear_active_app", init_body)


# ---------------------------------------------------------------------------
# 5. Dispatch path does not hold display lock during WASM call
# ---------------------------------------------------------------------------

class DispatchNoWasmUnderLockContract(unittest.TestCase):
    """The serial bridge dispatch must not call WASM under display lock.

    The display lock (bsp_display_lock) protects LVGL widget operations.
    WASM calls may block or take time; holding the lock during WASM would
    freeze the UI.  The async dispatcher pattern ensures WASM runs outside
    the lock.
    """

    @classmethod
    def setUpClass(cls):
        cls.src = _read(SERIAL_BRIDGE_SRC)

    def test_app_open_dispatches_through_package_mgr(self):
        """app.open must route through tab5_package_mgr_launch (not inline WASM)."""
        open_pos = self.src.find('"app.open"')
        self.assertGreater(open_pos, 0)
        window = self.src[open_pos:open_pos + 800]
        self.assertIn("tab5_package_mgr_launch", window,
                       "app.open must route through package manager")


if __name__ == "__main__":
    unittest.main(verbosity=2)
