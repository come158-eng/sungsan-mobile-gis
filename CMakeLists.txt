# Modified for Meta Engineering GIS by Sungsan on 2026-08-11.
#############################################################
# CMake settings
cmake_minimum_required(VERSION 3.19)

set(VCPKG_TAG 57987637aac17d62bac535bc839baca3754490e6)

set(QML_IMPORT_PATH ${CMAKE_SOURCE_DIR}/src/qml/imports CACHE PATH "QML import path for Qt Creator to detect custom modules properly")

set(CMAKE_COLOR_MAKEFILE ON)
set(APP_NAME "QField" CACHE STRING "Application Name")
set(APP_ICON "qfield_logo" CACHE STRING "Application Icon (must exist as images/icons/{APP_ICON}.svg and platform/android/res/drawable/{APP_ICON}_vector.xml)")
set(APP_ICON_PATH "" CACHE PATH "Path to custom icon assets. Structure: {APP_ICON}.svg at root, android/drawable-*/{APP_ICON}.png, android/drawable/{APP_ICON}_vector.xml")
set(APP_SPLASH_PATH "" CACHE FILEPATH "Optional Android splash image (PNG recommended). When unset, the vector application icon is used.")
set(APP_THEME_PATH "" CACHE PATH "Path to a custom theme.json file. When set, the Theme engine will load colours from this file instead of the built-in default. Structure must match src/core/theme/theme.json.")
set(APP_DEFAULT_LANGUAGE "" CACHE STRING "Default UI language code used until the user chooses another language (for example: ko)")
set(APP_URL_SCHEME "qfield" CACHE STRING "Application deep-link URL scheme")
set(APP_DATA_DIR_NAME "QField" CACHE STRING "Android external data subdirectory name")
set(APP_POSITIONING_CHANNEL_NAME "" CACHE STRING "Android positioning notification channel name override")
set(APP_POSITIONING_CHANNEL_DESCRIPTION "" CACHE STRING "Android positioning notification channel description override")
set(APP_POSITIONING_NOTIFICATION_TITLE "" CACHE STRING "Android positioning notification title override")
set(APP_POSITIONING_NOTIFICATION_RUNNING_TEXT "" CACHE STRING "Android positioning foreground notification text override")
set(APP_CLOUD_CHANNEL_NAME "" CACHE STRING "Android cloud notification channel name override")
set(APP_CLOUD_CHANNEL_DESCRIPTION "" CACHE STRING "Android cloud notification channel description override")
set(APP_CLOUD_NOTIFICATION_TITLE "" CACHE STRING "Android cloud notification title override")
set(APP_CLOUD_NOTIFICATION_RUNNING_TEXT "" CACHE STRING "Android cloud foreground notification text override")
set(APK_VERSION_CODE "1" CACHE STRING "Apk Version Code (Example: 1)")
set(APP_VERSION "0" CACHE STRING "Application Version (Example: v1.0.0)")
set(APP_VERSION_STR "local - dev" CACHE STRING "Application Version Name (Example: 1.0.0 - Homerun)")
set(APP_PACKAGE_NAME "qfield" CACHE STRING "Package name suffix. E.g. qfield --> ch.opengis.qfield")
set(APP_PACKAGE_ID "" CACHE STRING "Full Android application ID. Defaults to ch.opengis.${APP_PACKAGE_NAME}")
set(APP_UPSTREAM_REVISION "" CACHE STRING "Upstream source revision shown in downstream open-source attribution")
set(APP_BUNDLED_PLUGINS "" CACHE STRING "Application bundled plugins path")

if(NOT APP_PACKAGE_ID)
  set(APP_PACKAGE_ID "ch.opengis.${APP_PACKAGE_NAME}")
endif()

if(NOT APP_PACKAGE_ID MATCHES "^[A-Za-z_][A-Za-z0-9_]*(\\.[A-Za-z_][A-Za-z0-9_]*)+$")
  message(FATAL_ERROR "APP_PACKAGE_ID must be a full Java-style package ID (received: ${APP_PACKAGE_ID})")
