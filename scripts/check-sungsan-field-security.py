#!/usr/bin/env python3
"""Fail the Sungsan release gate when field-security controls regress."""

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = (ROOT / "platform/android/AndroidManifest.xml.in").read_text(
    encoding="utf-8"
)
NETWORK = (
    ROOT / "platform/android/res/xml/network_security_config.xml"
).read_text(encoding="utf-8")
ACTIVITY = (
    ROOT
    / "platform/android/src/ch/opengis/qfield/QFieldActivity.java"
).read_text(encoding="utf-8")
BRIDGE = (ROOT / "src/core/sungsansurveybridge.cpp").read_text(encoding="utf-8")
ANDROID_UTILITIES = (
    ROOT / "src/core/platforms/android/androidplatformutilities.cpp"
).read_text(encoding="utf-8")
WORKFLOW = (
    ROOT / ".github/workflows/sungsan-android.yml"
).read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> None:
    require('android:usesCleartextTraffic="false"' in MANIFEST,
            "Android cleartext traffic must be disabled")
    require('android:allowBackup="false"' in MANIFEST,
            "GIS data backup must be disabled")
    require("android.permission.RECORD_AUDIO" not in MANIFEST,
            "microphone permission is outside the Sungsan field scope")
    require("android.permission.RECORD_AUDIO" not in ANDROID_UTILITIES,
            "Android code must not request microphone permission")
    require('cleartextTrafficPermitted="false"' in NETWORK,
            "network security config must reject cleartext traffic")
    require('<certificates src="user"' not in NETWORK,
            "user-added certificate authorities must not be trusted globally")

    for marker in (
        'Signature.getInstance("SHA256withECDSA")',
        'new X509EncodedKeySpec(publicKeyBytes)',
        'Context.MODE_PRIVATE',
        'Settings.Secure.ANDROID_ID',
        'WindowManager.LayoutParams.FLAG_SECURE',
        'SUNGSAN_PACKAGE_ID.equals(getPackageName())',
    ):
        require(marker in ACTIVITY, f"activation protection missing: {marker}")
    require("BEGIN PRIVATE KEY" not in ACTIVITY,
            "activation private key must never be embedded in the APK")

    for marker in (
        "25 * 1024 * 1024",
        "safeLeafName",
        "CodingErrorAction.REPORT",
        "copyLandStarPointFile",
        "destination.delete()",
    ):
        require(marker in ACTIVITY, f"LandStar input guard missing: {marker}")
    require("MAX_POINT_FILE_BYTES" in BRIDGE and "MAX_POINT_ROWS" in BRIDGE,
            "native LandStar parser limits are missing")

    require("permissions: {}" in WORKFLOW,
            "GitHub workflow default token permissions must be empty")
    require("Restrict secret-bearing builds to the default branch" in WORKFLOW,
            "release secrets must be default-branch restricted")
    require("apksigner verify --verbose --print-certs" in WORKFLOW,
            "signed APK verification gate is missing")
    require("persist-credentials: false" in WORKFLOW,
            "checkout credentials must not persist")

    forbidden_suffixes = {".p12", ".jks", ".keystore", ".pem"}
    forbidden = [
        path.relative_to(ROOT).as_posix()
        for path in ROOT.rglob("*")
        if path.is_file() and path.suffix.casefold() in forbidden_suffixes
    ]
    require(not forbidden,
            "private signing material found in source: " + ", ".join(forbidden))

    print("OK: 25 Sungsan field-security release checks passed")


if __name__ == "__main__":
    main()
