#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Modified for Sungsan Mobile GIS by Sungsan on 2026-08-11.
"""Dependency-free static release gate for the Sungsan Android shell.

This check intentionally does not claim to compile QML or build an APK.  It
guards the resource, QML-to-core API, Android import/export, manual-save and
VWorld wiring which can be verified without Qt, QGIS or the Android SDK.
"""

from __future__ import annotations

import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
FAILURES: list[str] = []
PASSES: list[str] = []


def read(relative: str) -> str:
    path = ROOT / relative
    try:
        return path.read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        FAILURES.append(f"{relative}: UTF-8 read failed: {error}")
        return ""


def require(relative: str, needle: str, purpose: str) -> None:
    if needle not in read(relative):
        FAILURES.append(f"{relative}: {purpose} missing ({needle!r})")
    else:
        PASSES.append(purpose)


def forbid(relative: str, needle: str, purpose: str) -> None:
    if needle in read(relative):
        FAILURES.append(f"{relative}: {purpose} present ({needle!r})")
    else:
        PASSES.append(purpose)


def require_regex(relative: str, pattern: str, purpose: str) -> None:
    if not re.search(pattern, read(relative), re.MULTILINE | re.DOTALL):
        FAILURES.append(f"{relative}: {purpose} missing (/{pattern}/)")
    else:
        PASSES.append(purpose)


def balanced_qml(relative: str) -> None:
    """Catch truncated strings/comments and unbalanced QML/JS delimiters.

    This is deliberately described as a structural scan rather than a QML
    parser.  The authoritative checks remain qmllint and a real Qt build.
    """

    source = read(relative)
    pairs = {")": "(", "]": "[", "}": "{"}
    opening = set(pairs.values())
    stack: list[tuple[str, int]] = []
    state = "code"
    quote = ""
    line = 1
    index = 0

    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if char == "\n":
            line += 1

        if state == "line-comment":
            if char == "\n":
                state = "code"
        elif state == "block-comment":
            if char == "*" and following == "/":
                state = "code"
                index += 1
        elif state == "string":
            if char == "\\":
                index += 1
            elif char == quote:
                state = "code"
        elif char == "/" and following == "/":
            state = "line-comment"
            index += 1
        elif char == "/" and following == "*":
            state = "block-comment"
            index += 1
        elif char in "'\"`":
            state = "string"
            quote = char
        elif char in opening:
            stack.append((char, line))
        elif char in pairs:
            if not stack or stack[-1][0] != pairs[char]:
                FAILURES.append(
                    f"{relative}:{line}: unmatched {char!r} in structural scan"
                )
                return
            stack.pop()
        index += 1

    if state in {"block-comment", "string"}:
        FAILURES.append(f"{relative}:{line}: unterminated {state}")
    elif stack:
        delimiter, delimiter_line = stack[-1]
        FAILURES.append(
            f"{relative}:{delimiter_line}: unclosed {delimiter!r} in structural scan"
        )
    else:
        PASSES.append(f"{relative} structural QML scan")


def parse_android_strings(relative: str) -> dict[str, str]:
    """Return Android string resources while rejecting duplicate names."""

    path = ROOT / relative
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError) as error:
        FAILURES.append(f"{relative}: invalid Android string XML: {error}")
        return {}

    resources: dict[str, str] = {}
    for element in root.findall("string"):
        name = element.get("name", "")
        if not name:
            FAILURES.append(f"{relative}: string without a resource name")
            continue
        if name in resources:
            FAILURES.append(f"{relative}: duplicate string resource {name!r}")
            continue
        resources[name] = "".join(element.itertext())
    return resources


