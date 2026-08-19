#!/usr/bin/env python3
# Copyright (C) 2026 Sungsan
# SPDX-License-Identifier: GPL-2.0-only

"""Fail fast if the reviewed libaec mirror override is lost or weakened."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PORT = ROOT / "vcpkg" / "ports" / "libaec" / "portfile.cmake"
MANIFEST = ROOT / "vcpkg" / "ports" / "libaec" / "vcpkg.json"
EXPECTED_SHA512 = (
    "76df7501d1b7d91a43b525ba828f092f18d83f8ab09a9331e5758f93942a9758"
    "ad580baca8f9316b92a98639bde2e23cacbc2f33f52d0dd98ce7efe412cf43cd"
)


def main() -> None:
    port = PORT.read_text(encoding="utf-8")
    manifest = MANIFEST.read_text(encoding="utf-8")

    required = (
        "vcpkg_from_github(",
        "REPO Deutsches-Klimarechenzentrum/libaec",
        'REF "v${VERSION}"',
        f"SHA512 {EXPECTED_SHA512}",
    )
    for marker in required:
        if marker not in port:
            raise SystemExit(f"libaec mirror check failed: missing {marker}")

    if "gitlab.dkrz.de" in port:
        raise SystemExit("libaec mirror check failed: rate-limited GitLab URL remains")
    if '"version": "1.1.6"' not in manifest:
        raise SystemExit("libaec mirror check failed: version is not 1.1.6")

    print("Sungsan libaec source check passed: official GitHub mirror, v1.1.6, SHA-512 pinned")


if __name__ == "__main__":
    main()
