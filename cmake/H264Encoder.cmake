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

add_library(h264-decoder src/h264_decoder.cpp src/h264_decoder.h)
target_include_directories(h264-decoder PUBLIC src)
target_link_libraries(h264-decoder PUBLIC h264-encoder PRIVATE cisco-openh264)

add_library(video-stream
    src/stream_config.h
    src/stream_worker.cpp src/stream_worker.h
    src/stream_session.cpp src/stream_session.h)
target_link_libraries(video-stream PUBLIC image-transport h264-encoder h264-decoder Qt6::Gui)

add_library(paint-stream-ui
    src/paint_stream_window.cpp src/paint_stream_window.h
    src/paint_canvas.cpp src/paint_canvas.h
    src/image_view.cpp src/image_view.h)
target_link_libraries(paint-stream-ui PUBLIC video-stream Qt6::Widgets)

qt_add_executable(paint-stream src/paint_stream_main.cpp)
target_link_libraries(paint-stream PRIVATE paint-stream-ui)

if(BUILD_TESTING)
    qt_add_executable(encoder-tests tests/encoder_tests.cpp)
    target_link_libraries(encoder-tests PRIVATE h264-encoder Qt6::Test cisco-openh264)
    add_test(NAME encoder-tests COMMAND encoder-tests)

    qt_add_executable(decoder-tests tests/decoder_tests.cpp)
    target_link_libraries(decoder-tests PRIVATE h264-decoder Qt6::Test)
    add_test(NAME decoder-tests COMMAND decoder-tests)

    qt_add_executable(stream-tests tests/stream_tests.cpp)
    target_link_libraries(stream-tests PRIVATE video-stream Qt6::Test)
    add_test(NAME stream-tests COMMAND stream-tests)

    qt_add_executable(stream-gui-tests tests/stream_gui_tests.cpp)
    target_link_libraries(stream-gui-tests PRIVATE paint-stream-ui Qt6::Test)
    add_test(NAME stream-gui-tests COMMAND stream-gui-tests)
    set_tests_properties(stream-gui-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    qt_add_executable(stream-process-peer tests/stream_process_peer.cpp)
    target_link_libraries(stream-process-peer PRIVATE video-stream)
    qt_add_executable(stream-process-tests tests/stream_process_tests.cpp)
    target_link_libraries(stream-process-tests PRIVATE Qt6::Test)
    add_dependencies(stream-process-tests stream-process-peer)
    add_test(NAME stream-process-tests COMMAND stream-process-tests)
endif()
