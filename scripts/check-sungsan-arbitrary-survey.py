#!/usr/bin/env python3
"""Static contract gate for surveying ordinary user-created vector layers.

This deliberately complements, rather than replaces, the field-project and
native-UI gates.  In particular, it prevents the branded survey workflow from
silently becoming dependent on the generated Sungsan point template again.
It cannot prove provider permissions, camera behavior or Android MediaStore
indexing; those still require QGIS/Qt and device tests.
"""

from __future__ import annotations

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
APP = (ROOT / "src/qml/qgismobileapp.qml").read_text(encoding="utf-8")
PANEL = (ROOT / "src/qml/sungsan/SungsanFieldPanel.qml").read_text(encoding="utf-8")
BRIDGE_H = (ROOT / "src/core/sungsansurveybridge.h").read_text(encoding="utf-8")
BRIDGE_CPP = (ROOT / "src/core/sungsansurveybridge.cpp").read_text(encoding="utf-8")
EXTERNAL_RESOURCE = (ROOT / "src/qml/editorwidgets/ExternalResource.qml").read_text(encoding="utf-8")
PROJECT_UTILS = (ROOT / "src/core/utils/projectutils.cpp").read_text(encoding="utf-8")
FILE_UTILS_H = (ROOT / "src/core/utils/fileutils.h").read_text(encoding="utf-8")
FILE_UTILS_CPP = (ROOT / "src/core/utils/fileutils.cpp").read_text(encoding="utf-8")
ORIENTATION_CPP = (
    ROOT / "src/core/cameraorientationnormalizer.cpp"
).read_text(encoding="utf-8")
ORIENTATION_H = (
    ROOT / "src/core/cameraorientationnormalizer.h"
).read_text(encoding="utf-8")
QFIELD_CAMERA = (ROOT / "src/qml/QFieldCamera.qml").read_text(encoding="utf-8")
ANDROID_ACTIVITY = (
    ROOT / "platform/android/src/ch/opengis/qfield/QFieldActivity.java"
).read_text(encoding="utf-8")


CHECKS: list[tuple[str, bool]] = []


def check(name: str, condition: bool) -> None:
    CHECKS.append((name, condition))


def between(source: str, start_token: str, end_token: str) -> str:
    start = source.find(start_token)
    if start < 0:
        return ""
    end = source.find(end_token, start + len(start_token))
    return source[start:] if end < 0 else source[start:end]


def qml_function_bodies(source: str) -> list[tuple[str, list[str], str]]:
    """Return ordinary JavaScript function bodies from a QML source file.

    A small delimiter scanner is used instead of a regular-expression-only
    match so braces in strings and comments do not make a cleanup contract
    appear to pass accidentally.
    """

    functions: list[tuple[str, list[str], str]] = []
    pattern = re.compile(r"\bfunction\s+(\w+)\s*\(([^)]*)\)\s*\{")
    for match in pattern.finditer(source):
        opening = match.end() - 1
        depth = 0
        state = "code"
        quote = ""
        index = opening
        while index < len(source):
            char = source[index]
            following = source[index + 1] if index + 1 < len(source) else ""
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
            elif char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    parameters = [
                        parameter.strip()
                        for parameter in match.group(2).split(",")
                        if parameter.strip()
                    ]
                    functions.append(
                        (match.group(1), parameters, source[opening + 1:index])
                    )
                    break
            index += 1
    return functions


start_survey = between(
    APP, "function sungsanStartSurvey", "function sungsanShowCurrentLocation"
)
layer_selector = between(
    APP, "function ensureEditableLayerSelected", "property bool shouldReturnHome"
)
existing_handler = between(
    APP, "onEditExistingFeatureRequested:", "onAddVertexRequested:"
)
prepare_layer = between(
    BRIDGE_CPP,
    "SungsanSurveyBridge::prepareFieldSurveyLayer",
    "SungsanSurveyBridge::queryLandStarMetadata",
)
object_naming = between(
    prepare_layer, "QString objectNameExpression", "const QStringList fixedPhotoSuffixes"
)
folder_setup = between(
    prepare_layer, "QString safeLayerName", "QVariantMap photoWidgetOptions"
)
external_functions = qml_function_bodies(EXTERNAL_RESOURCE)


