#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Modified for Sungsan Mobile GIS by Sungsan on 2026-08-11.

# Sign exactly one audited unsigned-release output. This script is mounted
# read-only into a network-disabled container together with only the release
# APK directory, the read-only keystore, and the pre-build certificate anchor.
set -euo pipefail
umask 077

BUILD_TOOLS_DIR="${SUNG_SAN_BUILD_TOOLS_DIR:-/android-sdk/build-tools/35.0.1}"
APK_DIRECTORY="${SUNG_SAN_APK_DIRECTORY:-/run/apk}"
UNSIGNED_APK="${APK_DIRECTORY}/android-build-release-unsigned.apk"
SIGNED_APK="${APK_DIRECTORY}/android-build-release-signed.apk"
KEYSTORE_FILE="${SUNG_SAN_KEYSTORE_FILE:-/run/secrets/sungsan-release-keystore.p12}"
STOREPASS_FILE="${SUNG_SAN_STOREPASS_FILE:-/run/secrets/storepass.txt}"
KEYPASS_FILE="${SUNG_SAN_KEYPASS_FILE:-/run/secrets/keypass.txt}"
CERTIFICATE_ANCHOR="${SUNG_SAN_CERTIFICATE_ANCHOR:-/run/anchor/prebuild-certificate.sha256}"
VALIDATOR="${SUNG_SAN_KEYSTORE_VALIDATOR:-/run/bin/validate-sungsan-keystore.sh}"
ZIPALIGN="${BUILD_TOOLS_DIR}/zipalign"
APKSIGNER="${BUILD_TOOLS_DIR}/apksigner"

if [[ -z "${KEYNAME:-}" ]]; then
  echo "오류: 성산 APK 서명 키 별칭이 지정되지 않았습니다." >&2
  exit 2
fi
for required_file in \
  "${UNSIGNED_APK}" \
  "${KEYSTORE_FILE}" \
  "${STOREPASS_FILE}" \
  "${KEYPASS_FILE}" \
  "${CERTIFICATE_ANCHOR}" \
  "${VALIDATOR}" \
  "${ZIPALIGN}" \
  "${APKSIGNER}"; do
  if [[ ! -f "${required_file}" || -L "${required_file}" ]]; then
    echo "오류: 격리 서명 입력 파일이 없거나 심볼릭 링크입니다." >&2
    exit 3
  fi
done
if [[ -e "${SIGNED_APK}" || -L "${SIGNED_APK}" ]]; then
  echo "오류: 이전 성산 서명 APK가 남아 있어 덮어쓰지 않습니다." >&2
  exit 4
fi

unsigned_size="$(stat -c '%s' "${UNSIGNED_APK}")"
if [[ ! "${unsigned_size}" =~ ^[0-9]+$ ]] || (( unsigned_size < 1 )); then
  echo "오류: 성산 unsigned release APK가 비어 있습니다." >&2
  exit 5
fi

anchored_digest="$(tr -d '[:space:]' <"${CERTIFICATE_ANCHOR}" | tr '[:upper:]' '[:lower:]')"
if [[ ! "${anchored_digest}" =~ ^[0-9a-f]{64}$ ]]; then
  echo "오류: 빌드 전 인증서 SHA-256 기준값이 올바르지 않습니다." >&2
  exit 6
fi

current_digest_file="$(mktemp "${TMPDIR:-/tmp}/sungsan-current-certificate.XXXXXX")"
aligned_apk="$(mktemp "${TMPDIR:-/tmp}/sungsan-aligned.XXXXXX.apk")"
verification_report="$(mktemp "${TMPDIR:-/tmp}/sungsan-apksigner-report.XXXXXX")"
signed_output_valid=0
cleanup() {
  rm -f -- "${current_digest_file}" "${aligned_apk}" "${verification_report}"
  if (( signed_output_valid == 0 )); then
    rm -f -- "${SIGNED_APK}"
  fi
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

export SUNG_SAN_KEYSTORE_FILE="${KEYSTORE_FILE}"
export SUNG_SAN_CERT_DIGEST_OUTPUT="${current_digest_file}"
"${VALIDATOR}" >/dev/null
current_digest="$(tr -d '[:space:]' <"${current_digest_file}" | tr '[:upper:]' '[:lower:]')"
if [[ "${current_digest}" != "${anchored_digest}" ]]; then
  echo "오류: 빌드 도중 서명 키스토어 인증서가 변경되었습니다." >&2
  exit 7
fi

# Android requires zipalign before apksigner. AGP normally aligns its output,
# but the explicit check prevents that implementation detail from becoming a
# release assumption. A non-aligned input is aligned into the private tmpfs.
signing_input="${UNSIGNED_APK}"
if ! "${ZIPALIGN}" -c -P 16 4 "${UNSIGNED_APK}" >/dev/null 2>&1; then
  "${ZIPALIGN}" -P 16 -f 4 "${UNSIGNED_APK}" "${aligned_apk}" >/dev/null
  "${ZIPALIGN}" -c -P 16 4 "${aligned_apk}" >/dev/null
  signing_input="${aligned_apk}"
fi

"${APKSIGNER}" sign \
  --v2-signing-enabled true \
  --ks "${KEYSTORE_FILE}" \
  --ks-key-alias "${KEYNAME}" \
  --ks-pass "file:${STOREPASS_FILE}" \
  --key-pass "file:${KEYPASS_FILE}" \
  --out "${SIGNED_APK}" \
  "${signing_input}"

"${APKSIGNER}" verify --verbose --print-certs "${SIGNED_APK}" \
  >"${verification_report}"
"${ZIPALIGN}" -c -P 16 4 "${SIGNED_APK}" >/dev/null

mapfile -t signed_certificate_digests < <(
  sed -n 's/^Signer #1 certificate SHA-256 digest: //p' "${verification_report}" |
    tr '[:upper:]' '[:lower:]'
)
if (( ${#signed_certificate_digests[@]} != 1 )) ||
    [[ ! "${signed_certificate_digests[0]}" =~ ^[0-9a-f]{64}$ ]] ||
    [[ "${signed_certificate_digests[0]}" != "${anchored_digest}" ]] ||
    grep -Eq '^Signer #[2-9][0-9]* certificate ' "${verification_report}"; then
  rm -f -- "${SIGNED_APK}"
  echo "오류: 완성 APK의 서명 인증서가 빌드 전 기준값과 일치하지 않습니다." >&2
  exit 8
fi
if ! grep -Fq 'Verified using v2 scheme (APK Signature Scheme v2): true' \
    "${verification_report}"; then
  rm -f -- "${SIGNED_APK}"
  echo "오류: 완성 APK의 v2 서명을 확인할 수 없습니다." >&2
  exit 9
fi

chmod 0644 "${SIGNED_APK}"
signed_output_valid=1
echo "성산 release APK의 정렬·서명·인증서 검증을 통과했습니다."
