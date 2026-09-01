"""Regression tests for the BuildScope release payload auditor."""

from __future__ import annotations

import base64
import hashlib
import io
import json
import stat
import sys
import tarfile
import unittest
import warnings
import zipfile
from collections.abc import Sequence
from contextlib import redirect_stderr
from pathlib import Path
from tempfile import TemporaryDirectory
from typing import cast

CI_DIR = Path(__file__).resolve().parent
if str(CI_DIR) not in sys.path:
    sys.path.insert(0, str(CI_DIR))

from check_buildscope_release_payload import (
    PACKAGE_MODULES,
    PYZ_SHEBANG,
    SCHEMA_NAMES,
    BuildScopeReleasePayloadError,
    check_bundle,
    check_provenance,
    check_pyz,
    check_release_payload,
    check_sdist,
    check_wheel,
    main,
)

VERSION = "0.5.0"
TAG = f"buildscope-v{VERSION}"
REPOSITORY = "jihoon22-lee/toy-projects"
TARGET_SHA = "a" * 40
ICI_VERSION = "v0.10.2"
ICI_SHA256 = "b" * 64
MERGE_GATE_URL = f"https://github.com/{REPOSITORY}/actions/runs/12345"
ZipEntry = tuple[str | zipfile.ZipInfo, bytes]
TarEntry = tuple[str, bytes, str]


class BuildScopeReleasePayloadTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.dist = self.root / "dist"
        self.dist.mkdir()
        self._write_valid_payload()

    @staticmethod
    def _write_zip(
        path: Path,
        entries: Sequence[ZipEntry],
        *,
        prefix: bytes = b"",
        mode: int | None = None,
    ) -> None:
        buffer = io.BytesIO()
        with zipfile.ZipFile(buffer, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for name, payload in entries:
                archive.writestr(name, payload)
        path.write_bytes(prefix + buffer.getvalue())
        if mode is not None:
            path.chmod(mode)

    @staticmethod
    def _write_tar(path: Path, entries: Sequence[TarEntry]) -> None:
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode="w:gz") as archive:
            for name, payload, kind in entries:
                member = tarfile.TarInfo(name)
                member.mtime = 0
                if kind == "file":
                    member.mode = 0o644
                    member.size = len(payload)
                    archive.addfile(member, io.BytesIO(payload))
                elif kind == "executable":
                    member.mode = 0o755
                    member.size = len(payload)
                    archive.addfile(member, io.BytesIO(payload))
                elif kind == "symlink":
                    member.type = tarfile.SYMTYPE
                    member.linkname = payload.decode("ascii")
                    archive.addfile(member)
                elif kind == "hardlink":
                    member.type = tarfile.LNKTYPE
                    member.linkname = payload.decode("ascii")
                    archive.addfile(member)
                elif kind == "special":
                    member.type = tarfile.CHRTYPE
                    member.devmajor = 1
                    member.devminor = 3
                    archive.addfile(member)
                else:
                    raise AssertionError(f"unknown tar fixture kind: {kind}")
        path.write_bytes(buffer.getvalue())

    @staticmethod
    def _metadata(version: str = VERSION) -> bytes:
        return (
            "Metadata-Version: 2.1\n"
            "Name: buildscope\n"
            f"Version: {version}\n"
            "Summary: Offline explorer for C and C++ compilation databases\n"
            "Requires-Python: >=3.10\n"
            "\n"
        ).encode("ascii")

    @staticmethod
    def _tool_record(name: str) -> dict[str, object]:
        return {
            "name": name,
            "path": f"/usr/bin/{name}",
            "version": f"{name} 18.1.0",
            "argv": [name, "--version"],
            "returncode": 0,
            "timed_out": False,
            "truncated": False,
            "error": "",
        }

    def _deep_report(self) -> dict[str, object]:
        lint_extra = {
            "qt_codegen_mode": "exact",
            "qt_codegen_inputs_checked": 3,
            "qt_codegen_moc_checked": 1,
            "qt_codegen_ui_checked": 1,
            "qt_codegen_qrc_checked": 1,
            "cpp_analysis_mode": "exact",
            "cpp_configurations_checked": 1,
            "qt5_compile_units": 0,
            "qt6_compile_units": 1,
            "clang_tidy_mode": "exact",
            "clang_tidy_sources_checked": 1,
            "clang_tidy_configurations_checked": 1,
            "clazy_mode": "exact",
            "clazy_sources_checked": 1,
            "clazy_configurations_checked": 1,
            "clazy_provider": "standalone",
        }

        def engine(name: str, extra: dict[str, object]) -> dict[str, object]:
            return {
                "schema_version": "ici.result/v3",
                "engine_name": name,
                "status": "PASS",
                "extra": extra,
                "cache_hit": False,
                "tool_evidence": [],
            }

        lint = engine("lint", lint_extra)
        lint["tool_evidence"] = [
            self._tool_record("clang-tidy"),
            self._tool_record("clazy"),
        ]
        return {
            "schema_version": "ici.result/v3",
            "analysis_metadata": {"producer_version": "0.10.2"},
            "analysis_context": {"profile": "deep"},
            "results": [
                lint,
                engine(
                    "compile_db",
                    {
                        "configurations": 27,
                        "production_units": 1,
                        "covered_units": 1,
                        "coverage_percent": 100.0,
                        "issues_count": 0,
                    },
                ),
                engine(
                    "test",
                    {"passed_tests": 1, "total_tests": 1, "pass_rate": 1.0},
                ),
            ],
        }

    @staticmethod
    def _deep_html() -> str:
        return (
            "<!doctype html><html><head>"
            "<title>ici Verification Report — buildscope</title>"
            "<style>body{background:#fff}</style>"
            "</head><body>BuildScope fixture</body></html>"
        )

    @staticmethod
    def _schema_entries(prefix: str) -> list[ZipEntry]:
        return [(f"{prefix}{name}", b'{"type":"object"}\n') for name in SCHEMA_NAMES]

    @staticmethod
    def _schema_tar_entries(prefix: str) -> list[TarEntry]:
        return [
            (f"{prefix}{name}", b'{"type":"object"}\n', "file") for name in SCHEMA_NAMES
        ]

    def _pyz_entries(self) -> list[ZipEntry]:
        modules = [
            (
                f"buildscope/{name}",
                b'__version__ = "0.5.0"\n' if name == "__init__.py" else b"pass\n",
            )
            for name in PACKAGE_MODULES
        ]
        return [
            ("__main__.py", b"pass\n"),
            *modules,
            *self._schema_entries("buildscope/schemas/"),
        ]

    def _wheel_entries(self) -> list[ZipEntry]:
        dist_info = f"buildscope-{VERSION}.dist-info/"
        entries: list[ZipEntry] = [
            *(
                (
                    f"buildscope/{name}",
                    b'__version__ = "0.5.0"\n' if name == "__init__.py" else b"pass\n",
                )
                for name in PACKAGE_MODULES
            ),
            *self._schema_entries("buildscope/schemas/"),
            (
                f"{dist_info}WHEEL",
                (
                    b"Wheel-Version: 1.0\n"
                    b"Generator: release-payload-test\n"
                    b"Root-Is-Purelib: true\n"
                    b"Tag: py3-none-any\n"
                ),
            ),
            (f"{dist_info}METADATA", self._metadata()),
            (
                f"{dist_info}entry_points.txt",
                (
                    b"[console_scripts]\n"
                    b"buildscope = buildscope.__main__:main\n"
                    b"buildscope-diff = buildscope.diff_cli:main\n"
                ),
            ),
        ]
        rows = []
        for name, payload in entries:
            digest = base64.urlsafe_b64encode(hashlib.sha256(payload).digest())
            encoded = digest.rstrip(b"=").decode("ascii")
            rows.append(f"{name},sha256={encoded},{len(payload)}")
        rows.append(f"{dist_info}RECORD,,")
        entries.append((f"{dist_info}RECORD", ("\n".join(rows) + "\n").encode("utf-8")))
        return entries

    def _sdist_entries(self) -> list[TarEntry]:
        root = f"buildscope-{VERSION}/"
        entries: list[TarEntry] = [
            (f"{root}README.md", b"BuildScope\n", "file"),
            (f"{root}docs/quickstart.md", b"Quickstart\n", "file"),
            (
                f"{root}pyproject.toml",
                (
                    b"[build-system]\n"
                    b'requires = ["hatchling>=1.27"]\n'
                    b'build-backend = "hatchling.build"\n'
                    b"[project]\n"
                    b'name = "buildscope"\n'
                    b'version = "0.5.0"\n'
                    b'requires-python = ">=3.10"\n'
                    b"dependencies = []\n"
                    b"[project.scripts]\n"
                    b'buildscope = "buildscope.__main__:main"\n'
                    b'buildscope-diff = "buildscope.diff_cli:main"\n'
                ),
                "file",
            ),
            *(
                (
                    f"{root}python/buildscope/{name}",
                    b'__version__ = "0.5.0"\n' if name == "__init__.py" else b"pass\n",
                    "file",
                )
                for name in PACKAGE_MODULES
            ),
            (f"{root}PKG-INFO", self._metadata(), "file"),
            *self._schema_tar_entries(f"{root}schemas/"),
        ]
        for relative in (
            "examples/cmake/compile_commands.json",
            "examples/cmake/include/buildscope-example/message.hpp",
            "examples/cmake/src/main.cpp",
            "examples/cmake/src/message.cpp",
            "examples/qmake/compile_commands.json",
            "examples/qmake/include/buildscope-example/message.hpp",
            "examples/qmake/src/main.cpp",
            "examples/qmake/src/message.cpp",
            "include/buildscope/contract.hpp",
            "src/core/contract.cpp",
            "src/gui/main_window.cpp",
            "ui/main_window.ui",
        ):
            entries.append((f"{root}{relative}", b"fixture\n", "file"))
        return entries

    @staticmethod
    def _elf64() -> bytes:
        header = bytearray(b"\x7fELF" + bytes((2, 1)) + b"\0" * 12)
        header[18:20] = (62).to_bytes(2, "little")
        return bytes(header) + b"fake executable\n"

    def _provenance(self) -> dict[str, object]:
        return {
            "product": "buildscope",
            "version": VERSION,
            "tag": TAG,
            "target_commit": TARGET_SHA,
            "exact_main_commit": TARGET_SHA,
            "merge_gate_check": MERGE_GATE_URL,
            "workflow": {
                "name": "BuildScope Release",
                "run_id": "12345",
                "server": "https://github.com",
                "repository": REPOSITORY,
            },
            "runner": {"os": "linux", "architecture": "x86_64"},
            "ici": {
                "version": ICI_VERSION,
                "asset": "ici.pyz",
                "sha256": ICI_SHA256,
            },
        }

    def _write_valid_payload(self) -> None:
        self._write_zip(
            self.dist / "buildscope.pyz",
            self._pyz_entries(),
            prefix=PYZ_SHEBANG,
            mode=0o755,
        )
        self._write_zip(
            self.dist / f"buildscope-{VERSION}-py3-none-any.whl",
            self._wheel_entries(),
        )
        self._write_tar(
            self.dist / f"buildscope-{VERSION}.tar.gz", self._sdist_entries()
        )
        self._write_bundle()
        (self.dist / "buildscope-provenance.json").write_text(
            json.dumps(self._provenance(), indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        (self.dist / "buildscope-ici-deep.json").write_text(
            json.dumps(self._deep_report(), indent=2, sort_keys=True) + "\n",
            encoding="utf-8",
        )
        (self.dist / "buildscope-ici-deep.html").write_text(
            self._deep_html(), encoding="utf-8"
        )
        pyz_digest = hashlib.sha256(
            (self.dist / "buildscope.pyz").read_bytes()
        ).hexdigest()
        (self.dist / "buildscope.pyz.sha256").write_text(
            f"{pyz_digest}  buildscope.pyz\n", encoding="utf-8"
        )
        manifest_names = (
            "buildscope.pyz",
            "buildscope.pyz.sha256",
            f"buildscope-{VERSION}-py3-none-any.whl",
            f"buildscope-{VERSION}.tar.gz",
            "buildscope-ici-deep.json",
            "buildscope-ici-deep.html",
            "buildscope-provenance.json",
            f"buildscope-{VERSION}-linux-x86_64.tar.gz",
        )
        manifest = "".join(
            f"{hashlib.sha256((self.dist / name).read_bytes()).hexdigest()}  {name}\n"
            for name in manifest_names
        )
        (self.dist / "SHA256SUMS").write_text(manifest, encoding="utf-8")

    def _write_bundle(
        self,
        *,
        cli: bytes | None = None,
        gui: bytes | None = None,
        extra_entries: Sequence[TarEntry] = (),
    ) -> None:
        root = f"buildscope-{VERSION}-linux-x86_64/"
        entries: list[TarEntry] = [
            (
                f"{root}README.runtime.md",
                f"BuildScope {VERSION} runtime for Qt\n".encode("ascii"),
                "file",
            ),
            (f"{root}bin/buildscope-cli", cli or self._elf64(), "executable"),
            (f"{root}bin/buildscope-gui", gui or self._elf64(), "executable"),
            (f"{root}share/doc/buildscope/README.md", b"BuildScope\n", "file"),
            (
                f"{root}share/doc/buildscope/quickstart.md",
                b"Quickstart\n",
                "file",
            ),
            (
                f"{root}share/buildscope/examples/cmake/CMakeLists.txt",
                b"cmake_minimum_required(VERSION 3.16)\n",
                "file",
            ),
            (
                f"{root}share/buildscope/examples/cmake/compile_commands.json",
                b"[]\n",
                "file",
            ),
            (
                f"{root}share/buildscope/examples/cmake/include/buildscope-example/message.hpp",
                b"#pragma once\n",
                "file",
            ),
            (
                f"{root}share/buildscope/examples/cmake/src/main.cpp",
                b"int main() { return 0; }\n",
                "file",
            ),
            (
                f"{root}share/buildscope/examples/cmake/src/message.cpp",
                b"// fixture\n",
                "file",
            ),
            (
                f"{root}share/buildscope/examples/qmake/example.pro",
                b"TEMPLATE = app\n",
                "file",
            ),
            (
                f"{root}share/buildscope/examples/qmake/compile_commands.json",
                b"[]\n",
                "file",
            ),
            (
                f"{root}share/buildscope/examples/qmake/include/buildscope-example/message.hpp",
                b"#pragma once\n",
                "file",
            ),
            (
                f"{root}share/buildscope/examples/qmake/src/main.cpp",
                b"int main() { return 0; }\n",
                "file",
            ),
            (
                f"{root}share/buildscope/examples/qmake/src/message.cpp",
                b"// fixture\n",
                "file",
            ),
            *self._schema_tar_entries(f"{root}share/buildscope/schemas/"),
        ]
        for name in (
            "buildscope.pyz",
            f"buildscope-{VERSION}-py3-none-any.whl",
            f"buildscope-{VERSION}.tar.gz",
        ):
            entries.append((f"{root}{name}", (self.dist / name).read_bytes(), "file"))
        entries.extend(extra_entries)
        self._write_tar(
            self.dist / f"buildscope-{VERSION}-linux-x86_64.tar.gz", entries
        )

    def _check(self) -> None:
        check_release_payload(
            self.dist,
            VERSION,
            TAG,
            TARGET_SHA,
            TARGET_SHA,
            MERGE_GATE_URL,
            REPOSITORY,
            ICI_VERSION,
            ICI_SHA256,
        )

    def test_accepts_minimal_complete_payload(self) -> None:
        self._check()

    def test_rejects_wheel_traversal_duplicate_symlink_and_native_extension(
        self,
    ) -> None:
        wheel = self.dist / f"buildscope-{VERSION}-py3-none-any.whl"

        cases: list[tuple[str, ZipEntry, str]] = [
            ("traversal", ("../escape", b"bad"), "unsafe"),
            (
                "symlink",
                (
                    self._symlink_zip_info("buildscope/evil"),
                    b"target",
                ),
                "non-file",
            ),
            (
                "native extension",
                ("buildscope/_native.so", b"native"),
                "member inventory|native",
            ),
        ]
        for label, entry, message in cases:
            with self.subTest(label=label):
                self._write_zip(wheel, [*self._wheel_entries(), entry])
                with self.assertRaisesRegex(BuildScopeReleasePayloadError, message):
                    check_wheel(wheel, VERSION)

        with self.subTest(label="duplicate"):
            entries = self._wheel_entries()
            entries.append(entries[0])
            with warnings.catch_warnings():
                warnings.simplefilter("ignore", UserWarning)
                self._write_zip(wheel, entries)
            with self.assertRaisesRegex(BuildScopeReleasePayloadError, "duplicate"):
                check_wheel(wheel, VERSION)

    @staticmethod
    def _symlink_zip_info(name: str) -> zipfile.ZipInfo:
        info = zipfile.ZipInfo(name)
        info.create_system = 3
        info.external_attr = (stat.S_IFLNK | 0o777) << 16
        return info

    def test_rejects_wrong_wheel_metadata_and_pyz_schema_inventory(self) -> None:
        wheel = self.dist / f"buildscope-{VERSION}-py3-none-any.whl"
        entries = self._wheel_entries()
        metadata_name = f"buildscope-{VERSION}.dist-info/METADATA"
        entries = [
            (
                name,
                b"Metadata-Version: 2.1\nName: buildscope\n"
                b"Version: 9.9.9\nRequires-Python: >=3.10\n\n"
                if name == metadata_name
                else payload,
            )
            for name, payload in entries
        ]
        self._write_zip(wheel, entries)
        with self.assertRaisesRegex(BuildScopeReleasePayloadError, "Version"):
            check_wheel(wheel, VERSION)

        pyz = self.dist / "buildscope.pyz"
        self._write_zip(
            pyz,
            [
                entry
                for entry in self._pyz_entries()
                if entry[0] != "buildscope/schemas/" + SCHEMA_NAMES[-1]
            ],
            prefix=PYZ_SHEBANG,
            mode=0o755,
        )
        with self.assertRaisesRegex(
            BuildScopeReleasePayloadError, "schema inventory|member inventory"
        ):
            check_pyz(pyz, VERSION)

    def test_rejects_sdist_traversal_duplicate_links_and_special_members(self) -> None:
        sdist = self.dist / f"buildscope-{VERSION}.tar.gz"
        cases = (
            ("traversal", ("../escape", b"bad", "file"), "unsafe"),
            ("symlink", ("buildscope-0.5.0/link", b"README.md", "symlink"), "link"),
            (
                "hardlink",
                ("buildscope-0.5.0/hardlink", b"README.md", "hardlink"),
                "link",
            ),
            ("special", ("buildscope-0.5.0/device", b"", "special"), "link"),
        )
        for label, entry, message in cases:
            with self.subTest(label=label):
                self._write_tar(sdist, [*self._sdist_entries(), entry])
                with self.assertRaisesRegex(BuildScopeReleasePayloadError, message):
                    check_sdist(sdist, VERSION)

        with self.subTest(label="duplicate"):
            entries = self._sdist_entries()
            entries.append(entries[0])
            self._write_tar(sdist, entries)
            with self.assertRaisesRegex(BuildScopeReleasePayloadError, "duplicate"):
                check_sdist(sdist, VERSION)

    def test_rejects_bundle_embedded_mismatch_and_invalid_elf(self) -> None:
        bundle = self.dist / f"buildscope-{VERSION}-linux-x86_64.tar.gz"
        pyz = self.dist / "buildscope.pyz"
        pyz.write_bytes(pyz.read_bytes() + b"changed")
        with self.assertRaisesRegex(BuildScopeReleasePayloadError, "embedded artifact"):
            check_bundle(bundle, self.dist, VERSION)

        self._write_valid_payload()
        self._write_bundle(cli=b"not an ELF file" + b"\0" * 20)
        with self.assertRaisesRegex(BuildScopeReleasePayloadError, "ELF64"):
            check_bundle(bundle, self.dist, VERSION)

        self._write_valid_payload()
        self._write_bundle(
            extra_entries=[
                (
                    f"buildscope-{VERSION}-linux-x86_64/unexpected.bin",
                    b"unexpected",
                    "file",
                )
            ]
        )
        with self.assertRaisesRegex(BuildScopeReleasePayloadError, "file inventory"):
            check_bundle(bundle, self.dist, VERSION)

    def test_rejects_python_package_drift_between_distribution_formats(self) -> None:
        sdist = self.dist / f"buildscope-{VERSION}.tar.gz"
        command_path = f"buildscope-{VERSION}/python/buildscope/_command.py"
        entries = [
            (name, b"different package bytes\n" if name == command_path else data, kind)
            for name, data, kind in self._sdist_entries()
        ]
        self._write_tar(sdist, entries)
        self._write_bundle()
        with self.assertRaisesRegex(
            BuildScopeReleasePayloadError, "package or schema bytes"
        ):
            self._check()

    def test_rejects_provenance_extra_and_wrong_fields(self) -> None:
        provenance = self.dist / "buildscope-provenance.json"
        payload = self._provenance()
        payload["unexpected"] = True
        provenance.write_text(json.dumps(payload) + "\n", encoding="utf-8")
        with self.assertRaisesRegex(BuildScopeReleasePayloadError, "keys mismatch"):
            check_provenance(
                provenance,
                version=VERSION,
                tag=TAG,
                target_sha=TARGET_SHA,
                main_sha=TARGET_SHA,
                merge_gate_url=MERGE_GATE_URL,
                repository=REPOSITORY,
                ici_version=ICI_VERSION,
                ici_sha256=ICI_SHA256,
            )

        payload = self._provenance()
        workflow = cast(dict[str, object], payload["workflow"])
        workflow["run_id"] = "0"
        provenance.write_text(json.dumps(payload) + "\n", encoding="utf-8")
        with self.assertRaisesRegex(BuildScopeReleasePayloadError, "run_id"):
            check_provenance(
                provenance,
                version=VERSION,
                tag=TAG,
                target_sha=TARGET_SHA,
                main_sha=TARGET_SHA,
                merge_gate_url=MERGE_GATE_URL,
                repository=REPOSITORY,
                ici_version=ICI_VERSION,
                ici_sha256=ICI_SHA256,
            )

        payload = self._provenance()
        payload["ici"] = {"version": "v9.9.9", "asset": "ici.pyz", "sha256": ICI_SHA256}
        provenance.write_text(json.dumps(payload) + "\n", encoding="utf-8")
        with self.assertRaisesRegex(BuildScopeReleasePayloadError, "ici identity"):
            check_provenance(
                provenance,
                version=VERSION,
                tag=TAG,
                target_sha=TARGET_SHA,
                main_sha=TARGET_SHA,
                merge_gate_url=MERGE_GATE_URL,
                repository=REPOSITORY,
                ici_version=ICI_VERSION,
                ici_sha256=ICI_SHA256,
            )

    def test_rejects_noncanonical_provenance_json_numbers_and_keys(self) -> None:
        provenance = self.dist / "buildscope-provenance.json"
        invalid_payloads = (
            '{"product": "buildscope", "product": "other"}',
            '{"product": "buildscope", "unexpected": NaN}',
            '{"product": "buildscope", "unexpected": 1e999}',
            '{"product": "buildscope", "unexpected": 123456789012345678901}',
        )
        for payload in invalid_payloads:
            with self.subTest(payload=payload):
                provenance.write_text(payload, encoding="utf-8")
                with self.assertRaisesRegex(
                    BuildScopeReleasePayloadError, "not valid JSON"
                ):
                    check_provenance(
                        provenance,
                        version=VERSION,
                        tag=TAG,
                        target_sha=TARGET_SHA,
                        main_sha=TARGET_SHA,
                        merge_gate_url=MERGE_GATE_URL,
                        repository=REPOSITORY,
                        ici_version=ICI_VERSION,
                        ici_sha256=ICI_SHA256,
                    )

    def test_rejects_release_version_identity_and_cli_reports_failure(self) -> None:
        with self.assertRaisesRegex(BuildScopeReleasePayloadError, "version and tag"):
            check_release_payload(
                self.dist,
                "0.5.1",
                TAG,
                TARGET_SHA,
                TARGET_SHA,
                MERGE_GATE_URL,
                REPOSITORY,
                ICI_VERSION,
                ICI_SHA256,
            )

        with self.assertRaisesRegex(BuildScopeReleasePayloadError, "Merge Gate URL"):
            check_release_payload(
                self.dist,
                VERSION,
                TAG,
                TARGET_SHA,
                TARGET_SHA,
                MERGE_GATE_URL + "/unexpected",
                REPOSITORY,
                ICI_VERSION,
                ICI_SHA256,
            )

        stderr = io.StringIO()
        with redirect_stderr(stderr), self.assertRaises(SystemExit) as raised:
            main(
                [
                    str(self.dist),
                    VERSION,
                    TAG,
                    TARGET_SHA,
                    "c" * 40,
                    MERGE_GATE_URL,
                    REPOSITORY,
                    ICI_VERSION,
                    ICI_SHA256,
                ]
            )
        self.assertEqual(raised.exception.code, 1)
        self.assertIn("release payload audit failed", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