endif()

string(REPLACE "." "/" APP_PACKAGE_PATH "${APP_PACKAGE_ID}")

string(REGEX REPLACE "v" "" CLEAN_APP_VERSION "${APP_VERSION}")

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

# Platform specific fixes (android, macos, ...)
include(Platform)
# Obtain git revision
include(GetGitRevisionDescription)
# The vcpkg toolchain to compile dependencies
include(VcpkgToolchain)
# The default installation paths
include(GNUInstallDirs)

project(${APP_NAME}
  VERSION ${CLEAN_APP_VERSION}
)

GET_GIT_HEAD_REVISION(GIT_REFSPEC GIT_REV)

# The distributable Sungsan corresponding-source archive intentionally omits
# .git. It is also a downstream product, so a fork commit would not be a valid
# opengisch/QField commit link. Pin the exact audited upstream base revision for
# this package only. Ordinary upstream packages continue using the live Git
# revision returned above.
if(APP_PACKAGE_ID STREQUAL "kr.co.metaengi.mobilegis")
  if(NOT APP_UPSTREAM_REVISION)
    set(APP_UPSTREAM_REVISION "f7123fc8dfa40be4e874d9bf5b46e81c6d05039b")
  endif()
  # CMake's regular-expression engine does not portably support counted
  # repetition; validate length and hexadecimal content separately.
  string(LENGTH "${APP_UPSTREAM_REVISION}" APP_UPSTREAM_REVISION_LENGTH)
  if(NOT APP_UPSTREAM_REVISION_LENGTH EQUAL 40 OR
     NOT APP_UPSTREAM_REVISION MATCHES "^[0-9a-fA-F]+$")
    message(FATAL_ERROR
            "APP_UPSTREAM_REVISION must be a full 40-character Git commit hash")
  endif()
  set(GIT_REV "${APP_UPSTREAM_REVISION}")
endif()

message(STATUS "Building for git rev ${GIT_REV}")

set(DEFAULT_BIN_SUBDIR     bin)
set(DEFAULT_CGIBIN_SUBDIR  bin)
set(DEFAULT_LIB_SUBDIR     lib${LIB_SUFFIX})
set(DEFAULT_INCLUDE_SUBDIR include/qfield)

set(QFIELD_BIN_SUBDIR     ${DEFAULT_BIN_SUBDIR}     CACHE STRING "Subdirectory where executables will be installed")
set(QFIELD_LIB_SUBDIR     ${DEFAULT_LIB_SUBDIR}     CACHE STRING "Subdirectory where libraries will be installed")
set(QFIELD_INCLUDE_SUBDIR ${DEFAULT_INCLUDE_SUBDIR} CACHE STRING "Subdirectory where header files will be installed")

set(RELATIVE_PREFIX_PATH ${WITH_VCPKG} CACHE BOOL "Use a prefix path relative to the application itself rather than hard coding the path while compiling")
if(RELATIVE_PREFIX_PATH)
  add_definitions(-DRELATIVE_PREFIX_PATH)
endif()

mark_as_advanced (QFIELD_INCLUDE_SUBDIR QFIELD_BIN_SUBDIR QFIELD_LIB_SUBDIR)
set(QFIELD_BIN_DIR ${QFIELD_BIN_SUBDIR})
set(QFIELD_LIB_DIR ${QFIELD_LIB_SUBDIR})
set(QFIELD_INCLUDE_DIR ${QFIELD_INCLUDE_SUBDIR})
set(QT_ANDROID_APPLICATION_BINARY "qfield")

# set the default locations where the targets (executables, libraries) will land when compiled
# this is to allow running QField directly from the build directory.
if (ANDROID)
  set(QFIELD_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/android-build)
