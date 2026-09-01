"""Tests for the exact BuildScope release checksum manifest contract."""

from __future__ import annotations

import hashlib
import os
import sys
import unittest
from contextlib import redirect_stderr
from io import StringIO
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

CI_DIR = Path(__file__).resolve().parent
if str(CI_DIR) not in sys.path:
    sys.path.insert(0, str(CI_DIR))

from check_buildscope_release_assets import BuildScopeReleaseAssetError
from check_buildscope_release_manifest import (
    check_release_manifest,
    main,
    manifest_asset_names,
)

VERSION = "0.5.0"


class BuildScopeReleaseManifestTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.dist = self.root / "dist"
        self.dist.mkdir()
        self._write_valid_dist()

    def _write_valid_dist(self) -> None:
        for index, name in enumerate(manifest_asset_names(VERSION)):
            if name == "buildscope.pyz.sha256":
                continue
            (self.dist / name).write_bytes(
                f"BuildScope manifest fixture {index}: {name}\n".encode()
            )
        pyz = self.dist / "buildscope.pyz"
        pyz_digest = hashlib.sha256(pyz.read_bytes()).hexdigest()
        (self.dist / "buildscope.pyz.sha256").write_text(
            f"{pyz_digest}  buildscope.pyz\n",
            encoding="utf-8",
        )
        self._rewrite_manifest()

    def _rewrite_manifest(self) -> None:
        lines = []
        for name in manifest_asset_names(VERSION):
            digest = hashlib.sha256((self.dist / name).read_bytes()).hexdigest()
            lines.append(f"{digest}  {name}")
        (self.dist / "SHA256SUMS").write_text("\n".join(lines) + "\n", encoding="utf-8")

    def test_accepts_exact_manifest_sidecar_and_streamed_files(self) -> None:
        with patch("check_buildscope_release_assets.HASH_CHUNK_BYTES", 7):
            check_release_manifest(self.dist, VERSION)

    def test_cli_accepts_the_valid_release_directory(self) -> None:
        self.assertEqual(main([str(self.dist), VERSION]), 0)

    def test_rejects_missing_extra_reordered_or_unsafe_manifest_names(self) -> None:
        manifest = self.dist / "SHA256SUMS"
        original = manifest.read_text(encoding="utf-8").splitlines()
        invalid = (
            original[:-1],
            [*original, original[-1]],
            [original[1], original[0], *original[2:]],
            [f"{'0' * 64}  ../buildscope.pyz", *original[1:]],
        )
        for lines in invalid:
            with self.subTest(lines=len(lines)):
                manifest.write_text("\n".join(lines) + "\n", encoding="utf-8")
                with self.assertRaises(BuildScopeReleaseAssetError):
                    check_release_manifest(self.dist, VERSION)

    def test_rejects_malformed_digest_separator_and_missing_newline(self) -> None:
        manifest = self.dist / "SHA256SUMS"
        original = manifest.read_text(encoding="utf-8")
        first, rest = original.split("\n", 1)
        invalid = (
            first.upper() + "\n" + rest,
            first[:64] + " *" + first[66:] + "\n" + rest,
            original.rstrip("\n"),
        )
        for text in invalid:
            with self.subTest(text=text[:70]):
                manifest.write_text(text, encoding="utf-8")
                with self.assertRaises(BuildScopeReleaseAssetError):
                    check_release_manifest(self.dist, VERSION)

    def test_rejects_manifest_digest_or_pyz_sidecar_mismatch(self) -> None:
        pyz = self.dist / "buildscope.pyz"
        pyz.write_bytes(pyz.read_bytes() + b"changed")
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "digest mismatch"):
            check_release_manifest(self.dist, VERSION)

        self._rewrite_manifest()
        sidecar = self.dist / "buildscope.pyz.sha256"
        sidecar.write_text(f"{'0' * 64}  buildscope.pyz\n", encoding="utf-8")
        self._rewrite_manifest()
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "sidecar|standalone"):
            check_release_manifest(self.dist, VERSION)

    def test_rejects_missing_empty_symlink_or_nonregular_assets(self) -> None:
        asset = self.dist / "buildscope-ici-deep.json"
        asset.unlink()
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "cannot be inspected"):
            check_release_manifest(self.dist, VERSION)

        asset.write_bytes(b"")
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "must not be empty"):
            check_release_manifest(self.dist, VERSION)

        asset.unlink()
        target = self.root / "outside.json"
        target.write_bytes(b"outside")
        os.symlink(target, asset)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "regular file"):
            check_release_manifest(self.dist, VERSION)

    def test_rejects_symlinked_manifest_or_release_directory(self) -> None:
        manifest = self.dist / "SHA256SUMS"
        target = self.root / "manifest.txt"
        manifest.rename(target)
        os.symlink(target, manifest)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "regular file"):
            check_release_manifest(self.dist, VERSION)

        manifest.unlink()
        target.rename(manifest)
        real_dist = self.root / "real-dist"
        self.dist.rename(real_dist)
        os.symlink(real_dist, self.dist)
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "real directory"):
            check_release_manifest(self.dist, VERSION)

    def test_rejects_manifest_bounds_encoding_controls_and_invalid_version(
        self,
    ) -> None:
        manifest = self.dist / "SHA256SUMS"
        with (
            patch("check_buildscope_release_manifest.MAX_MANIFEST_BYTES", 16),
            self.assertRaisesRegex(BuildScopeReleaseAssetError, "outside"),
        ):
            check_release_manifest(self.dist, VERSION)

        manifest.write_bytes(b"\xff\n")
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "UTF-8"):
            check_release_manifest(self.dist, VERSION)

        manifest.write_bytes(b"bad\r\n")
        with self.assertRaisesRegex(BuildScopeReleaseAssetError, "control"):
            check_release_manifest(self.dist, VERSION)

        with self.assertRaisesRegex(
            BuildScopeReleaseAssetError, "invalid BuildScope version"
        ):
            check_release_manifest(self.dist, "0.5")

    def test_cli_reports_a_clear_nonzero_error(self) -> None:
        (self.dist / "SHA256SUMS").unlink()
        stderr = StringIO()
        with redirect_stderr(stderr), self.assertRaises(SystemExit) as raised:
            main([str(self.dist), VERSION])
        self.assertEqual(raised.exception.code, 1)
        self.assertIn("BuildScope release manifest audit failed", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