def check_android_default_strings() -> None:
    """Verify Sungsan native fallbacks are complete, Korean and package-only."""

    upstream_path = "platform/android/res/values/strings.xml"
    korean_path = "platform/android/res/values-ko/strings.xml"
    sungsan_path = "branding/sungsan/android/res/values/strings.xml"
    package_path = "cmake/Package.cmake"

    upstream = parse_android_strings(upstream_path)
    korean = parse_android_strings(korean_path)
    sungsan = parse_android_strings(sungsan_path)
    upstream_names = set(upstream)
    korean_names = set(korean)
    sungsan_names = set(sungsan)

    expected_identity_names = {"app_name", "lib_name"}
    # The original catalog has app_name/lib_name plus 66 translated entries.
    # The compressed-export safety fix adds one more translated error message,
    # making the current release catalog 69 resources. Checking the complete
    # current key set is stricter than the original 68-resource release gate.
    if len(upstream) != 69:
        FAILURES.append(
            f"{upstream_path}: expected 69 resources, found {len(upstream)}"
        )
    else:
        PASSES.append("upstream Android defaults retain all 69 current resources")

    if len(korean) != 67 or korean_names != upstream_names - expected_identity_names:
        missing = sorted((upstream_names - expected_identity_names) - korean_names)
        extra = sorted(korean_names - (upstream_names - expected_identity_names))
        FAILURES.append(
            f"{korean_path}: expected the 67 current translatable resources; "
            f"missing={missing}, extra={extra}, count={len(korean)}"
        )
    else:
        PASSES.append("Korean Android catalog covers all 67 current translations")

    if len(sungsan) != 69 or sungsan_names != upstream_names:
        missing = sorted(upstream_names - sungsan_names)
        extra = sorted(sungsan_names - upstream_names)
        FAILURES.append(
            f"{sungsan_path}: expected all 69 current Android resources; "
            f"missing={missing}, extra={extra}, count={len(sungsan)}"
        )
    else:
        PASSES.append("Sungsan Android defaults cover all 69 current resources")

    translation_mismatches = sorted(
        name for name in korean_names if sungsan.get(name) != korean.get(name)
    )
    if translation_mismatches:
        FAILURES.append(
            f"{sungsan_path}: differs from values-ko for: "
            f"{', '.join(translation_mismatches)}"
        )
    else:
        PASSES.append("Sungsan defaults match all 67 reviewed Korean translations")

    if sungsan.get("app_name") != "Sungsan Mobile GIS":
        FAILURES.append(f"{sungsan_path}: app_name is not Sungsan Mobile GIS")
    else:
        PASSES.append("Sungsan native app_name is independent")

    # android.app.lib_name is a loader identifier for libqfield.so, not visible
    # copy. Renaming it without renaming the native target would prevent launch.
    if sungsan.get("lib_name") != "qfield":
        FAILURES.append(f"{sungsan_path}: lib_name must continue to load libqfield.so")
    else:
        PASSES.append("Sungsan native loader library name remains compatible")

    visible_text = "\n".join(
        value for name, value in sungsan.items() if name != "lib_name"
    )
    if re.search(r"\bQField(?:Cloud|Sync)?\b", visible_text, re.IGNORECASE):
        FAILURES.append(f"{sungsan_path}: visible legacy QField brand literal found")
    else:
        PASSES.append("Sungsan default native dialogs contain no visible QField literal")

    require(
        sungsan_path,
        "Modified for Sungsan Mobile GIS by Sungsan on 2026-08-11.",
        "Sungsan Android default catalog has the required dated change notice",
    )

    require(
        package_path,
        'if(APP_PACKAGE_ID STREQUAL "kr.co.sungsan.mobilegis")',
        "Sungsan default resources are gated by the exact independent package ID",
    )
    require(
        package_path,
        '"${CMAKE_SOURCE_DIR}/branding/sungsan/android/res/values/strings.xml"',
        "Sungsan default resource source is staged from branding assets",
    )
    require_regex(
        package_path,
        r"file\s*\(\s*GLOB\s+SUNGSAN_ANDROID_LOCALIZED_STRINGS\s*"
        r"\"\$\{ANDROID_TEMPLATE_FOLDER\}/res/values-\*/strings\.xml\"\s*\)\s*"
        r"file\s*\(\s*REMOVE\s+\$\{SUNGSAN_ANDROID_LOCALIZED_STRINGS\}\s*\)",
        "Sungsan staging removes locale catalogs that could override Korean defaults",
    )
    require_regex(
        package_path,
        r"configure_file\s*\(\s*\"\$\{SUNGSAN_ANDROID_DEFAULT_STRINGS\}\"\s*"
        r"\"\$\{ANDROID_TEMPLATE_FOLDER\}/res/values/strings\.xml\"\s*COPYONLY\s*\)",
        "Sungsan default resources replace only the staged Android values file",
    )


def check_upstream_revision_attribution() -> None:
    """Keep the Sungsan About source link valid when the archive has no .git."""

    revision = "f7123fc8dfa40be4e874d9bf5b46e81c6d05039b"
    cmake = read("CMakeLists.txt")
    git_lookup = cmake.find("GET_GIT_HEAD_REVISION(GIT_REFSPEC GIT_REV)")
    sungsan_gate = cmake.find(
        'if(APP_PACKAGE_ID STREQUAL "kr.co.sungsan.mobilegis")', git_lookup
    )
    next_message = cmake.find('message(STATUS "Building for git rev ${GIT_REV}")')
    branded_block = cmake[sungsan_gate:next_message]

    if git_lookup < 0 or sungsan_gate < 0 or next_message < 0:
        FAILURES.append(
            "CMakeLists.txt: Git lookup and Sungsan revision gate are not ordered safely"
        )
    elif (
        f'set(APP_UPSTREAM_REVISION "{revision}")' not in branded_block
        or 'set(GIT_REV "${APP_UPSTREAM_REVISION}")' not in branded_block
        or 'string(LENGTH "${APP_UPSTREAM_REVISION}" APP_UPSTREAM_REVISION_LENGTH)'
        not in branded_block
        or 'NOT APP_UPSTREAM_REVISION_LENGTH EQUAL 40'
        not in branded_block
        or 'APP_UPSTREAM_REVISION MATCHES "^[0-9a-fA-F]+$"'
        not in branded_block
    ):
        FAILURES.append(
            "CMakeLists.txt: Sungsan must validate and pin the audited upstream revision"
        )
    else:
        PASSES.append("Sungsan CMake pins a valid audited upstream revision after Git lookup")

    require(
        "scripts/build-sungsan-android.sh",
        f'APP_UPSTREAM_REVISION="{revision}"',
        "Sungsan build exports its exact audited upstream base commit",
    )
    require(
        "scripts/build.sh",
        "-e APP_UPSTREAM_REVISION",
        "upstream revision enters the isolated Android build container",
    )
    require(
        "scripts/build-vcpkg.sh",
        '-D APP_UPSTREAM_REVISION="${APP_UPSTREAM_REVISION}"',
        "upstream revision reaches CMake configuration",
    )
    require(
        "src/core/qfield.h.in",
        'static inline const QString gitRev{ QStringLiteral( "@GIT_REV@" ) };',
        "CMake revision remains the qfield::gitRev source",
    )
    require_regex(
        "src/qml/About.qml",
        r"visible:\s*appIsSungsan.*?github\.com/opengisch/QField/commit/['\"]\s*"
        r"\+\s*gitRev.*?QField 원본 커밋",
        "Sungsan open-source About section links qfield::gitRev to the upstream commit",
    )
    if "GITDIR-NOTFOUND" in read("src/qml/About.qml"):
        FAILURES.append("src/qml/About.qml: invalid missing-Git revision is visible")
    else:
        PASSES.append("About screen contains no hard-coded missing-Git revision")


