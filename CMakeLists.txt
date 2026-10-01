cmake_minimum_required(VERSION 3.24)

project(KPlayer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.11.2 REQUIRED COMPONENTS Quick OpenGL)
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
qt_standard_project_setup(REQUIRES 6.11.2)

qt_add_executable(KPlayer
    main.cpp
)

qt_add_qml_module(KPlayer
    URI KPlayer
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/KPlayer
    QML_FILES Main.qml
    SOURCES MpvVideo.h MpvVideo.cpp
)

target_link_libraries(KPlayer PRIVATE Qt6::Quick Qt6::OpenGL PkgConfig::MPV)

target_compile_definitions(KPlayer PRIVATE
    KPLAYER_DUMMY_VIDEO_PATH="${CMAKE_CURRENT_SOURCE_DIR}/dummy.mp4"
    $<$<CONFIG:Debug>:QT_QML_DEBUG>
)

set_target_properties(KPlayer PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