else()
  set(QFIELD_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${QFIELD_OUTPUT_DIRECTORY}/${QFIELD_BIN_SUBDIR}/$<0:>)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${QFIELD_OUTPUT_DIRECTORY}/${QFIELD_LIB_SUBDIR}/$<0:>)

set(WITH_CCACHE OFF CACHE BOOL "Build with ccache")
if(WITH_CCACHE)
  include(ccache)
endif()

set(WITH_BLUETOOTH
    ON
    CACHE BOOL "Enable bluetooth support")
set(WITH_SERIALPORT
    ON
    CACHE BOOL "Enable serial port support")
set(WITH_NFC
    ON
    CACHE BOOL "Enable NFC support")

set(SENTRY_DSN "" CACHE STRING "The sentry dsn for logging purpose. If not set, sentry will be disabled")
set(SENTRY_ENV "" CACHE STRING "The sentry environment for logging purpose.")
mark_as_advanced(SENTRY_DSN SENTRY_ENV)
if(SENTRY_DSN)
  set(WITH_SENTRY 1)
else()
  set(WITH_SENTRY 0)
endif()

set(WITH_SAMPLE_PROJECTS
    ON
    CACHE BOOL "Enable sample projects")

if (WITH_SAMPLE_PROJECTS)
  install(DIRECTORY ${CMAKE_SOURCE_DIR}/resources/sample_projects DESTINATION ${CMAKE_INSTALL_DATADIR}/qfield)
endif()

if (APP_BUNDLED_PLUGINS)
  install(
    DIRECTORY "${APP_BUNDLED_PLUGINS}/"
    DESTINATION ${CMAKE_INSTALL_DATADIR}/qfield/plugins)
endif()

