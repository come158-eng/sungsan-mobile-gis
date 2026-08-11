# Sungsan Mobile GIS 독립 Android 앱

<!-- Modified for Sungsan Mobile GIS by Sungsan on 2026-08-11. -->

이 폴더는 `Sungsan Mobile GIS` 전용 화면·작업 흐름·브랜드 자산과 빌드
설정을 담습니다. QField v4.2.11 오픈소스 코드는 QGIS 프로젝트 호환 엔진으로만
사용하며, 일반 사용 화면은 성산 전용 현장조사 흐름으로 구성합니다. 공식 QField나
QFieldSync의 설치 폴더를 덮어쓰지 않습니다. 이 포크 소스는 성산 전용이며,
기기에 이미 설치된 공식 QField 설치본은 건드리지 않습니다.

## 분리 기준

- 앱 표시 이름: `Sungsan Mobile GIS`
- Android 패키지 ID: `kr.co.sungsan.mobilegis`
- 딥링크 스킴: `sungsanmobilegis://`
- Android 외부 데이터 하위 폴더: `SungsanMobileGIS/`
- 기본 및 사용자 선택 언어: 한국어(`ko`)
- Android 네이티브 기본 문구: 한국어 69개 리소스(한국어가 아닌 휴대폰
  시스템 언어에서도 초기 권한·저장소·가져오기 대화상자에 성산 문구 표시)
- 기본 테마: 성산 파란색(`#194793`)
- 기본 배경지도: VWorld 위성영상, 프로젝트 레이어 트리의 가장 아래에 자동 추가
- 기본 작업 흐름: 프로젝트 가져오기 → 현장조사 시작 → GPS/레이어/객체 입력 →
  자동·수동 저장 → 결과 내보내기
- 제외 항목: QFieldCloud, QField 스토어·후원·플러그인·문서 링크, QField 예제
  프로젝트와 QField 첫 실행 안내

패키지 ID와 딥링크 스킴이 공식 QField와 다르므로 두 앱을 같은 Android 기기에 동시에 설치할 수 있습니다. 파일 열기 선택창에서는 두 앱이 함께 보일 수 있으며, 이는 Android의 정상 동작입니다.

성산 전용 기본 문자열은 `branding/sungsan/android/res/values/strings.xml`에
분리되어 있습니다. 빌드할 때 패키지 ID가 정확히
`kr.co.sungsan.mobilegis`인 경우에만 임시 Android 패키징 폴더의 기본
`values/strings.xml`을 이 파일로 교체합니다. 따라서 일반 upstream 빌드의 영어
기본 문자열은 변경되지 않습니다. 성산 패키징 중에는 휴대폰의 다른 시스템 언어가
upstream 번역을 우선 선택하지 않도록 임시 패키징 폴더의 `values-*/strings.xml`만
제외하고, 모든 언어가 검토한 성산 한국어 기본 문구로 돌아가게 합니다. 원본 소스의
다국어 파일과 일반 upstream 빌드는 그대로 유지됩니다. `lib_name`의 `qfield` 값은 화면 문구가 아니라
기존 네이티브 공유 라이브러리를 찾는 내부 식별자이므로 호환성을 위해 유지합니다.

## 빌드

QField의 기존 Android Docker 빌드 환경이 필요합니다. 저장소 루트에서 다음처럼 실행합니다.

```bash
export SUNG_SAN_VWORLD_API_KEY='발급받은-VWorld-키'
export STOREPASS='배포-키스토어-암호'
export KEYNAME='배포-키-별칭'
export KEYPASS='배포-키-암호'
# 저장소 루트에 배포용 keystore.p12를 둡니다.
./scripts/build-sungsan-android.sh
```

기본 대상은 현재 Android 휴대폰 대부분에 맞는 `arm64-android`입니다. 다른 ABI가 필요하면 다음처럼 지정합니다.

```bash
triplet=arm-neon-android ./scripts/build-sungsan-android.sh
```

생성된 APK는 `build-sungsan-apk/`에 복사됩니다. 배포용 APK는 Android 서명 키가 필요하며, 기존 QField 빌드의 `STOREPASS`, `KEYNAME`, `KEYPASS` 환경 변수를 사용합니다.

