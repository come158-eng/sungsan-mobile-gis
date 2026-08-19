# Sungsan Mobile GIS modifications

Modified by Sungsan through 2026-08-11 from QField v4.2.11 commit
`f7123fc8dfa40be4e874d9bf5b46e81c6d05039b`.

Additional Sungsan build reliability modification on 2026-08-19:
`vcpkg/ports/libaec/` overrides the pinned vcpkg 1.1.6 port to download the
same upstream release from the Deutsches Klimarechenzentrum official GitHub
mirror with a fixed SHA-512. This avoids repeated HTTP 429 failures from the
original DKRZ GitLab archive endpoint. The source version, build logic and
BSD-2-Clause license remain unchanged.

This source is Sungsan Mobile GIS `1.0.0-sungsan-beta2`
(`versionCode 10000002`). Beta2 adds the archive, project-read, Qt-thread and
clean-build safety fixes described below; it must not be published under the
older beta1 artifact name.

- `CMakeLists.txt`, `cmake/Package.cmake`: configurable full Android package ID,
  icon, splash, language, theme, bundled app plugins, and foreground-service
  notification channel/title/description text. Sungsan packages pin the audited
  upstream base revision `f7123fc8dfa40be4e874d9bf5b46e81c6d05039b`
  after Git discovery so corresponding-source ZIP builds without `.git` still
  produce a valid About source link; ordinary upstream Git builds retain their
  detected revision.
- `i18n/qfield_ko.ts`: completed and reviewed the active Korean interface;
  the 2026-08-08 audit added 203 missing current-catalog messages and verifies
  full coverage against `qfield_en.ts`, placeholders, markup and line breaks.
- `platform/android/**`, `branding/sungsan/android/**`: Sungsan package,
  authorities, labels, launcher/splash, Korean native strings and dynamic Java
  package relocation. The Sungsan package stages a separate 69-resource Korean
  default catalog (including its app and native-library identities), so native
  dialogs remain Korean and Sungsan-branded even under a non-Korean system
  locale. During Sungsan staging only, upstream locale-specific string files
  are omitted so Android cannot select an old QField-branded locale catalog;
  ordinary upstream builds retain every original locale catalog.
  Positioning and
  retained cloud-service notifications use the configured app icon and expose
  Sungsan channel/title/description text without changing upstream defaults.
  The 2026-08-11 Android safety pass streams and verifies every exported ZIP
  entry (length and CRC), requires QGS/QGZ content, closes every project-file
  stream, and rejects failed, corrupt, partial and empty sharing. New imports
  never overwrite an existing project folder; explicit updates close QGIS on
  its Qt object thread, stage and commit transactionally, restore the prior
  folder on failure, and reopen the QGS/QGZ actually supplied by the archive.
  Extraction rejects path traversal, more than 100,000 entries, or more than
  8 GiB of expanded archive data.
- `scripts/build.sh`, `scripts/build-vcpkg.sh`,
  `scripts/build-sungsan-android.sh`, `scripts/validate-sungsan-keystore.sh`,
  `scripts/sign-sungsan-release-apk.sh`: isolated
  `build-sungsan-native-<ABI>` Release cache and generated-plugin directory,
  fail-fast behavior, blank Sentry settings and safe unset-variable defaults.
  Beta2 refuses any pre-existing native/generated/APK output path, accepts only
  APKs newer than the current build marker, keeps release credentials entirely
  out of the full source-build container, and rejects source-tree keystores.
  Qt produces an exact unsigned release APK without `--sign`; a separate
  network-disabled, read-only container validates an external read-only
  keystore, fixes its certificate SHA-256 before compilation, runs `zipalign`
  before `apksigner`, reads passwords from private files, and checks the final
  signature against the fixed certificate. Internal debug-key output is named
  `DEBUG-KEY-TEST-ONLY`; secret-free CI handoff output is named
  `UNSIGNED-RELEASE-SIGNING-INPUT-ONLY`, so neither can be mistaken for a
  signed release.
- `.docker/android_dev/Dockerfile`, `cmake/Platform.cmake`: Android target and
  installed SDK platform synchronized at API 36; build-tools remains 35.0.1.
