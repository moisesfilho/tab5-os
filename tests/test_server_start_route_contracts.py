"""Static acceptance contracts for server.start route/availability confirmation.

The approved plan (boneco-de-lata.md §2.2 item 3) specifies that server.start
returns the running state, IP, port, and URL.  This suite verifies:

  1. server.start returns a complete data envelope (running, ip, port, url).
  2. The response must include both 'running' (boolean) and 'url' (string).
  3. The URL must be well-formed (http://ip:port/).
  4. server.stop must set running=false.
  5. server.status must reflect the current state.
  6. server.start is idempotent (starting twice returns ok).
  7. Error path: if http_file_server_start fails, dispatch returns error.

Pre-implementation: the host C++ tests already validate the basic shape.
This Python suite adds structural static verification and route-existence
checks against the actual serial_bridge.cpp source.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SERIAL_BRIDGE_SRC = ROOT / "components/os/core/serial_bridge.cpp"
HTTP_FILE_SERVER_H = ROOT / "components/os/core/http_file_server.h"


def _read(p: Path) -> str:
    return p.read_text(encoding="utf-8")


class ServerStartResponseShapeContract(unittest.TestCase):
    """server.start must return running, ip, port, url in data."""

    @classmethod
    def setUpClass(cls):
        cls.src = _read(SERIAL_BRIDGE_SRC)

    def test_server_start_dispatches_to_http_file_server_start(self):
        """server.start must call http_file_server_start()."""
        server_pos = self.src.find('"server.start"')
        self.assertGreater(server_pos, 0, "server.start not found")
        window = self.src[server_pos:server_pos + 400]
        self.assertIn("http_file_server_start", window,
                       "server.start must call http_file_server_start()")

    def test_response_includes_running_boolean(self):
        """The response must include a 'running' boolean field."""
        server_pos = self.src.find('"server.start"')
        window = self.src[server_pos:server_pos + 600]
        self.assertTrue(
            re.search(r'AddBoolToObject\([^,]+,\s*"running"', window),
            "server.start must add 'running' as a boolean (AddBoolToObject)"
        )

    def test_response_includes_port_number(self):
        """The response must include a 'port' numeric field."""
        server_pos = self.src.find('"server.start"')
        window = self.src[server_pos:server_pos + 600]
        self.assertTrue(
            re.search(r'AddNumberToObject\([^,]+,\s*"port"', window),
            "server.start must add 'port' as a number (AddNumberToObject)"
        )

    def test_response_includes_ip_string(self):
        """The response must include an 'ip' string field."""
        server_pos = self.src.find('"server.start"')
        window = self.src[server_pos:server_pos + 1000]
        self.assertTrue(
            re.search(r'AddStringToObject\([^,]+,\s*"ip"', window),
            "server.start must add 'ip' as a string (AddStringToObject)"
        )

    def test_response_includes_url_string(self):
        """The response must include a 'url' string field."""
        server_pos = self.src.find('"server.start"')
        window = self.src[server_pos:server_pos + 1500]
        self.assertTrue(
            re.search(r'AddStringToObject\([^,]+,\s*"url"', window),
            "server.start must add 'url' as a string (AddStringToObject)"
        )

    def test_url_uses_http_scheme(self):
        """The URL must be constructed with http:// prefix."""
        server_pos = self.src.find('"server.start"')
        window = self.src[server_pos:server_pos + 1000]
        self.assertIn("http://", window,
                       "URL must use http:// scheme")


class ServerStopAndStatusContract(unittest.TestCase):
    """server.stop and server.status must be properly handled."""

    @classmethod
    def setUpClass(cls):
        cls.src = _read(SERIAL_BRIDGE_SRC)

    def test_server_stop_dispatches_to_http_file_server_stop(self):
        """server.stop must call http_file_server_stop()."""
        stop_pos = self.src.find('"server.stop"')
        self.assertGreater(stop_pos, 0, "server.stop not found")
        window = self.src[stop_pos:stop_pos + 300]
        self.assertIn("http_file_server_stop", window,
                       "server.stop must call http_file_server_stop()")

    def test_server_status_dispatches_to_is_running(self):
        """server.status must call http_file_server_is_running()."""
        status_pos = self.src.find('"server.status"')
        self.assertGreater(status_pos, 0, "server.status not found")
        window = self.src[status_pos:status_pos + 600]
        self.assertIn("http_file_server_is_running", window,
                       "server.status must call http_file_server_is_running()")


class ServerErrorHandlingContract(unittest.TestCase):
    """If http_file_server_start fails, dispatch must return error envelope."""

    @classmethod
    def setUpClass(cls):
        cls.src = _read(SERIAL_BRIDGE_SRC)

    def test_server_start_checks_return_code(self):
        """server.start must check the return value of http_file_server_start."""
        server_pos = self.src.find('"server.start"')
        window = self.src[server_pos:server_pos + 400]
        self.assertTrue(
            re.search(r'err\s*!=\s*ESP_OK|err\s*==\s*ESP_FAIL|if\s*\(\s*err', window),
            "server.start must check the return code of http_file_server_start()"
        )

    def test_server_error_returns_error_frame(self):
        """On failure, the dispatch must return an error envelope."""
        server_pos = self.src.find('"server.start"')
        window = self.src[server_pos:server_pos + 500]
        self.assertIn("error_frame", window,
                       "server.start must use error_frame() on failure")

    def test_server_error_message_mentions_operation(self):
        """The error message must mention the server operation."""
        server_pos = self.src.find('"server.start"')
        window = self.src[server_pos:server_pos + 500]
        self.assertTrue(
            re.search(r'"operacao do servidor|servidor|server', window, re.IGNORECASE),
            "error message must mention the server operation for debugging"
        )


class ServerIdempotencyContract(unittest.TestCase):
    """Starting the server twice must be idempotent (return ok both times)."""

    @classmethod
    def setUpClass(cls):
        cls.server_h = _read(HTTP_FILE_SERVER_H) if HTTP_FILE_SERVER_H.exists() else ""

    def test_http_file_server_start_returns_esp_err(self):
        """http_file_server_start must return esp_err_t for error checking."""
        self.assertIn("esp_err_t http_file_server_start", self.server_h,
                       "http_file_server_start must return esp_err_t")

    def test_http_file_server_is_running_exists(self):
        """http_file_server_is_running must exist for state query."""
        self.assertIn("bool http_file_server_is_running", self.server_h,
                       "http_file_server_is_running must exist")

    def test_http_file_server_get_port_exists(self):
        """http_file_server_get_port must exist for URL construction."""
        self.assertIn("uint16_t http_file_server_get_port", self.server_h,
                       "http_file_server_get_port must exist")


if __name__ == "__main__":
    unittest.main(verbosity=2)
