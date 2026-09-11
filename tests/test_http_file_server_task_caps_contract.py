"""Static contract: http_file_server_start must pin the httpd task to PSRAM.

Correction of server.start (approved plan). The ESP-IDF httpd task is
created from `httpd_config_t config` inside http_file_server_start(). By
default HTTPD_DEFAULT_CONFIG() allocates the task stack from internal RAM
(MALLOC_CAP_8BIT only), consuming scarce internal/DMA memory on the
ESP32-P4. The approved fix pins the task stack to PSRAM:

    config.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;

Safely-verifiable equivalent forms are accepted, as long as one statement
that assigns `config.task_caps` inside http_file_server_start() mentions
BOTH MALLOC_CAP_SPIRAM and MALLOC_CAP_8BIT (e.g.
`config.task_caps |= MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;` or a named
constant that expands to exactly those flags).

TDD red phase: production does not set config.task_caps yet, so the
contract tests below fail until the approved change lands. The parser
self-check tests (TaskCapsParserContract) must stay green.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HTTP_FILE_SERVER_SRC = ROOT / "components/os/core/http_file_server.cpp"

REQUIRED_FLAGS = ("MALLOC_CAP_SPIRAM", "MALLOC_CAP_8BIT")


def _read(p: Path) -> str:
    return p.read_text(encoding="utf-8")


def _task_caps_statements(src: str) -> list[str]:
    """Every `config.task_caps` assignment inside http_file_server_start.

    Continuation lines are merged up to the terminating ';'. Commented-out
    occurrences (// or /* ... */) are ignored so the contract cannot be
    satisfied by a comment alone.
    """
    start = src.find("esp_err_t http_file_server_start(void)")
    if start < 0:
        return []
    end = src.find("esp_err_t http_file_server_stop(void)", start)
    if end < 0:
        end = len(src)
    body = src[start:end]

    statements: list[str] = []
    for m in re.finditer(r"config\.task_caps\s*[|+\-*/]?=", body):
        line_start = body.rfind("\n", 0, m.start()) + 1
        prefix = body[line_start:m.start()].lstrip()
        if prefix.startswith(("//", "/*", "*")):
            continue
        stmt = body[m.start():]
        semi = stmt.find(";")
        if semi >= 0:
            stmt = stmt[: semi + 1]
        statements.append(re.sub(r"\s+", " ", stmt.strip()))
    return statements


class HttpFileServerTaskCapsContract(unittest.TestCase):
    """http_file_server_start must allocate the httpd task from PSRAM."""

    @classmethod
    def setUpClass(cls):
        cls.src = _read(HTTP_FILE_SERVER_SRC)
        cls.statements = _task_caps_statements(cls.src)

    def test_http_file_server_start_exists(self):
        """Anchor: http_file_server_start must exist (non-vacuous guard)."""
        self.assertIn("esp_err_t http_file_server_start(void)", self.src,
                      "http_file_server_start must exist")

    def test_task_caps_assignment_present(self):
        """http_file_server_start must assign config.task_caps."""
        self.assertTrue(
            self.statements,
            "http_file_server_start must assign config.task_caps, e.g. "
            "`config.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;`",
        )

    def test_task_caps_includes_spiram(self):
        """The task_caps assignment must include MALLOC_CAP_SPIRAM."""
        joined = " ".join(self.statements)
        self.assertIn(
            "MALLOC_CAP_SPIRAM", joined,
            "config.task_caps must include MALLOC_CAP_SPIRAM so the httpd "
            "task stack is allocated from PSRAM instead of internal RAM",
        )

    def test_task_caps_includes_8bit(self):
        """The task_caps assignment must include MALLOC_CAP_8BIT."""
        joined = " ".join(self.statements)
        self.assertIn(
            "MALLOC_CAP_8BIT", joined,
            "config.task_caps must include MALLOC_CAP_8BIT alongside "
            "MALLOC_CAP_SPIRAM (the task stack must be byte-addressable)",
        )


class TaskCapsParserContract(unittest.TestCase):
    """The static checker itself must be sound and never vacuous."""

    def test_detects_direct_assignment(self):
        src = (
            "esp_err_t http_file_server_start(void)\n{\n"
            "    httpd_config_t config = HTTPD_DEFAULT_CONFIG();\n"
            "    config.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;\n"
            "    return ESP_OK;\n}\n"
            "esp_err_t http_file_server_stop(void)\n{\n    return ESP_OK;\n}\n"
        )
        statements = _task_caps_statements(src)
        self.assertEqual(len(statements), 1)
        self.assertIn("MALLOC_CAP_SPIRAM", statements[0])
        self.assertIn("MALLOC_CAP_8BIT", statements[0])

    def test_detects_or_equal_assignment(self):
        src = (
            "esp_err_t http_file_server_start(void)\n{\n"
            "    config.task_caps |= MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;\n"
            "    return ESP_OK;\n}\n"
            "esp_err_t http_file_server_stop(void)\n{\n    return ESP_OK;\n}\n"
        )
        statements = _task_caps_statements(src)
        self.assertEqual(len(statements), 1)
        self.assertIn("MALLOC_CAP_SPIRAM", statements[0])
        self.assertIn("MALLOC_CAP_8BIT", statements[0])

    def test_default_config_only_is_not_enough(self):
        """Without a task_caps assignment the contract must fail (red)."""
        src = (
            "esp_err_t http_file_server_start(void)\n{\n"
            "    httpd_config_t config = HTTPD_DEFAULT_CONFIG();\n"
            "    config.server_port = 8080;\n"
            "    return ESP_OK;\n}\n"
            "esp_err_t http_file_server_stop(void)\n{\n    return ESP_OK;\n}\n"
        )
        self.assertEqual(_task_caps_statements(src), [])

    def test_commented_assignment_is_ignored(self):
        src = (
            "esp_err_t http_file_server_start(void)\n{\n"
            "    // config.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;\n"
            "    /* config.task_caps = MALLOC_CAP_SPIRAM; */\n"
            "    return ESP_OK;\n}\n"
            "esp_err_t http_file_server_stop(void)\n{\n    return ESP_OK;\n}\n"
        )
        self.assertEqual(_task_caps_statements(src), [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
