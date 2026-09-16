#!/usr/bin/env python3
"""Contract for the CI test boundary."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = (ROOT / ".github/workflows/quality-gate.yml").read_text(encoding="utf-8")


def test_ci_runs_only_official_tests_tree():
    assert "python3 -m pytest -q -ra tests" in WORKFLOW
    assert "pytest -q ." not in WORKFLOW
    assert "--ignore=managed_components" in WORKFLOW
    assert "--ignore=tests/device" in WORKFLOW


def test_ci_excludes_external_fixture_roots_and_declares_real_count():
    assert "--ignore=tests/fixtures" in WORKFLOW
    assert "--ignore=tests/external" in WORKFLOW
    assert "358 tests" not in WORKFLOW
    assert "353 passed" not in WORKFLOW
    assert "355 passed" not in WORKFLOW
    assert "pytest.log" in WORKFLOW
    assert "Pytest result" in WORKFLOW
