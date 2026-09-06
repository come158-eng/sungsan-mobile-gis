#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Static regression gate for safe Android project ZIP sharing.

Modified for Meta Engineering GIS by Sungsan on 2026-08-11.
This check is intentionally dependency-free because the release workspace does
not contain the Android SDK/Qt toolchain needed for an APK build.
"""

from pathlib import Path
import re
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
    export = method_body(
        source, "private void sendCompressedFolderTo(String path)"
    )
    validate = method_body(
        source, "private boolean isNonEmptyReadableZip(File archive)"
    )
    add_folder = method_body(
        utils_source,
        "private static boolean addFolderToZip(ZipOutputStream zip, String folder,",
    )
    copy_stream = method_body(
        utils_source,
        "public static boolean inputStreamToOutputStream(InputStream in,",
    )

    checks = {
        "2026-08-11 change notice":
            "Modified for Meta Engineering GIS by Sungsan on 2026-08-11." in source,
        "compression result is used": bool(re.search(
            r"archiveReady\s*=.*?QFieldUtils\.folderToZip", export, re.S
        )),
        "stale archive is removed before compression":
            export.find("temporaryFile.delete()") <
            export.find("QFieldUtils.folderToZip"),
        "created archive is validated":
            "isNonEmptyReadableZip(temporaryFile)" in export,
        "failed archive is deleted": bool(re.search(
            r"if\s*\(\s*!archiveReady.*?temporaryFile\.delete\(\)",
            export,
            re.S,
        )),
        "empty ZIP is rejected": "regularFileCount > 0" in validate,
        "corrupt ZIP is rejected":
            "new ZipFile(archive)" in validate and
            "catch (IOException e)" in validate,
        "every ZIP entry is streamed for validation":
            "zipFile.getInputStream(entry)" in validate and
            "while ((size = input.read(buffer)) != -1)" in validate,
        "ZIP entry CRC and length are verified":
            "CRC32" in validate and "crc.getValue() != entry.getCrc()" in validate and
            "uncompressedBytes != entry.getSize()" in validate,
        "exactly one non-empty QGIS project file is required":
            'entryName.endsWith(".qgs")' in validate and
            'entryName.endsWith(".qgz")' in validate and
            "entry.getSize() <= 0" in validate and
            "projectFileCount == 1" in validate,
        "project file inputs use deterministic close":
            "try (InputStream input = new FileInputStream(file))" in add_folder,
        "failed project file copy aborts ZIP creation":
            "!inputStreamToOutputStream" in add_folder and
            "return false" in add_folder,
        "ZIP creation rejects symlinks and root escapes":
            "getCanonicalFile()" in add_folder and
            "startsWith(rootPrefix)" in add_folder and
            "canonicalFile.equals(file.getAbsoluteFile())" in add_folder,
        "stream copy counts actual read bytes":
            "bytesRead += size" in copy_stream and
            "bytesRead == totalBytes" in copy_stream and
            "bufferRead += bufferSize" not in copy_stream,
        "share UI runs on Android UI thread":
            export.find("runOnUiThread") < export.find("startActivity"),
        "read permission is granted":
            "Intent.FLAG_GRANT_READ_URI_PERMISSION" in export,
        "ZIP MIME is explicit": 'intent.setType("application/zip")' in export,
        "failed ZIP reports a native error before sharing":
            export.find("if (!canShareArchive)") < export.find("startActivity") and
            "export_project_zip_error" in export,
    }

    default = strings(DEFAULT_STRINGS)
    korean = strings(KOREAN_STRINGS)
    sungsan = strings(SUNGSAN_STRINGS)
    checks["upstream-safe default error string"] = bool(
        default.get("export_project_zip_error")
    )
    checks["Sungsan Korean error string"] = (
        "공유하지 않았습니다" in korean.get("export_project_zip_error", "")
    )
    checks["Sungsan default catalog carries Korean native error"] = (
        sungsan.get("export_project_zip_error") ==
        korean.get("export_project_zip_error")
    )

    failed = [name for name, passed in checks.items() if not passed]
    for name, passed in checks.items():
        print(f"[{'PASS' if passed else 'FAIL'}] {name}")
    if failed:
        print(f"\n{len(failed)} regression check(s) failed.", file=sys.stderr)
        return 1
    print(f"\nAll {len(checks)} compressed-export regression checks passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
