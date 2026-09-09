#!/usr/bin/env python3
"""Write the provenance record that ships alongside a product's release.

`<product>-provenance.json` is the asset a consumer reads to answer "where did
these bytes come from". It is only worth shipping if it cannot record a claim
that is not true, so the fields are validated before anything is written:

- the tag has to belong to this product and name this version,
- the released commit has to be the exact `main` commit the gate approved,
- the commit and the ici pin have to be well-formed digests.

Every one of those has already been enforced upstream by the provenance job.
Re-checking here is deliberate: this file is what outlives the workflow run, so
it should not be able to state something the run did not verify.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from release_registry import load_release_specs  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent

SHA1_PATTERN = re.compile(r"^[0-9a-f]{40}$")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")

REQUIRED = (
    "TAG",
    "VERSION",
    "TARGET_SHA",
    "MAIN_SHA",
    "GITHUB_RUN_ID",
    "GITHUB_SERVER_URL",
    "GITHUB_REPOSITORY",
    "ICI_VERSION",
    "ICI_PYZ_SHA256",
)


class ProvenanceError(Exception):
    """The release cannot describe itself truthfully."""


def _require(env: dict[str, str], key: str) -> str:
    value = env.get(key, "")
    if not value:
        raise ProvenanceError(f"provenance field is missing or empty: {key}")
    return value


def build_provenance(product: str, env: dict[str, str], repo_root: Path | None = None) -> dict:
    """Return the provenance payload, or raise describing the false claim."""

    specs = load_release_specs(repo_root or REPO_ROOT)
    if product not in specs:
        raise ProvenanceError(f"{product} is not a released product")
    spec = specs[product]

    for key in REQUIRED:
        _require(env, key)

    version = env["VERSION"]
    tag = env["TAG"]
    if not tag.startswith(spec.tag_prefix):
        raise ProvenanceError(
            f"tag {tag!r} does not belong to {product} (expected prefix {spec.tag_prefix!r})"
        )
    if tag != f"{spec.tag_prefix}{version}":
        raise ProvenanceError(
            f"tag {tag!r} does not name the released version {version!r}"
        )

    target_sha = env["TARGET_SHA"]
    main_sha = env["MAIN_SHA"]
    for label, value in (("target_commit", target_sha), ("exact_main_commit", main_sha)):
        if SHA1_PATTERN.fullmatch(value) is None:
            raise ProvenanceError(f"{label} is not a full commit sha: {value!r}")
    if target_sha != main_sha:
        raise ProvenanceError(
            f"release commit {target_sha} is not the exact main commit {main_sha}"
        )

    ici_digest = env["ICI_PYZ_SHA256"]
    if SHA256_PATTERN.fullmatch(ici_digest) is None:
        raise ProvenanceError(f"ici pin digest is not a SHA-256: {ici_digest!r}")

    return {
        "product": product,
        "version": version,
        "tag": tag,
        "target_commit": target_sha,
        "exact_main_commit": main_sha,
        "merge_gate_check": env.get("MERGE_GATE_URL", ""),
        "workflow": {
            "name": f"{spec.display_name} Release",
            "run_id": env["GITHUB_RUN_ID"],
            "server": env["GITHUB_SERVER_URL"],
            "repository": env["GITHUB_REPOSITORY"],
        },
        "runner": {"os": "linux", "architecture": "x86_64"},
        "ici": {
            "version": env["ICI_VERSION"],
            "asset": "ici.pyz",
            "sha256": ici_digest,
        },
    }


def serialize(payload: dict) -> str:
    """Serialize deterministically: two runs of one release must not differ."""

    return json.dumps(payload, indent=2, sort_keys=True) + "\n"


def main(argv: list[str] | None = None, env: dict[str, str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("product")
    parser.add_argument("destination", type=Path)
    args = parser.parse_args(argv)

    try:
        payload = build_provenance(args.product, dict(env if env is not None else os.environ))
    except ProvenanceError as error:
        print(f"release provenance is not writable: {error}", file=sys.stderr)
        return 1

    args.destination.write_text(serialize(payload), encoding="utf-8")
    print(f"wrote {args.destination.name} for {args.product} {payload['version']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
