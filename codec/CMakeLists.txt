cmake_minimum_required(VERSION 3.20)
project(codec_practice LANGUAGES CXX)

set(OPENH264_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.deps/install" CACHE PATH
    "OpenH264 installation prefix")
if(NOT EXISTS "${OPENH264_ROOT}/lib/libopenh264.a")
    message(FATAL_ERROR "Build the codec first: bash scripts/build-openh264.sh")
endif()

find_package(Threads REQUIRED)
add_library(openh264 STATIC IMPORTED)
set_target_properties(openh264 PROPERTIES
    IMPORTED_LOCATION "${OPENH264_ROOT}/lib/libopenh264.a"
    INTERFACE_INCLUDE_DIRECTORIES "${OPENH264_ROOT}/include")

add_executable(codec_smoke src/main.cpp)
target_compile_features(codec_smoke PRIVATE cxx_std_23)
set_target_properties(codec_smoke PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(codec_smoke PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(codec_smoke PRIVATE openh264 Threads::Threads ${CMAKE_DL_LIBS})

enable_testing()
add_test(NAME codec_roundtrip COMMAND codec_smoke)
