#!/usr/bin/env bash
# Modified for Sungsan Mobile GIS by Sungsan on 2026-08-11.

set -e

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null && pwd)"/..

export APK_VERSION_CODE=${APK_VERSION_CODE:-1}
export APP_VERSION_STR=${APP_VERSION_STR:-dev}

triplet=${triplet:-arm64-android}

if [[ ${triplet} == arm-android ]]; then
	install_qt_arch="android_armv7"
elif [[ ${triplet} == arm-neon-android ]]; then
	install_qt_arch="android_armv7"
elif [[ ${triplet} == arm64-android ]]; then
	install_qt_arch="android_arm64_v8a"
elif [[ ${triplet} == x86-android ]]; then
	install_qt_arch="android_x86"
elif [[ ${triplet} == x64-android ]]; then
	install_qt_arch="android_x86_64"
else
	install_qt_arch="android_arm64_v8a"
fi

if [[ "${SUNG_SAN_DOCKER_IMAGE_READY:-0}" != "1" ]]; then
	DOCKER_BUILDKIT=1 docker build "${SRC_DIR}/.docker/android_dev" -t qfield_and_dev
fi

DOCKER_TTY_ARGS=()
if [[ -t 0 && -t 1 ]]; then
	DOCKER_TTY_ARGS=(-it)
fi

SIGNING_ENV_ARGS=(-e STOREPASS -e KEYNAME -e KEYPASS)
if [[ "${APP_PACKAGE_ID:-}" == "kr.co.sungsan.mobilegis" ]]; then
	# The full Sungsan build container must never receive release credentials.
	# Dedicated network-disabled containers perform preflight and final signing.
	SIGNING_ENV_ARGS=()
fi

docker run "${DOCKER_TTY_ARGS[@]}" --rm \
	-v "$SRC_DIR":/usr/src/qfield:Z \
	$(if [ -n "$CACHE_DIR" ]; then echo "-v $CACHE_DIR:/io/.cache:Z"; fi) \
	-e triplet=${triplet} \
	-e install_qt_version=${install_qt_version} \
	-e install_qt_arch=${install_qt_arch} \
	"${SIGNING_ENV_ARGS[@]}" \
	-e APP_PACKAGE_NAME \
	-e APP_PACKAGE_ID \
	-e APP_UPSTREAM_REVISION \
	-e APP_NAME \
	-e APP_ICON \
	-e APP_ICON_PATH \
	-e APP_SPLASH_PATH \
	-e APP_THEME_PATH \
	-e APP_DEFAULT_LANGUAGE \
	-e APP_URL_SCHEME \
	-e APP_DATA_DIR_NAME \
	-e APP_POSITIONING_CHANNEL_NAME \
	-e APP_POSITIONING_CHANNEL_DESCRIPTION \
	-e APP_POSITIONING_NOTIFICATION_TITLE \
	-e APP_POSITIONING_NOTIFICATION_RUNNING_TEXT \
	-e APP_CLOUD_CHANNEL_NAME \
	-e APP_CLOUD_CHANNEL_DESCRIPTION \
	-e APP_CLOUD_NOTIFICATION_TITLE \
	-e APP_CLOUD_NOTIFICATION_RUNNING_TEXT \
	-e APP_BUNDLED_PLUGINS \
	-e QFIELD_CMAKE_BUILD_DIR \
	-e CMAKE_BUILD_TYPE \
	-e WITH_ALL_FILES_ACCESS \
	-e WITH_SAMPLE_PROJECTS \
	-e SENTRY_DSN \
	-e SENTRY_ENV \
	-e SUNG_SAN_CONFIGURE_VWORLD \
	-e SUNG_SAN_VWORLD_API_KEY \
	-e SUNG_SAN_RELEASE_BUILD \
	-e APP_VERSION \
	-e APP_VERSION_STR \
	-e APK_VERSION_CODE \
	-e NUGET_TOKEN \
	-e NUGET_USERNAME \
	-e USER_GID=$(stat -c "%g" .) \
	-e USER_UID=$(stat -c "%u" .) \
	-e VCPKG_BINARY_SOURCES=clear\;files,/io/.cache,readwrite \
	qfield_and_dev \
	/usr/src/qfield/scripts/build-vcpkg.sh
