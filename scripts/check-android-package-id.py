#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Modified for Sungsan Mobile GIS by Sungsan on 2026-08-11.
"""Static regression checks for Android package ID templating.

This intentionally has no QField build dependencies.  It mirrors CMake's
``configure_file(... @ONLY)`` replacement for the package-related templates so
the upstream and Sungsan configurations can be checked before a Docker build.
"""

from __future__ import annotations

import re
import shutil
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def configure_at_only(path: str, values: dict[str, str]) -> str:
    content = (ROOT / path).read_text(encoding="utf-8")
    return re.sub(
        r"@([A-Za-z_][A-Za-z0-9_]*)@",
        lambda match: values.get(match.group(1), match.group(0)),
        content,
    )


def configured_java_sources(package_id: str, values: dict[str, str]) -> dict[str, str]:
    """Mirror Package.cmake's Java source-folder relocation and replacement."""
    with tempfile.TemporaryDirectory(prefix="qfield-package-id-") as temp_dir:
        temp_root = Path(temp_dir)
        shutil.copytree(ROOT / "platform/android/src", temp_root / "src")
        default_dir = temp_root / "src/ch/opengis/qfield"
        target_dir = temp_root / "src" / Path(package_id.replace(".", "/"))
        if package_id != "ch.opengis.qfield":
            staged_dir = temp_root / "src/.qfield-java-source"
            if staged_dir.exists():
                shutil.rmtree(staged_dir)
            default_dir.rename(staged_dir)
            if target_dir.exists():
                shutil.rmtree(target_dir)
            target_dir.parent.mkdir(parents=True, exist_ok=True)
            staged_dir.rename(target_dir)
            assert not default_dir.exists()

        configured: dict[str, str] = {}
        for source in target_dir.glob("*.java"):
            content = source.read_text(encoding="utf-8")
            content = content.replace("ch.opengis.qfield", package_id)
            content = content.replace(
                'scheme.equals("qfield")',
                f'scheme.equals("{values["APP_URL_SCHEME"]}")',
            )
            content = content.replace("QField/", f'{values["APP_DATA_DIR_NAME"]}/')
            content = content.replace('"QField"', f'"{values["APP_NAME"]}"')
            content = content.replace(
                "R.drawable.qfield_logo", f'R.drawable.{values["APP_ICON"]}'
            )
            for key in (
                "APP_POSITIONING_CHANNEL_NAME",
                "APP_POSITIONING_CHANNEL_DESCRIPTION",
                "APP_POSITIONING_NOTIFICATION_TITLE",
                "APP_POSITIONING_NOTIFICATION_RUNNING_TEXT",
                "APP_CLOUD_CHANNEL_NAME",
                "APP_CLOUD_CHANNEL_DESCRIPTION",
                "APP_CLOUD_NOTIFICATION_TITLE",
                "APP_CLOUD_NOTIFICATION_RUNNING_TEXT",
            ):
                content = content.replace(f"@{key}@", values[key])
            source.write_text(content, encoding="utf-8")
            configured[source.name] = content
        return configured


def jni_name(package_id: str) -> str:
    return package_id.replace("_", "_1").replace(".", "_")


def check_android_sdk_alignment() -> None:
    """Ensure the Docker SDK contains the platform selected by CMake."""
    platform_cmake = (ROOT / "cmake/Platform.cmake").read_text(encoding="utf-8")
    dockerfile = (ROOT / ".docker/android_dev/Dockerfile").read_text(
        encoding="utf-8"
    )

    target_match = re.search(
        r"set\(ANDROID_TARGET_PLATFORM\s+(\d+)\s+CACHE\s+INT", platform_cmake
    )
    build_tools_match = re.search(
        r'set\(ANDROID_BUILD_TOOLS_VERSION\s+"([^"]+)"\s+CACHE\s+STRING',
        platform_cmake,
    )
    assert target_match, "cmake/Platform.cmake의 Android 대상 SDK를 찾을 수 없습니다"
    assert build_tools_match, "cmake/Platform.cmake의 build-tools 버전을 찾을 수 없습니다"

    target_platform = target_match.group(1)
    build_tools = build_tools_match.group(1)
    installed_platforms = set(
        re.findall(r'"platforms;android-(\d+)"', dockerfile)
    )
    installed_build_tools = set(
        re.findall(r'"build-tools;([^"]+)"', dockerfile)
    )

    assert target_platform == "36", "Sungsan Android 대상 SDK는 36이어야 합니다"
    assert target_platform in installed_platforms, (
        f"Docker SDK에 CMake 대상 플랫폼 android-{target_platform}이 없습니다: "
        f"설치값={sorted(installed_platforms)}"
    )
    assert build_tools == "35.0.1", "검증된 Android build-tools 35.0.1을 유지해야 합니다"
    assert build_tools in installed_build_tools, (
        f"Docker SDK에 CMake build-tools {build_tools}가 없습니다: "
        f"설치값={sorted(installed_build_tools)}"
    )


