#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Modified for Sungsan Mobile GIS by Sungsan on 2026-08-11.

set -euo pipefail

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." >/dev/null && pwd)"
OUTPUT_DIR="${OUTPUT_DIR:-${SOURCE_DIR}/build-sungsan-apk}"
export triplet="${triplet:-arm64-android}"
SOURCE_BUILD_DIR="${SOURCE_DIR}/build-sungsan-native-${triplet}"
GENERATED_BUILD_DIR="${SOURCE_DIR}/build-sungsan-native-generated"

# A release source checkout must produce one traceable build. Reusing CMake,
# generated-plugin, or copied-APK directories can silently package a prior API
# key, package identity, native library, or APK. Never delete them here: stop
# and let the operator archive or remove the exact directories deliberately.
for existing_path in \
  "${SOURCE_BUILD_DIR}" \
  "${GENERATED_BUILD_DIR}" \
  "${OUTPUT_DIR}"; do
  if [[ -e "${existing_path}" || -L "${existing_path}" ]]; then
    echo "오류: 이전 성산 빌드 경로가 남아 있습니다: ${existing_path}" >&2
    echo "새 소스 사본에서 빌드하거나, 위 전용 경로를 직접 보관/정리한 뒤 다시 실행하세요." >&2
    exit 5
  fi
done

if [[ -z "${SUNG_SAN_VWORLD_API_KEY:-}" ]]; then
  echo "오류: SUNG_SAN_VWORLD_API_KEY 환경 변수에 VWorld API 키를 지정하세요." >&2
  exit 2
fi

signing_variable_count=0
for signing_variable in STOREPASS KEYNAME KEYPASS; do
  if [[ -n "${!signing_variable:-}" ]]; then
    ((signing_variable_count += 1))
  fi
done
if (( signing_variable_count != 0 && signing_variable_count != 3 )); then
  echo "오류: 배포 서명은 STOREPASS, KEYNAME, KEYPASS 세 값을 모두 지정해야 합니다." >&2
  exit 6
fi
ALLOW_UNSIGNED_TEST_BUILD="${SUNG_SAN_ALLOW_UNSIGNED_TEST_BUILD:-0}"
if [[ "${ALLOW_UNSIGNED_TEST_BUILD}" != "0" && "${ALLOW_UNSIGNED_TEST_BUILD}" != "1" ]]; then
  echo "오류: SUNG_SAN_ALLOW_UNSIGNED_TEST_BUILD는 0 또는 1만 사용할 수 있습니다." >&2
  exit 9
fi
if (( signing_variable_count == 0 )) && [[ "${ALLOW_UNSIGNED_TEST_BUILD}" != "1" ]]; then
  echo "오류: 배포 빌드는 STOREPASS, KEYNAME, KEYPASS와 keystore.p12가 모두 필요합니다." >&2
  echo "내부 시험용 미서명 빌드만 SUNG_SAN_ALLOW_UNSIGNED_TEST_BUILD=1로 명시할 수 있습니다." >&2
  exit 8
fi
if (( signing_variable_count == 3 )) && [[ ! -f "${SOURCE_DIR}/keystore.p12" ]]; then
  echo "오류: 배포 서명용 keystore.p12가 소스 루트에 없습니다." >&2
  exit 7
fi