- `.github/workflows/sungsan-android.yml`: manual-only arm64 CI with immutable
  official Action commit pins. The build job produces a debug-key test APK or
  unsigned release input but receives no release keystore, alias or password.
  A second no-checkout job protected by the repository `sungsan-release`
  environment downloads only the immutable artifact ID, enforces an exact
  three-file inventory, fixes the signing certificate SHA-256, then aligns,
  signs and verifies exactly one signer using the official `ubuntu-24.04`
  runner's Android build-tools 35.0.1. Required reviewers and administrator
  bypass prevention must be configured in repository settings because YAML
  cannot enforce those environment rules. In the development worktree, the
  upstream Android workflow is retained only as
  `.github/workflows/android.yml.disabled`, preventing its push/PR/release and
  S3/Play/Sentry paths from registering in this fork. The distributable source
  ZIP includes only `.github/workflows/sungsan-android.yml` and excludes that
  disabled reference together with every other upstream workflow.
- `README.md`, `branding/sungsan/README.ko.md`,
  `branding/sungsan/PRODUCT_REQUIREMENTS.ko.md`: Sungsan-first source identity,
  one-command build instructions, product boundaries and explicit APK/device
  release gates.
- `src/app/main.cpp`, `src/core/qfield.h.in`: application identity, default
  language, package ID, independent Sungsan settings identity and custom data
  directory.
- `src/core/CMakeLists.txt`, `src/core/platforms/**`: generated branding values,
  Android package/JNI separation and asset update revision.
- `src/core/qgismobileapp.cpp`: configurable application URL scheme, explicit
  Sungsan QML branding flag, branded network user agent and print-layout stamp.
  Beta2 respects `QgsProject::read()` failure, clears incomplete state and
  returns safely to the home screen instead of allowing a corrupt QGS/QGZ to be
  treated as loaded or rewritten.
- `src/core/appinterface.*`: QML-callable save checkpoint used by Sungsan's
  explicit save and five-minute controls. Feature forms keep the upstream
  commit path; when an imported project came from an older QGIS major version,
  the checkpoint preserves its QGZ schema instead of rewriting it with the
  newer mobile engine. Projects from the running major version retain the
  normal `QgsProject::write()` path.
- `src/core/qgsgpkgflusher.*`: beta2 marshals SQLite work to the flusher's Qt
  thread and requires a complete synchronous WAL checkpoint before a project
  directory is exported or replaced, preventing the base database and WAL from
  being copied on opposite sides of a delayed checkpoint.
- `src/core/pluginmanager.cpp`: the exact bundled `sungsan_vworld` plugin is
  mandatory in Sungsan builds and recovers from stale disabled settings without
  changing any other bundled or upstream plugin.
- `src/core/localfilesmodel.cpp`, `src/core/recentprojectlistmodel.cpp`,
  `src/core/locator/locatormodelsuperbridge.cpp`, `src/core/utils/fileutils.cpp`:
  Sungsan file-folder/photo metadata labels, no upstream sample/cloud recent
  projects, and no upstream documentation search in the Sungsan build.
- `src/core/utils/projectutils.*`: safe VWorld detection, attribution and
  insertion at the bottom of the project layer tree.
- `src/core/utils/urlutils.*`, `src/qml/WelcomeScreen.qml`: branded deep links
  and package-aware store URLs; Sungsan disables upstream feedback, metrics,
  sample-project and first-run tour prompts.
- `src/qml/About.qml`, `src/qml/DashBoard.qml`, `src/qml/QFieldSettings.qml`,
  `src/qml/QFieldLocalDataPickerScreen.qml`, and related cloud/help screens:
  Sungsan-only task-oriented branding, a simplified layer panel, hidden
  QFieldCloud/store/plugin/documentation entry points, Korean-only language
  selection, Sungsan-only automatic saving, disabled anonymous metrics, and
  retained QField/QGIS copyright and GPL notices under
  “오픈소스 정보”. This fork source is Sungsan-only and does not modify an
  independently installed official QField application.
- `src/qml/sungsan/**`, `src/qml/qgismobileapp.qml`,
  `src/qml/FeatureListForm.qml`, `src/qml/qml.qrc`: Sungsan home and compact
  field panel, Korean GPS/layer/survey/digitizing/save/export controls, actual
  validated form save before project write, five-minute safe checkpoints and
  Android project import/export wiring.
- `src/service/qfieldcloudservice.cpp`, `src/service/qfieldpositioningservice.cpp`:
  full package-aware JNI symbols.

New Sungsan files live under `branding/sungsan/`, `src/qml/sungsan/`, and
`scripts/`. The rejected QField-shaped preview APK and its reconstruction recipe
are intentionally excluded from this product-source archive. Release packaging
provides the QGIS and Poppler sources, the matching vcpkg Poppler port and its
MIT license as the separate `Sungsan-Mobile-GIS-v1.0.0-beta2-corresponding-source.zip`;
the QGIS build patches remain in `vcpkg/ports/qgis/` in the main source archive.
