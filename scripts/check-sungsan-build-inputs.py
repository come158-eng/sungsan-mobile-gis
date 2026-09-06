#!/usr/bin/env python3
# Copyright (C) 2026 Sungsan
# SPDX-License-Identifier: GPL-2.0-only
# Modified for Meta Engineering GIS by Sungsan on 2026-08-19.

"""Fast, dependency-free validation of every Sungsan Android brand input.

This gate runs both on the GitHub runner and again inside the build container,
before CMake starts the multi-hour vcpkg dependency build.
"""

from __future__ import annotations

import json
import re
import struct
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "branding" / "sungsan" / "assets"
FAILURES: list[str] = []
PASSES: list[str] = []


def require_file(relative: str, purpose: str) -> Path:
    path = ROOT / relative
    if not path.is_file() or path.stat().st_size == 0:
        FAILURES.append(f"{relative}: missing or empty ({purpose})")
    else:
        PASSES.append(purpose)
    return path


def require_text(relative: str, needle: str, purpose: str) -> None:
    path = require_file(relative, f"{purpose} file")
    if not path.is_file():
        return
    try:
        source = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        FAILURES.append(f"{relative}: UTF-8 read failed: {error}")
        return
    if needle not in source:
        FAILURES.append(f"{relative}: missing {needle!r} ({purpose})")
    else:
        PASSES.append(purpose)


def check_png(relative: str, expected_size: tuple[int, int] | None = None) -> None:
    path = require_file(relative, f"PNG exists: {relative}")
    if not path.is_file():
        return
    try:
        with path.open("rb") as stream:
            signature = stream.read(8)
            length_bytes = stream.read(4)
            chunk_type = stream.read(4)
            ihdr = stream.read(13)
    except OSError as error:
        FAILURES.append(f"{relative}: PNG read failed: {error}")
        return
    if signature != b"\x89PNG\r\n\x1a\n" or len(length_bytes) != 4:
        FAILURES.append(f"{relative}: invalid PNG signature")
        return
    length = struct.unpack(">I", length_bytes)[0]
    if chunk_type != b"IHDR" or length != 13 or len(ihdr) != 13:
        FAILURES.append(f"{relative}: invalid PNG IHDR")
        return
    width, height, bit_depth, color_type = struct.unpack(">IIBB", ihdr[:10])
    if width <= 0 or height <= 0 or bit_depth != 8 or color_type != 6:
        FAILURES.append(
            f"{relative}: expected non-empty 8-bit RGBA PNG; "
            f"got {width}x{height}, depth={bit_depth}, color={color_type}"
        )
        return
    if expected_size and (width, height) != expected_size:
        FAILURES.append(
            f"{relative}: expected {expected_size[0]}x{expected_size[1]}, "
            f"got {width}x{height}"
        )
        return
    PASSES.append(f"valid PNG: {relative} ({width}x{height})")


def check_xml(relative: str, purpose: str) -> None:
    path = require_file(relative, f"{purpose} exists")
    if not path.is_file():
        return
    try:
        ET.parse(path)
    except (OSError, ET.ParseError) as error:
        FAILURES.append(f"{relative}: invalid XML: {error}")
    else:
        PASSES.append(f"valid XML: {purpose}")


