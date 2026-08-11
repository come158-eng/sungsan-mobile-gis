#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Static gate for transactional Android project ZIP imports.

Modified for Sungsan Mobile GIS by Sungsan on 2026-08-11.
The Android/Qt SDK is not present in the release workspace, so this gate checks
the safety-critical source structure without claiming an APK build or device
test.
"""

from pathlib import Path
import sys
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
ACTIVITY = ROOT / "platform/android/src/ch/opengis/qfield/QFieldActivity.java"
UTILS = ROOT / "platform/android/src/ch/opengis/qfield/QFieldUtils.java"
DEFAULT_STRINGS = ROOT / "platform/android/res/values/strings.xml"
KOREAN_STRINGS = ROOT / "platform/android/res/values-ko/strings.xml"
SUNGSAN_STRINGS = (
    ROOT / "branding/sungsan/android/res/values/strings.xml"
)


def method_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start < 0:
        raise AssertionError(f"method not found: {signature}")
    brace = source.find("{", start)
    if brace < 0:
        raise AssertionError(f"method body not found: {signature}")
    depth = 0
    for index in range(brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[brace + 1 : index]
    raise AssertionError(f"unterminated method: {signature}")


def strings(path: Path) -> dict[str, str]:
    root = ET.parse(path).getroot()
    return {
        item.attrib["name"]: "".join(item.itertext()).strip()
        for item in root.findall("string")
    }


def main() -> int:
    source = ACTIVITY.read_text(encoding="utf-8")
    utils_source = UTILS.read_text(encoding="utf-8")
    import_archive = method_body(
        source, "void importProjectArchive(Uri archiveUri)"
    )
    update_archive = method_body(
        source, "void updateProjectFromArchive(Uri archiveUri)"
    )
    shared_archive = method_body(source, "private void processProjectIntent()")
    replace = method_body(
        source,
        "private boolean replaceDirectoryFromStaging(File staging, File target)",
    )
    install_new = method_body(
        source,
        "private boolean installNewDirectoryFromStaging(File staging, File target)",
    )
    unzip = method_body(
        utils_source,
        "public static boolean zipToFolder(InputStream in, String folder)",
    )
    inspect_archive = method_body(
        utils_source,
        "public static String getArchiveProjectName(InputStream in)",
    )

    checks = {
        "2026-08-11 change notice":
            "Modified for Sungsan Mobile GIS by Sungsan on 2026-08-11." in source,
        "new imports extract into a sibling staging folder":
            'createSiblingStagingDirectory(\n                            targetDirectory, "importing")'
            in import_archive,
        "ZIP extraction result controls import success":
            "imported = input != null &&" in import_archive and
            "QFieldUtils.zipToFolder" in import_archive,
        "extracted QGIS project file is required":
            "new File(stagingDirectory, projectName).isFile()"
            in import_archive,
        "new imports never overwrite an existing folder":
            "while (targetDirectory.exists())" in import_archive and
            "installNewDirectoryFromStaging(\n                                    stagingDirectory, targetDirectory)"
            in import_archive and
            "target.exists()" in install_new,
        "failed staging data is cleaned":
            "finally" in import_archive and
            "deletePath(stagingDirectory)" in import_archive,
        "only successful import opens the imported QGIS project":
            "if (importSucceeded)" in import_archive and
            import_archive.find("if (importSucceeded)") <
            import_archive.find("openProject(importedProjectPath)"),
        "failed import shows native error":
            "import_project_archive_error" in import_archive,
        "import completion is marshalled to UI thread":
            import_archive.find("runOnUiThread") <
            import_archive.find("progressDialog.dismiss()"),
        "existing folder is backed up before staged rename":
            replace.find("target.renameTo(backup)") <
            replace.find("staging.renameTo(target)"),
        "failed staged rename restores old folder":
            "backup.renameTo(target)" in replace,
        "archive update starts from a staged copy":
            "copyDirectoryTree(projectFolder," in update_archive,
        "archive update requires acknowledged project close":
            "if (!clearProject())" in update_archive and
            update_archive.find("if (!clearProject())") <
            update_archive.find("copyDirectoryTree(projectFolder,"),
        "archive update checks unzip result and project file":
            "QFieldUtils.zipToFolder" in update_archive and
            "archivedProjectName).isFile()" in update_archive,
        "archive update is committed transactionally":
            "replaceDirectoryFromStaging(\n                                stagingDirectory, projectFolder)"
            in update_archive,
        "archive update reopens the validated archive project":
            "new File(projectFolder, archivedProjectName)" in update_archive and
            "openProject(reopenedProjectPath)" in update_archive,
        "renamed archive project removes the staged old QGS/QGZ":
            "stagedOriginalProject.getCanonicalFile()" in update_archive and
            "stagedArchivedProject" in update_archive and
            "stagedOriginalProject.delete()" in update_archive,
        "failed archive update is reported":
            "if (!updateSucceeded && !isFinishing())" in update_archive and
            "import_project_archive_error" in update_archive,
        "shared ZIP launch checks extraction result":
            "importSucceeded = imported" in shared_archive and
            "QFieldUtils.zipToFolder" in shared_archive,
        "shared ZIP launch requires extracted project file":
            "new File(projectPath, projectName).isFile()" in shared_archive,
        "failed shared ZIP is removed and reported":
            "deletePath(importedFolder)" in shared_archive and
            "import_project_archive_error" in shared_archive,
        "shared ZIP opens only after successful extraction":
            "if (importSucceeded)" in shared_archive and
            shared_archive.find("if (importSucceeded)") <
            shared_archive.find("openProject(importedProjectPath)"),
        "ZIP extraction uses a canonical destination root":
            "new File(folder).getCanonicalFile()" in unzip,
        "ZIP boundary includes a path separator":
            "destinationRoot.getPath() + File.separator" in unzip,
        "every ZIP destination is canonicalized":
            "new File(destinationRoot, entryName).getCanonicalFile()"
            in unzip,
        "path traversal fails extraction":
            "!destination.getPath().startsWith(destinationPrefix)" in unzip and
            "throw new SecurityException" in unzip,
        "unsafe string-concatenated ZIP destination is gone":
            "new File(folder + entry.getName())" not in unzip,
        "archive entry count has a safety limit":
            "MAX_PROJECT_ARCHIVE_ENTRIES" in unzip and
            "entryCount > MAX_PROJECT_ARCHIVE_ENTRIES" in unzip,
        "archive expansion bytes have a safety limit":
            "MAX_PROJECT_ARCHIVE_UNCOMPRESSED_BYTES" in unzip and
            "uncompressedBytes >" in unzip,
        "ambiguous archives with multiple QGS/QGZ are rejected":
            "if (!projectName.isEmpty())" in inspect_archive and
            'return ""' in inspect_archive,
    }

    default = strings(DEFAULT_STRINGS)
    korean = strings(KOREAN_STRINGS)
    sungsan = strings(SUNGSAN_STRINGS)
    checks["upstream fallback explains safe failure"] = (
        "No incomplete project was opened"
        in default.get("import_project_archive_error", "")
    )
    checks["Korean safe-failure guidance is present"] = (
        "불완전한 프로젝트는 열지 않았으며"
        in korean.get("import_project_archive_error", "")
    )
    checks["Sungsan default catalog carries Korean safe-failure guidance"] = (
        sungsan.get("import_project_archive_error") ==
        korean.get("import_project_archive_error")
    )

    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(f"[{'PASS' if passed else 'FAIL'}] {name}")
    if failed:
        print(f"\n{len(failed)} regression check(s) failed.", file=sys.stderr)
        return 1
    print(f"\nAll {len(checks)} archive-import regression checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