def check_qml_resources() -> None:
    qrc_path = ROOT / "src/qml/qml.qrc"
    try:
        tree = ET.parse(qrc_path)
    except (OSError, ET.ParseError) as error:
        FAILURES.append(f"src/qml/qml.qrc: invalid XML: {error}")
        return

    entries = [element.text or "" for element in tree.findall(".//file")]
    if len(entries) != len(set(entries)):
        FAILURES.append("src/qml/qml.qrc: duplicate resource entries")

    expected = {
        "sungsan/SungsanActionButton.qml",
        "sungsan/SungsanHomeScreen.qml",
        "sungsan/SungsanFieldPanel.qml",
        "qgismobileapp.qml",
    }
    missing = sorted(expected - set(entries))
    if missing:
        FAILURES.append(f"src/qml/qml.qrc: missing resources: {', '.join(missing)}")
    else:
        PASSES.append("Sungsan QML files packaged in qml.qrc")

    for entry in entries:
        if not (qrc_path.parent / entry).is_file():
            FAILURES.append(f"src/qml/qml.qrc: nonexistent file: {entry}")


def check_shell_wiring() -> None:
    root = "src/qml/qgismobileapp.qml"
    home = "src/qml/sungsan/SungsanHomeScreen.qml"
    panel = "src/qml/sungsan/SungsanFieldPanel.qml"
    gallery = "src/qml/editorwidgets/relationeditors/gallery_relation_editor.qml"
    cloud = "src/qml/QFieldCloudScreen.qml"
    branded_surface = "\n".join(
        read(path)
        for path in (
            "src/qml/sungsan/SungsanHomeScreen.qml",
            "src/qml/sungsan/SungsanFieldPanel.qml",
        )
    )

    require(root, 'import "sungsan"', "Sungsan QML directory imported")
    require(root, "SungsanHomeScreen {", "Sungsan home instantiated")
    require(root, "SungsanFieldPanel {", "Sungsan field panel instantiated")
    require(panel, "import org.qfield", "Sungsan field panel imports the Theme provider")
    require(panel, 'text: "‹ 뒤로"', "Sungsan map header exposes a visible back button")
    require(panel, 'Accessible.name: "레이어 목록 열기"', "Sungsan map header exposes layer access")
    panel_source = read(panel)
    if panel_source.count('text: "레이어"') != 1:
        FAILURES.append(f"{panel}: layer access must appear exactly once in the field UI")
    else:
        PASSES.append("Sungsan field UI exposes one non-duplicated layer button")
    require(panel, "visible: root.editMode && root.multiVertexLayer", "line and polygon controls stay hidden for point layers")
    require(panel, 'text: root.pointLayer ? "지점 추가" : "객체 추가"', "point capture has one direct add action")
    require(panel, 'text: root.existingPointSelectionPending ? "지점 선택 취소" : "지점 사진·속성"', "existing point photo and attribute workflow is exposed for point layers")
    require(
        panel,
        'detailText: root.existingPointSelectionPending ? "지도 선택 대기 중" : "기존 지점의 근경·원경·기타·추가사진"',
        "existing point workflow advertises categorized and extra photos",
    )
    forbid(panel, 'text: "LandStar 측점 받기"', "mobile field panel excludes the manual LandStar file picker")
    forbid(panel, 'text: "CAD TXT 생성"', "mobile field panel excludes desktop CAD text export")
    forbid(panel, "property string lastLandStarSyncText", "mobile field panel excludes obsolete LandStar sync state")
    forbid(panel, "signal landStarSyncRequested", "mobile field panel excludes obsolete manual LandStar signal")
    forbid(panel, "signal cadTextExportRequested", "mobile field panel excludes obsolete CAD export signal")
    forbid(root, "function sungsanRequestLandStarSync", "mobile shell excludes the manual LandStar picker function")
    forbid(root, "function sungsanCreateCadText", "mobile shell excludes the CAD text export function")
    forbid(root, "onLandStarSyncRequested", "mobile shell excludes the manual LandStar handler")
    forbid(root, "onCadTextExportRequested", "mobile shell excludes the CAD text export handler")
    require(root, "function onLandStarFileReceived(path)", "Android LandStar share is received automatically")
    require(root, "mainWindow.sungsanImportLandStarFile(path)", "received LandStar share enters the native importer")
    require(root, "property string sungsanPendingLandStarPath", "LandStar share path survives project loading")
    require_regex(
        root,
        r"function\s+onLoadProjectEnded\s*\([^)]*\)\s*\{.*?"
        r"sungsanPendingLandStarPath\.length\s*>\s*0.*?"
        r"Qt\.callLater\s*\(\s*function\s*\(\s*\)\s*\{.*?"
        r"sungsanImportLandStarFile\s*\(\s*mainWindow\.sungsanPendingLandStarPath\s*\)",
        "pending LandStar share imports automatically after project load",
    )
    require(panel, 'text: "외부 GNSS 연결"', "generic external GNSS setup is exposed in the field panel")
    require(panel, "CHCNAV 포함 표준 NMEA", "GNSS setup describes model-independent NMEA compatibility")
    require(panel, "Bluetooth/BLE · TCP/UDP", "GNSS setup lists the supported receiver transports")
    require(panel, "visible: root.pointLayer", "existing point attribute action is limited to point layers")
    require(
        gallery,
        'relationId === "sungsan_field_photos"',
        "categorized photo chooser is limited to the Sungsan field-photo relation",
    )
    for photo_type in ("근경", "원경", "기타"):
        require(
            gallery,
            f'text: "{photo_type} 촬영"',
            f"existing point photo chooser exposes {photo_type}",
        )
    require(gallery, 'text: "추가 사진 촬영"', "existing point photo chooser exposes unlimited extra photos")
    require(
        gallery,
        'attributeFormModel.changeAttribute("photo_type", photoType)',
        "photo type is written before the attachment naming expression is evaluated",
    )
    require(
        gallery,
        "pendingSungsanPhotoType",
        "photo type survives the Android or built-in camera round trip",
    )
    require(
        root,
        "projectLoaded: !welcomeScreen.visible",
        "Sungsan map controls do not disappear on a loaded map with an empty project filename",
    )
    require(
        root,
        "visible: true\n\n    model: RecentProjectListModel",
        "Sungsan always starts at the project hub instead of a cached blank canvas",
    )
    require(home, "property string projectPath: ProjectPath", "recent project path role is safely aliased")
    require(home, "property string projectTitle: ProjectTitle", "recent project title role is safely aliased")
    require(home, "property int projectType: ProjectType", "recent project type role is safely aliased")
    forbid(home, "property string ProjectPath", "QML property declarations cannot begin with uppercase letters")
    forbid(home, "property string ProjectTitle", "QML property declarations cannot begin with uppercase letters")
    forbid(home, "property int ProjectType", "QML property declarations cannot begin with uppercase letters")
    cloud_source = read(cloud)
    if cloud_source.count("onVisibleChanged:") != 1:
        FAILURES.append(f"{cloud}: visible-change handler must be declared exactly once")
    else:
        PASSES.append("QFieldCloudScreen has one visible-change handler")
    require_regex(
        cloud,
        r"onVisibleChanged:\s*\{\s*if\s*\(appIsSungsan\s*&&\s*visible\)\s*"
        r"\{.*?finished\(\);\s*return;\s*\}\s*prepareCloudScreen\(\);",
        "Sungsan cloud bypass and ordinary cloud preparation share one QML handler",
    )
    require(root, "platformUtilities.importProjectArchive()", "ZIP project import wired")
    require(root, "platformUtilities.sendCompressedFolderTo(", "ZIP project export wired")
    require(
        root,
        "FileUtils.absolutePath(projectInfo.filePath)",
        "current project folder passed to export",
    )
    require(root, "iface.saveProject()", "manual/project save uses disk-write API")
    require(root, "qfieldSettings.autoSave", "feature auto-save setting wired")
    require(root, "positioningSettings.positioningActivated", "GNSS activation wired")
    require(root, "gnssButton.jumpToLocation()", "current-location jump wired")
    require(root, "onGnssSettingsRequested", "field GNSS action is wired to settings")
    require(root, "qfieldSettings.currentPanel = 1", "GNSS actions open the positioning settings panel directly")
    require(root, "positionSource.positionInformation.qualityDescription", "NMEA RTK quality is shown on the field panel")
    require(root, "positionSource.positionInformation.satellitesUsed", "GNSS satellites used are shown on the field panel")
    require(root, "property bool sungsanGnssFresh: false", "stale GNSS state defaults to unsafe")
    require(root, "ageSeconds <= 5", "field GNSS position expires after five seconds")
    require(root, "Number.isFinite(positionTimeMs)", "invalid GNSS timestamps stay invalid")
    require(panel, "이전 위치 사용 금지", "stale GNSS is explicitly rejected in the field status bar")
    require(panel, "Flickable {", "short screens can scroll the bottom action panel")
    require(panel, "mainWindow.sceneBottomMargin", "bottom action panel includes the system safe area")
    require(panel, 'root.vworldReady ? "영상 준비" : "선택 가능"', "disabled VWorld is presented as an option, not a pending forced layer")
    require(root, 'changeMode("digitize")', "survey mode wired")
    require(root, "dashBoard.ensureEditableLayerSelected()", "editable layer selection wired")
    require_regex(
        root,
        r"function\s+sungsanStartSurvey\s*\([^)]*\).*?"
        r"ensureEditableLayerSelected\s*\(\s*\).*?"
        r"!dashBoard\.activeLayer\s*\|\|\s*!digitizingToolbar\.digitizingAllowed",
        "survey start blocks missing, read-only and addition-locked layers",
    )
    require_regex(
        root,
        r"onAddVertexRequested\s*:\s*\{.*?digitizingToolbar\.triggerAddVertex\s*\(\s*\)",
        "Sungsan vertex button reaches digitizing toolbar",
    )
    require_regex(
        root,
        r"onAddFeatureRequested\s*:\s*\{.*?Qt\.callLater.*?"
        r"geometryType\s*\(\s*\)\s*===\s*Qgis\.GeometryType\.Point.*?"
        r"digitizingToolbar\.triggerAddVertex\s*\(\s*\)",
        "Sungsan point add captures once and opens the attribute workflow directly",
    )
    require_regex(
        root,
        r"onEditExistingPointRequested\s*:\s*\{.*?"
        r"sungsanExistingPointEditPending\s*=\s*true.*?"
        r"기존 지점의 근경·원경·기타·추가사진과 속성을 입력하려면 지도에서 지점을 눌러 주세요",
        "existing point photo workflow waits for a map selection without creating geometry",
    )
    require_regex(
        root,
        r"onIdentifyFinished\s*:\s*\{.*?"
        r"sungsanExistingPointEditPending.*?"
        r"MultiFeatureListModel\.LayerRole.*?"
        r"featureListForm\.state\s*=\s*\"FeatureFormEdit\"",
        "identified features from the active layer open directly in attribute edit mode",
    )
    require_regex(
        "src/qml/FeatureListForm.qml",
        r"editExistingAfterSelection.*?"
        r"featureFormList\.state\s*=\s*featureFormList\.editExistingAfterSelection\s*\?\s*\"FeatureFormEdit\"",
        "overlapping identified points enter edit mode after the user chooses one",
    )
    require_regex(
        root,
        r"onRemoveVertexRequested\s*:\s*\{.*?digitizingToolbar\.removeVertex\s*\(\s*\)",
        "Sungsan undo button reaches digitizing toolbar",
    )
    require_regex(
        root,
        r"onConfirmGeometryRequested\s*:\s*\{.*?digitizingToolbar\.confirm\s*\(\s*\)",
        "Sungsan geometry confirmation reaches digitizing toolbar",
    )
    require_regex(
        root,
        r"onCancelGeometryRequested\s*:\s*\{.*?digitizingToolbar\.cancelDialog\.open\s*\(\s*\)",
        "Sungsan geometry cancellation reaches digitizing toolbar",
    )
    require_regex(
        root,
        r"function\s+sungsanSaveCurrentWork\s*\([^)]*\).*?"
        r"(?:featureForm\.save|saveCurrentForm).*?iface\.saveProject\s*\(\s*\)",
        "manual save commits an open form before project disk write",
    )
    require_regex(
        root,
        r"function\s+sungsanExportCurrentProject\s*\([^)]*\).*?"
        r"sungsanSaveCurrentWork\s*\(\s*false\s*\).*?"
        r"gpkgFlusher\.flushDirectory\s*\([^)]*\).*?sendCompressedFolderTo",
        "export saves and checkpoints GeoPackage WAL before compression",
    )
    require_regex(
        "src/qml/QFieldLocalDataPickerScreen.qml",
        r"Compress project and send to.*?if\s*\(\s*appIsSungsan\s*\).*?"
        r"mainWindow\.sungsanExportCurrentProject\s*\(\s*\).*?else.*?"
        r"platformUtilities\.sendCompressedFolderTo",
        "alternate Sungsan project-menu export uses the same safe workflow",
    )
    require_regex(
        "src/core/qgsgpkgflusher.cpp",
        r"bool\s+QgsGpkgFlusher::flushDirectory.*?"
        r"QFileInfo::exists\s*\(\s*fileName\s*\+\s*QStringLiteral\s*\(\s*['\"]-wal['\"]\s*\)\s*\).*?"
        r"flush\s*\(\s*fileName\s*\)",
        "project export checkpoints every pending GeoPackage/SQLite WAL",
    )
    require_regex(
        "src/core/qgsgpkgflusher.cpp",
        r"bool\s+QgsGpkgFlusher::flush\s*\([^)]*\).*?"
        r"QMetaObject::invokeMethod\s*\(\s*mFlusher.*?Qt::BlockingQueuedConnection",
        "database checkpoint waits for the flusher object thread",
    )
    require_regex(
        "src/core/qgsgpkgflusher.cpp",
        r"sqlite3_wal_checkpoint_v2\s*\(.*?SQLITE_CHECKPOINT_FULL.*?"
        r"checkpointedFrames\s*>=\s*logFrames",
        "database export requires a complete SQLite WAL checkpoint",
    )
    require_regex(
        root,
        r"Timer\s*\{.*?interval:\s*(?:60000|300000).*?running:\s*qfieldSettings\.autoSave"
        r".*?overlayFeatureFormDrawer\.opened.*?digitizingToolbar\.isDigitizing"
        r".*?iface\.saveProject\s*\(\s*\)",
        "timed save skips active form/geometry and writes the project",
    )

    if re.search(r"^\s*mentMode\s*\(\s*\)\s*;", read(root), re.MULTILINE):
        FAILURES.append(
            "src/qml/qgismobileapp.qml: undefined truncated call 'mentMode();'; "
            "expected activateMeasurementMode()"
        )
    else:
        PASSES.append("known truncated mentMode() call absent")

    # The branded surface may use internal QField type/module names, but it
    # must not present that product name as visible text.
    for literal in re.findall(r'(["\'])(.*?)\1', branded_surface):
        value = literal[1]
        if re.search(r"\bQField(?:Cloud|Sync)?\b", value, re.IGNORECASE):
            FAILURES.append(f"Sungsan visible QML contains legacy brand literal: {value!r}")

    icon_qrc = read("images/images.qrc")
    icon_names = set(re.findall(r'["\'](ic_[A-Za-z0-9_-]+)["\']', branded_surface))
    missing_icons = sorted(name for name in icon_names if name not in icon_qrc)
    if missing_icons:
        FAILURES.append(f"Sungsan theme icons missing from images.qrc: {', '.join(missing_icons)}")
    else:
        PASSES.append(f"all {len(icon_names)} Sungsan theme icons packaged")