# Generic survey selection: generated-template markers must not be prerequisites.
check("generic field-panel layer state", "property bool editableVectorLayer: false" in PANEL)
check("generic existing-feature capability", "property bool canEditExistingFeature: false" in PANEL)
check("generic existing-feature pending state", "property bool existingFeatureSelectionPending: false" in PANEL)
check("generic existing-feature signal", "signal editExistingFeatureRequested" in PANEL)
check("generic existing-feature handler", bool(existing_handler))
check("point-only panel API removed", all(token not in PANEL for token in (
    "canEditExistingPoint", "existingPointSelectionPending", "editExistingPointRequested"
)))
check("point-only app state removed", all(token not in APP for token in (
    "sungsanExistingPointEditPending", "onEditExistingPointRequested"
)))
check("permission-aware layer selector", "ensureEditableLayerSelected(requireFeatureAddition)" in layer_selector)
check("permission-aware survey entry", "function sungsanStartSurvey(requireFeatureAddition)" in start_survey)
check(
    "survey entry returns a result",
    "return false" in start_survey
    and bool(
        re.search(
            r"return\s+stateMachine\.state\s*===\s*[\"']digitize[\"']",
            start_survey,
        )
    ),
)
check("new features require addition capability", "sungsanStartSurvey(true)" in APP)
check("existing features do not require addition capability", "sungsanStartSurvey(false)" in existing_handler)
check(
    "selected layer is prepared",
    "sungsanPrepareFieldPhotoLayer(dashBoard.activeLayer, true)" in start_survey
    and "sungsanSurveyBridge.prepareFieldSurveyLayer(qgisProject, layer)"
    in start_survey,
)
check("survey start is not generated-template gated", all(token not in start_survey for token in (
    "sungsan_field_template", "fieldPackage", "fieldObjects", "landstarImportTarget"
)))
check(
    "survey start permits a loaded unnamed user project",
    "if (!qgisProject)" in start_survey
    and not re.search(
        r"if\s*\(\s*!qgisProject\s*\|\|\s*!qgisProject\.fileName",
        start_survey,
    )
    and "조사는 시작할 수 있지만" in start_survey,
)
check(
    "existing editing supports point, line and polygon",
    all(
        geometry in existing_handler
        for geometry in (
            "Qgis.GeometryType.Point",
            "Qgis.GeometryType.Line",
            "Qgis.GeometryType.Polygon",
        )
    )
    and "geometryType() !== Qgis.GeometryType.Point" not in existing_handler,
)
check("all identified geometry opens edit form", "featureListForm.state = \"FeatureFormEdit\"" in APP)
check("identified feature is restricted to active layer", all(token in APP for token in (
    "MultiFeatureListModel.LayerRole", "resultLayer === activeLayer"
)))


