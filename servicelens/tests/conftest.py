from pathlib import Path

import pytest


@pytest.fixture
def image(tmp_path: Path):
    def write(path: str, text: str = "", link: str | None = None) -> Path:
        target = tmp_path / path.lstrip("/")
        target.parent.mkdir(parents=True, exist_ok=True)
        if link is not None:
            target.symlink_to(link)
        else:
            target.write_text(text, encoding="utf-8")
        return target

    return tmp_path, write