def check_save_api() -> None:
    require_regex(
        "src/core/appinterface.h",
        r"Q_INVOKABLE\s+bool\s+saveProject\s*\(\s*\)\s*;",
        "saveProject exposed to QML",
    )
    require_regex(
        "src/core/appinterface.cpp",
        r"bool\s+AppInterface::saveProject\s*\(\s*\).*?project->write\s*\(\s*\)",
        "saveProject retains the native QgsProject write path",
    )
    require_regex(
        "src/core/appinterface.cpp",
        r"qfield::isSungsanBuild.*?lastSaveVersion\s*\(\s*\).*?sourceVersion\.majorVersion\s*\(\s*\)\s*<\s*runningMajorVersion.*?return\s+true",
        "Sungsan save avoids upgrading an older desktop QGZ schema",
    )
    require_regex(
        "src/core/appinterface.cpp",
        r"sourceVersion\.isNull\s*\(\s*\).*?return\s+false",
        "Sungsan save refuses to rewrite a project with unknown source version",
    )
    require_regex(
        "src/core/appinterface.cpp",
        r"project->fileName\s*\(\s*\)\.isEmpty\s*\(\s*\)",
        "saveProject rejects missing project path",
    )
    require_regex(
        "src/core/featuremodel.h",
        r"Q_INVOKABLE\s+bool\s+save\s*\(\s*bool\s+flushBuffer\s*=\s*true\s*\)",
        "feature save commit API remains available",
    )
    require(
        "src/qml/QFieldSettings.qml",
        "property alias autoSave: registry.autoSave",
        "auto-save setting persists through QField settings engine",
    )
    require_regex(
        "src/core/qgismobileapp.cpp",
        r"projectLoaded\s*=\s*mProject->read\s*\(.*?"
        r"projectLoadAttempted\s*&&\s*!projectLoaded.*?"
        r"mProject->clear\s*\(\s*\).*?"
        r"emit\s+loadProjectEnded\s*\(\s*QString\s*\(\s*\)\s*,\s*QString\s*\(\s*\)\s*\)",
        "unreadable QGS/QGZ is rejected instead of reported as loaded",
    )
    require_regex(
        "src/qml/qgismobileapp.qml",
        r"function\s+onLoadProjectEnded\s*\(\s*path\s*,\s*name\s*\).*?"
        r"if\s*\(\s*path\s*===\s*['\"]{2}\s*\).*?welcomeScreen\.visible\s*=\s*true",
        "failed project read returns to the Sungsan home screen",
    )


