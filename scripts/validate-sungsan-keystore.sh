#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Modified for Meta Engineering GIS by Sungsan on 2026-08-11.

# Run only in the isolated signing container. Passwords are read by keytool
# directly from read-only mode-0600 files and never enter container metadata,
# command arguments, or diagnostics.
set -euo pipefail
umask 077

if [[ -z "${KEYNAME:-}" ]]; then
  echo "오류: 성산 배포 서명 키 별칭이 지정되지 않았습니다." >&2
  exit 2
fi

KEYSTORE_FILE="${SUNG_SAN_KEYSTORE_FILE:-/run/secrets/metaengi-release-keystore.p12}"
STOREPASS_FILE="${SUNG_SAN_STOREPASS_FILE:-/run/secrets/storepass.txt}"
KEYPASS_FILE="${SUNG_SAN_KEYPASS_FILE:-/run/secrets/keypass.txt}"
if [[ ! -f "${KEYSTORE_FILE}" || -L "${KEYSTORE_FILE}" ]]; then
  echo "오류: 격리된 성산 배포 키스토어 파일을 찾을 수 없습니다." >&2
  exit 3
fi
for password_file in "${STOREPASS_FILE}" "${KEYPASS_FILE}"; do
  if [[ ! -f "${password_file}" || -L "${password_file}" ]]; then
    echo "오류: 격리된 성산 서명 암호 파일을 찾을 수 없습니다." >&2
    exit 3
  fi
done
if ! command -v keytool >/dev/null 2>&1; then
  echo "오류: 키스토어 사전 검증에 필요한 JDK keytool을 찾을 수 없습니다." >&2
  exit 4
fi

validation_directory="$(mktemp -d "${TMPDIR:-/tmp}/sungsan-keystore-check.XXXXXX")"
entry_information="${validation_directory}/entry-information.txt"
validation_keystore="${validation_directory}/validation.p12"
certificate_file="${validation_directory}/certificate.der"
cleanup() {
  rm -f -- "${entry_information}" "${validation_keystore}" "${certificate_file}"
  rmdir -- "${validation_directory}" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

if ! LC_ALL=C keytool -list -v \
    -keystore "${KEYSTORE_FILE}" \
    -storetype PKCS12 \
    -alias "${KEYNAME}" \
    -storepass:file "${STOREPASS_FILE}" \
    >"${entry_information}" 2>/dev/null; then
  echo "오류: 키스토어 암호가 틀렸거나 지정한 키 별칭을 찾을 수 없습니다." >&2
  exit 5
fi

if ! grep -Fq "Entry type: PrivateKeyEntry" "${entry_information}"; then
  echo "오류: 지정한 키 별칭이 APK 서명용 개인키 항목이 아닙니다." >&2
  exit 6
fi

# `keytool -list` does not validate a distinct private-key password. Import
# exactly one entry into a mode-0600 temporary PKCS12 store and delete it on
# every exit path. The source keystore is mounted read-only.
export SUNGSAN_KEYTOOL_VALIDATION_PASS="temporary-validation-password-not-a-release-secret"
if ! LC_ALL=C keytool -importkeystore \
    -srckeystore "${KEYSTORE_FILE}" \
    -srcstoretype PKCS12 \
    -srcstorepass:file "${STOREPASS_FILE}" \
    -srcalias "${KEYNAME}" \
    -srckeypass:file "${KEYPASS_FILE}" \
    -destkeystore "${validation_keystore}" \
    -deststoretype PKCS12 \
    -deststorepass:env SUNGSAN_KEYTOOL_VALIDATION_PASS \
    -destkeypass:env SUNGSAN_KEYTOOL_VALIDATION_PASS \
    -destalias __sungsan_validation__ \
    -noprompt \
    >/dev/null 2>&1; then
  echo "오류: 지정한 키 별칭의 개인키 암호가 틀렸거나 서명키를 읽을 수 없습니다." >&2
  exit 7
fi
unset SUNGSAN_KEYTOOL_VALIDATION_PASS

if [[ -n "${SUNG_SAN_CERT_DIGEST_OUTPUT:-}" ]]; then
  if [[ "${SUNG_SAN_CERT_DIGEST_OUTPUT}" != /* || -L "${SUNG_SAN_CERT_DIGEST_OUTPUT}" ]]; then
    echo "오류: 인증서 지문 출력은 심볼릭 링크가 아닌 절대경로여야 합니다." >&2
    exit 8
  fi
  if ! LC_ALL=C keytool -exportcert \
      -keystore "${KEYSTORE_FILE}" \
      -storetype PKCS12 \
      -alias "${KEYNAME}" \
      -storepass:file "${STOREPASS_FILE}" \
      -file "${certificate_file}" \
      >/dev/null 2>&1; then
    echo "오류: 서명 인증서 지문을 고정할 수 없습니다." >&2
    exit 9
  fi
  certificate_digest="$(sha256sum "${certificate_file}" | awk '{print $1}')"
  if [[ ! "${certificate_digest}" =~ ^[0-9a-f]{64}$ ]]; then
    echo "오류: 서명 인증서 SHA-256 형식이 올바르지 않습니다." >&2
    exit 10
  fi
  printf '%s\n' "${certificate_digest}" >"${SUNG_SAN_CERT_DIGEST_OUTPUT}"
  chmod 0600 "${SUNG_SAN_CERT_DIGEST_OUTPUT}"
fi

echo "성산 배포 서명 키스토어 사전 검증을 통과했습니다."
