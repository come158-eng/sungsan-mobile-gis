#!/usr/bin/env python3
"""Static contract checks for the Sungsan default field-project template."""

from __future__ import annotations

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]
HOME = (ROOT / "src/qml/sungsan/SungsanHomeScreen.qml").read_text(encoding="utf-8")
APP = (ROOT / "src/qml/qgismobileapp.qml").read_text(encoding="utf-8")
UTILS = (ROOT / "src/core/utils/projectutils.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "src/core/utils/projectutils.h").read_text(encoding="utf-8")
ANDROID_ACTIVITY = (
    ROOT / "platform/android/src/ch/opengis/qfield/QFieldActivity.java"
).read_text(encoding="utf-8")
EXTERNAL_RESOURCE = (
    ROOT / "src/qml/editorwidgets/ExternalResource.qml"
).read_text(encoding="utf-8")


CHECKS: list[tuple[str, bool]] = []


def check(name: str, condition: bool) -> None:
    CHECKS.append((name, condition))


check("home signal", "signal createFieldProjectRequested" in HOME)
check("generic home label", 'text: "기본 현장 프로젝트 만들기"' in HOME)
check("LandStar/photo home detail", "LandStar 측점과 현장 사진" in HOME)
check("home emits create signal", "root.createFieldProjectRequested(rawRegion, rawSite, safeDate)" in HOME)
check("home has no fixed region", "성주군" not in HOME and "EPSG:5183" not in HOME)

handler_start = APP.find("onCreateFieldProjectRequested:")
handler_end = APP.find("onOpenRecentProjectRequested:", handler_start)
handler = APP[handler_start:handler_end] if handler_start >= 0 and handler_end > handler_start else ""
check("app create handler", bool(handler))
check("field template option", '"sungsan_field_template": true' in handler)
check("blank basemap", '"basemap": "blank"' in handler)
check("device position passed", "positionSource.positionInformation" in handler)
check("regional metadata passed", '"region_name"' in handler)
check("site metadata passed", '"site_name"' in handler)
check("work date metadata passed", '"work_date"' in handler)
check("created project loads immediately", "iface.loadFile(projectFilePath, resolvedTitle)" in handler)
check("create failure is handled", "projectFilePath.length === 0" in handler)
check("no automatic VWorld", "VWorld" not in handler and "vworld" not in handler.lower())
check("comparison export invoked", "exportFieldSurveyComparisonReport" in APP)

check("header documents option", "sungsan_field_template" in HEADER)
check("template is opt-in", 'options.value( QStringLiteral( "sungsan_field_template" ) ).toBool()' in UTILS)
check("normal default CRS retained", 'defaultProjectCrs( QStringLiteral( "EPSG:3857" ) )' in UTILS)
check(
    "no region or EPSG hardcoding",
    all(token not in UTILS for token in ("성주군", "EPSG:5183", "128.282", "35.919", "seongju")),
)
check("template package marker", "kr.co.sungsan.mobilegis.field-package/1" in UTILS)
check("LandStar CRS warning metadata", "/landstarCrsNotice" in UTILS and "현재 프로젝트 좌표계" in UTILS)

parent_fields = (
    "object_id",
    "name",
    "category",
    "memo",
    "color",
    "created_at",
    "gps_accuracy_m",
    "landstar_id",
    "landstar_code",
    "northing",
    "easting",
    "elevation",
    "fix_status",
    "surveyed_at",
    "source_device",
    "photo_near",
    "photo_far",
    "photo_other",
    "photo_other_2",
)
for field_name in parent_fields:
    check(f"parent field {field_name}", f'QStringLiteral( "{field_name}" )' in UTILS)

check("PointZ field layer", "Qgis::WkbType::PointZ, defaultProjectCrs" in UTILS)
check("field object marker", "kr.co.sungsan.mobilegis/fieldObjects" in UTILS)
check("LandStar target marker", "kr.co.sungsan.mobilegis/landstarImportTarget" in UTILS)
check(
    "only technical parent fields hidden",
    'const QStringList hiddenObjectFields = { QStringLiteral( "fid" ), QStringLiteral( "object_id" ) }'
    in UTILS,
)
check("external resource editor", 'QgsEditorWidgetSetup( QStringLiteral( "ExternalResource" )' in UTILS)
check("project-relative photo storage", 'fieldPhotoOptions.insert( QStringLiteral( "RelativeStorage" ), 1 )' in UTILS)
check("layer-based field-photo directory", "'images/성산_현장객체/' || @object_name_safe" in UTILS)
check("no conflicting external-resource root", 'fieldPhotoOptions.insert( QStringLiteral( "DefaultRoot" )' not in UTILS)
check("images included as attachment directory", 'attachmentDirectories << "images"' in UTILS)
check("QField attachment naming", "QFieldSync/attachment_naming" in UTILS)
check(
    "object-based fixed attachment names",
    all(
        token in UTILS
        for token in (
            'fixedPhotoNaming.insert( QStringLiteral( "photo_near" ), photoNameBaseExpression.arg( 1 ) )',
            'fixedPhotoNaming.insert( QStringLiteral( "photo_far" ), photoNameBaseExpression.arg( 2 ) )',
            'fixedPhotoNaming.insert( QStringLiteral( "photo_other" ), photoNameBaseExpression.arg( 3 ) )',
            'fixedPhotoNaming.insert( QStringLiteral( "photo_other_2" ), photoNameBaseExpression.arg( 4 ) )',
            ".{extension}",
        )
    ),
)
check("photo section follows object name", UTILS.index('QStringLiteral( "name" ), nameFieldIndex') < UTILS.index('QStringLiteral( "현장사진" )'))
check("no plus-button photo relation", "현장 사진 · 근경/원경/기타/추가" not in UTILS)
check("layer photo directory created", 'mkpath( QStringLiteral( "images/성산_현장객체" ) )' in UTILS)
check("legacy photo directories removed", all(path not in UTILS for path in (
    "photos/현장사진", "photos/근경", "photos/원경", "photos/기타", "photos/추가"
)))
check(
    "fourth fixed photo is labelled 기타2",
    'QStringLiteral( "photo_other_2" ), QStringLiteral( "기타2 사진" )' in UTILS,
)
check("managed photo marker", "kr.co.sungsan.mobilegis/managedFieldPhotos" in UTILS)
check("managed photo field list", "kr.co.sungsan.mobilegis/fieldPhotoFields" in UTILS)
check("configured photo object-name field", "kr.co.sungsan.mobilegis/photoObjectNameField" in UTILS)
check("configured layer photo folder", "kr.co.sungsan.mobilegis/fieldPhotoFolder" in UTILS)
check("gallery duplicate helper removed", "publishSungsanFieldPhotoToGallery" not in ANDROID_ACTIVITY)
check("gallery duplicate-copy wording removed", "Keeps a second copy" not in ANDROID_ACTIVITY)
check("legacy duplicate gallery album removed", '"/성산 GIS/"' not in ANDROID_ACTIVITY)
check(
    "no second MediaStore photo is created",
    "MediaStore.Images.Media.RELATIVE_PATH" not in ANDROID_ACTIVITY
    and "MediaStore.Images.Media.IS_PENDING" not in ANDROID_ACTIVITY,
)
check(
    "project photo itself is media-scanned",
    bool(
        re.search(
            r"scanCapturedResource\s*\(\s*result\s*,\s*capturedIsVideo\s*\)"
            r".*?resourceReceived\s*\(",
            ANDROID_ACTIVITY,
            re.DOTALL,
        )
    )
    and bool(
        re.search(
            r"MediaScannerConnection\.scanFile\s*\(.*?"
            r"result\.getAbsolutePath\s*\(\s*\)",
            ANDROID_ACTIVITY,
            re.DOTALL,
        )
    ),
)
capture_start = EXTERNAL_RESOURCE.find("function capturePhoto()")
native_camera_start = EXTERNAL_RESOURCE.find(
    "PlatformUtilities.NativeCamera", capture_start
)
capture_preamble = (
    EXTERNAL_RESOURCE[capture_start:native_camera_start]
    if capture_start >= 0 and native_camera_start > capture_start
    else ""
)
check(
    "managed photo capture resolves the configured object name first",
    "sungsanObjectNameEvaluator.evaluate()" in capture_preamble
    and "FeatureUtils.attributeIsNull" in capture_preamble
    and (
        "객체명을 먼저 입력" in capture_preamble
        or "내부 객체 ID" in capture_preamble
    ),
)
check("name guard recognizes managed arbitrary layers", "kr.co.sungsan.mobilegis/managedFieldPhotos" in EXTERNAL_RESOURCE)
check("name guard reads the managed field list", "kr.co.sungsan.mobilegis/fieldPhotoFields" in EXTERNAL_RESOURCE)
check(
    "name guard decodes bridge JSON field lists",
    "JSON.parse" in EXTERNAL_RESOURCE
    and "sungsanManagedPhotoFields().indexOf(field.name)" in EXTERNAL_RESOURCE,
)

failed = [name for name, ok in CHECKS if not ok]
if failed:
    for name in failed:
        print(f"FAIL: {name}")
    print(f"{len(CHECKS) - len(failed)}/{len(CHECKS)} checks passed")
    sys.exit(1)

print(f"PASS: {len(CHECKS)}/{len(CHECKS)} Sungsan field-project checks")
