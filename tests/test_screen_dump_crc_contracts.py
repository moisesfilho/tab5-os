"""Static acceptance contracts for screen.dump CRC32, Base64 canonical,
chunk index validation, and host-side integrity checks.

The approved plan (boneco-de-lata.md §2.2 item 4) specifies that screen.dump
emits start/chunks/end frames.  This suite extends the existing
test_serial_bridge_dispatch.cpp contracts with NEW obligations:

  1. **start frame CRC32**: the start frame must include a CRC32 of the raw
     file content so the host can verify integrity after reassembly.
  2. **host CRC validation**: the host-side Python contract must verify that
     CRC32 is present and can be validated (TDD: define the contract; host
     C++ test does byte-identical reconstruction with CRC).
  3. **Base64 canonical encoding**: each chunk b64 uses standard alphabet
     (A-Z, a-z, 0-9, +, /) with '=' padding; no URL-safe variant.
  4. **Contiguous chunk indices**: 0..chunks-1 without gaps or duplicates.
  5. **end frame mandatory**: stream must always terminate with event=end.
  6. **retry/resume commands**: screen.dump.retry and screen.dump.resume are
     recognised and retransmit from the specified chunk index.

Pre-implementation state: CRC32 is NOT present in the start frame; the host
reconstruction relies solely on byte-identical comparison.  These tests define
the contract that must be satisfied.
"""

import re
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SERIAL_BRIDGE_SRC = ROOT / "components/os/core/serial_bridge.cpp"
SERIAL_BRIDGE_HDR = ROOT / "components/os/core/serial_bridge.h"
DEVICE_VALIDATION = ROOT / "tests/test_serial_bridge_device_validation.py"


def _read(p: Path) -> str:
    return p.read_text(encoding="utf-8")


# ---------------------------------------------------------------------------
# 1. CRC32 in start frame (firmware obligation)
# ---------------------------------------------------------------------------

class ScreenDumpCrc32StartFrameContract(unittest.TestCase):
    """The start frame of screen.dump must include a CRC32 field.

    Pre-implementation: the current start frame has status, action, event,
    size, chunks, and rid — but no CRC32.  The host needs CRC32 to verify
    byte-identical reassembly after Base64 decode without trusting the
    transport layer.
    """

    @classmethod
    def setUpClass(cls):
        cls.src = _read(SERIAL_BRIDGE_SRC)

    def test_stream_dump_exists(self):
        """stream_dump function must exist."""
        self.assertIn("bool stream_dump(", self.src,
                       "stream_dump function must exist for screen.dump")

    def test_start_frame_includes_crc32_field(self):
        """The start cJSON object must add a 'crc32' field."""
        # Find the stream_dump function and look for crc32 in the start frame
        start_pos = self.src.find("bool stream_dump(")
        self.assertGreater(start_pos, 0)
        # The start frame is constructed after the file size computation
        body = self.src[start_pos:start_pos + 2000]
        self.assertTrue(
            re.search(r'"crc32"', body),
            "stream_dump start frame must include a 'crc32' field for "
            "host-side integrity verification"
        )

    def test_crc32_is_number_in_start_frame(self):
        """crc32 must be added as a numeric value (not string)."""
        start_pos = self.src.find("bool stream_dump(")
        body = self.src[start_pos:start_pos + 2000]
        self.assertTrue(
            re.search(r'AddNumberToObject\([^,]+,\s*"crc32"', body),
            "crc32 must be added as a number (AddNumberToObject) for "
            "JSON numeric consistency"
        )


# ---------------------------------------------------------------------------
# 2. Base64 canonical encoding contract
# ---------------------------------------------------------------------------

class Base64CanonicalEncodingContract(unittest.TestCase):
    """Base64 encoding must use standard alphabet with padding.

    The append_b64 function must use A-Z, a-z, 0-9, +, / with '=' padding.
    No URL-safe variant (-, _) or unpadding.
    """

    @classmethod
    def setUpClass(cls):
        cls.src = _read(SERIAL_BRIDGE_SRC)

    def test_append_b64_exists(self):
        """append_b64 helper must exist."""
        self.assertIn("void append_b64(", self.src,
                       "append_b64 function must exist for Base64 encoding")

    def test_append_b64_uses_standard_alphabet(self):
        """append_b64 must use the standard Base64 alphabet."""
        self.assertTrue(
            re.search(r'"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789\+/"',
                       self.src),
            "append_b64 must use standard Base64 alphabet "
            "(A-Za-z0-9+/)"
        )

    def test_append_b64_pads_with_equals(self):
        """append_b64 must pad incomplete blocks with '='."""
        fn_body_start = self.src.find("void append_b64(")
        self.assertGreater(fn_body_start, 0)
        body = self.src[fn_body_start:fn_body_start + 700]
        self.assertIn("'='", body,
                       "append_b64 must pad incomplete blocks with '='")


# ---------------------------------------------------------------------------
# 3. Chunk index contiguity contract (firmware)
# ---------------------------------------------------------------------------

