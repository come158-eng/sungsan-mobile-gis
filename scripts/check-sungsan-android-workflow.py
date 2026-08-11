#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Modified for Sungsan Mobile GIS by Sungsan on 2026-08-11.
"""Dependency-free security gate for the manual two-job Android workflow.

The release build job must never receive Sungsan release credentials.  Only a
second, protected-environment job may materialize them, and that job may not
check out or execute repository source.  This is a static gate; it does not run
GitHub Actions, compile QField or sign an APK.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = ROOT / ".github/workflows/sungsan-android.yml"
DISABLED_UPSTREAM = ROOT / ".github/workflows/android.yml.disabled"
ACTIVE_UPSTREAM = ROOT / ".github/workflows/android.yml"
README_KO = ROOT / "branding/sungsan/README.ko.md"
MODIFICATIONS = ROOT / "MODIFICATIONS.md"

CHECKOUT_SHA = "d23441a48e516b6c34aea4fa41551a30e30af803"  # v6.1.0
UPLOAD_SHA = "043fb46d1a93c77aae656e7c1c64a875d1fc6a0a"  # v7.0.1
DOWNLOAD_SHA = "3e5f45b2cfb9172054b4087a40e8e0b5a5461e7c"  # v8.0.1


def shell_run_blocks(source: str) -> list[str]:
    """Extract literal ``run: |`` blocks without importing a YAML package."""

    lines = source.splitlines(keepends=True)
    blocks: list[str] = []
    index = 0
    while index < len(lines):
        match = re.match(r"^(?P<indent> +)run:\s*\|\s*$", lines[index].rstrip("\r\n"))
        if not match:
            index += 1
            continue
        header_indent = len(match.group("indent"))
        content_indent = header_indent + 2
        block: list[str] = []
        index += 1
        while index < len(lines):
            line = lines[index]
            if line.strip():
                actual_indent = len(line) - len(line.lstrip(" "))
                if actual_indent <= header_indent:
                    break
                if actual_indent < content_indent:
                    raise ValueError("run block has inconsistent indentation")
                block.append(line[content_indent:])
            else:
                block.append("\n")
            index += 1
        blocks.append("".join(block))
    return blocks


def section(source: str, start: str, end: str | None) -> str:
    start_index = source.find(start)
    if start_index < 0:
        return ""
    end_index = source.find(end, start_index + len(start)) if end else len(source)
    if end_index < 0:
        return ""
    return source[start_index:end_index]


def main() -> int:
    source = WORKFLOW.read_text(encoding="utf-8")
    failures: list[str] = []
    passes: list[str] = []

    def require(condition: bool, description: str) -> None:
        (passes if condition else failures).append(description)

    require("\t" not in source, "workflow contains no YAML tab indentation")
    require(not source.startswith("\ufeff"), "workflow is UTF-8 without a BOM")
    step_matches = list(re.finditer(r"(?m)^      - name:\s*(\S.*)$", source))
    step_names = [match.group(1) for match in step_matches]
    require(bool(step_names), "workflow contains explicitly named steps")
    require(
        len(step_names) == len(set(step_names)),
        "every workflow step name is unique",
    )
    complete_steps = True
    for index, match in enumerate(step_matches):
        end = step_matches[index + 1].start() if index + 1 < len(step_matches) else len(source)
        step_body = source[match.start():end]
        execution_keys = re.findall(r"(?m)^        (?:uses|run):", step_body)
        complete_steps = complete_steps and len(execution_keys) == 1
    require(
        complete_steps,
        "every named step has exactly one uses or run execution key",
    )
    require(
        step_names.count("Validate exact input inventory before secret access") == 1,
        "the pre-secret input validation step exists exactly once",
    )
    try:
        run_blocks = shell_run_blocks(source)
    except ValueError:
        run_blocks = []
    require(len(run_blocks) == 12, "all 12 literal shell blocks are structurally found")
    shell_syntax_ok = bool(run_blocks)
    for block in run_blocks:
        result = subprocess.run(
            ["bash", "-n"],
            input=block,
            text=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        )
        shell_syntax_ok = shell_syntax_ok and result.returncode == 0
    require(shell_syntax_ok, "all workflow shell blocks pass bash syntax validation")

    trigger_match = re.search(
        r"(?m)^on:\s*\r?\n"
        r"(?P<body>(?:(?:^[ \t]+[^\r\n]*|^[ \t]*)\r?\n)*)",
        source,
    )
    trigger_body = trigger_match.group("body") if trigger_match else ""
    trigger_keys = re.findall(r"(?m)^  ([A-Za-z_][A-Za-z0-9_-]*):", trigger_body)
    require(trigger_keys == ["workflow_dispatch"], "workflow_dispatch is the only event trigger")
    require(source.count("workflow_dispatch:") == 1, "manual trigger is declared exactly once")
    require("default: test_only" in trigger_body, "debug-key test build is the default")
    require("- test_only" in trigger_body and "- signed_release" in trigger_body, "build modes are explicit")
    require(
        not re.search(
            r"(?m)^  (push|pull_request|pull_request_target|release|schedule|"
            r"repository_dispatch|workflow_call|workflow_run):",
            trigger_body,
        ),
        "no fork-PR, automatic, release, callable or chained trigger exists",
    )

    require("permissions: {}" in source, "global GITHUB_TOKEN permissions default to none")
    require(source.count("  build_arm64:") == 1, "one arm64 build job exists")
    require(source.count("  sign_release:") == 1, "one isolated release-signing job exists")
    build = section(source, "  build_arm64:\n", "  sign_release:\n")
    signer = section(source, "  sign_release:\n", None)
    require(bool(build) and bool(signer), "two job bodies are cleanly delimited")
    require("permissions:\n      contents: read" in build, "build job has read-only source permission")
    require("permissions:\n      actions: read" in signer, "sign job has artifact-read permission only")
    require("needs: build_arm64" in signer, "sign job depends on the completed build job")

    require(
        f"actions/checkout@{CHECKOUT_SHA}" in build and source.count("actions/checkout@") == 1,
        "checkout is pinned once to official v6.1.0 full SHA",
    )
    require("actions/checkout v6.1.0" in source, "checkout pin has an auditable version comment")
    require(
        source.count(f"actions/upload-artifact@{UPLOAD_SHA}") == 3,
        "all three uploads use official v7.0.1 full SHA",
    )
    require("actions/upload-artifact v7.0.1" in source, "upload pins have version comments")
    require(
        f"actions/download-artifact@{DOWNLOAD_SHA}" in signer
        and source.count("actions/download-artifact@") == 1,
        "download uses official v8.0.1 full SHA",
    )
    require("actions/download-artifact v8.0.1" in signer, "download pin has a version comment")
    all_uses = re.findall(r"(?m)^\s*uses:\s*([^\s#]+)", source)
    require(
        bool(all_uses)
        and all(re.fullmatch(r"[^@\s]+@[0-9a-f]{40}", action) for action in all_uses),
        "every third-party action is immutable full-SHA pinned",
    )

    ref_gate_index = source.find("Restrict secret-bearing builds to the default branch")
    first_secret_index = source.find("secrets.")
    require(
        ref_gate_index >= 0 and first_secret_index > ref_gate_index,
        "default-branch gate precedes every secret reference",
    )
    require(
        '[[ "${GITHUB_REF}" != "${expected_ref}" ]]' in build,
        "tags, forks and non-default branches fail before secret-bearing steps",
    )
    require("persist-credentials: false" in build, "checkout credentials are not persisted")

    release_secret_names = (
        "SUNG_SAN_RELEASE_KEYSTORE_BASE64",
        "SUNG_SAN_RELEASE_STOREPASS",
        "SUNG_SAN_RELEASE_KEYNAME",
        "SUNG_SAN_RELEASE_KEYPASS",
    )
    for secret_name in release_secret_names:
        require(
            source.count(f"secrets.{secret_name}") == 1,
            f"protected sign job consumes {secret_name} exactly once",
        )
        require(secret_name not in build, f"build job never references {secret_name}")
        require(secret_name in signer, f"{secret_name} is confined to the sign job")
    require(
        not re.search(r"secrets\.(?:STOREPASS|KEYPASS|KEYNAME|PLAYSTORE_SIGNINGKEY)", source),
        "no legacy or upstream release-secret name remains",
    )
    require("environment:\n      name: sungsan-release" in signer, "sign job uses protected sungsan-release environment")
    require(
        "required reviewers" in signer and "disallow" in signer and "administrator bypass" in signer,
        "workflow warns that reviewer protection requires repository settings",
    )
    require("actions/checkout@" not in signer, "sign job performs no source checkout")
    require("${{ github.workspace }}" not in signer, "sign job does not use a source workspace")
    require("docker " not in signer, "sign job uses trusted official-runner tools, not an unpinned container")
    require("runs-on: ubuntu-24.04" in signer, "sign job pins the official runner image label")

    require("SUNG_SAN_BUILD_UNSIGNED_RELEASE_ONLY: \"1\"" in build, "build job requests unsigned release only")
    require("UNSIGNED-RELEASE-SIGNING-INPUT-ONLY" in build, "unsigned artifact is unmistakably named")
    for forbidden in ("STOREPASS", "KEYPASS", "KEYNAME", "KEYSTORE_BASE64", "keystore.p12"):
        require(forbidden not in build, f"build job contains no release material marker {forbidden}")
    require("DEBUG-KEY-TEST-ONLY" in build, "debug-key test artifact remains separately named")
    require("artifact-id" in build and "unsigned_release_artifact_id" in source, "immutable upload ID crosses jobs")
    require(
        "artifact-ids: ${{ needs.build_arm64.outputs.unsigned_release_artifact_id }}" in signer,
        "sign job downloads by immutable artifact ID",
    )
    require("merge-multiple: true" in signer, "artifact contents extract directly into the validated directory")
    require("digest-mismatch: error" in signer, "artifact service digest mismatch is fatal")

    require(source.count("${#inventory[@]} != 3") == 3, "build, sign-input and signed-output inventories require three entries")
    require(source.count('-L "${inventory_path}"') >= 3, "all three inventories reject symlinks")
    require(source.count("BUILD-INFO.txt SHA256SUMS.txt") >= 3, "all artifacts pin APK plus checksum plus build record")
    require("wc -l < SHA256SUMS.txt" in signer, "sign input accepts exactly one checksum record")
    require("artifact_kind=unsigned_release_signing_input_only" in signer, "build metadata is matched exactly")

    secret_index = signer.find("secrets.SUNG_SAN_RELEASE_")
    input_validation_index = signer.find("Validate exact input inventory before secret access")
    require(
        input_validation_index >= 0 and secret_index > input_validation_index,
        "artifact inventory and metadata are checked before secret injection",
    )
    require("${{ runner.temp }}/sungsan-release-secrets" in signer, "all release secrets live under RUNNER_TEMP")
    require("umask 077" in signer and "chmod 0600" in signer, "secret files are mode restricted")
    require("prebuild-certificate.sha256" in signer, "certificate digest is fixed before signing")
    require("keytool -importkeystore" in signer and "keytool -exportcert" in signer, "private key and certificate are prevalidated")
    require("-storepass:file" in signer and "-srckeypass:file" in signer, "keytool reads passwords from files")
    require("--ks-pass \"file:${SECRET_DIR}/storepass.txt\"" in signer, "apksigner reads store password from file")
    require("--key-pass \"file:${SECRET_DIR}/keypass.txt\"" in signer, "apksigner reads key password from file")
    require("if: ${{ always() }}" in signer and "Remove protected signing material" in signer, "secret cleanup is always attempted")
    require("rm -f --" in signer and "rmdir -- \"${SECRET_DIR}\"" in signer, "secret files and directory are explicitly removed")

    align_index = signer.find('"${zipalign}" -P 16 -f 4')
    align_check_index = signer.find('"${zipalign}" -c -P 16 -v 4')
    sign_index = signer.find('"${apksigner}" sign')
    require(
        0 <= align_index < align_check_index < sign_index,
        "zipalign -P 16 and alignment check both precede signing",
    )
    require("apksigner verify --verbose --print-certs" not in signer or '"${apksigner}" verify --verbose --print-certs' in signer, "signed APK uses verbose certificate verification")
    require("${#signer_digests[@]} != 1" in signer, "exactly one signer certificate is required")
    require("${signer_digests[0]}" in signer and "${anchored_digest}" in signer, "final signer matches the pre-sign certificate anchor")
    require("Verified using v2 scheme (APK Signature Scheme v2): true" in signer, "APK v2 signature is mandatory")
    require("package: name='kr.co.sungsan.mobilegis'" in build and "package: name='kr.co.sungsan.mobilegis'" in signer, "package ID and versions are checked on both sides")
    require(source.count("native-code: 'arm64-v8a'") >= 2, "arm64 ABI is checked before and after trust boundary")

    forbidden_deployments = {
        "Sentry": r"(?i)sentry_(?:dsn|auth|org|project)",
        "Play Store": r"(?i)(playstore|google[ _-]?play|basic_upload_apks)",
        "S3": r"(?i)(s3cmd|aws-actions|amazonaws|qfieldapks)",
        "GitHub release": r"(?i)(upload-release-assets|gh release|action-gh-release)",
    }
    for description, pattern in forbidden_deployments.items():
        require(not re.search(pattern, source), f"active workflow has no {description} deployment wiring")
    require("set -x" not in source and "set -eux" not in source, "shell tracing is disabled")
    require(
        not re.search(
            r"(?i)echo[^\n]*\$\{(?:KEYSTORE_BASE64|STOREPASS_VALUE|KEYPASS_VALUE|KEYNAME_VALUE)",
            source,
        ),
        "release secret values are never echoed",
    )

    require(not ACTIVE_UPSTREAM.exists(), "upstream Android workflow is not active")
    disabled_source = DISABLED_UPSTREAM.read_text(encoding="utf-8") if DISABLED_UPSTREAM.is_file() else ""
    require(bool(disabled_source), "upstream Android workflow is retained only as .disabled reference")
    require(
        disabled_source.startswith("# Modified for Sungsan Mobile GIS by Sungsan on 2026-08-11."),
        "disabled upstream workflow carries the Sungsan change notice",
    )
    require("DISABLED UPSTREAM REFERENCE ONLY" in disabled_source, "disabled upstream workflow explains its status")

    readme_source = README_KO.read_text(encoding="utf-8") if README_KO.is_file() else ""
    require(bool(readme_source), "Korean build and release instructions are present")
    require(
        ".github/workflows/android.yml.disabled" in readme_source
        and "workflow_dispatch" in readme_source,
        "Korean instructions explain manual-only CI and upstream workflow disablement",
    )
    require(
        "소스\nZIP의 `.github/workflows/`에는 `sungsan-android.yml`만 포함" in readme_source
        and "upstream 워크플로는 모두 제외" in readme_source,
        "Korean instructions distinguish the worktree from distributable ZIP workflows",
    )
    require(
        "sungsan-release" in readme_source
        and "Required reviewers" in readme_source
        and "관리자 우회" in readme_source,
        "Korean instructions require protected-environment reviewers and no administrator bypass",
    )
    for secret_name in release_secret_names:
        require(
            secret_name in readme_source,
            f"Korean instructions name protected environment secret {secret_name}",
        )
    require(
        "YAML 파일로 강제할 수 없습니다" in readme_source,
        "Korean instructions state the reviewer gate cannot be enforced in YAML",
    )

    modifications_source = MODIFICATIONS.read_text(encoding="utf-8") if MODIFICATIONS.is_file() else ""
    require(bool(modifications_source), "Sungsan modification record is present")
    require(
        ".github/workflows/sungsan-android.yml" in modifications_source
        and ".github/workflows/android.yml.disabled" in modifications_source,
        "modification record covers the isolated and disabled Android workflows",
    )
    require(
        "distributable source\n  ZIP includes only `.github/workflows/sungsan-android.yml`" in modifications_source
        and "every other upstream workflow" in modifications_source,
        "modification record states the distributable ZIP workflow allowlist",
    )
    require(
        "no-checkout" in modifications_source
        and "immutable artifact ID" in modifications_source
        and "three-file inventory" in modifications_source,
        "modification record captures the signing trust-boundary controls",
    )

    if failures:
        print(f"FAIL: {len(failures)} issue(s); {len(passes)} workflow checks passed")
        for failure in failures:
            print(f" - {failure}")
        return 1

    print(f"OK: {len(passes)} isolated Sungsan Android workflow checks passed")
    print("NOTE: static checks do not replace an Actions run, approval or device test.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
