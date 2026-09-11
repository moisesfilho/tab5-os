"""Static acceptance contracts for strict retry merge of screen.dump.

When the host detects missing chunks (gap in indices 0..N-1) or a missing
end frame, it sends screen.dump.retry with a 'from' parameter indicating
the first missing index.  The firmware must retransmit from that index
onward, producing a clean continuation that the host can merge without
duplicates.

Contracts:
  1. screen.dump.retry accepts a 'from' (or equivalent) chunk index.
  2. The retransmitted stream starts from the specified index.
  3. The retransmitted stream includes an end frame.
  4. The host-side merge must deduplicate by chunk index.
  5. After merge, the final chunk index list must be 0..N-1 contiguous.
  6. screen.dump.resume retransmits from the last received +1 (implicit).

Pre-implementation: retry/resume exist but do NOT implement partial
retransmission — they call stream_dump from index 0.  These tests
define the strict merge contract.
"""

import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SERIAL_BRIDGE_SRC = ROOT / "components/os/core/serial_bridge.cpp"
DEVICE_VALIDATION = ROOT / "tests/test_serial_bridge_device_validation.py"
PYTHON_CLI = ROOT / "tools/tab5_cli.py"


def _read(p: Path) -> str:
    return p.read_text(encoding="utf-8")


class RetryFromChunkContract(unittest.TestCase):
    """The retry command must accept a chunk index for partial retransmission."""

    @classmethod
    def setUpClass(cls):
        cls.src = _read(SERIAL_BRIDGE_SRC)

    def test_retry_dispatches_from_parameter(self):
        """screen.dump.retry must parse a 'from' chunk index from the request."""
        retry_pos = self.src.find('"screen.dump.retry"')
        self.assertGreater(retry_pos, 0, "screen.dump.retry not found in dispatch")
        window = self.src[retry_pos:retry_pos + 500]
        # Must parse a numeric parameter for the starting chunk
        self.assertTrue(
            re.search(
                r'has_number\(root,\s*"(?:from|start_index|chunk|index)"',
                window
            ),
            "screen.dump.retry must parse a numeric chunk index parameter "
            "(from/start_index/chunk/index)"
        )

    def test_retry_passes_from_to_stream_dump(self):
        """The parsed 'from' index must be passed to stream_dump."""
        retry_pos = self.src.find('"screen.dump.retry"')
        window = self.src[retry_pos:retry_pos + 600]
        # stream_dump must receive the from parameter somehow
        self.assertTrue(
            re.search(r'stream_dump\([^)]*(?:from|start_index|chunk|index)', window) or
            re.search(r'from_chunk|from_idx|skip_chunks|start_chunk', window),
            "the parsed 'from' index must be forwarded to stream_dump "
            "for partial retransmission"
        )


class StrictMergeContract(unittest.TestCase):
    """After merge, chunk indices must be strictly 0..N-1 without duplicates."""

    def test_device_validation_merges_retry_chunks(self):
        """Device validation must support retry+merge workflow."""
        if not DEVICE_VALIDATION.exists():
            self.skipTest("device validation not present")
        dev_src = _read(DEVICE_VALIDATION)
        # The exchange_dump method must support collecting chunks across
        # multiple dump+retry cycles
        self.assertTrue(
            re.search(r'screen\.dump\.retry|retry.*dump|retransmit', dev_src, re.IGNORECASE),
            "device validation must include retry/retransmit logic for "
            "missing chunks"
        )

    def test_chunk_indices_after_merge_are_contiguous(self):
        """After merging original + retry chunks, indices must be 0..N-1."""
        if not DEVICE_VALIDATION.exists():
            self.skipTest("device validation not present")
        dev_src = _read(DEVICE_VALIDATION)
        self.assertTrue(
            re.search(r'chunk_indices_contiguous|indices.*range', dev_src),
            "device validation must verify chunk index contiguity after merge"
        )


class ResumeImplicitFromContract(unittest.TestCase):
    """screen.dump.resume must retransmit from last received index + 1."""

    @classmethod
    def setUpClass(cls):
        cls.src = _read(SERIAL_BRIDGE_SRC)

    def test_resume_dispatches_to_stream_dump(self):
        """screen.dump.resume must route to stream_dump."""
        resume_pos = self.src.find('"screen.dump.resume"')
        self.assertGreater(resume_pos, 0, "screen.dump.resume not found")
        window = self.src[resume_pos:resume_pos + 300]
        self.assertIn("stream_dump", window,
                       "screen.dump.resume must route to stream_dump()")

    def test_resume_implements_partial_retransmission(self):
        """resume must retransmit from a computed start index, not from 0."""
        resume_pos = self.src.find('"screen.dump.resume"')
        self.assertGreater(resume_pos, 0)
        window = self.src[resume_pos:resume_pos + 500]
        # resume should compute the start index from the last received chunk
        # rather than always starting from 0
        self.assertTrue(
            re.search(r'(?:from|start_index|chunk|last_index|resume_from)', window, re.IGNORECASE),
            "resume must compute a start index for partial retransmission "
            "(not always from chunk 0)"
        )


class CliRetryMergeContract(unittest.TestCase):
    """The tab5_cli.py script must support retry+merge for screen.dump."""

    def test_cli_exists(self):
        """tab5_cli.py must exist."""
        self.assertTrue(PYTHON_CLI.exists(),
                        "tools/tab5_cli.py must exist for CLI automation")

    def test_cli_handles_missing_chunks(self):
        """CLI must detect gaps in chunk indices and request retry."""
        cli_src = _read(PYTHON_CLI)
        self.assertTrue(
            re.search(r'screen\.dump\.retry|retry|retransmit|missing.*chunk',
                       cli_src, re.IGNORECASE),
            "tab5_cli.py must handle missing chunks with retry"
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