빌드 스크립트는 이전 CMake 캐시, 생성된 VWorld 플러그인 또는 복사된 APK가
남아 있으면 중단합니다. `build-sungsan-native-<ABI>/`,
`build-sungsan-native-generated/`, `build-sungsan-apk/`를 자동 삭제하지 않으므로,
필요한 산출물을 먼저 보관한 뒤 정확한 전용 경로만 직접 정리하거나 새 소스 사본에서
빌드하세요. 기본 빌드는 서명 환경 변수 세 값과 소스 루트의 `keystore.p12`가 모두
있어야 하며, 이번 실행에서 새로 생성된 `*-signed.apk`만 배포 산출물로 인정합니다.
서명하지 않은 내부 컴파일 확인이 꼭 필요할 때에만
`SUNG_SAN_ALLOW_UNSIGNED_TEST_BUILD=1`을 명시할 수 있습니다. 이 경우 결과 파일명은
강제로 `UNSIGNED-TEST-ONLY-*.apk`가 되며 설치 시험 전용이므로 배포하면 안 됩니다.

기존 설치 위에 업데이트할 때에는 `APP_VERSION_STR`과 `APK_VERSION_CODE`를 이전 빌드보다 올리세요. 앱 버전 문자열이 바뀌면 VWorld 플러그인·테마 등 번들 자산도 새 APK의 내용으로 다시 복사됩니다.

성산 빌드는 공식 QField의 일반 빌드 캐시와 섞이지 않도록
`build-sungsan-native-<ABI>/` 전용 CMake 디렉터리를 사용합니다. Release 구성으로
빌드하며 Sentry DSN과 환경값은 빈 값으로 고정해 공식 QField 오류수집 주소가
성산 앱에 들어가지 않게 합니다.

배포용 대응 소스 ZIP에는 `.git` 폴더를 넣지 않습니다. 성산 빌드 스크립트는 이
경우에도 오픈소스 정보 화면이 깨진 주소를 만들지 않도록 감사한 QField v4.2.11
원본 기준 커밋 `f7123fc8dfa40be4e874d9bf5b46e81c6d05039b`를
`APP_UPSTREAM_REVISION`으로 고정합니다. 이 고정은 성산 패키지 ID에만 적용되며,
일반 upstream 빌드는 기존처럼 현재 Git 커밋을 사용합니다.

## VWorld 주의사항

VWorld API 키는 공개 Git 소스에 저장하지 않습니다. 빌드 스크립트가 키를 무시되는 생성 폴더에 넣은 뒤 APK에 포함합니다. Android 앱의 키는 결국 추출될 수 있으므로 VWorld 개발자센터에서 허용 도메인·앱·IP 등 사용 가능한 제한을 반드시 설정해야 합니다.

타일 주소는 VWorld 공식 WMTS 형식을 사용합니다.

```text
https://api.vworld.kr/req/wmts/1.0.0/{KEY}/Satellite/{z}/{y}/{x}.jpeg
```

레이어 표시는 `출처: 국토교통부 브이월드`를 포함합니다. 이 구현은 온라인 표시만
하며 오프라인 대량 다운로드나 사전 캐시 기능을 제공하지 않습니다. 현장 오프라인
운용은 PC의 QGIS와 성산 모바일 GIS 플러그인에서 별도 오프라인 배경지도를
준비해야 합니다.

QGIS에서 내보낸 프로젝트에 이미 VWorld 레이어가 있으면 번들 플러그인은 같은 서비스 레이어를 중복 추가하지 않습니다.

## 성산 전용 현장 화면

앱 시작 화면과 지도 위 현장 패널은 성산 전용 QML로 구성합니다. 일반 작업자는
`현장조사 시작`, `현재 위치`, `레이어`, `객체 추가`, `저장`, `완료` 버튼을 사용합니다.
레이어 창도 닫기·레이어 목록·보기/편집 전환만 보이도록 단순화합니다. QGIS에서
설정한 라벨·심볼·입력 양식·사진 필드는 호환 엔진을 통해 그대로 사용합니다.

## 라이선스

이 포크는 원본 QField의 GNU GPL v2 이상 라이선스와 저작권 고지를 그대로 유지합니다. APK를 제3자에게 배포할 때에는 해당 APK에 대응하는 전체 소스 코드와 라이선스 고지를 함께 제공해야 합니다. 성산 로고의 사용 권리는 별도로 관리해야 합니다.