def check_configuration(package_name: str, package_id: str) -> None:
    values = {
        "AT": "@",
        "APP_ANDROID_THEME": "@android:style/Theme.Material.NoActionBar" if package_name == "qfield" else "@style/MetaEngTheme",
        "APP_PACKAGE_NAME": package_name,
        "APP_PACKAGE_ID": package_id,
        "APP_PACKAGE_PATH": package_id.replace(".", "/"),
        "APP_PACKAGE_JNI_NAME": jni_name(package_id),
        "APP_NAME": "QField" if package_name == "qfield" else "metaeng mobile gis",
        "APP_ICON": "qfield_logo" if package_name == "qfield" else "metaengi_mobile_gis",
        "APP_URL_SCHEME": "qfield" if package_name == "qfield" else "metaengimobilegis",
        "APP_DATA_DIR_NAME": "QField" if package_name == "qfield" else "MetaEngiMobileGIS",
        "APP_POSITIONING_CHANNEL_NAME": "" if package_name == "qfield" else "metaeng mobile gis",
        "APP_POSITIONING_CHANNEL_DESCRIPTION": "" if package_name == "qfield" else "메타이엔지 위치 서비스",
        "APP_POSITIONING_NOTIFICATION_TITLE": "" if package_name == "qfield" else "메타이엔지 위치 서비스",
        "APP_POSITIONING_NOTIFICATION_RUNNING_TEXT": "" if package_name == "qfield" else "메타이엔지 위치 서비스가 실행 중입니다",
        "APP_CLOUD_CHANNEL_NAME": "" if package_name == "qfield" else "metaeng mobile gis",
        "APP_CLOUD_CHANNEL_DESCRIPTION": "" if package_name == "qfield" else "메타이엔지 데이터 전송 서비스",
        "APP_CLOUD_NOTIFICATION_TITLE": "" if package_name == "qfield" else "메타이엔지 데이터 전송",
        "APP_CLOUD_NOTIFICATION_RUNNING_TEXT": "" if package_name == "qfield" else "현장 첨부 파일을 전송하고 있습니다",
        "APP_VERSION_STR": "test",
        "APK_VERSION_CODE": "1",
    }

    manifest = configure_at_only("platform/android/AndroidManifest.xml.in", values)
    gradle = configure_at_only("platform/android/build.gradle.in", values)
    header = configure_at_only(
        "src/core/platforms/android/qfield_android.h.in", values
    )
    java = configured_java_sources(package_id, values)

    assert f'<manifest package="{package_id}"' in manifest
    assert f'android:name="{package_id}.QFieldActivity"' in manifest
    assert f'android:taskAffinity="{package_id}"' in manifest
    assert f'android:authorities="{package_id}.fileprovider"' in manifest
    assert f'android:name="{package_id}.QFieldCloudService"' in manifest
    assert f'android:name="{package_id}.QFieldPositioningService"' in manifest
    assert manifest.count(f'android:icon="@drawable/{values["APP_ICON"]}"') >= 4
    assert f"namespace = '{package_id}'" in gradle
    assert f"applicationId = '{package_id}'" in gradle
    assert f'#define APP_PACKAGE_PATH "{package_id.replace(".", "/")}"' in header
    assert f"#define APP_PACKAGE_JNI_NAME {jni_name(package_id)}" in header

    expected_symbol = f"Java_{jni_name(package_id)}_QFieldActivity_openProject"
    assert expected_symbol.startswith(f"Java_{jni_name(package_id)}_")

    expected_package = f"package {package_id};"
    assert java and all(expected_package in content for content in java.values())
    if package_id != "ch.opengis.qfield":
        assert all(
            "package ch.opengis.qfield;" not in content
            and "import ch.opengis.qfield." not in content
            for content in java.values()
        )
    if package_id != f"ch.opengis.{package_name}":
        legacy_suffix_id = "ch.opengis." + package_name
        assert legacy_suffix_id not in manifest + gradle + header

    positioning = java["QFieldPositioningService.java"]
    cloud = java["QFieldCloudService.java"]
    assert f"R.drawable.{values['APP_ICON']}" in positioning
    assert f"R.drawable.{values['APP_ICON']}" in cloud
    assert "R.drawable.qfield_logo" not in positioning + cloud or values["APP_ICON"] == "qfield_logo"
    assert not re.search(r"@[A-Z][A-Z0-9_]+@", positioning + cloud)
    if package_name == "qfield":
        # Empty overrides deliberately retain upstream resource/literal fallbacks.
        assert "configuredText(" in positioning
        assert "CONFIGURED_NOTIFICATION_TITLE" in positioning
        assert 'getString(R.string.positioning_title)' in positioning
        assert "configuredText(" in cloud
        assert "CONFIGURED_NOTIFICATION_TITLE" in cloud
        assert '"QFieldCloud"' in cloud
    else:
        for expected_text in (
            "metaeng mobile gis",
            "메타이엔지 위치 서비스",
            "메타이엔지 위치 서비스가 실행 중입니다",
        ):
            assert expected_text in positioning
        for expected_text in (
            "metaeng mobile gis",
            "메타이엔지 데이터 전송 서비스",
            "메타이엔지 데이터 전송",
            "현장 첨부 파일을 전송하고 있습니다",
        ):
            assert expected_text in cloud


