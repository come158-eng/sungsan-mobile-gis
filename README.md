<!-- Modified for Sungsan Mobile GIS by Sungsan on 2026-08-11. -->

# Sungsan Mobile GIS 1.0.0 beta2 소스

이 포크는 QGIS 프로젝트 호환 엔진으로 QField v4.2.11 오픈소스를
사용하지만, 일반 사용자가 보는 홈·현장 작업 화면은 **성산 전용**으로
다시 구성합니다. Android 패키지는 `kr.co.sungsan.mobilegis`이며
공식 QField 설치본을 덮어쓰지 않습니다.

- 성산 홈: 현장 패키지 가져오기, 기기 프로젝트, 최근 현장
- 성산 현장 패널: GPS, 레이어, 조사 시작, 수동·5분 자동 저장, ZIP 내보내기
- 성산 브랜드: 로고, 테마, Android 알림, 한국어 기본 문구
- VWorld 위성영상: 빌드 시 키 주입, 프로젝트의 맨 아래에 자동 추가

빌드 순서와 제품 기준은
[`branding/sungsan/README.ko.md`](branding/sungsan/README.ko.md)와
[`branding/sungsan/PRODUCT_REQUIREMENTS.ko.md`](branding/sungsan/PRODUCT_REQUIREMENTS.ko.md)를
보세요. 이 압축 파일은 APK가 아니며, Android/Qt 전체 빌드와
실제 휴대폰 시험 전에는 현장용 완성판으로 취급하면 안 됩니다.

---

## 기반 오픈소스: QField for QGIS

[![Read the Docs](https://img.shields.io/badge/Read-the%20Docs-green.svg)](https://docs.qfield.org/)
[![Community Platform](https://img.shields.io/discourse/topics?server=https://community.qfield.org)](https://community.qfield.org)
[![Sponsor](https://img.shields.io/static/v1?label=Support&message=%E2%9D%A4)](https://github.com/sponsors/opengisch)
[![Contribute](https://img.shields.io/static/v1?label=Contribute&message=💪)](#contribute)
[![Release](https://img.shields.io/github/release/opengisch/QField.svg?label=Release)](https://github.com/opengisch/QField/releases)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/8392/badge)](https://www.bestpractices.dev/projects/8392)
[![Digital Public Good](https://img.shields.io/badge/Digital%20Public%20Good-verified-brightgreen)](https://www.digitalpublicgoods.net/r/qfield)
[![QFieldCloud](https://img.shields.io/badge/QFieldCloud-GitHub-blue)](https://github.com/opengisch/QFieldCloud)

# QField for QGIS

A simplified touch-optimized interface for QGIS in the field.

[![Visit QField's homepage](https://github.com/user-attachments/assets/88771ae0-3701-4cf4-8d8c-cd295c0831b1)](https://qfield.org)

## 🧭 About QField

QField works fully offline or connected, and supports seamless synchronization with the optional [**QFieldCloud** platform](https://qfield.cloud) for collaborative field-to-office workflows.
You can find the open-source QFieldCloud backend on GitHub here: [github.com/opengisch/QFieldCloud](https://github.com/opengisch/QFieldCloud)

QField is officially recognized as a [Digital Public Good](https://digitalpublicgoods.net/r/qfield) for its contributions to open, inclusive, and sustainable digital development.

Explore the full documentation at [docs.qfield.org](https://docs.qfield.org/)

## 📲 Get QField
<p align="center">
  <a href="https://play.google.com/store/apps/details?id=ch.opengis.qfield"><img src="https://qfield.org/images/play_store.png" alt="Get it on Google Play" height="60"/></a>
  <a href="https://apps.microsoft.com/detail/xp99h3bcx4bw7f"><img src="https://qfield.org/images/download_windows.png" alt="Get it on Microsoft Store" height="60"/></a>
  <a href="https://apps.apple.com/app/qfield-for-qgis/id1531726814"><img src="https://qfield.org/images/app_store.png" alt="Get it on the App Store" height="60"/></a>
</p>
<p align="center">
  <a href="https://qfield.org/get-latest?platform=linux"><img src="https://cdn.jsdelivr.net/gh/devicons/devicon/icons/linux/linux-original.svg" alt="Linux" width="20"/>Download for Linux</a>
  <a href="https://qfield.org/get-latest?platform=macos"><img src="https://cdn.jsdelivr.net/gh/devicons/devicon/icons/apple/apple-original.svg" alt="macOS" width="20"/>Download for macOS</a>
</p>

### All Platforms
📦 Prefer direct downloads or older versions?  Check out the full list of releases on [GitHub Releases](https://github.com/opengisch/QField/releases)

### Get master (unstable) version

We automatically publish the latest master build to a [dedicated channel on the playstore](https://play.google.com/store/apps/details?id=ch.opengis.qfield_dev). You'll need to [join the beta program](https://play.google.com/apps/testing/ch.opengis.qfield_dev) to start getting the latest version.

Please remember that this is the latest development build and is not meant for production.

## Contribute

QField is an open source project, licensed under the terms of the GPLv2 or later. This means that it is free to use and modify and will stay like that.

We are very happy if this app helps you to get your job done or in whatever creative way you may use it.

If you found it useful, we will be even happier if you could give something back. A couple of things you can do are

 * Rate the app [★★★★★](https://play.google.com/store/apps/details?id=ch.opengis.qfield&hl=en#details-reviews)
 * Write about your experience (please [let us know](mailto:sales@qfield.cloud)!)
 * [Help with the documentation](https://github.com/opengisch/QField-docs#documentation-process)
 * [Translate the documentation](https://github.com/opengisch/QField-docs#translation-process) or [the app](https://explore.transifex.com/opengisch/qfield-for-qgis/)
 * [Sponsor a feature](https://qfield.org/support-us/)
 * And just drop by to say thank you or have a beer with us next time you meet OPENGIS.ch at a conference

## Share

The world loves to hear about the usage of QField, follow us or share your story on your favorite channel

[![share on linkedin](images/icons/linkedin.svg)](https://www.linkedin.com/products/opengisch-qfield/)
[![share on bluesky](images/icons/bluesky.svg)](https://bsky.app/profile/qfield.bsky.social/share?text=Looking%20for%20a%20good%20tool%20for%20field%20work%20in%20GIS?%20Check%20out%20%23QField!)
[![share on mastodon](images/icons/mastodon.svg)](https://mastodon.social/share?text=Looking%20for%20a%20good%20tool%20for%20field%20work%20in%20GIS?%20Check%20out%20%23QField!)
[![share on X](images/icons/twitter-x.svg)](https://x.com/QFieldForQGIS)

## Development

For development information, refer to the dedicated [developer documentation](doc/dev.md).

## 공식 QField 원본 패키지 인증서 (성산 APK 아님)

아래 인증서 해시는 upstream 공식 QField Android 패키지에만 해당합니다.
`kr.co.sungsan.mobilegis` 성산 APK의 진위를 보증하지 않으며, 성산 배포본은
성산이 관리하는 별도 서명 인증서와 공개된 SHA-256 값으로 검증해야 합니다.

SHA-256 hash of signing certificate:

```5a7dd946a4b700c081a5bd375dbc8f0d11aa89d53832567ce5b8a92088e0e898```

Use the following command to verify the hash of the signing certificate:

```apksigner verify --print-certs [filename.apk] | grep "5a7dd946a4b700c081a5bd375dbc8f0d11aa89d53832567ce5b8a92088e0e898"```
