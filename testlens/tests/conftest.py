from pathlib import Path

import pytest

from testlens.core import collect


@pytest.fixture
def fixture_dir():
    return Path(__file__).parent / "fixtures"


@pytest.fixture
def make_run(tmp_path):
    counter = 0

    def make(xml, **kwargs):
        nonlocal counter
        counter += 1
        path = tmp_path / f"report-{counter}.xml"
        path.write_text(xml, encoding="utf-8")
        options = {
            "project": "demo",
            "run_id": f"run-{counter}",
            "declared_complete": True,
            "executed_at": f"2026-01-{counter:02d}T00:00:00Z",
        }
        options.update(kwargs)
        return collect([path], **options)

    return make
