#!/usr/bin/env python3
"""Guard the features ported from Sungsan release 8186000, without its brand."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
def read(path):
    return (root / path).read_text(encoding="utf-8")

panel = read("src/qml/sungsan/SungsanFieldPanel.qml")
app = read("src/qml/qgismobileapp.qml")
activity = read("platform/android/src/ch/opengis/qfield/QFieldActivity.java")
photos = read("src/qml/editorwidgets/ExternalResource.qml")
utils = read("src/core/utils/fileutils.cpp")
bridge = read("src/core/sungsansurveybridge.cpp")
checks = {
    "foldable dock with persistent GPS/save status": all(s in panel for s in (
        "property bool dockCollapsed: false", "조작 펼치기", "패널 내리기",
        "dockFlickable.contentY = 0", "visible: !root.dockCollapsed && root.moreExpanded")),
    "geometry capture restores controls": "if (geometryInProgress)\n      dockCollapsed = false;" in panel,
    "north-up button and live rotation": "signal northUpRequested" in panel and "rotation: root.mapRotation" in panel,
    "north-up stops heading/compass rotation, not GPS": all(s in app for s in (
        "mapRotation: mapCanvas.mapSettings.rotation", "onNorthUpRequested:",
        "positioningSettings.positionFollowMode = PositioningSettings.FollowMode.PositionOnly;",
        "mapCanvas.mapSettings.rotation = 0;")),
    "Meta project photo flag": "kr.co.metaengi.mobilegis/saveFieldPhotosToGallery" in bridge and "kr.co.metaengi.mobilegis/saveFieldPhotosToGallery" in photos,
    "Meta package gates public photos": 'SUNGSAN_PACKAGE_ID = "kr.co.metaengi.mobilegis"' in activity and "!SUNGSAN_PACKAGE_ID.equals(getPackageName())" in activity,
    "Meta-only scoped and legacy albums": '"/메타이엔지 GIS/"' in activity and 'new File(pictures, "메타이엔지 GIS")' in activity,
    "no Sungsan public album or temp filenames": "성산 GIS" not in activity and '".sungsan_' not in activity,
    "gallery copy respects owner package": "MediaStore.Images.Media.OWNER_PACKAGE_NAME" in activity,
    "board precedes gallery copy": photos.index("FileUtils.addImageNameBoard(finalPhotoPath, finalPhotoName)") < photos.index("platformUtilities.publishImageToGallery(finalPhotoPath, finalPhotoName)"),
    "atomic board with image writer and orientation": all(s in utils for s in (
        "#include <QImageWriter>", "reader.setAutoTransform( true )", "QSaveFile saveFile( imagePath )", "Exif.Image.Orientation", "result.fill( Qt::white )")),
    "retired SDK tools package not requested": '"platform-tools" "tools"' not in read(".docker/android_dev/Dockerfile"),
}
for name, passed in checks.items():
    print(("PASS: " if passed else "FAIL: ") + name)
raise SystemExit(0 if all(checks.values()) else 1)