if (CMAKE_BUILD_TYPE MATCHES Debug OR CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
  message (STATUS "QField debug output enabled")
  set(QFIELDDEBUG TRUE)
else()
  set(QFIELDDEBUG FALSE)
endif()

set(QT_MIN_VERSION 6.6.0)

if (ANDROID)
  if(VCPKG_TARGET_TRIPLET STREQUAL "arm-android" OR VCPKG_TARGET_TRIPLET STREQUAL "arm-neon-android")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -fstack-protector-strong -lunwind -Wl,--exclude-libs=libunwind.a")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -fstack-protector-strong -lunwind -Wl,--exclude-libs=libunwind.a")
  else()
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC -fstack-protector-strong")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIC -fstack-protector-strong")
  endif()

  link_libraries("-ljnigraphics")

  # Make sure the generator for android_deployment_settings.json can figure out the architecture
  list(APPEND CMAKE_FIND_ROOT_PATH /)

  set(ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_BINARY_DIR}/android-template)
  set(ANDROID_DRAWABLE_DENSITIES mdpi hdpi xhdpi xxhdpi xxxhdpi)

  foreach(DENSITY ${ANDROID_DRAWABLE_DENSITIES})
    set(DST_DIR "${ANDROID_PACKAGE_SOURCE_DIR}/res/drawable-${DENSITY}")
    file(MAKE_DIRECTORY "${DST_DIR}")

    if(APP_ICON_PATH AND EXISTS "${APP_ICON_PATH}/android/drawable-${DENSITY}/${APP_ICON}.png")
      message(STATUS "Using custom icon [${DENSITY}]: ${APP_ICON_PATH}/android/drawable-${DENSITY}/${APP_ICON}.png")
      file(COPY
            "${APP_ICON_PATH}/android/drawable-${DENSITY}/${APP_ICON}.png"
            DESTINATION "${DST_DIR}"
          )
    else()
      message(STATUS "Using default icon [${DENSITY}]: qfield_logo.png -> ${APP_ICON}.png")
      file(COPY
            "${CMAKE_SOURCE_DIR}/platform/android/res/drawable-${DENSITY}/${APP_ICON}.png"
            DESTINATION "${DST_DIR}"
          )
    endif()
  endforeach()

  set(DST_DRAWABLE_DIR "${ANDROID_PACKAGE_SOURCE_DIR}/res/drawable")
  file(MAKE_DIRECTORY "${DST_DRAWABLE_DIR}")

  if(APP_ICON_PATH AND EXISTS "${APP_ICON_PATH}/android/drawable/${APP_ICON}_vector.xml")
    message(STATUS "Using custom vector drawable: ${APP_ICON_PATH}/android/drawable/${APP_ICON}_vector.xml")
    file(COPY
        "${APP_ICON_PATH}/android/drawable/${APP_ICON}_vector.xml"
        DESTINATION "${DST_DRAWABLE_DIR}"
      )
  else()
    message(STATUS "Using default vector drawable: qfield_logo_vector.xml -> ${APP_ICON}_vector.xml")
    file(COPY
        "${CMAKE_SOURCE_DIR}/platform/android/res/drawable/${APP_ICON}_vector.xml"
        DESTINATION "${DST_DRAWABLE_DIR}"
      )
  endif()

  set(APP_SPLASH_DRAWABLE "@drawable/${APP_ICON}_vector")
  set(APP_SPLASH_WIDTH "192dp")
  set(APP_SPLASH_HEIGHT "192dp")
  if(APP_SPLASH_PATH)
    if(IS_ABSOLUTE "${APP_SPLASH_PATH}")
      set(APP_SPLASH_IMAGE_PATH "${APP_SPLASH_PATH}")
    else()
      set(APP_SPLASH_IMAGE_PATH "${CMAKE_SOURCE_DIR}/${APP_SPLASH_PATH}")
    endif()

    if(EXISTS "${APP_SPLASH_IMAGE_PATH}")
      get_filename_component(APP_SPLASH_IMAGE_NAME "${APP_SPLASH_IMAGE_PATH}" NAME_WE)
      set(DST_SPLASH_DIR "${ANDROID_PACKAGE_SOURCE_DIR}/res/drawable-nodpi")
      file(MAKE_DIRECTORY "${DST_SPLASH_DIR}")
      file(COPY "${APP_SPLASH_IMAGE_PATH}" DESTINATION "${DST_SPLASH_DIR}")
      set(APP_SPLASH_DRAWABLE "@drawable/${APP_SPLASH_IMAGE_NAME}")
      set(APP_SPLASH_WIDTH "256dp")
      set(APP_SPLASH_HEIGHT "154dp")
      message(STATUS "Using custom Android splash image: ${APP_SPLASH_IMAGE_PATH}")
    else()
      message(
        FATAL_ERROR
          "APP_SPLASH_PATH '${APP_SPLASH_PATH}' does not exist")
    endif()
  endif()

endif()

find_package(Qt6 COMPONENTS Concurrent Core Qml Gui Xml Positioning Widgets Network Quick Svg Sql Sensors WebView Multimedia Bluetooth Nfc RemoteObjects WebSockets Quick3D REQUIRED)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
  find_package(Qt6 COMPONENTS PrintSupport REQUIRED)
endif()

find_package(QGIS COMPONENTS Core Analysis REQUIRED)
find_package(PROJ REQUIRED)
find_package(GDAL REQUIRED)
find_package(Qca REQUIRED)
find_package(LibZip REQUIRED)