def check_android_bridge() -> None:
    require(
        "src/core/platforms/platformutilities.h",
        "Q_INVOKABLE virtual void importProjectArchive() const;",
        "platform import API exposed to QML",
    )
    require(
        "src/core/platforms/platformutilities.h",
        "Q_INVOKABLE virtual void sendCompressedFolderTo( const QString &path ) const;",
        "platform compressed export API exposed to QML",
    )
    require(
        "src/core/platforms/android/androidplatformutilities.cpp",
        'activity.callMethod<void>( "triggerImportProjectArchive" )',
        "QML import reaches Android activity",
    )
    require(
        "src/core/platforms/android/androidplatformutilities.cpp",
        'activity.callMethod<void>( "sendCompressedFolderTo", "(Ljava/lang/String;)V"',
        "QML export reaches Android activity",
    )
    require(
        "platform/android/src/ch/opengis/qfield/QFieldActivity.java",
        "private void triggerImportProjectArchive()",
        "Android ZIP picker exists",
    )
    require(
        "platform/android/src/ch/opengis/qfield/QFieldActivity.java",
        "private void sendCompressedFolderTo(String path)",
        "Android ZIP share exists",
    )
    require_regex(
        "src/core/platforms/android/androidplatformutilities.cpp",
        r"QFieldActivity,\s*clearProject.*?QThread::currentThread\s*\(\s*\)\s*==\s*iface->thread\s*\(\s*\).*?"
        r"QMetaObject::invokeMethod\s*\(\s*iface\s*,\s*clearCurrentProject\s*,\s*Qt::BlockingQueuedConnection",
        "Android project close is completed on the Qt object thread",
    )
    require_regex(
        "src/core/platforms/android/androidplatformutilities.cpp",
        r"QFieldActivity,\s*openProject.*?QThread::currentThread\s*\(\s*\)\s*==\s*iface->thread\s*\(\s*\).*?"
        r"QMetaObject::invokeMethod\s*\(\s*iface\s*,\s*loadProject\s*,\s*Qt::BlockingQueuedConnection",
        "Android project open is completed on the Qt object thread",
    )
    require(
        "scripts/build-sungsan-android.sh",
        'APP_PACKAGE_ID="kr.co.sungsan.mobilegis"',
        "independent Android package ID",
    )
    require(
        "scripts/build-sungsan-android.sh",
        'APP_URL_SCHEME="sungsanmobilegis"',
        "independent deep-link scheme",
    )
    require(
        "scripts/build-sungsan-android.sh",
        'APP_DATA_DIR_NAME="SungsanMobileGIS"',
        "independent data directory",
    )
    require(
        "scripts/build-sungsan-android.sh",
        'APP_VERSION_STR="${APP_VERSION_STR:-1.2.0}"',
        "Sungsan product version name",
    )
    require(
        "scripts/build-sungsan-android.sh",
        'APK_VERSION_CODE="${APK_VERSION_CODE:-10200000}"',
        "monotonically increased Android version code",
    )
    require(
        "scripts/build-sungsan-android.sh",
        'WITH_SAMPLE_PROJECTS="OFF"',
        "upstream sample projects disabled for Sungsan build",
    )
    require(
        "scripts/build-sungsan-android.sh",
        'QFIELD_CMAKE_BUILD_DIR="/usr/src/qfield/build-sungsan-native-${triplet}"',
        "Sungsan native build uses an isolated CMake cache",
    )
    require(
        "scripts/build-sungsan-android.sh",
        'APK_SOURCE_DIR="${SOURCE_BUILD_DIR}/src/app/android-build/build/outputs/apk"',
        "APK lookup uses the isolated Sungsan native build directory",
    )
    require_regex(
        "scripts/build-sungsan-android.sh",
        r"for\s+existing_path\s+in.*?SOURCE_BUILD_DIR.*?GENERATED_BUILD_DIR.*?OUTPUT_DIR.*?"
        r"\[\[\s+-e\s+['\"]?\$\{existing_path\}.*?exit\s+5",
        "Sungsan build refuses stale native, generated-plugin and APK paths",
    )
    require(
        "scripts/build-sungsan-android.sh",
        '-newer "${BUILD_MARKER}"',
        "only APKs created by the current build are collected",
    )
    require_regex(
        "scripts/build-sungsan-android.sh",
        r"signing_variable_count\s*!=\s*0\s*&&\s*signing_variable_count\s*!=\s*3.*?"
        r"ALLOW_UNSIGNED_TEST_BUILD.*?BUILD_KIND=\"signed_release\".*?"
        r"SUNG_SAN_KEYSTORE_PATH.*?android-build-release-unsigned\.apk.*?"
        r"android-build-release-signed\.apk",
        "distribution build requires external signing and selects the exact signed output",
    )
    require(
        "scripts/build-sungsan-android.sh",
        '"${OUTPUT_DIR}/DEBUG-KEY-TEST-ONLY-$(basename "${APK_FILES[0]}")"',
        "explicit debug-key test artifacts cannot be mistaken for releases",
    )
    forbid(
        "scripts/build-sungsan-android.sh",
        '/usr/src/qfield/build-sungsan-${triplet}',
        "legacy mixed Sungsan CMake cache path",
    )
    forbid(
        "scripts/build-sungsan-android.sh",
        '${SOURCE_DIR}/build-sungsan-${triplet}/src/app/android-build',
        "legacy mixed Sungsan APK lookup path",
    )