export QFIELD_CMAKE_BUILD_DIR="/usr/src/qfield/build-sungsan-native-${triplet}"
export CMAKE_BUILD_TYPE="Release"
export APP_NAME="Sungsan Mobile GIS"
export APP_PACKAGE_NAME="sungsanmobilegis"
export APP_PACKAGE_ID="kr.co.sungsan.mobilegis"
# Audited QField v4.2.11 base commit used by the About open-source link. Keep
# this fixed even when building the corresponding-source ZIP without .git.
export APP_UPSTREAM_REVISION="f7123fc8dfa40be4e874d9bf5b46e81c6d05039b"
export APP_ICON="sungsan_mobile_gis"
export APP_ICON_PATH="/usr/src/qfield/branding/sungsan/assets"
export APP_SPLASH_PATH="branding/sungsan/assets/sungsan_splash.png"
export APP_THEME_PATH="/usr/src/qfield/branding/sungsan/theme.json"
export APP_DEFAULT_LANGUAGE="ko"
export APP_URL_SCHEME="sungsanmobilegis"
export APP_DATA_DIR_NAME="SungsanMobileGIS"
export APP_POSITIONING_CHANNEL_NAME="Sungsan Mobile GIS"
export APP_POSITIONING_CHANNEL_DESCRIPTION="성산 위치 서비스"
export APP_POSITIONING_NOTIFICATION_TITLE="성산 위치 서비스"
export APP_POSITIONING_NOTIFICATION_RUNNING_TEXT="성산 위치 서비스가 실행 중입니다"
export APP_CLOUD_CHANNEL_NAME="Sungsan Mobile GIS"
export APP_CLOUD_CHANNEL_DESCRIPTION="성산 데이터 전송 서비스"
export APP_CLOUD_NOTIFICATION_TITLE="성산 데이터 전송"
export APP_CLOUD_NOTIFICATION_RUNNING_TEXT="현장 첨부 파일을 전송하고 있습니다"
export APP_BUNDLED_PLUGINS="/usr/src/qfield/build-sungsan-native-generated/plugins"
export WITH_SAMPLE_PROJECTS="OFF"
export APP_VERSION="${APP_VERSION:-v4.2.11}"
export APP_VERSION_STR="${APP_VERSION_STR:-1.0.0-sungsan-beta2}"
# Keep the code above beta1 (10,000,001) and the earlier 4,021,102 internal
# builds so Android accepts this safety-hardened Sungsan app as an upgrade.
export APK_VERSION_CODE="${APK_VERSION_CODE:-10000002}"
export WITH_ALL_FILES_ACCESS="${WITH_ALL_FILES_ACCESS:-OFF}"
export SENTRY_DSN=""
export SENTRY_ENV=""
export SUNG_SAN_CONFIGURE_VWORLD="ON"
export SUNG_SAN_VWORLD_API_KEY

BUILD_MARKER="$(mktemp)"
trap 'rm -f "${BUILD_MARKER}"' EXIT
"${SOURCE_DIR}/scripts/build.sh"

mkdir -p "${OUTPUT_DIR}"
APK_SOURCE_DIR="${SOURCE_DIR}/build-sungsan-native-${triplet}/src/app/android-build/build/outputs/apk"
if [[ ! -d "${APK_SOURCE_DIR}" ]]; then
  echo "오류: APK 출력 폴더를 찾을 수 없습니다: ${APK_SOURCE_DIR}" >&2
  exit 3
fi

if (( signing_variable_count == 3 )); then
  mapfile -d '' APK_FILES < <(
    find "${APK_SOURCE_DIR}" -type f -name '*-signed.apk' \
      -newer "${BUILD_MARKER}" -print0
  )
else
  mapfile -d '' APK_FILES < <(
    find "${APK_SOURCE_DIR}" -type f -name '*.apk' \
      -newer "${BUILD_MARKER}" -print0
  )
fi
if (( ${#APK_FILES[@]} == 0 )); then
  echo "오류: 이번 실행에서 새로 생성된 조건 일치 APK가 없습니다: ${APK_SOURCE_DIR}" >&2
  exit 4
fi

for apk_file in "${APK_FILES[@]}"; do
  if (( signing_variable_count == 3 )); then
    cp -f "${apk_file}" "${OUTPUT_DIR}/"
  else
    cp -f "${apk_file}" \
      "${OUTPUT_DIR}/UNSIGNED-TEST-ONLY-$(basename "${apk_file}")"
  fi
done

if (( signing_variable_count == 3 )); then
  echo "완료: 서명된 APK ${#APK_FILES[@]}개를 ${OUTPUT_DIR}에 복사했습니다."
else
  echo "경고: 배포 금지 미서명 시험 APK ${#APK_FILES[@]}개를 UNSIGNED-TEST-ONLY 이름으로 복사했습니다." >&2
fi