# Common preparation API: it must configure, not replace, an ordinary user layer.
check("prepare API exposed to QML", bool(re.search(
    r"Q_INVOKABLE\s+QVariantMap\s+prepareFieldSurveyLayer\s*\(\s*QgsProject\s*\*\s*project\s*,\s*QgsVectorLayer\s*\*\s*layer\s*\)",
    BRIDGE_H,
)))
check("prepare API implemented", bool(prepare_layer))
check(
    "prepare API accepts every supported survey geometry",
    all(
        geometry in prepare_layer
        for geometry in (
            "Qgis::GeometryType::Point",
            "Qgis::GeometryType::Line",
            "Qgis::GeometryType::Polygon",
        )
    )
    and "Qgis::GeometryType::Null" not in prepare_layer
    and "PointZ" not in prepare_layer,
)
check("prepare API rejects unusable layers", all(token in prepare_layer for token in (
    "isValid()", "supportsEditing()", "readOnly()"
)))
check(
    "remote database schemas are never changed automatically",
    'providerType == QLatin1String( "ogr" )' in prepare_layer
    and 'providerType == QLatin1String( "spatialite" )' in prepare_layer
    and "공유·원격 데이터베이스의 스키마는 앱이 자동으로 바꾸지 않습니다" in prepare_layer,
)
check("prepared-layer marker", "kr.co.sungsan.mobilegis/managedFieldPhotos" in prepare_layer)
check("photo field metadata", "kr.co.sungsan.mobilegis/fieldPhotoFields" in prepare_layer)
check("object-name field metadata", "kr.co.sungsan.mobilegis/photoObjectNameField" in prepare_layer)
check("layer photo-folder metadata", "kr.co.sungsan.mobilegis/fieldPhotoFolder" in prepare_layer)
check("existing attachment naming is preserved", "QFieldSync/attachment_naming" in prepare_layer)
check("project-relative photo widgets", "RelativeStorage" in prepare_layer and "ExternalResource" in prepare_layer)
check("four fixed legacy slots can be reused", all(token in prepare_layer for token in (
    "photo_near", "photo_far", "photo_other", "photo_other_2"
)))
check("four fixed fallback slots can be created", all(token in prepare_layer for token in (
    "ss_photo1", "ss_photo2", "ss_photo3", "ss_photo4"
)))
check(
    "four photo slot labels",
    all(f'tr( "{label}" )' in prepare_layer for label in (
        "근경", "원경", "기타", "기타2"
    )),
)
check(
    "layer-based images directory",
    'QStringLiteral( "images/%1" ).arg( safeLayerName )' in prepare_layer
    and "'%2/' || @object_name_safe || %3 || '%4'" in prepare_layer,
)
check(
    "layer and object filename components are sanitized",
    (
        "safeLayerName.replace( QRegularExpression" in prepare_layer
        or (
            "safeLayerFolderComponent" in prepare_layer
            and "name.replace( QRegularExpression" in prepare_layer
        )
    )
    and "objectNameSafeExpression" in prepare_layer
    and "regexp_replace" in prepare_layer,
)
check(
    "missing object name falls back to a stable feature identifier",
    "'객체_' || to_string($id)" in prepare_layer,
)
check(
    "duplicate object names cannot overwrite another feature's photo",
    "duplicateNameSuffixExpression" in prepare_layer
    and "aggregate(@layer, 'count', $id" in prepare_layer
    and "$id != @current_fid" in prepare_layer
    and "'_' || to_string(@current_fid)" in prepare_layer,
)
check(
    "filename collision detection compares sanitized object names",
    "aggregate(@layer, 'count', $id" in object_naming
    and "regexp_replace" in object_naming
    and bool(
        re.search(
            r"duplicateNameSuffixExpression\s*=.*?"
            r"lower\(%2\)\s*=\s*lower\(@object_name_safe\).*?"
            r"\.arg\(\s*objectNameExpression\s*,\s*objectNameSafeExpression\s*\)",
            object_naming,
            re.DOTALL,
        )
    ),
)
check(
    "sanitized duplicate layer names receive distinct folders",
    "project->mapLayers()" in folder_setup
    and "safeLayerFolderComponent( candidateLayer->name(), candidateLayer->id() )"
    in folder_setup
    and "candidateSafeName.compare( safeLayerName, Qt::CaseInsensitive ) == 0"
    in folder_setup
    and bool(
        re.search(
            r"QCryptographicHash::hash\(\s*layer->id\(\)\.toUtf8\(\).*?"
            r"photoFolder\s*=\s*QStringLiteral\(\s*\"%1_%2\"\s*\)"
            r"\.arg\(\s*defaultPhotoFolder\s*,\s*layerIdSuffix\s*\)",
            folder_setup,
            re.DOTALL,
        )
    ),
)
configured_folder_read = re.search(
    r"(?:"
    r"layer->customProperty\s*\(\s*QStringLiteral\s*\(\s*"
    r'"kr\.co\.sungsan\.mobilegis/fieldPhotoFolder"\s*\)\s*\)'
    r"|"
    r'photoFolderProperty\s*=\s*QStringLiteral\s*\(\s*"kr\.co\.sungsan\.mobilegis/fieldPhotoFolder"\s*\)'
    r".*?layer->customProperty\s*\(\s*photoFolderProperty\s*\)"
    r")\.toString\s*\(\s*\)",
    prepare_layer,
    re.DOTALL,
)
check(
    "existing managed photo folder is read and preserved",
    bool(configured_folder_read)
    and "QString photoFolder = configuredPhotoFolder" in prepare_layer
    and "if ( photoFolder.isEmpty() )" in folder_setup,
)
check(
    "fixed numbered filenames",
    ".{extension}" in prepare_layer
    and (
        all(
            re.search(rf"\({number}\).*?\{{extension\}}", prepare_layer, re.DOTALL)
            for number in range(1, 5)
        )
        or (
            "(%3).{extension}" in prepare_layer
            and "slot + 1" in prepare_layer
            and "photoFields.size()" in prepare_layer
        )
    ),
)
check("prepared arbitrary layers are attached to exports", "attachmentDirectories" in prepare_layer or "attachment_directories" in prepare_layer)
check(
    "custom drag-and-drop form is preserved",
    "Qgis::AttributeFormLayout::DragAndDrop" in prepare_layer
    and "findElements" in prepare_layer
    and "fieldsInForm.contains( fieldName )" in prepare_layer
    and "addChildElement" in prepare_layer
    and "layer->setEditFormConfig( formConfig )" in prepare_layer
    and "clearTabs" not in prepare_layer,
)
check(
    "missing photos are grouped in the field form",
    'tr( "현장사진" )' in prepare_layer
    and "QgsAttributeEditorContainer" in prepare_layer
    and "QgsAttributeEditorField" in prepare_layer,
)


