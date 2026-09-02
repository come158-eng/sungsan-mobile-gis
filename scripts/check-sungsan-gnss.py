#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Dependency-free static contract check for Sungsan external GNSS support.

This release gate proves only source wiring which is inspectable without an
Android device or receiver.  It cannot prove radio pairing, CHCNAV firmware
compatibility, NMEA output settings, RTK corrections or field accuracy.
"""

from __future__ import annotations

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ANDROID_NS = "{http://schemas.android.com/apk/res/android}"
FAILURES: list[str] = []
PASSES: list[str] = []
_SOURCE_CACHE: dict[str, str] = {}


def read(relative: str) -> str:
    if relative in _SOURCE_CACHE:
        return _SOURCE_CACHE[relative]
    path = ROOT / relative
    try:
        source = path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        FAILURES.append(f"{relative}: UTF-8 read failed: {error}")
        source = ""
    _SOURCE_CACHE[relative] = source
    return source


def require(relative: str, needle: str, purpose: str) -> None:
    if needle not in read(relative):
        FAILURES.append(f"{relative}: {purpose} missing ({needle!r})")
    else:
        PASSES.append(purpose)


def require_regex(relative: str, pattern: str, purpose: str) -> None:
    if not re.search(pattern, read(relative), re.MULTILINE | re.DOTALL):
        FAILURES.append(f"{relative}: {purpose} missing (/{pattern}/)")
    else:
        PASSES.append(purpose)


def check_build_contract() -> None:
    require_regex(
        "CMakeLists.txt",
        r"set\s*\(\s*WITH_BLUETOOTH\s+ON\s+CACHE\s+BOOL\s+"
        r"\"Enable bluetooth support\"\s*\)",
        "CMake enables Bluetooth support by default",
    )
    require(
        "CMakeLists.txt",
        "Bluetooth Nfc RemoteObjects",
        "the Qt Bluetooth component is a required build dependency",
    )


def check_android_permissions() -> None:
    relative = "platform/android/AndroidManifest.xml.in"
    try:
        manifest = ET.fromstring(read(relative))
    except ET.ParseError as error:
        FAILURES.append(f"{relative}: invalid XML: {error}")
        return

    permissions: dict[str, ET.Element] = {}
    for element in manifest.findall("uses-permission"):
        name = element.get(f"{ANDROID_NS}name", "")
        if name:
            permissions[name] = element

    required = {
        "android.permission.ACCESS_FINE_LOCATION": "precise location permission",
        "android.permission.ACCESS_COARSE_LOCATION": "coarse location permission",
        "android.permission.BLUETOOTH_SCAN": "Android 12 Bluetooth scan permission",
        "android.permission.BLUETOOTH_CONNECT": "Android 12 Bluetooth connect permission",
    }
    for permission, purpose in required.items():
        if permission not in permissions:
            FAILURES.append(f"{relative}: {purpose} missing ({permission})")
        else:
            PASSES.append(purpose)

    for permission in (
        "android.permission.BLUETOOTH",
        "android.permission.BLUETOOTH_ADMIN",
    ):
        element = permissions.get(permission)
        if element is None or element.get(f"{ANDROID_NS}maxSdkVersion") != "30":
            FAILURES.append(
                f"{relative}: legacy {permission} must be limited to maxSdkVersion 30"
            )
        else:
            PASSES.append(f"legacy {permission} is limited to Android 11")

    features = {
        element.get(f"{ANDROID_NS}name", ""): element
        for element in manifest.findall("uses-feature")
    }
    for feature in (
        "android.hardware.location.gps",
        "android.hardware.bluetooth",
        "android.hardware.bluetooth_le",
    ):
        element = features.get(feature)
        if element is None or element.get(f"{ANDROID_NS}required") != "false":
            FAILURES.append(
                f"{relative}: optional hardware declaration missing for {feature}"
            )
        else:
            PASSES.append(f"{feature} is declared as optional hardware")


def check_positioning_transports() -> None:
    settings = "src/qml/PositioningDeviceSettings.qml"
    requirements = (
        ("Bluetooth BT + BLE (NMEA)", "Bluetooth BT and BLE NMEA option"),
        ("PositioningDeviceModel.BluetoothDevice", "Bluetooth device model wiring"),
        ("TCP (NMEA)", "TCP NMEA option"),
        ("PositioningDeviceModel.TcpDevice", "TCP device model wiring"),
        ("UDP (NMEA)", "UDP NMEA option"),
        ("PositioningDeviceModel.UdpDevice", "UDP device model wiring"),
        ("qrc:/qml/BluetoothDeviceChooser.qml", "Bluetooth chooser loader"),
        ("qrc:/qml/TcpDeviceChooser.qml", "TCP chooser loader"),
        ("qrc:/qml/UdpDeviceChooser.qml", "UDP chooser loader"),
    )
    for needle, purpose in requirements:
        require(settings, needle, purpose)

    require(
        "src/qml/BluetoothDeviceChooser.qml",
        "deviceBLE ? ' (BLE)' : ' (BT)'",
        "Bluetooth chooser distinguishes BLE and classic BT",
    )
    require(
        "src/qml/BluetoothDeviceChooser.qml",
        '"ble": deviceBLE',
        "Bluetooth chooser persists the BLE transport choice",
    )

    source = "src/core/positioning/positioningsource.cpp"
    core_receivers = (
        ("std::make_unique<BluetoothLowEnergyReceiver>", "BLE receiver creation"),
        ("std::make_unique<BluetoothReceiver>", "classic Bluetooth receiver creation"),
        ("std::make_unique<TcpReceiver>", "TCP receiver creation"),
        ("std::make_unique<UdpReceiver>", "UDP receiver creation"),
    )
    for needle, purpose in core_receivers:
        require(source, needle, purpose)

    for header, receiver in (
        ("src/core/positioning/bluetoothreceiver.h", "BluetoothReceiver"),
        ("src/core/positioning/bluetoothlowenergyreceiver.h", "BluetoothLowEnergyReceiver"),
        ("src/core/positioning/tcpreceiver.h", "TcpReceiver"),
        ("src/core/positioning/udpreceiver.h", "UdpReceiver"),
    ):
        require_regex(
            header,
            rf"class\s+{receiver}\s*:\s*public\s+NmeaGnssReceiver",
            f"{receiver} feeds the common NMEA receiver pipeline",
        )

    require(
        "src/core/positioning/bluetoothdevicemodel.cpp",
        "QBluetoothPermission::Access",
        "Bluetooth discovery requests runtime communication access",
    )


def check_sungsan_ui_contract() -> None:
    panel = "src/qml/sungsan/SungsanFieldPanel.qml"
    require(panel, "signal gnssSettingsRequested", "field panel exposes GNSS settings signal")
    require(panel, 'text: "외부 GNSS 연결"', "field panel exposes the external GNSS button")
    require(
        panel,
        'detailText: "CHCNAV 포함 표준 NMEA · Bluetooth/BLE · TCP/UDP"',
        "external GNSS button documents generic CHCNAV/NMEA transports",
    )
    require(
        panel,
        "onClicked: root.gnssSettingsRequested()",
        "external GNSS button emits its settings request",
    )

    app = "src/qml/qgismobileapp.qml"
    require_regex(
        app,
        r"onGnssSettingsRequested\s*:\s*\{\s*"
        r"qfieldSettings\.reset\s*\(\s*\)\s*;\s*"
        r"qfieldSettings\.currentPanel\s*=\s*1\s*;\s*"
        r"qfieldSettings\.visible\s*=\s*true\s*;\s*\}",
        "GNSS button opens positioning settings panel 1",
    )

    bindings = (
        ("gpsActive: positionSource.active", "GNSS active-state binding"),
        (
            "gpsPositionValid: positionSource.active && mainWindow.sungsanGnssFresh",
            "fresh valid-position binding",
        ),
        (
            "gpsSignalStale: positionSource.active && positionSource.positionInformation",
            "stale-position warning binding",
        ),
        (
            "gpsAccuracy: positionSource.positionInformation && positionSource.positionInformation.haccValid ? positionSource.positionInformation.hacc : -1",
            "horizontal-accuracy binding",
        ),
        (
            "gpsDeviceName: positioningSettings.positioningDeviceName",
            "selected receiver-name binding",
        ),
        (
            "gpsQualityText: positionSource.positionInformation ? positionSource.positionInformation.qualityDescription : \"\"",
            "GNSS FIX/quality binding",
        ),
        (
            "gpsSatellites: positionSource.positionInformation ? positionSource.positionInformation.satellitesUsed : 0",
            "satellites-used binding",
        ),
    )
    for needle, purpose in bindings:
        require(app, needle, purpose)

    panel_status = (
        ("root.gpsPositionValid", "status bar uses position validity"),
        ("root.gpsDeviceName", "status bar uses receiver name"),
        ("root.gpsQualityText", "status bar displays FIX/quality text"),
        ("root.gpsSatellites", "status bar displays satellites used"),
        ("root.gpsAccuracy.toFixed(2)", "status bar displays horizontal accuracy"),
        ("이전 위치 사용 금지", "status bar explicitly rejects cached coordinates"),
    )
    for needle, purpose in panel_status:
        require(panel, needle, purpose)

    require(app, "property bool sungsanGnssFresh: false", "field freshness state is initialized unsafe")
    require(app, "property double sungsanLastGnssReceiptMs: 0", "local GNSS receipt clock is initialized unsafe")
    require(app, "mainWindow.sungsanLastGnssReceiptMs = Date.now()", "every received phone or NMEA fix refreshes the local clock")
    require(app, "receiptAgeSeconds <= 5", "field position expires five seconds after the last received update")
    require(app, "sungsanPositionDeviceSwitchTimer.restart()", "phone/external switching releases the previous receiver first")
    require(app, "positionSource.jumpToPosition = true", "current-position action waits for and reveals the first fix")
    require(app, "positionSource.triggerConnectDevice()", "current-position action retries a disconnected external receiver")
    require(app, "gpsConnectionText: positionSource.deviceSocketStateString", "field status exposes external socket state")
    require(app, "gpsLastError: positionSource.deviceLastError", "field status exposes the external receiver error")
    require(panel, 'text: (root.gpsExternal ? "" : "✓ ") + "휴대폰 GPS 사용"', "source dialog marks phone selection")
    require(panel, 'text: (root.gpsExternal ? "✓ " : "") + "외부 GNSS 사용 (CHCNAV / Bluetooth)"', "source dialog marks external selection")
    require(
        "src/core/positioning/positioning.cpp",
        "if ( active && devId.isEmpty() )",
        "stopping internal GPS cannot reopen it through a permission callback",
    )
    require(
        "src/core/positioning/positioning.cpp",
        "else if ( active )",
        "stopping external GNSS cannot reopen it through a permission callback",
    )
    require_regex(
        app,
        r"onAddFeatureRequested\s*:\s*\{\s*if\s*\(positionSource\.active\s*&&\s*"
        r"coordinateLocator\.positionLocked\s*&&\s*!mainWindow\.sungsanGnssFresh\).*?return\s*;",
        "point capture blocks stale locked GNSS coordinates",
    )


def main() -> int:
    check_build_contract()
    check_android_permissions()
    check_positioning_transports()
    check_sungsan_ui_contract()

    if FAILURES:
        print(f"FAIL: {len(FAILURES)} issue(s); {len(PASSES)} GNSS contract checks passed")
        for failure in FAILURES:
            print(f" - {failure}")
        print(
            "NOTE: static checks cannot verify pairing, CHCNAV firmware/NMEA "
            "configuration, RTK corrections, radio coexistence or field accuracy."
        )
        return 1

    print(f"OK: {len(PASSES)} Sungsan GNSS contract checks passed")
    print(
        "NOTE: static checks verify source wiring only; a real receiver and Android "
        "device are still required for pairing, NMEA/RTK and accuracy tests."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
