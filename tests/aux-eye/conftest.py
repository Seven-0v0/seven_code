from pathlib import Path

import pytest


@pytest.fixture
def fixtures_dir() -> str:
    return str(Path(__file__).resolve().parents[1] / "fixtures" / "aux-eye")
