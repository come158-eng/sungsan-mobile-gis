#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Modified for Sungsan Mobile GIS by Sungsan on 2026-08-19.

set -euo pipefail
umask 077

SOURCE_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." >/dev/null && pwd)"
python3 "${SOURCE_DIR}/scripts/check-sungsan-build-inputs.py"
OUTPUT_DIR="${OUTPUT_DIR:-${SOURCE_DIR}/build-sungsan-apk}"
export triplet="${triplet:-arm64-android}"
SOURCE_BUILD_DIR="${SOURCE_DIR}/build-sungsan-native-${triplet}"
GENERATED_BUILD_DIR="${SOURCE_DIR}/build-sungsan-native-generated"

# A release source checkout must produce one traceable build. Reusing CMake,
# generated-plugin, or copied-APK directories can silently package a prior API
# key, package identity, native library, or APK. Never delete them here.
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
UNSIGNED_RELEASE_ONLY="${SUNG_SAN_BUILD_UNSIGNED_RELEASE_ONLY:-0}"
if [[ "${UNSIGNED_RELEASE_ONLY}" != "0" && "${UNSIGNED_RELEASE_ONLY}" != "1" ]]; then
  echo "오류: SUNG_SAN_BUILD_UNSIGNED_RELEASE_ONLY는 0 또는 1만 사용할 수 있습니다." >&2
  exit 18
fi
if [[ "${UNSIGNED_RELEASE_ONLY}" == "1" ]] && (( signing_variable_count != 0 )); then
  echo "오류: unsigned release 전용 빌드에는 어떠한 서명 비밀도 전달할 수 없습니다." >&2
  exit 19
fi
if [[ "${UNSIGNED_RELEASE_ONLY}" == "1" && "${ALLOW_UNSIGNED_TEST_BUILD}" != "0" ]]; then
  echo "오류: unsigned release 모드와 디버그키 시험 모드를 동시에 선택할 수 없습니다." >&2
  exit 20
fi
if [[ "${UNSIGNED_RELEASE_ONLY}" == "1" ]]; then
  BUILD_KIND="unsigned_release"
  export SUNG_SAN_RELEASE_BUILD="1"
elif (( signing_variable_count == 3 )); then
  BUILD_KIND="signed_release"
  export SUNG_SAN_RELEASE_BUILD="1"
elif [[ "${ALLOW_UNSIGNED_TEST_BUILD}" == "1" ]]; then
  BUILD_KIND="debug_test"
  export SUNG_SAN_RELEASE_BUILD="0"
else
  echo "오류: 배포 빌드는 외부 키스토어와 STOREPASS, KEYNAME, KEYPASS가 모두 필요합니다." >&2
  echo "내부 시험용 디버그키 빌드만 SUNG_SAN_ALLOW_UNSIGNED_TEST_BUILD=1로 명시할 수 있습니다." >&2
  exit 8
fi

KEYSTORE_HOST_PATH=""
if [[ "${BUILD_KIND}" != "debug_test" ]] &&
    [[ -e "${SOURCE_DIR}/keystore.p12" || -L "${SOURCE_DIR}/keystore.p12" ]]; then
  echo "오류: release 빌드 소스 루트에는 keystore.p12가 존재할 수 없습니다." >&2
  exit 12
