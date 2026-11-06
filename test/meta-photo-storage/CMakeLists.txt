cmake_minimum_required(VERSION 3.16)
project(MetaPhotoStorageRegression LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract.py"
  "${CMAKE_CURRENT_SOURCE_DIR}/../../src/core/utils/fileutils.cpp"
  "${CMAKE_CURRENT_BINARY_DIR}/production-fileutils.inc"
  COMMAND_ERROR_IS_FATAL ANY)
add_executable(meta-photo-storage main.cpp)
target_include_directories(meta-photo-storage PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(meta-photo-storage PRIVATE Qt6::Core)
enable_testing()
add_test(NAME production-photo-storage COMMAND meta-photo-storage)