def check_vworld() -> None:
    build = "scripts/build-sungsan-android.sh"
    plugin = "branding/sungsan/plugins/sungsan_vworld/main.qml.in"
    require(build, 'SUNG_SAN_CONFIGURE_VWORLD="ON"', "VWorld plugin generation enabled")
    require(build, "SUNG_SAN_VWORLD_API_KEY", "VWorld key required at build time")
    require(
        build,
        'APP_BUNDLED_PLUGINS="/usr/src/qfield/build-sungsan-native-generated/plugins"',
        "generated VWorld plugin bundled from an isolated directory",
    )
    require(
        "scripts/build-vcpkg.sh",
        '-D OUTPUT_DIR="${SOURCE_DIR}/build-sungsan-native-generated/plugins"',
        "VWorld generation uses the isolated Sungsan native directory",
    )
    forbid(
        build,
        '/usr/src/qfield/build-sungsan-generated/plugins',
        "legacy mixed Sungsan bundled-plugin directory",
    )
    forbid(
        "scripts/build-vcpkg.sh",
        '${SOURCE_DIR}/build-sungsan-generated/plugins',
        "legacy mixed Sungsan generated-plugin directory",
    )
    require(
        plugin,
        "@SUNG_SAN_VWORLD_API_KEY@",
        "VWorld source keeps a build-time key placeholder",
    )
    require(plugin, "/Satellite/%7Bz%7D/%7By%7D/%7Bx%7D.jpeg", "VWorld z/y/x WMTS path")
    require(plugin, "zmax=19&zmin=6", "VWorld zoom bounds")
    require(plugin, "ProjectUtils.addMapLayerAtBottom", "VWorld layer added at tree bottom")
    require(plugin, "onLoadProjectEnded", "VWorld layer checked after project load")
    require_regex(
        "src/core/utils/projectutils.cpp",
        r"addMapLayerAtBottom.*?project->addMapLayer\s*\(\s*layer\s*,\s*false\s*\).*?layerTreeRoot\s*\(\s*\)->addLayer",
        "VWorld bottom insertion implemented in core",
    )

    key_pattern = re.compile(
        r"api\.vworld\.kr/req/wmts/1\.0\.0/(?!@SUNG_SAN_VWORLD_API_KEY@)"
        r"[A-Za-z0-9-]{20,}/Satellite/"
    )
    source_files = [
        build,
        plugin,
        "branding/sungsan/configure-vworld-plugin.cmake",
    ]
    if any(key_pattern.search(read(path)) for path in source_files):
        FAILURES.append("real-looking VWorld API key committed to production source")
    else:
        PASSES.append("no real-looking VWorld key in production source")


