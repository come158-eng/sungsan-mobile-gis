# SPDX-License-Identifier: GPL-2.0-or-later
cmake_minimum_required(VERSION 3.19)

if(NOT DEFINED SOURCE_DIR OR SOURCE_DIR STREQUAL "")
  message(FATAL_ERROR "SOURCE_DIR is required")
endif()

if(NOT DEFINED OUTPUT_DIR OR OUTPUT_DIR STREQUAL "")
  message(FATAL_ERROR "OUTPUT_DIR is required")
endif()

set(SUNG_SAN_VWORLD_API_KEY "$ENV{SUNG_SAN_VWORLD_API_KEY}")
if(SUNG_SAN_VWORLD_API_KEY STREQUAL "")
  message(FATAL_ERROR "Set SUNG_SAN_VWORLD_API_KEY before configuring the Meta Engineering build")
endif()

if(NOT SUNG_SAN_VWORLD_API_KEY MATCHES "^[A-Za-z0-9-]+$")
  message(FATAL_ERROR "SUNG_SAN_VWORLD_API_KEY contains unsupported characters")
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}/sungsan_vworld")
configure_file(
  "${SOURCE_DIR}/main.qml.in"
  "${OUTPUT_DIR}/sungsan_vworld/main.qml"
  @ONLY)
file(COPY "${SOURCE_DIR}/metadata.txt" DESTINATION "${OUTPUT_DIR}/sungsan_vworld")
file(COPY "${SOURCE_DIR}/icon.png" DESTINATION "${OUTPUT_DIR}/sungsan_vworld")
# SPDX-License-Identifier: GPL-2.0-or-later