# Generated projects must use the same portable contract as arbitrary layers.
check("generated project uses images directory", "images/성산_현장객체" in PROJECT_UTILS)
check("legacy single photo directory removed", "photos/현장사진" not in PROJECT_UTILS)
check("generated project has fixed fourth slot", "photo_other_2" in PROJECT_UTILS and "기타2" in PROJECT_UTILS)


# The editor guard must recognize managed arbitrary-layer slots, not just the
# four column names from the generated point template.
check("managed arbitrary photo guard", "kr.co.sungsan.mobilegis/managedFieldPhotos" in EXTERNAL_RESOURCE)
check("managed photo-field list guard", "kr.co.sungsan.mobilegis/fieldPhotoFields" in EXTERNAL_RESOURCE)
check(
    "managed JSON photo-field list is decoded",
    "JSON.parse" in EXTERNAL_RESOURCE,
)
check(
    "managed photo can be deleted without clearing other slots",
    "visible: image.source !== ''" in EXTERNAL_RESOURCE
    and "isSungsanManagedFieldPhoto()" in EXTERNAL_RESOURCE
    and "FileUtils.deleteFiles([absolutePath])" in EXTERNAL_RESOURCE
    and 'valueChangeRequested("", false)' in EXTERNAL_RESOURCE,
)
check(
    "preparation never mutates existing feature values",
    all(
        mutation not in prepare_layer
        for mutation in (
            "changeAttributeValue(",
            "setAttribute(",
            "updateFeature(",
            "deleteFeature(",
        )
    ),
)