if(ANDROID)
  # Qt by default assumes that cmake files live in `lib/cmake`. Point to `share` instead.
  # See https://github.com/qt/qtbase/blob/39290c508e2c7fa816c6f79b3eb284adc0cad3c0/src/corelib/Qt6AndroidMacros.cmake#L1016C16-L1026
  get_filename_component(QT_ANDROID_PATH_CMAKE_DIR_${ANDROID_ABI} "${Qt6_DIR}/.." ABSOLUTE)
  message(STATUS "QT_ANDROID_PATH_CMAKE_DIR_${ANDROID_ABI}: ${QT_ANDROID_PATH_CMAKE_DIR_${ANDROID_ABI}}")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(QT_HOST_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_HOST_TRIPLET}")
  set(QT_HOST_PATH_CMAKE_DIR "${QT_HOST_PATH}/share")
  # The QmlImportScanner is not discovered on the host path, it should probably be caught by
  # https://github.com/qt/qtbase/blob/8c14b0c/cmake/QtConfig.cmake.in#L104-L122
  # at least we know where to find it:
  set(Qt6QmlImportScanner_DIR "${VCPKG_INSTALLED_DIR}/share/Qt6QmlImportScanner")
  message(STATUS "QT_HOST_PATH: ${QT_HOST_PATH}")
endif()

set(ENABLE_TESTS CACHE BOOL "Enable tests")
set(WITH_UNITTESTS TRUE CACHE BOOL "Enable unit tests")
set(WITH_SPIX FALSE CACHE BOOL "Compile with Spix for testing")

if(MSVC)
  add_definitions(-D_USE_MATH_DEFINES)
  add_definitions(-DNOMINMAX)
  add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)

  # Generate pdb files
  add_compile_options("$<$<NOT:$<CONFIG:Debug>>:/Zi>")
  add_link_options("$<$<NOT:$<CONFIG:Debug>>:/DEBUG>")
  add_link_options("$<$<NOT:$<CONFIG:Debug>>:/OPT:REF>")
  add_link_options("$<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>")
elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
  set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(SHARE_DIR "${CMAKE_BINARY_DIR}/output/bin/qfield.app/Contents/share")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
  set(SHARE_DIR "${ANDROID_PACKAGE_SOURCE_DIR}/assets")
else()
  set(SHARE_DIR "${CMAKE_BINARY_DIR}/output/share")
endif()
file(MAKE_DIRECTORY "${SHARE_DIR}")

if(WITH_SENTRY)
  add_subdirectory(src/sentry)
endif()

if (WITH_SPIX)
  find_package(Spix REQUIRED)
endif ()

add_subdirectory(src/core)
add_subdirectory(src/qml)
if (ANDROID)
  add_subdirectory(src/service)
endif()
add_subdirectory(src/app)