fi
if [[ "${BUILD_KIND}" == "signed_release" ]]; then
  if [[ "${STOREPASS}" == *$'\n'* || "${STOREPASS}" == *$'\r'* ||
        "${KEYPASS}" == *$'\n'* || "${KEYPASS}" == *$'\r'* ||
        "${KEYNAME}" == *$'\n'* || "${KEYNAME}" == *$'\r'* ]]; then
    echo "오류: 서명 암호와 키 별칭에는 줄바꿈 문자를 사용할 수 없습니다." >&2
    exit 17
  fi
  if [[ -z "${SUNG_SAN_KEYSTORE_PATH:-}" ]]; then
    echo "오류: SUNG_SAN_KEYSTORE_PATH에 소스 폴더 밖의 배포 키스토어를 지정하세요." >&2
    exit 7
  fi
  if [[ "${SUNG_SAN_KEYSTORE_PATH}" == *:* ||
        "${SUNG_SAN_KEYSTORE_PATH}" == *$'\n'* ||
        "${SUNG_SAN_KEYSTORE_PATH}" == *$'\r'* ]]; then
    echo "오류: Docker 안전 마운트를 위해 키스토어 경로에 콜론·줄바꿈을 사용할 수 없습니다." >&2
    exit 10
  fi
  if [[ -L "${SUNG_SAN_KEYSTORE_PATH}" || ! -f "${SUNG_SAN_KEYSTORE_PATH}" ||
        ! -r "${SUNG_SAN_KEYSTORE_PATH}" ]]; then
    echo "오류: 배포 키스토어는 현재 사용자가 읽을 수 있는 일반 파일이어야 합니다." >&2
    exit 10
  fi
  KEYSTORE_HOST_PATH="$(realpath -e -- "${SUNG_SAN_KEYSTORE_PATH}")"
  case "${KEYSTORE_HOST_PATH}" in
    "${SOURCE_DIR}"|"${SOURCE_DIR}"/*)
      echo "오류: 배포 키스토어를 소스 폴더 내부에 둘 수 없습니다." >&2
      exit 11
      ;;
  esac
  KEYSTORE_MODE="$(stat -c '%a' "${KEYSTORE_HOST_PATH}")"
  if [[ ! "${KEYSTORE_MODE}" =~ ^[0-7]{3,4}$ ]] ||
      (( (8#${KEYSTORE_MODE} & 8#077) != 0 )); then
    echo "오류: 배포 키스토어는 그룹·기타 사용자가 읽을 수 없도록 chmod 600 또는 400으로 제한하세요." >&2
    exit 13
  fi
fi

# androiddeployqt must never see release-signing state for Sungsan. Qt 6.10.x
# turns its password environment variables back into `pass:<secret>` child
# process arguments. Only the isolated post-build signer receives secrets.
unset QT_ANDROID_KEYSTORE_PATH
unset QT_ANDROID_KEYSTORE_ALIAS
unset QT_ANDROID_KEYSTORE_STORE_PASS
unset QT_ANDROID_KEYSTORE_KEY_PASS

export QFIELD_CMAKE_BUILD_DIR="/usr/src/qfield/build-sungsan-native-${triplet}"
export CMAKE_BUILD_TYPE="Release"
export APP_NAME="Sungsan Mobile GIS"
export APP_PACKAGE_NAME="sungsanmobilegis"
export APP_PACKAGE_ID="kr.co.sungsan.mobilegis"
export APP_UPSTREAM_REVISION="f7123fc8dfa40be4e874d9bf5b46e81c6d05039b"
export APP_ICON="sungsan_mobile_gis"
export APP_ICON_PATH="/usr/src/qfield/branding/sungsan/assets"
# The dedicated PNG is retained as a reviewed branding asset, but the Android
# package uses the already-validated Sungsan vector drawable for its splash.
# This removes a redundant optional FILEPATH that previously failed only after
# the multi-hour vcpkg configure step despite the icon assets being available.
export APP_SPLASH_PATH=""
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
export APP_VERSION_STR="${APP_VERSION_STR:-1.0.0-sungsan-beta3}"
export APK_VERSION_CODE="${APK_VERSION_CODE:-10000003}"
export WITH_ALL_FILES_ACCESS="${WITH_ALL_FILES_ACCESS:-OFF}"
export SENTRY_DSN=""
export SENTRY_ENV=""
export SUNG_SAN_CONFIGURE_VWORLD="ON"
export SUNG_SAN_VWORLD_API_KEY

BUILD_MARKER="$(mktemp "${TMPDIR:-/tmp}/sungsan-build-marker.XXXXXX")"
SIGNING_STATE_DIR=""
HOST_USER_UID="$(id -u)"
HOST_USER_GID="$(id -g)"
cleanup() {
  rm -f -- "${BUILD_MARKER}"
  if [[ -n "${SIGNING_STATE_DIR}" ]]; then
    rm -f -- \
      "${SIGNING_STATE_DIR}/anchor/prebuild-certificate.sha256" \
      "${SIGNING_STATE_DIR}/secrets/storepass.txt" \
      "${SIGNING_STATE_DIR}/secrets/keypass.txt" \
      "${SIGNING_STATE_DIR}/bin/validate-sungsan-keystore.sh" \
      "${SIGNING_STATE_DIR}/bin/sign-sungsan-release-apk.sh"
    rmdir -- \
      "${SIGNING_STATE_DIR}/anchor" \
      "${SIGNING_STATE_DIR}/secrets" \
      "${SIGNING_STATE_DIR}/bin" \
      2>/dev/null || true
    rmdir -- "${SIGNING_STATE_DIR}" 2>/dev/null || true
  fi
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

if [[ "${BUILD_KIND}" == "signed_release" ]]; then
  # Build the tool image without release credentials in that process, then
  # anchor the certificate in a minimal read-only, network-disabled container.
  (
    unset STOREPASS KEYNAME KEYPASS SUNG_SAN_KEYSTORE_PATH
    DOCKER_BUILDKIT=1 docker build "${SOURCE_DIR}/.docker/android_dev" -t qfield_and_dev
  )
  export SUNG_SAN_DOCKER_IMAGE_READY="1"
  SIGNING_STATE_DIR="$(mktemp -d "${TMPDIR:-/tmp}/sungsan-signing-state.XXXXXX")"
  mkdir -m 0700 \
    "${SIGNING_STATE_DIR}/anchor" \
    "${SIGNING_STATE_DIR}/secrets" \
    "${SIGNING_STATE_DIR}/bin"
  cp -- \
    "${SOURCE_DIR}/scripts/validate-sungsan-keystore.sh" \
    "${SIGNING_STATE_DIR}/bin/validate-sungsan-keystore.sh"
  cp -- \
    "${SOURCE_DIR}/scripts/sign-sungsan-release-apk.sh" \
    "${SIGNING_STATE_DIR}/bin/sign-sungsan-release-apk.sh"
  chmod 0500 \
    "${SIGNING_STATE_DIR}/bin/validate-sungsan-keystore.sh" \
    "${SIGNING_STATE_DIR}/bin/sign-sungsan-release-apk.sh"
  printf '%s\n' "${STOREPASS}" >"${SIGNING_STATE_DIR}/secrets/storepass.txt"
  printf '%s\n' "${KEYPASS}" >"${SIGNING_STATE_DIR}/secrets/keypass.txt"
  chmod 0600 \
    "${SIGNING_STATE_DIR}/secrets/storepass.txt" \
    "${SIGNING_STATE_DIR}/secrets/keypass.txt"
  docker run --rm \
    --network none \
    --read-only \
    --user "${HOST_USER_UID}:${HOST_USER_GID}" \
    --cap-drop ALL \
    --security-opt no-new-privileges \
    --pids-limit 128 \
    --tmpfs "/tmp:rw,noexec,nosuid,nodev,size=64m,mode=700,uid=${HOST_USER_UID},gid=${HOST_USER_GID}" \
    -v "${KEYSTORE_HOST_PATH}:/run/secrets/sungsan-release-keystore.p12:ro" \
    -v "${SIGNING_STATE_DIR}/secrets/storepass.txt:/run/secrets/storepass.txt:ro" \
    -v "${SIGNING_STATE_DIR}/secrets/keypass.txt:/run/secrets/keypass.txt:ro" \
    -v "${SIGNING_STATE_DIR}/bin/validate-sungsan-keystore.sh:/run/bin/validate-sungsan-keystore.sh:ro" \
    -v "${SIGNING_STATE_DIR}/anchor:/run/anchor:rw" \
    -e KEYNAME \
    -e HOME=/tmp \
    -e SUNG_SAN_CERT_DIGEST_OUTPUT=/run/anchor/prebuild-certificate.sha256 \
    qfield_and_dev \
    /run/bin/validate-sungsan-keystore.sh
  if [[ ! -s "${SIGNING_STATE_DIR}/anchor/prebuild-certificate.sha256" ]]; then
    echo "오류: 빌드 전 인증서 SHA-256 기준값이 생성되지 않았습니다." >&2
    exit 14
  fi
fi

if [[ "${BUILD_KIND}" == "signed_release" ]]; then
  # The full native build and all dependency scripts receive no keystore path,
  # alias, or password. They only receive the non-secret release-mode flag.
  (
    unset STOREPASS KEYNAME KEYPASS SUNG_SAN_KEYSTORE_PATH
    "${SOURCE_DIR}/scripts/build.sh"
  )
else
  "${SOURCE_DIR}/scripts/build.sh"
fi

APK_SOURCE_DIR="${SOURCE_BUILD_DIR}/src/app/android-build/build/outputs/apk"
if [[ ! -d "${APK_SOURCE_DIR}" ]]; then
  echo "오류: APK 출력 폴더를 찾을 수 없습니다: ${APK_SOURCE_DIR}" >&2
  exit 3
fi

if [[ "${BUILD_KIND}" != "debug_test" ]]; then
  RELEASE_APK_DIR="${APK_SOURCE_DIR}/release"
  UNSIGNED_APK="${RELEASE_APK_DIR}/android-build-release-unsigned.apk"
  SIGNED_APK="${RELEASE_APK_DIR}/android-build-release-signed.apk"
  if [[ ! -f "${UNSIGNED_APK}" || -L "${UNSIGNED_APK}" || ! "${UNSIGNED_APK}" -nt "${BUILD_MARKER}" ]]; then
    echo "오류: 이번 빌드의 정확한 unsigned release APK를 찾을 수 없습니다." >&2
    exit 15
  fi

  if [[ "${BUILD_KIND}" == "signed_release" ]]; then
    docker run --rm \
      --network none \
      --read-only \
      --user "${HOST_USER_UID}:${HOST_USER_GID}" \
      --cap-drop ALL \
      --security-opt no-new-privileges \
      --pids-limit 128 \
      --tmpfs "/tmp:rw,noexec,nosuid,nodev,size=256m,mode=700,uid=${HOST_USER_UID},gid=${HOST_USER_GID}" \
      -v "${KEYSTORE_HOST_PATH}:/run/secrets/sungsan-release-keystore.p12:ro" \
      -v "${SIGNING_STATE_DIR}/secrets/storepass.txt:/run/secrets/storepass.txt:ro" \
      -v "${SIGNING_STATE_DIR}/secrets/keypass.txt:/run/secrets/keypass.txt:ro" \
      -v "${SIGNING_STATE_DIR}/anchor/prebuild-certificate.sha256:/run/anchor/prebuild-certificate.sha256:ro" \
      -v "${RELEASE_APK_DIR}:/run/apk:rw" \
      -v "${SIGNING_STATE_DIR}/bin/validate-sungsan-keystore.sh:/run/bin/validate-sungsan-keystore.sh:ro" \
      -v "${SIGNING_STATE_DIR}/bin/sign-sungsan-release-apk.sh:/run/bin/sign-sungsan-release-apk.sh:ro" \
      -e KEYNAME \
      -e HOME=/tmp \
      qfield_and_dev \
      /run/bin/sign-sungsan-release-apk.sh
    if [[ ! -f "${SIGNED_APK}" || -L "${SIGNED_APK}" || ! "${SIGNED_APK}" -nt "${BUILD_MARKER}" ]]; then
      echo "오류: 격리 서명 컨테이너가 검증된 release APK를 생성하지 못했습니다." >&2
      exit 16
    fi
    APK_FILES=("${SIGNED_APK}")
  else
    APK_FILES=("${UNSIGNED_APK}")
  fi
else
  mapfile -d '' APK_FILES < <(
    find "${APK_SOURCE_DIR}" -type f -name '*-debug.apk' \
      -newer "${BUILD_MARKER}" -print0
  )
fi
if (( ${#APK_FILES[@]} != 1 )); then
  echo "오류: 이번 실행에서 정확히 한 개의 조건 일치 APK가 필요합니다: ${APK_SOURCE_DIR}" >&2
  exit 4
fi

mkdir -p "${OUTPUT_DIR}"
if [[ "${BUILD_KIND}" == "signed_release" ]]; then
  RELEASE_OUTPUT_APK="${OUTPUT_DIR}/$(basename "${APK_FILES[0]}")"
  cp -f -- "${APK_FILES[0]}" "${RELEASE_OUTPUT_APK}"
  cp -f -- "${SIGNING_STATE_DIR}/anchor/prebuild-certificate.sha256" \
    "${OUTPUT_DIR}/Sungsan-release-certificate.sha256"
  chmod 0644 \
    "${RELEASE_OUTPUT_APK}" \
    "${OUTPUT_DIR}/Sungsan-release-certificate.sha256"
  echo "완료: 격리 서명·검증된 APK 1개를 ${OUTPUT_DIR}에 복사했습니다."
elif [[ "${BUILD_KIND}" == "unsigned_release" ]]; then
  UNSIGNED_OUTPUT_APK="${OUTPUT_DIR}/UNSIGNED-RELEASE-SIGNING-INPUT-ONLY-android-build-release-unsigned.apk"
  cp -f -- "${APK_FILES[0]}" \
    "${UNSIGNED_OUTPUT_APK}"
  chmod 0644 "${UNSIGNED_OUTPUT_APK}"
  echo "완료: 비밀 없는 unsigned release 서명입력 APK 1개를 ${OUTPUT_DIR}에 복사했습니다."
else
  DEBUG_OUTPUT_APK="${OUTPUT_DIR}/DEBUG-KEY-TEST-ONLY-$(basename "${APK_FILES[0]}")"
  cp -f -- "${APK_FILES[0]}" \
    "${DEBUG_OUTPUT_APK}"
  chmod 0644 "${DEBUG_OUTPUT_APK}"
  echo "경고: 배포 금지 디버그키 시험 APK 1개를 DEBUG-KEY-TEST-ONLY 이름으로 복사했습니다." >&2
fi