replacement_helper = next(
    (
        body
        for name, parameters, body in external_functions
        if name == "commitSungsanManagedPhoto" and parameters == ["path"]
    ),
    "",
)
remember_helper = next(
    (
        body
        for name, parameters, body in external_functions
        if name == "rememberSungsanPreviousPhoto" and not parameters
    ),
    "",
)
capture_helper = next(
    (
        body
        for name, parameters, body in external_functions
        if name == "capturePhoto" and not parameters
    ),
    "",
)
gallery_helper = next(
    (
        body
        for name, parameters, body in external_functions
        if name == "attachGallery" and not parameters
    ),
    "",
)
new_file_check_position = replacement_helper.find(
    "FileUtils.fileExists(absoluteNewPath)"
)
value_change_position = replacement_helper.find("valueChangeRequested(relativePath")
if new_file_check_position >= 0:
    value_change_position = replacement_helper.find(
        "valueChangeRequested(relativePath", new_file_check_position
    )
old_file_delete_position = replacement_helper.find(
    "FileUtils.deleteFiles([absolutePreviousPath])"
)
remember_capture_position = capture_helper.find("rememberSungsanPreviousPhoto()")
path_capture_position = capture_helper.find("getResourceFilePath()")

check(
    "object-name changes keep the stored photo link until recapture succeeds",
    bool(remember_helper)
    and "sungsanPendingPreviousPhotoPath" in remember_helper
    and "currentValue" in remember_helper
    and "String(currentValue)" in remember_helper
    and 0 <= remember_capture_position < path_capture_position,
)
check(
    "recapture removes the previous managed photo only after replacement succeeds",
    bool(replacement_helper)
    and "const previousPath = sungsanPendingPreviousPhotoPath" in replacement_helper
    and "const currentPathBeforeCommit = currentValue" in replacement_helper
    and "normalizedSungsanPhotoPath(currentPathBeforeCommit) === normalizedSungsanPhotoPath(previousPath)"
    in replacement_helper
    and "normalizedSungsanPhotoPath(previousPath) !== normalizedSungsanPhotoPath(relativePath)"
    in replacement_helper
    and "normalizedSungsanPhotoPath(absolutePreviousPath) !== normalizedSungsanPhotoPath(absoluteNewPath)"
    in replacement_helper
    and 0 <= new_file_check_position < value_change_position < old_file_delete_position
    # Only camera capture may arm cleanup. Gallery/file imports do not expose a
    # uniformly validated success callback, so they must clear pending state.
    and 'sungsanPendingPreviousPhotoPath = ""' in gallery_helper
    and "rememberSungsanPreviousPhoto()" not in gallery_helper
    and EXTERNAL_RESOURCE.count("commitSungsanManagedPhoto(") >= 3,
)
check(
    "portable camera fallback replaces an existing photo atomically",
    "FileUtils.replaceFileSafely(path, destinationPath)" in EXTERNAL_RESOURCE
    and "Q_INVOKABLE static bool replaceFileSafely" in FILE_UTILS_H
    and "QSaveFile destinationFile( destinationFilePath )" in FILE_UTILS_CPP
    and "destinationFile.commit()" in FILE_UTILS_CPP
    and FILE_UTILS_CPP.find("destinationFile.commit()")
    < FILE_UTILS_CPP.find("QFile::remove( sourceFilePath )"),
)


