#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Modified for Sungsan Mobile GIS by Sungsan on 2026-08-11.
"""Static gate for isolated Sungsan Android release signing.

Qt 6.10.x expands its signing environment passwords into child-process argv.
The independent Sungsan release must therefore build unsigned through Qt and
perform zipalign/apksigner in a separate, network-disabled container.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PASSES: list[str] = []
FAILURES: list[str] = []


def read(relative: str) -> str:
    try:
        return (ROOT / relative).read_text(encoding="utf-8")
    except (OSError, UnicodeError) as error:
        FAILURES.append(f"{relative}: UTF-8 read failed: {error}")
        return ""


def require(source: str, pattern: str, purpose: str) -> None:
    if re.search(pattern, source, re.MULTILINE | re.DOTALL):
        PASSES.append(purpose)
    else:
        FAILURES.append(f"{purpose}: missing /{pattern}/")


def forbid(source: str, pattern: str, purpose: str) -> None:
    if re.search(pattern, source, re.MULTILINE | re.DOTALL):
        FAILURES.append(f"{purpose}: forbidden /{pattern}/")
    else:
        PASSES.append(purpose)


def main() -> int:
    cpack = read("platform/android/CPackAndroidDeployQt.cmake.in")
    branch_start = cpack.find(
        'if("@APP_PACKAGE_ID@" STREQUAL "kr.co.sungsan.mobilegis")'
    )
    branch_end = cpack.find("# Keep the upstream QField packaging behavior unchanged.")
    if branch_start < 0 or branch_end <= branch_start:
        FAILURES.append("Sungsan-only CPack branch is not delimited")
        sungsan_branch = ""
    else:
        PASSES.append("Sungsan packaging is isolated by package ID")
        sungsan_branch = cpack[branch_start:branch_end]

    require(
        sungsan_branch,
        r'SUNG_SAN_RELEASE_BUILD\}" STREQUAL "1".*?ANDROIDDEPLOYQT_EXECUTABLE.*?'
        r"--gradle\s+--release",
        "release mode asks Qt for an unsigned release build",
    )
    require(
        sungsan_branch,
        r"android-build-release-unsigned\.apk.*?EXISTS.*?file\(SIZE",
        "Qt output is pinned to one non-empty unsigned release APK",
    )
    require(
        sungsan_branch,
        r'SUNG_SAN_RELEASE_BUILD\}" STREQUAL "0".*?android-build-debug\.apk.*?'
        r"apksigner\"\s+verify",
        "test mode verifies the Gradle debug-key APK",
    )
    forbid(
        sungsan_branch,
        r"--sign\b|--aab\b|--(?:storepass|keypass)\b|--ks-pass\b|"
        r"--key-pass\b|QT_ANDROID_KEYSTORE_|pass:\$ENV\{|COMMAND_ECHO\s+(?:STDOUT|STDERR)",
        "Qt's Sungsan branch receives no release signing material",
    )

    entrypoint = read("scripts/build-sungsan-android.sh")
    require(
        entrypoint,
        r"SUNG_SAN_KEYSTORE_PATH.*?realpath -e.*?"
        r'"\$\{SOURCE_DIR\}"\|"\$\{SOURCE_DIR\}"/\*.*?exit 11',
        "release keystore must resolve outside the source tree",
    )
    require(
        entrypoint,
        r"SUNG_SAN_BUILD_UNSIGNED_RELEASE_ONLY.*?signing_variable_count != 0.*?"
        r"BUILD_KIND=\"unsigned_release\".*?SUNG_SAN_RELEASE_BUILD=\"1\"",
        "CI unsigned-release mode rejects signing secrets and selects release output",
    )
    require(
        entrypoint,
        r"UNSIGNED-RELEASE-SIGNING-INPUT-ONLY-android-build-release-unsigned\.apk",
        "unsigned signing input has an unmistakable non-installable output name",
    )
    require(
        entrypoint,
        r"SOURCE_DIR\}/keystore\.p12.*?exit 12.*?KEYSTORE_MODE.*?8#077",
        "source-root or broadly readable keystores are rejected",
    )
    require(
        entrypoint,
        r"unset QT_ANDROID_KEYSTORE_PATH.*?unset QT_ANDROID_KEYSTORE_KEY_PASS",
        "Qt signing environment is removed before every Sungsan build",
    )
    require(
        entrypoint,
        r"unset STOREPASS KEYNAME KEYPASS SUNG_SAN_KEYSTORE_PATH.*?"
        r"scripts/build\.sh",
        "the full source build is launched without signing secrets",
    )
    require(
        entrypoint,
        r"prebuild-certificate\.sha256.*?sign-sungsan-release-apk\.sh",
        "pre-build certificate anchor is passed to the isolated signer",
    )
    require(
        entrypoint,
        r"cp --.*?validate-sungsan-keystore\.sh.*?"
        r"SIGNING_STATE_DIR\}/bin/validate-sungsan-keystore\.sh.*?"
        r"cp --.*?sign-sungsan-release-apk\.sh.*?"
        r"SIGNING_STATE_DIR\}/bin/sign-sungsan-release-apk\.sh.*?chmod 0500",
        "signing scripts are frozen outside the source tree before compilation",
    )
    require(
        entrypoint,
        r"android-build-release-unsigned\.apk.*?"
        r"android-build-release-signed\.apk",
        "post-build signing uses exact audited release filenames",
    )
    for option, description in (
        ("--network none", "signing containers have no network"),
        ("--read-only", "signing containers have read-only root filesystems"),
        ("--cap-drop ALL", "signing containers drop Linux capabilities"),
        ("--security-opt no-new-privileges", "signing containers block privilege gain"),
        ('--user "${HOST_USER_UID}:${HOST_USER_GID}"', "signing containers run as the host user"),
    ):
        if entrypoint.count(option) == 2:
            PASSES.append(description)
        else:
            FAILURES.append(f"{description}: expected twice ({option})")
    require(
        entrypoint,
        r"KEYSTORE_HOST_PATH\}:/run/secrets/sungsan-release-keystore\.p12:ro",
        "external keystore is mounted read-only into isolated containers",
    )
    require(
        entrypoint,
        r"storepass\.txt:/run/secrets/storepass\.txt:ro.*?"
        r"keypass\.txt:/run/secrets/keypass\.txt:ro",
        "passwords enter signing containers only through read-only files",
    )
    forbid(
        entrypoint,
        r"SOURCE_DIR\}:/run/(?:secrets|apk)|-e\s+(?:STOREPASS|KEYPASS)(?:\s|=)|"
        r"-e\s+KEYNAME=",
        "signing containers neither mount the whole source nor expose password env",
    )

    docker_build = read("scripts/build.sh")
    require(
        docker_build,
        r"SIGNING_ENV_ARGS=.*?APP_PACKAGE_ID.*?kr\.co\.sungsan\.mobilegis.*?"
        r"SIGNING_ENV_ARGS=\(\)",
        "generic build strips signing env forwarding for the Sungsan package",
    )
    require(
        docker_build,
        r"-e\s+SUNG_SAN_RELEASE_BUILD",
        "only the non-secret release-mode flag enters the full build",
    )
    forbid(
        docker_build,
        r"QT_ANDROID_KEYSTORE_",
        "generic Docker launcher no longer forwards Qt signing variables",
    )

    native_build = read("scripts/build-vcpkg.sh")
    require(
        native_build,
        r'APP_PACKAGE_ID\}" == "kr\.co\.sungsan\.mobilegis".*?'
        r"for forbidden_signing_variable in.*?STOREPASS.*?KEYPASS.*?"
        r"QT_ANDROID_KEYSTORE_KEY_PASS.*?exit 11",
        "full Sungsan container fails if any signing secret enters it",
    )
    require(
        native_build,
        r"SUNG_SAN_RELEASE_BUILD.*?!= \"0\".*?!= \"1\".*?exit 10",
        "full build accepts only explicit non-secret release modes",
    )

    validator = read("scripts/validate-sungsan-keystore.sh")
    require(
        validator,
        r"keytool -list -v.*?-storepass:file \"\$\{STOREPASS_FILE\}\"",
        "keytool validates store password and alias via file input",
    )
    require(validator, r"Entry type: PrivateKeyEntry", "alias must contain a private key")
    require(
        validator,
        r"keytool -importkeystore.*?-srcstorepass:file \"\$\{STOREPASS_FILE\}\".*?"
        r"-srckeypass:file \"\$\{KEYPASS_FILE\}\"",
        "keytool validates distinct private-key password via file input",
    )
    require(
        validator,
        r"keytool -exportcert.*?-storepass:file \"\$\{STOREPASS_FILE\}\".*?sha256sum.*?"
        r"SUNG_SAN_CERT_DIGEST_OUTPUT",
        "pre-build certificate SHA-256 anchor is exported",
    )
    forbid(
        validator,
        r"pass:\$\{|(?:storepass|keypass)\s+\"?\$\{(?:STOREPASS|KEYPASS)\}",
        "validator never puts passwords in argv",
    )

    signer = read("scripts/sign-sungsan-release-apk.sh")
    require(
        signer,
        r"ZIPALIGN.*?-c -P 16 4.*?ZIPALIGN.*?-P 16 -f 4.*?"
        r"APKSIGNER.*?sign",
        "APK is alignment-checked or aligned before signing",
    )
    require(
        signer,
        r"--ks-pass \"file:\$\{STOREPASS_FILE\}\".*?"
        r"--key-pass \"file:\$\{KEYPASS_FILE\}\".*?"
        r"--out \"\$\{SIGNED_APK\}\"",
        "apksigner reads passwords via files and writes an explicit output",
    )
    require(
        signer,
        r"APKSIGNER.*?verify --verbose --print-certs.*?"
        r"signed_certificate_digests.*?anchored_digest.*?Signer #\[2-9\]",
        "signed APK has exactly one signer matching the pre-build certificate anchor",
    )
    require(
        signer,
        r"ZIPALIGN.*?-c -P 16 4 \"\$\{SIGNED_APK\}\"",
        "final signed APK alignment is reverified",
    )
    forbid(
        signer,
        r"pass:\$\{|--(?:ks|key)-pass\s+pass:|"
        r"--(?:storepass|keypass)\s+\"?\$\{(?:STOREPASS|KEYPASS)\}|"
        r"--(?:ks|key)-pass\s+env:",
        "signer never puts passwords in argv",
    )

    if FAILURES:
        print(f"FAIL: {len(FAILURES)} issue(s); {len(PASSES)} checks passed")
        for failure in FAILURES:
            print(f" - {failure}")
        print("NOTE: static checks do not replace an isolated build or APK verification.")
        return 1

    print(f"OK: {len(PASSES)} isolated Sungsan Android signing checks passed")
    print("NOTE: static checks do not replace an isolated build or APK verification.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
