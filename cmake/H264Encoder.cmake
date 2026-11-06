set(OPENH264_ROOT "${PROJECT_SOURCE_DIR}/codec/.deps/install" CACHE PATH
    "OpenH264 installation prefix")
find_path(OPENH264_INCLUDE_DIR wels/codec_api.h HINTS "${OPENH264_ROOT}/include" REQUIRED)
find_library(OPENH264_LIBRARY NAMES openh264 HINTS "${OPENH264_ROOT}/lib" REQUIRED)
find_package(Threads REQUIRED)

add_library(cisco-openh264 UNKNOWN IMPORTED)
set_target_properties(cisco-openh264 PROPERTIES
    IMPORTED_LOCATION "${OPENH264_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${OPENH264_INCLUDE_DIR}")

add_library(h264-encoder src/h264_encoder.cpp src/h264_encoder.h
    src/i420_image.cpp src/i420_image.h)
target_include_directories(h264-encoder PUBLIC src)
target_compile_features(h264-encoder PUBLIC cxx_std_23)
target_link_libraries(h264-encoder PUBLIC Qt6::Gui
    PRIVATE cisco-openh264 Threads::Threads ${CMAKE_DL_LIBS})
if(MSVC)
    target_compile_options(h264-encoder PRIVATE /W4)
else()
    target_compile_options(h264-encoder PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    qt_add_executable(encoder-tests tests/encoder_tests.cpp)
    target_link_libraries(encoder-tests PRIVATE h264-encoder Qt6::Test cisco-openh264)
    add_test(NAME encoder-tests COMMAND encoder-tests)
endif()