# A second MediaStore/Pictures copy violates the single-original contract.
check("gallery copy helper removed", "publishSungsanFieldPhotoToGallery" not in ANDROID_ACTIVITY)
check("gallery duplicate-copy wording removed", "Keeps a second copy" not in ANDROID_ACTIVITY)
check("legacy duplicate gallery album removed", '"/성산 GIS/"' not in ANDROID_ACTIVITY)
check(
    "no second MediaStore photo is created",
    "MediaStore.Images.Media.RELATIVE_PATH" not in ANDROID_ACTIVITY
    and "MediaStore.Images.Media.IS_PENDING" not in ANDROID_ACTIVITY,
)
check("captured file is validated before replacement", "isValidCapturedResource" in ANDROID_ACTIVITY)
check(
    "recapture uses atomic replacement with backup recovery",
    "StandardCopyOption.ATOMIC_MOVE" in ANDROID_ACTIVITY
    and "StandardCopyOption.REPLACE_EXISTING" in ANDROID_ACTIVITY
    and "restoreCaptureBackup" in ANDROID_ACTIVITY,
)
check(
    "Android capture normalizes EXIF pixels",
    "matrixForExifOrientation" in ANDROID_ACTIVITY
    and "ExifInterface.TAG_ORIENTATION" in ANDROID_ACTIVITY
    and "ExifInterface.ORIENTATION_NORMAL" in ANDROID_ACTIVITY,
)
check(
    "portable camera normalizer handles Android safely",
    "Q_OS_ANDROID" in ORIENTATION_CPP
    and "setAutoTransform( hasExifTransform )" in ORIENTATION_CPP
    and "QSaveFile" in ORIENTATION_CPP,
)
check(
    "managed field photos always use the orientation-aware in-app camera",
    bool(capture_helper)
    and "if (isSungsanManagedFieldPhoto())" in capture_helper
    and "cameraLoader.active = true" in capture_helper
    and "return;" in capture_helper
    and capture_helper.find("cameraLoader.active = true")
    < capture_helper.find("PlatformUtilities.NativeCamera"),
)
check(
    "camera records physical device orientation before capture",
    "recordCaptureDeviceOrientation( int deviceOrientation, bool viewportLandscape )" in ORIENTATION_H
    and "CameraOrientationNormalizer::recordCaptureDeviceOrientation( int deviceOrientation, bool viewportLandscape )"
    in ORIENTATION_CPP
    and "OrientationSensor" in QFIELD_CAMERA
    and "lastStableDeviceOrientation" in QFIELD_CAMERA
    and "recordCaptureDeviceOrientation(" in QFIELD_CAMERA
    and QFIELD_CAMERA.find("recordCaptureDeviceOrientation(")
    < QFIELD_CAMERA.find("imageCapture.captureToFile"),
)
check(
    "active ordinary layer receives default photo slots",
    "onActiveLayerChanged" in APP
    and "sungsanPhotoLayerPreparationTimer.restart()" in APP
    and "function sungsanPrepareFieldPhotoLayer(layer, showWarnings)" in APP
    and "sungsanSurveyBridge.prepareFieldSurveyLayer(qgisProject, layer)" in APP,
)
check(
    "all project layers receive default photo slots on project load",
    "prepareFieldSurveyProject( QgsProject *project )" in BRIDGE_H
    and "SungsanSurveyBridge::prepareFieldSurveyProject( QgsProject *project )" in BRIDGE_CPP
    and "sungsanPhotoProjectPreparationTimer.restart()" in APP
    and "sungsanSurveyBridge.prepareFieldSurveyProject(qgisProject)" in APP,
)
check(
    "project photo itself is media-scanned",
    bool(re.search(
        r"scanCapturedResource\s*\(\s*result\s*,\s*capturedIsVideo\s*\)"
        r".*?resourceReceived\s*\(",
        ANDROID_ACTIVITY,
        re.DOTALL,
    ))
    and bool(re.search(
        r"MediaScannerConnection\.scanFile\s*\(.*?"
        r"result\.getAbsolutePath\s*\(\s*\)",
        ANDROID_ACTIVITY,
        re.DOTALL,
    )),
)


failed = [name for name, ok in CHECKS if not ok]
for name, ok in CHECKS:
    print(f"[{'PASS' if ok else 'FAIL'}] {name}")

if failed:
    print(f"\nFAIL: {len(failed)} of {len(CHECKS)} arbitrary-survey checks failed", file=sys.stderr)
    print("NOTE: static checks do not replace QGIS provider, Android camera, MediaStore or real-device tests.")
    sys.exit(1)

print(f"\nPASS: {len(CHECKS)}/{len(CHECKS)} arbitrary-survey checks")
print("NOTE: static checks do not replace QGIS provider, Android camera, MediaStore or real-device tests.")