def check_opt_in_vworld_plugin_restore() -> None:
    plugin_manager = "src/core/pluginmanager.cpp"
    require(plugin_manager, '#include "qfield.h"', "plugin manager reads Sungsan build identity")
    require(
        plugin_manager,
        'QStringLiteral( "sungsan_vworld" )',
        "opt-in plugin uses the exact bundled-directory UUID",
    )
    require_regex(
        plugin_manager,
        r"isOptInSungsanVWorldPlugin.*?qfield::isSungsanBuild\s*&&\s*"
        r"pluginInformation\.bundled\s*&&\s*pluginInformation\.uuid\s*==\s*"
        r"sungsanVWorldPluginUuid",
        "opt-in VWorld predicate is limited to Sungsan, bundled and exact UUID",
    )
    forbid(
        plugin_manager,
        "normalizeMandatorySungsanVWorldSettings",
        "legacy forced VWorld enablement",
    )

    source = read(plugin_manager)
    restore_start = source.find("void PluginManager::restoreAppPlugins()")
    enable_start = source.find("void PluginManager::enableAppPlugin", restore_start)
    restore_body = source[restore_start:enable_start]
    opt_in_restore_pattern = re.compile(
        r"if\s*\(\s*isOptInSungsanVWorldPlugin\s*\(\s*plugin\s*\)\s*\)\s*"
        r"\{\s*continue\s*;\s*\}",
        re.DOTALL,
    )
    if restore_start < 0 or enable_start < 0 or not opt_in_restore_pattern.search(restore_body):
        FAILURES.append(
            "src/core/pluginmanager.cpp: first-run bundled VWorld must remain disabled "
            "until the user explicitly enables it"
        )
    else:
        PASSES.append("bundled VWorld is opt-in on first run")

    plugin_dir = ROOT / "branding/sungsan/plugins/sungsan_vworld"
    if plugin_dir.name != "sungsan_vworld" or not (plugin_dir / "main.qml.in").is_file():
        FAILURES.append("bundled VWorld plugin directory does not match its exact UUID")
    else:
        PASSES.append("bundled VWorld directory UUID is exactly sungsan_vworld")


def main() -> int:
    qml_files = [
        "src/qml/qgismobileapp.qml",
        "src/qml/editorwidgets/relationeditors/gallery_relation_editor.qml",
        "src/qml/sungsan/SungsanActionButton.qml",
        "src/qml/sungsan/SungsanHomeScreen.qml",
        "src/qml/sungsan/SungsanFieldPanel.qml",
        "branding/sungsan/plugins/sungsan_vworld/main.qml.in",
    ]
    for qml_file in qml_files:
        balanced_qml(qml_file)

    check_android_default_strings()
    check_upstream_revision_attribution()
    check_qml_resources()
    check_shell_wiring()
    check_save_api()
    check_android_bridge()
    check_vworld()
    check_opt_in_vworld_plugin_restore()

    if FAILURES:
        print(f"FAIL: {len(FAILURES)} issue(s); {len(PASSES)} static checks passed")
        for failure in FAILURES:
            print(f" - {failure}")
        print("NOTE: this check does not replace qmllint, a Qt/QGIS build, or device tests.")
        return 1

    print(f"OK: {len(PASSES)} Sungsan native UI static checks passed")
    print("NOTE: this check does not replace qmllint, a Qt/QGIS build, or device tests.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