def check_static_wiring() -> None:
    package_cmake = (ROOT / "cmake/Package.cmake").read_text(encoding="utf-8")
    build_sh = (ROOT / "scripts/build.sh").read_text(encoding="utf-8")
    build_vcpkg = (ROOT / "scripts/build-vcpkg.sh").read_text(encoding="utf-8")
    build_sungsan = (ROOT / "scripts/build-metaengi-android.sh").read_text(
        encoding="utf-8"
    )
    welcome = (ROOT / "src/qml/WelcomeScreen.qml").read_text(encoding="utf-8")
    vworld_plugin = (
        ROOT / "branding/metaengi/plugins/sungsan_vworld/main.qml.in"
    ).read_text(encoding="utf-8")
    project_utils = (ROOT / "src/core/utils/projectutils.cpp").read_text(
        encoding="utf-8"
    )
    manifest_template = (ROOT / "platform/android/AndroidManifest.xml.in").read_text(
        encoding="utf-8"
    )
    qfield_header = (ROOT / "src/core/qfield.h.in").read_text(encoding="utf-8")
    mobile_app = (ROOT / "src/core/qgismobileapp.cpp").read_text(encoding="utf-8")
    main_cpp = (ROOT / "src/app/main.cpp").read_text(encoding="utf-8")
    dashboard = (ROOT / "src/qml/DashBoard.qml").read_text(encoding="utf-8")
    about = (ROOT / "src/qml/About.qml").read_text(encoding="utf-8")
    project_creation = (ROOT / "src/qml/ProjectCreationScreen.qml").read_text(
        encoding="utf-8"
    )
    settings_qml = (ROOT / "src/qml/QFieldSettings.qml").read_text(
        encoding="utf-8"
    )
    local_picker = (ROOT / "src/qml/QFieldLocalDataPickerScreen.qml").read_text(
        encoding="utf-8"
    )
    recent_projects = (ROOT / "src/core/recentprojectlistmodel.cpp").read_text(
        encoding="utf-8"
    )

    assert 'set(APP_PACKAGE_ID "ch.opengis.${APP_PACKAGE_NAME}")' in (
        ROOT / "CMakeLists.txt"
    ).read_text(encoding="utf-8")
    assert 'src/${APP_PACKAGE_PATH}' in package_cmake
    assert '"${APP_PACKAGE_ID}" CONTENT' in package_cmake
    assert "-e APP_PACKAGE_ID" in build_sh
    assert '-D APP_PACKAGE_ID="${APP_PACKAGE_ID}"' in build_vcpkg
    for notification_variable in (
        "APP_POSITIONING_CHANNEL_NAME",
        "APP_POSITIONING_CHANNEL_DESCRIPTION",
        "APP_POSITIONING_NOTIFICATION_TITLE",
        "APP_POSITIONING_NOTIFICATION_RUNNING_TEXT",
        "APP_CLOUD_CHANNEL_NAME",
        "APP_CLOUD_CHANNEL_DESCRIPTION",
        "APP_CLOUD_NOTIFICATION_TITLE",
        "APP_CLOUD_NOTIFICATION_RUNNING_TEXT",
    ):
        assert f"-e {notification_variable}" in build_sh
        assert f'-D {notification_variable}="${{{notification_variable}}}"' in build_vcpkg
        assert f'export {notification_variable}=' in build_sungsan
        assert f'@{notification_variable}@' in package_cmake
    assert 'APP_PACKAGE_ID="kr.co.metaengi.mobilegis"' in build_sungsan
    assert 'APP_VERSION_STR="${APP_VERSION_STR:-1.2.2}"' in build_sungsan
    assert 'APK_VERSION_CODE="${APK_VERSION_CODE:-10202000}"' in build_sungsan
    assert 'APP_DEFAULT_LANGUAGE="ko"' in build_sungsan
    assert 'APP_URL_SCHEME="metaengimobilegis"' in build_sungsan
    assert 'APP_DATA_DIR_NAME="MetaEngiMobileGIS"' in build_sungsan
    assert (
        'QFIELD_CMAKE_BUILD_DIR="/usr/src/qfield/build-metaengi-native-${triplet}"'
        in build_sungsan
    )
    assert (
        'APP_BUNDLED_PLUGINS="/usr/src/qfield/build-metaengi-native-generated/plugins"'
        in build_sungsan
    )
    assert (
        'SOURCE_BUILD_DIR="${SOURCE_DIR}/build-metaengi-native-${triplet}"'
        in build_sungsan
    )
    assert (
        'APK_SOURCE_DIR="${SOURCE_BUILD_DIR}/src/app/android-build/build/outputs/apk"'
        in build_sungsan
    )
    assert (
        '-D OUTPUT_DIR="${SOURCE_DIR}/build-metaengi-native-generated/plugins"'
        in build_vcpkg
    )
    assert '/usr/src/qfield/build-metaengi-${triplet}' not in build_sungsan
    assert '${SOURCE_DIR}/build-metaengi-${triplet}/src/app/android-build' not in build_sungsan
    assert '/usr/src/qfield/build-metaengi-generated/plugins' not in build_sungsan
    assert '${SOURCE_DIR}/build-metaengi-generated/plugins' not in build_vcpkg
    assert 'SENTRY_DSN=""' in build_sungsan
    assert 'WITH_SAMPLE_PROJECTS="OFF"' in build_sungsan
    assert "-e WITH_SAMPLE_PROJECTS" in build_sh
    assert '-D WITH_SAMPLE_PROJECTS="${WITH_SAMPLE_PROJECTS}"' in build_vcpkg
    assert 'SUNG_SAN_CONFIGURE_VWORLD="ON"' in build_sungsan
    assert (
        "https://api.vworld.kr/req/wmts/1.0.0/"
        "@SUNG_SAN_VWORLD_API_KEY@/Satellite/%7Bz%7D/%7By%7D/%7Bx%7D.jpeg"
        in vworld_plugin
    )
    assert "zmax=19&zmin=6" in vworld_plugin
    assert "ProjectUtils.addMapLayerAtBottom" in vworld_plugin
    assert 'QStringLiteral( "출처: 국토교통부 브이월드(VWorld)" )' in project_utils
    assert 'project->addMapLayer( layer, false )' in project_utils
    assert 'android:name="io.sentry.auto-init" android:value="false"' in manifest_template
    assert 'market://details?id=" + appPackageId' in welcome
    official_market_link = "market://details?id=" + "ch.opengis.qfield"
    assert official_market_link not in welcome
    assert 'appPackageId == QStringLiteral( "kr.co.metaengi.mobilegis" )' in qfield_header
    assert 'setContextProperty( "appIsSungsan", qfield::isSungsanBuild )' in mobile_app
    assert 'QStringLiteral( "Meta Engineering" ) : QStringLiteral( "OPENGIS.ch" )' in main_cpp
    assert 'QStringLiteral( "metaengi.co.kr" ) : QStringLiteral( "opengis.ch" )' in main_cpp
    assert "visible: !appIsSungsan" in dashboard
    for dashboard_action in (
        "measurementButton",
        "view3DButton",
        "printItemButton",
        "projectFolderButton",
        "menuButton",
    ):
        action_block = dashboard.split(f"id: {dashboard_action}", 1)[1].split("}", 1)[0]
        assert "visible: !appIsSungsan" in action_block
    assert 'text: "레이어 관리"' in dashboard
    assert "오픈소스 정보" in about
    assert "GNU GPL v2 이상" in about
    assert "visible: !appIsSungsan" in project_creation
    assert "enabled: !appIsSungsan" in settings_qml
    assert "property bool autoSave: appIsSungsan" in settings_qml
    assert "property bool enableInfoCollection: !appIsSungsan" in settings_qml
    assert "registry.enableInfoCollection = false" in settings_qml
    assert (
        "!appIsSungsan && (platformUtilities.capabilities & "
        "PlatformUtilities.SentryFramework)"
        in settings_qml
    )
    assert "!appIsSungsan && table.selectedItemsPushableToQField" in local_picker
    assert 'settings.setValue("/QField/showMapCanvasGuide", false)' in welcome
    assert "if ( qfield::isSungsanBuild )" in recent_projects
    assert "qfield::isSungsanBuild || ( skipNonAvailable" in recent_projects


def main() -> None:
    check_android_sdk_alignment()
    check_configuration("qfield", "ch.opengis.qfield")
    check_configuration("qfield_home", "ch.opengis.qfield_home")
    check_configuration("qfield_dev", "ch.opengis.qfield_dev")
    check_configuration("metaengimobilegis", "kr.co.metaengi.mobilegis")
    check_static_wiring()
    print("OK: upstream 및 Meta Engineering Android 패키지 ID 정적 검증 통과")


if __name__ == "__main__":
    main()