def check_android_manifest_string_resources() -> None:
    """Catch source-owned Android string links before the multi-hour build."""

    manifest_path = ROOT / "platform/android/AndroidManifest.xml.in"
    strings_path = ROOT / "branding/sungsan/android/res/values/strings.xml"
    generated_path = ROOT / "platform/android/generated.xml.in"
    package_path = ROOT / "cmake/Package.cmake"

    try:
        manifest = manifest_path.read_text(encoding="utf-8")
        strings_root = ET.parse(strings_path).getroot()
        generated_root = ET.parse(generated_path).getroot()
        package_source = package_path.read_text(encoding="utf-8")
    except (OSError, UnicodeError, ET.ParseError) as error:
        FAILURES.append(f"Android resource-link preflight failed: {error}")
        return

    manifest_names = set(re.findall(r"@string/([A-Za-z0-9_]+)", manifest))
    staged_names = {
        element.get("name", "")
        for root in (strings_root, generated_root)
        for element in root.findall("string")
        if element.get("name")
    }

    # fatal_error_msg is supplied by Qt's Android template at androiddeployqt
    # time. The other Manifest string resources are owned by this repository.
    qt_generated_names = {"fatal_error_msg"}
    missing = sorted(manifest_names - staged_names - qt_generated_names)
    if missing:
        FAILURES.append(
            "AndroidManifest.xml.in: unresolved source-owned @string resources: "
            + ", ".join(missing)
        )
    else:
        PASSES.append("all Manifest @string resources have a staged provider")

    required_stage_fragments = (
        "${ANDROID_TEMPLATE_FOLDER}/res/values/generated.xml",
        "platform/android/generated.xml.in",
        "@ONLY",
    )
    if not all(fragment in package_source for fragment in required_stage_fragments):
        FAILURES.append(
            "cmake/Package.cmake: configured git_rev resource is not staged "
            "under res/values/generated.xml"
        )
    else:
        PASSES.append("configured git_rev is staged in Android res/values")


def check_json(relative: str, purpose: str) -> None:
    path = require_file(relative, f"{purpose} exists")
    if not path.is_file():
        return
    try:
        json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        FAILURES.append(f"{relative}: invalid UTF-8 JSON: {error}")
    else:
        PASSES.append(f"valid JSON: {purpose}")


def main() -> int:
    icon_sizes = {
        "mdpi": (48, 48),
        "hdpi": (72, 72),
        "xhdpi": (96, 96),
        "xxhdpi": (144, 144),
        "xxxhdpi": (192, 192),
    }
    for density, dimensions in icon_sizes.items():
        check_png(
            f"branding/sungsan/assets/android/drawable-{density}/"
            "sungsan_mobile_gis.png",
            dimensions,
        )

    check_png("branding/sungsan/assets/sungsan_splash.png", (1288, 772))
    check_png("branding/sungsan/plugins/sungsan_vworld/icon.png")
    check_xml(
        "branding/sungsan/assets/android/drawable/sungsan_mobile_gis_vector.xml",
        "Sungsan Android vector icon",
    )
    check_xml(
        "branding/sungsan/assets/sungsan_mobile_gis.svg",
        "Sungsan Qt SVG icon",
    )
    check_xml(
        "branding/sungsan/android/res/values/strings.xml",
        "Sungsan native default strings",
    )
    check_xml("platform/android/generated.xml.in", "generated Android build strings")
    check_android_manifest_string_resources()
    check_json("branding/sungsan/theme.json", "Sungsan theme")

    for relative, purpose in (
        ("branding/sungsan/configure-vworld-plugin.cmake", "VWorld generator"),
        ("branding/sungsan/plugins/sungsan_vworld/main.qml.in", "VWorld QML template"),
        ("branding/sungsan/plugins/sungsan_vworld/metadata.txt", "VWorld metadata"),
    ):
        require_file(relative, purpose)

    build = "scripts/build-sungsan-android.sh"
    require_text(
        build,
        'export APP_ICON_PATH="/usr/src/qfield/branding/sungsan/assets"',
        "container icon path is fixed",
    )
    require_text(
        build,
        'export APP_SPLASH_PATH=""',
        "splash uses the already-validated Sungsan vector drawable",
    )
    require_text(
        build,
        'export APP_THEME_PATH="/usr/src/qfield/branding/sungsan/theme.json"',
        "container theme path is fixed",
    )
    require_text(
        "CMakeLists.txt",
        'set(APP_SPLASH_DRAWABLE "@drawable/${APP_ICON}_vector")',
        "CMake vector splash fallback is present",
    )
    require_text(
        "scripts/build-vcpkg.sh",
        'python3 "${SOURCE_DIR}/scripts/check-sungsan-build-inputs.py"',
        "brand inputs are rechecked inside the build container",
    )

    if FAILURES:
        print(f"FAIL: {len(FAILURES)} Sungsan Android build input issue(s)")
        for failure in FAILURES:
            print(f" - {failure}")
        return 1

    print(f"OK: {len(PASSES)} Sungsan Android build input checks passed")
    print("NOTE: this fast gate runs before the multi-hour dependency build.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