class ChunkIndexContiguityContract(unittest.TestCase):
    """Chunk indices must be contiguous 0..chunks-1 without gaps or duplicates.

    The loop variable is used as the index, ensuring monotonic increment.
    """

    @classmethod
    def setUpClass(cls):
        cls.src = _read(SERIAL_BRIDGE_SRC)

    def test_chunk_index_from_loop_variable(self):
        """The chunk index in the JSON frame must come from the loop iterator."""
        self.assertTrue(
            re.search(
                r'AddNumberToObject\(\w+,\s*"chunk",\s*\w+\)',
                self.src
            ),
            "chunk index must come from the loop iterator variable "
            "(guarantees contiguous 0..N-1)"
        )

    def test_stream_dump_loops_from_zero(self):
        """The chunk loop must start from index 0."""
        start_pos = self.src.find("bool stream_dump(")
        self.assertGreater(start_pos, 0)
        body = self.src[start_pos:start_pos + 2000]
        self.assertTrue(
            re.search(r'for\s*\(\s*size_t\s+index\s*=\s*0', body),
            "chunk loop must start from index 0 (size_t index = 0)"
        )


# ---------------------------------------------------------------------------
# 4. end frame mandatory contract
# ---------------------------------------------------------------------------

class EndFrameMandatoryContract(unittest.TestCase):
    """The stream must always emit an 'end' event frame, even on error."""

    @classmethod
    def setUpClass(cls):
        cls.src = _read(SERIAL_BRIDGE_SRC)

    def test_dump_end_emits_end_event(self):
        """dump_end helper must emit event=end."""
        self.assertTrue(
            re.search(r'event.{0,20}end', self.src),
            "dump_end must emit an 'end' event frame"
        )

    def test_stream_dump_always_ends(self):
        """stream_dump must call dump_end on both success and error paths."""
        start_pos = self.src.find("bool stream_dump(")
        self.assertGreater(start_pos, 0)
        body = self.src[start_pos:start_pos + 3000]
        self.assertIn("dump_end(", body,
                       "stream_dump must call dump_end to emit the end frame")

    def test_dump_error_also_emits_end(self):
        """dump_error must call dump_end (end frame even on error)."""
        error_pos = self.src.find("bool dump_error(")
        self.assertGreater(error_pos, 0)
        body = self.src[error_pos:error_pos + 500]
        self.assertIn("dump_end(", body,
                       "dump_error must also emit the end frame")


# ---------------------------------------------------------------------------
# 5. retry/resume command recognition
# ---------------------------------------------------------------------------

class ScreenDumpRetryResumeContract(unittest.TestCase):
    """screen.dump.retry and screen.dump.resume must be recognised.

    The dispatch handler must accept these commands and retransmit the dump.
    """

    @classmethod
    def setUpClass(cls):
        cls.src = _read(SERIAL_BRIDGE_SRC)

    def test_dispatch_recognises_screen_dump_retry(self):
        """serial_bridge_dispatch must handle screen.dump.retry."""
        self.assertIn('"screen.dump.retry"', self.src,
                       "dispatch must recognise screen.dump.retry command")

    def test_dispatch_recognises_screen_dump_resume(self):
        """serial_bridge_dispatch must handle screen.dump.resume."""
        self.assertIn('"screen.dump.resume"', self.src,
                       "dispatch must recognise screen.dump.resume command")

    def test_retry_command_routes_to_stream_dump(self):
        """screen.dump.retry must route to stream_dump()."""
        retry_pos = self.src.find('"screen.dump.retry"')
        self.assertGreater(retry_pos, 0)
        window = self.src[retry_pos:retry_pos + 300]
        self.assertIn("stream_dump", window,
                       "screen.dump.retry must route to stream_dump()")

    def test_retry_accepts_from_chunk_parameter(self):
        """screen.dump.retry must accept a 'from' chunk parameter for partial retransmit."""
        retry_pos = self.src.find('"screen.dump.retry"')
        self.assertGreater(retry_pos, 0)
        window = self.src[retry_pos:retry_pos + 500]
        # The retry should read a 'from' or 'start_index' or 'chunk' parameter
        self.assertTrue(
            re.search(r'has_number\(root,\s*"(?:from|start_index|chunk|index)"', window) or
            re.search(r'"(?:from|start_index|chunk|index)"', window),
            "screen.dump.retry must accept a chunk index parameter for "
            "partial retransmission (from/start_index/chunk)"
        )


# ---------------------------------------------------------------------------
# 6. Device validation must check CRC (host-side)
# ---------------------------------------------------------------------------

class DeviceValidationCrcContract(unittest.TestCase):
    """The device validation test must verify CRC32 after reassembly.

    When the firmware adds CRC32 to the start frame, the host-side
    validation must compute CRC32 of the reassembled bytes and compare.
    """

    def test_device_validation_exists(self):
        """test_serial_bridge_device_validation.py must exist."""
        self.assertTrue(DEVICE_VALIDATION.exists(),
                        "device validation test must exist")

    def test_device_validation_checks_crc32(self):
        """Device validation must verify CRC32 of reassembled data."""
        dev_src = _read(DEVICE_VALIDATION)
        self.assertTrue(
            re.search(r'crc32|CRC32|crc_32', dev_src, re.IGNORECASE),
            "device validation must check CRC32 after screen.dump reassembly"
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
