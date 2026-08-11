#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract, merge, and validate Sungsan's Korean Qt translations.

The script keeps translation work in independent JSON chunks so several
reviewers can work without editing qfield_ko.ts at the same time.
"""

from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import sys

from lxml import etree


PLACEHOLDER_RE = re.compile(r"%(?:L?\d+|n)|\{[A-Za-z_][A-Za-z0-9_]*\}")
# Restrict this to markup QField actually uses. Strings such as ``<unknown>``
# are visible unit placeholders, not HTML, and may be translated.
TAG_RE = re.compile(r"</?(a|b|br|i|p|span|strong)(?:\s[^>]*)?>", re.IGNORECASE)
HANGUL_RE = re.compile(r"[가-힣]")
SKIP_TYPES = {"vanished", "obsolete"}
NONLOCALIZED_SOURCES = {
    "NULL",
    "X",
    "Y",
    "TCP (NMEA)",
    "UDP (NMEA)",
    "Egeniouss",
    "QFieldCloud",
    "PDOP",
    "HDOP",
    "VDOP",
    "PPS",
    "DGPS",
    "+ IMU",
    "%1°",
    "NTRIP",
    "NTRIP SSL/TLS",
    "B",
    "KB",
    "MB",
    "GB",
    "TB",
}


def element_text(element: etree._Element | None) -> str:
    return "" if element is None else "".join(element.itertext())


def key_for(context: str, source: str, comment: str, numerus: bool) -> str:
    return json.dumps([context, source, comment, numerus], ensure_ascii=False)


def needs_translation(source: str, translation: str, translation_type: str) -> bool:
    if translation_type == "unfinished" or not translation.strip():
        return True
    normalized_source = source.strip()
    return (
        normalized_source == translation.strip()
        and bool(re.search(r"[A-Za-z]", source))
        and normalized_source not in NONLOCALIZED_SOURCES
    )


def iter_active_messages(root: etree._Element):
    for context_element in root.findall("context"):
        context = context_element.findtext("name") or ""
        for message in context_element.findall("message"):
            translation = message.find("translation")
            if translation is None or translation.get("type", "") in SKIP_TYPES:
                continue
            source = element_text(message.find("source"))
            comment = message.findtext("comment") or ""
            numerus = message.get("numerus") == "yes"
            yield context, source, comment, numerus, message, translation


def command_extract(ts_path: Path, output_dir: Path, chunks: int) -> int:
    root = etree.parse(str(ts_path)).getroot()
    rows: list[dict[str, object]] = []
    for context, source, comment, numerus, _message, translation in iter_active_messages(root):
        current = element_text(translation).strip()
        if not needs_translation(source, current, translation.get("type", "")):
            continue
        rows.append(
            {
                "key": key_for(context, source, comment, numerus),
                "context": context,
                "source": source,
                "comment": comment,
                "numerus": numerus,
                "current_translation": current,
                "new_translation": "",
            }
        )

    output_dir.mkdir(parents=True, exist_ok=True)
    for old in output_dir.glob("chunk_*.json"):
        old.unlink()
    for index in range(chunks):
        start = len(rows) * index // chunks
        end = len(rows) * (index + 1) // chunks
        path = output_dir / f"chunk_{index + 1:02d}.json"
        path.write_text(
            json.dumps(rows[start:end], ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        print(f"{path}: {end - start} messages")
    print(f"total: {len(rows)} messages")
    return 0


def translation_checks(source: str, translated: str) -> list[str]:
    errors: list[str] = []
    if not translated.strip():
        errors.append("empty translation")
    if Counter(PLACEHOLDER_RE.findall(source)) != Counter(PLACEHOLDER_RE.findall(translated)):
        errors.append("placeholder mismatch")
    if Counter(TAG_RE.findall(source)) != Counter(TAG_RE.findall(translated)):
        errors.append("HTML tag mismatch")
    if source.count("\n") != translated.count("\n"):
        errors.append("newline mismatch")
    return errors


def load_chunks(input_dir: Path) -> tuple[dict[str, str], list[str]]:
    translations: dict[str, str] = {}
    errors: list[str] = []
    paths = sorted(input_dir.glob("chunk_*.json"))
    if not paths:
        return {}, [f"no chunk_*.json files in {input_dir}"]
    for path in paths:
        rows = json.loads(path.read_text(encoding="utf-8"))
        for offset, row in enumerate(rows, start=1):
            key = str(row.get("key", ""))
            expected_key = key_for(
                str(row.get("context", "")),
                str(row.get("source", "")),
                str(row.get("comment", "")),
                bool(row.get("numerus", False)),
            )
            source = str(row.get("source", ""))
            translated = str(row.get("new_translation", ""))
            if key != expected_key:
                errors.append(f"{path.name}:{offset}: source metadata or key was changed")
            for issue in translation_checks(source, translated):
                errors.append(f"{path.name}:{offset}: {issue}: {source!r}")
            if key in translations:
                errors.append(f"{path.name}:{offset}: duplicate key")
            translations[key] = translated
    return translations, errors


def command_merge(ts_path: Path, input_dir: Path, output_path: Path) -> int:
    translations, errors = load_chunks(input_dir)
    if errors:
        print("\n".join(errors), file=sys.stderr)
        return 2

    parser = etree.XMLParser(remove_blank_text=False)
    document = etree.parse(str(ts_path), parser)
    updated = 0
    missing: list[str] = []
    for context, source, comment, numerus, _message, translation in iter_active_messages(
        document.getroot()
    ):
        key = key_for(context, source, comment, numerus)
        if key not in translations:
            continue
        translated = translations[key]
        if numerus:
            forms = translation.findall("numerusform")
            if not forms:
                forms = [etree.SubElement(translation, "numerusform")]
            forms[0].text = translated
            for extra in forms[1:]:
                translation.remove(extra)
        else:
            for child in list(translation):
                translation.remove(child)
            translation.text = translated
        translation.attrib.pop("type", None)
        updated += 1

    expected = len(translations)
    if updated != expected:
        present = {
            key_for(c, s, m, n)
            for c, s, m, n, _message, _translation in iter_active_messages(document.getroot())
        }
        missing = [key for key in translations if key not in present]
        print(
            f"refusing partial merge: updated {updated} of {expected}; missing={len(missing)}",
            file=sys.stderr,
        )
        return 3

    document.write(
        str(output_path),
        encoding="UTF-8",
        xml_declaration=True,
        doctype="<!DOCTYPE TS>",
        pretty_print=False,
    )
    print(f"updated {updated} messages in {output_path}")
    return 0


def active_message_keys(root: etree._Element) -> list[str]:
    return [
        key_for(context, source, comment, numerus)
        for context, source, comment, numerus, _message, _translation in iter_active_messages(root)
    ]


def command_check(ts_path: Path, reference_path: Path) -> int:
    document = etree.parse(str(ts_path))
    unfinished = 0
    empty = 0
    same_english = 0
    placeholder_errors = 0
    active = 0
    keys = active_message_keys(document.getroot())
    duplicate_keys = len(keys) - len(set(keys))
    for _context, source, _comment, _numerus, _message, translation in iter_active_messages(
        document.getroot()
    ):
        active += 1
        translated = element_text(translation).strip()
        unfinished += translation.get("type") == "unfinished"
        empty += not translated
        same_english += (
            source.strip() == translated
            and bool(re.search(r"[A-Za-z]", source))
            and source.strip() not in NONLOCALIZED_SOURCES
        )
        placeholder_errors += bool(translation_checks(source, translated))

    reference_document = etree.parse(str(reference_path))
    reference_keys = set(active_message_keys(reference_document.getroot()))
    missing_reference_messages = len(reference_keys - set(keys))
    print(
        json.dumps(
            {
                "active": active,
                "reference_active": len(reference_keys),
                "missing_reference_messages": missing_reference_messages,
                "duplicate_keys": duplicate_keys,
                "unfinished": unfinished,
                "empty": empty,
                "same_english": same_english,
                "placeholder_or_tag_errors": placeholder_errors,
            },
            ensure_ascii=False,
        )
    )
    return 1 if (
        unfinished
        or empty
        or same_english
        or placeholder_errors
        or missing_reference_messages
        or duplicate_keys
    ) else 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("command", choices=("extract", "merge", "check"))
    parser.add_argument("--ts", type=Path, default=Path("i18n/qfield_ko.ts"))
    parser.add_argument("--reference", type=Path, default=Path("i18n/qfield_en.ts"))
    parser.add_argument("--work-dir", type=Path, default=Path("branding/sungsan/i18n_work"))
    parser.add_argument("--output", type=Path)
    parser.add_argument("--chunks", type=int, default=6)
    args = parser.parse_args()
    if args.command == "extract":
        return command_extract(args.ts, args.work_dir, args.chunks)
    if args.command == "merge":
        return command_merge(args.ts, args.work_dir, args.output or args.ts)
    return command_check(args.ts, args.reference)


if __name__ == "__main__":
    raise SystemExit(main())
