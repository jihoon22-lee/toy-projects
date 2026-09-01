#!/usr/bin/env python3
"""Build the dependency-free, reproducible BuildScope command-line zipapp."""

from __future__ import annotations

import argparse
import ast
import contextlib
import hashlib
import os
import re
import tempfile
import zipfile
from collections.abc import Iterable
from pathlib import Path

FIXED_ZIP_TIME = (1980, 1, 1, 0, 0, 0)
SHEBANG = b"#!/usr/bin/env python3\n"
ENTRYPOINT = b"from buildscope.__main__ import main\nraise SystemExit(main())\n"
EXPECTED_SCHEMAS = (
    "buildscope-diff-v1.schema.json",
    "buildscope-snapshot-v1.schema.json",
    "buildscope-snapshot-v2.schema.json",
    "buildscope-snapshot-v3.schema.json",
)


def _string_assignment(path: Path, name: str) -> str:
    tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    for node in tree.body:
        if not isinstance(node, ast.Assign):
            continue
        if not any(isinstance(target, ast.Name) and target.id == name for target in node.targets):
            continue
        value = ast.literal_eval(node.value)
        if isinstance(value, str):
            return value
    raise ValueError(f"{path} does not define a string {name}")


def _single_match(path: Path, pattern: str, label: str) -> str:
    matches = re.findall(pattern, path.read_text(encoding="utf-8"), flags=re.MULTILINE)
    if len(matches) != 1:
        raise ValueError(f"{path} must contain exactly one {label}")
    return matches[0]


def validate_version(project_root: Path) -> str:
    """Return the release version after checking every public metadata surface."""

    version = _string_assignment(project_root / "python/buildscope/__init__.py", "__version__")
    values = {
        "pyproject.toml": _single_match(
            project_root / "pyproject.toml",
            r'^version\s*=\s*"([^"]+)"$',
            "project version",
        ),
        "CMakeLists.txt": _single_match(
            project_root / "CMakeLists.txt",
            r"^project\(buildscope VERSION ([^ )]+)",
            "CMake project version",
        ),
        "ici.toml": _single_match(
            project_root / "ici.toml",
            r'^version\s*=\s*"([^"]+)"$',
            "ici project version",
        ),
    }
    mismatched = {name: value for name, value in values.items() if value != version}
    if mismatched:
        raise ValueError(f"BuildScope version metadata disagrees with {version}: {mismatched}")
    return version


def _payload_files(project_root: Path) -> Iterable[tuple[str, bytes]]:
    package_root = project_root / "python/buildscope"
    for path in sorted(package_root.rglob("*")):
        if not path.is_file() or "__pycache__" in path.parts or path.suffix == ".pyc":
            continue
        relative = path.relative_to(project_root / "python").as_posix()
        yield relative, path.read_bytes()

    schemas_root = project_root / "schemas"
    actual_schemas = tuple(path.name for path in sorted(schemas_root.glob("*.schema.json")))
    if actual_schemas != EXPECTED_SCHEMAS:
        raise ValueError(
            f"schema inventory changed: expected={EXPECTED_SCHEMAS!r} actual={actual_schemas!r}"
        )
    for name in actual_schemas:
        yield f"buildscope/schemas/{name}", (schemas_root / name).read_bytes()


def _zip_info(name: str) -> zipfile.ZipInfo:
    info = zipfile.ZipInfo(name, FIXED_ZIP_TIME)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    info.external_attr = 0o100644 << 16
    return info


def build_standalone(project_root: Path, output: Path) -> tuple[str, str]:
    """Build ``output`` atomically and return its version and SHA-256 digest."""

    project_root = project_root.resolve(strict=True)
    version = validate_version(project_root)
    output = output.absolute()
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.is_symlink():
        raise ValueError(f"refusing to replace symlink output: {output}")

    descriptor, temporary_name = tempfile.mkstemp(
        dir=output.parent,
        prefix=f".{output.name}.",
        suffix=".tmp",
    )
    try:
        with os.fdopen(descriptor, "w+b") as stream:
            stream.write(SHEBANG)
            with zipfile.ZipFile(
                stream,
                mode="w",
                compression=zipfile.ZIP_DEFLATED,
                compresslevel=9,
                strict_timestamps=True,
            ) as archive:
                archive.writestr(_zip_info("__main__.py"), ENTRYPOINT)
                seen = {"__main__.py"}
                for name, payload in _payload_files(project_root):
                    if name in seen:
                        raise ValueError(f"duplicate standalone archive member: {name}")
                    seen.add(name)
                    archive.writestr(_zip_info(name), payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary_name, 0o755)
        os.replace(temporary_name, output)
    except BaseException:
        with contextlib.suppress(FileNotFoundError):
            os.unlink(temporary_name)
        raise

    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    return version, digest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--project-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="BuildScope source root (default: parent of tools/)",
    )
    parser.add_argument("--output", type=Path, required=True, help="output .pyz path")
    args = parser.parse_args()
    version, digest = build_standalone(args.project_root, args.output)
    print(f"built BuildScope {version}: {args.output} sha256:{digest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
