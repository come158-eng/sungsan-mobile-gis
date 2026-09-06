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
ROOT_QML = (ROOT / "src/qml/qgismobileapp.qml").read_text(encoding="utf-8")
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
    require(MANIFEST.count('android:mimeType="application/octet-stream"') >= 3,
            "LandStar PXY/KOF share and open MIME routing is incomplete")

    for marker in (
        'Signature.getInstance("SHA256withECDSA")',
        'new X509EncodedKeySpec(publicKeyBytes)',
        'Context.MODE_PRIVATE',
        'Settings.Secure.ANDROID_ID',
        'WindowManager.LayoutParams.FLAG_SECURE',
        'SUNGSAN_PACKAGE_ID.equals(getPackageName())',
        'protected void onResume()',
    ):
        require(marker in ACTIVITY, f"activation protection missing: {marker}")
    require(ACTIVITY.count(
                'requiresSungsanActivation() && !hasValidSungsanActivation()'
            ) >= 4,
            "activation must be revalidated when the app resumes")
    require("BEGIN PRIVATE KEY" not in ACTIVITY,
            "activation private key must never be embedded in the APK")

    for marker in (
        "25 * 1024 * 1024",
        "safeLeafName",
        "CodingErrorAction.REPORT",
        "copyLandStarPointFile",
        "destination.delete()",
        "incomingIntent.getClipData()",
        "declaredLength > maximumBytes",
        "initialCapacity",
    ):
        require(marker in ACTIVITY, f"LandStar input guard missing: {marker}")
    require("MAX_POINT_FILE_BYTES" in BRIDGE and "MAX_POINT_ROWS" in BRIDGE,
            "native LandStar parser limits are missing")
    require("usableFieldObjectLayer" in BRIDGE and
            "kr.co.metaengi.mobilegis/fieldObjects" in BRIDGE,
            "LandStar imports must be restricted to the field-object layer")
    require("QgsWkbTypes::hasZ" in BRIDGE and "requiredFields" in BRIDGE and
            all(field in BRIDGE for field in (
                "landstar_code", "northing", "easting", "elevation",
                "fix_status", "gps_accuracy_m", "surveyed_at", "source_device",
            )),
            "LandStar target must preserve the complete PointZ survey schema")
    require("kr.co.metaengi.mobilegis/landstarImportTarget" in BRIDGE and
            "explicitTargets" in BRIDGE and "candidates.size() > 1" in BRIDGE,
            "LandStar imports must select one explicit target and reject ambiguity")
    require("QUuid::createUuid()" in BRIDGE,
            "imported LandStar features need stable object UUIDs")
    require('QStringLiteral( "/landstarCrsConfirmed" )' in BRIDGE and
            'QStringLiteral( "/landstarCrsAuthId" )' in BRIDGE,
            "projected LandStar coordinates need explicit CRS confirmation")
    require("latitudeHeaders.contains( northHeader )" in BRIDGE and
            "longitudeHeaders.contains( eastHeader )" in BRIDGE,
            "WGS84 handling must require explicit latitude/longitude headers")
    require("looksLikeWgs84" not in BRIDGE,
            "coordinate magnitude must never silently guess a source CRS")
    require("duplicateExistingNames" in BRIDGE and "incomingNames.contains" in BRIDGE,
            "duplicate point names must block ambiguous updates")
    require("classifyFixQuality" in BRIDGE and "FixQuality::Rejected" in BRIDGE,
            "explicit non-FIX survey points must be rejected")
    require("accuracyColumn" in BRIDGE and "record.horizontalAccuracyValid" in BRIDGE,
            "LandStar horizontal accuracy must be parsed when the file provides it")
    require("accuracyScaleToMeters" in BRIDGE and
            'endsWith( QStringLiteral( "cm" ) )' in BRIDGE and
            'endsWith( QStringLiteral( "mm" ) )' in BRIDGE,
            "explicit LandStar accuracy units must be converted to metres")
    require("accuracyField" in BRIDGE and "gps_accuracy_m" in BRIDGE,
            "LandStar horizontal accuracy must be stored on the field point")
    require("std::isfinite" in BRIDGE,
            "non-finite survey coordinates must be rejected")
    require("std::isfinite( record.elevation )" in BRIDGE,
            "non-finite survey elevation must be rejected")
    require("북ing(N)·동ing(E)·표고(Z)" in BRIDGE,
            "LandStar import must require all N/E/Z coordinate columns")
    require("record.surveyedAt.isEmpty() ? QDateTime::currentDateTime()" not in BRIDGE,
            "missing survey timestamps must never be replaced by import time")
    require("fieldsToClearOnUpdate" in BRIDGE and 'QStringLiteral( "미제공" )' in BRIDGE,
            "updated coordinates must not retain stale FIX, accuracy, time or code metadata")
    require("std::isfinite( mapPoint.x() )" in BRIDGE and
            "std::isfinite( mapPoint.y() )" in BRIDGE,
            "transformed coordinates must remain finite before storage")
    require("preserveLandStarSource" in BRIDGE and "QCryptographicHash::Sha256" in BRIDGE,
            "normalized LandStar source evidence must be retained by hash")
    require('destination.getAbsolutePath() + ".source"' in ACTIVITY and
            "rawOutput.getFD().sync()" in ACTIVITY,
            "Android must preserve and fsync the exact shared LandStar bytes")
    require("rawAuditFile" in BRIDGE and "auditBytes = rawBytes" in BRIDGE,
            "the project audit copy must prefer the exact Android raw sidecar")
    require("unverifiedQuality" in ROOT_QML and "비-FIX 제외" in ROOT_QML,
            "mobile import result must disclose unverified and rejected quality")
    require("result.sourceCrs" in ROOT_QML and "result.targetCrs" in ROOT_QML,
            "mobile import result must disclose source and target CRS")
    require("Qt.callLater(function()" in ROOT_QML and
            "sungsanImportLandStarFile(mainWindow.sungsanPendingLandStarPath)" in ROOT_QML,
            "pending LandStar shares must import after project load")
    require("function sungsanRequestLandStarSync" not in ROOT_QML and
            "function sungsanCreateCadText" not in ROOT_QML,
            "manual LandStar/CAD actions must stay out of the mobile workflow")

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

    print("OK: 48 Sungsan field-security release checks passed")


if __name__ == "__main__":
    main()
