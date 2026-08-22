#!/usr/bin/env python3
"""Static contract checks for the Sungsan default field-project template."""

from __future__ import annotations

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
HOME = (ROOT / "src/qml/sungsan/SungsanHomeScreen.qml").read_text(encoding="utf-8")
APP = (ROOT / "src/qml/qgismobileapp.qml").read_text(encoding="utf-8")
UTILS = (ROOT / "src/core/utils/projectutils.cpp").read_text(encoding="utf-8")
HEADER = (ROOT / "src/core/utils/projectutils.h").read_text(encoding="utf-8")


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
)
for field_name in parent_fields:
    check(f"parent field {field_name}", f'QStringLiteral( "{field_name}" )' in UTILS)

check("PointZ field layer", "Qgis::WkbType::PointZ, defaultProjectCrs" in UTILS)
check("field object marker", "kr.co.sungsan.mobilegis/fieldObjects" in UTILS)
check("LandStar target marker", "kr.co.sungsan.mobilegis/landstarImportTarget" in UTILS)
check("legacy photo fields hidden", all(name in UTILS for name in ('QStringLiteral( "photo_near" )', 'QStringLiteral( "photo_far" )', 'QStringLiteral( "photo_other" )')))

photo_fields = (
    "photo_id",
    "object_id",
    "point_name",
    "photo_type",
    "sequence",
    "media",
    "captured_at",
    "memo",
)
for field_name in photo_fields:
    check(f"photo field {field_name}", f'QStringLiteral( "{field_name}" )' in UTILS)

check("no-geometry photo table", "Qgis::WkbType::NoGeometry" in UTILS)
check("photo layer marker", "kr.co.sungsan.mobilegis/fieldPhotos" in UTILS)
check("unlimited photo types", all(f'QStringLiteral( "{value}" )' in UTILS for value in ("근경", "원경", "기타", "추가")))
check("external resource editor", 'QgsEditorWidgetSetup( QStringLiteral( "ExternalResource" )' in UTILS)
check("project-relative photo storage", 'mediaOptions.insert( QStringLiteral( "RelativeStorage" ), 1 )' in UTILS)
check("photo naming supplies its own category path", "'photos/' || @photo_type_safe || '/'" in UTILS)
check("no conflicting external-resource root", 'mediaOptions.insert( QStringLiteral( "DefaultRoot" )' not in UTILS)
check("photos included as attachment directory", 'attachmentDirectories << "photos"' in UTILS)
check("QField attachment naming", "QFieldSync/attachment_naming" in UTILS)
check(
    "point-based unique attachment name",
    all(token in UTILS for token in ('point_name_safe', 'photo_type_safe', '\\"sequence\\"', '\\"photo_id\\"', '.{extension}')),
)
check("relation id", "sungsan_field_photos" in UTILS)
check("relation parent", "setReferencedLayer( sungsanFieldObjectsLayer->id() )" in UTILS)
check("relation child", "setReferencingLayer( sungsanFieldPhotosLayer->id() )" in UTILS)
check("relation key", 'addFieldPair( QStringLiteral( "object_id" ), QStringLiteral( "object_id" ) )' in UTILS)
check("composition relation", "Qgis::RelationshipStrength::Composition" in UTILS)
check("relation shown in parent form", "현장 사진 · 근경/원경/기타/추가" in UTILS)

failed = [name for name, ok in CHECKS if not ok]
if failed:
    for name in failed:
        print(f"FAIL: {name}")
    print(f"{len(CHECKS) - len(failed)}/{len(CHECKS)} checks passed")
    sys.exit(1)

print(f"PASS: {len(CHECKS)}/{len(CHECKS)} Sungsan field-project checks")