if (ENABLE_TESTS)
  set (TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/test/testdata")

  find_package(Qt6 COMPONENTS Test QuickTest)
  enable_testing()
  add_subdirectory(test)
endif()

if(WITH_VCPKG)
  # Copy files from the install dir to where it
  # will be bundled
  add_custom_target(deploy)

  function(copy_resource source target)
    add_custom_command(TARGET deploy
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${SHARE_DIR}/${target}"
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${source}" "${SHARE_DIR}/${target}"
    )
  endfunction()

  set(VCPKG_BASE_DIR "${_VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
  if(MSVC)
    set(QGIS_PLUGIN_DIR "${VCPKG_BASE_DIR}/tools/qgis/plugins")
    file(GLOB PROVIDER_LIBS
      "${QGIS_PLUGIN_DIR}/*provider*.dll"
    )
    file(GLOB AUTHMETHODS_LIBS
      "${QGIS_PLUGIN_DIR}/*authmethod*.dll"
    )
    # From QGIS CMakeLists.txt
    set(QGIS_PLUGIN_INSTALL_PREFIX "plugins")
  else()
    set(QGIS_PLUGIN_DIR "${VCPKG_BASE_DIR}/lib/qgis/plugins")
    file(GLOB PROVIDER_LIBS
      "${QGIS_PLUGIN_DIR}/*provider*.so"
    )
    file(GLOB AUTHMETHODS_LIBS
      "${QGIS_PLUGIN_DIR}/*authmethod*.so"
    )
    # From QGIS CMakeLists.txt
    set(QGIS_PLUGIN_INSTALL_PREFIX "lib${LIB_SUFFIX}/qgis/plugins")
  endif()
  add_custom_command(TARGET deploy
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/output/bin/qgis/plugins"
  )
  foreach(LIB ${PROVIDER_LIBS})
    add_custom_command(TARGET deploy
                       POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIB}" "${CMAKE_BINARY_DIR}/output/bin/qgis/plugins"
    )
    install(FILES "${LIB}" DESTINATION "${QGIS_PLUGIN_INSTALL_PREFIX}")
  endforeach()
  foreach(LIB ${AUTHMETHODS_LIBS})
    add_custom_command(TARGET deploy
                       POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LIB}" "${CMAKE_BINARY_DIR}/output/bin/qgis/plugins"
    )
  endforeach()

  # This is needed for gdal to successfully access remote datasets through encrypted connections
  file(DOWNLOAD https://curl.se/ca/cacert.pem "${CMAKE_BINARY_DIR}/cacert.pem" TLS_VERIFY ON STATUS DOWNLOAD_STATUS)
  list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
  list(GET DOWNLOAD_STATUS 1 ERROR_MESSAGE)
  if(NOT ${STATUS_CODE} EQUAL 0)
      message(FATAL_ERROR "Error occurred during download: ${ERROR_MESSAGE}")
  endif()

  add_custom_command(TARGET deploy
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/cacert.pem" "${SHARE_DIR}/cacert.pem"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SHARE_DIR}/qgis/resources"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${SHARE_DIR}/qgis/svg"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${VCPKG_BASE_DIR}/share/qgis/resources" "${SHARE_DIR}/qgis/resources"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${VCPKG_BASE_DIR}/share/qgis/svg" "${SHARE_DIR}/qgis/svg"
    COMMAND ${CMAKE_COMMAND} -E rm -R -- "${SHARE_DIR}/qgis/resources/data"
    COMMAND ${CMAKE_COMMAND} -E rm -R -- "${SHARE_DIR}/qgis/resources/cpt-city-qgis-min"
  )

  set(PROJ_DATA_PATH "${VCPKG_BASE_DIR}/share/proj")

  if(NOT EXISTS "${PROJ_DATA_PATH}/proj.db")
    message(FATAL_ERROR "proj.db not found at ${PROJ_DATA_PATH}/proj.db")
  endif()

  copy_resource("${PROJ_DATA_PATH}" "proj")
  copy_resource("${VCPKG_BASE_DIR}/share/gdal" "gdal")

  if (WITH_SAMPLE_PROJECTS)
    copy_resource("${CMAKE_SOURCE_DIR}/resources/sample_projects" "qfield/sample_projects")
  endif()

  if (APP_BUNDLED_PLUGINS)
    copy_resource("${APP_BUNDLED_PLUGINS}" "qfield/plugins")
  endif()

  install(DIRECTORY "${VCPKG_BASE_DIR}/share/qgis/resources/" DESTINATION "${CMAKE_INSTALL_DATADIR}/qgis/resources")
  install(DIRECTORY "${VCPKG_BASE_DIR}/share/qgis/svg/" DESTINATION "${CMAKE_INSTALL_DATADIR}/qgis/svg")
  install(DIRECTORY "${PROJ_DATA_PATH}/" DESTINATION "${CMAKE_INSTALL_DATADIR}/proj/")
  install(DIRECTORY "${VCPKG_BASE_DIR}/share/gdal/" DESTINATION "${CMAKE_INSTALL_DATADIR}/gdal")
  install(FILES "${CMAKE_BINARY_DIR}/cacert.pem" DESTINATION ${CMAKE_INSTALL_DATADIR}/)

  add_dependencies(qfield deploy)
endif()

if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  install(FILES ${CMAKE_SOURCE_DIR}/images/icons/qfield_logo.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps RENAME qfield.svg)
  install(FILES ${CMAKE_SOURCE_DIR}/platform/linux/qfield.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/qfield)
  install(FILES ${CMAKE_SOURCE_DIR}/platform/linux/qfield.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
endif()

include(Package)
