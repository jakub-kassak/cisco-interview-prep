cmake_minimum_required(VERSION 3.21)
project(PaintShare VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets Network)
set(CMAKE_AUTOMOC ON)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

qt_add_executable(paint-share
    src/main.cpp
    src/paint_canvas.cpp
    src/paint_canvas.h
)
target_link_libraries(paint-share PRIVATE Qt6::Widgets)

add_library(image-transport
    src/tcp_sender.cpp src/tcp_sender.h
    src/tcp_receiver.cpp src/tcp_receiver.h
    src/transport_limits.h
)
target_include_directories(image-transport PUBLIC src)
target_link_libraries(image-transport PUBLIC Qt6::Network)

qt_add_executable(image-transfer
    src/image_transfer_main.cpp
    src/transfer_options.h
    src/image_transfer_window.cpp src/image_transfer_window.h
    src/image_view.cpp src/image_view.h
)
target_link_libraries(image-transfer PRIVATE image-transport Qt6::Widgets)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)
    qt_add_executable(transport-tests tests/transport_tests.cpp)
    target_link_libraries(transport-tests PRIVATE image-transport Qt6::Test)
    add_test(NAME transport-tests COMMAND transport-tests)
endif()

option(BUILD_H264_ENCODER "Build the optional OpenH264 encoder and its tests" OFF)
if(BUILD_H264_ENCODER)
    include(cmake/H264Encoder.cmake)
endif()
