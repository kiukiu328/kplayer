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
    src/main.cpp
)

# Keep reusable UI types at the QML module root.
set(KPLAYER_QML_FILES
    qml/Main.qml
    qml/Theme.qml
    qml/PlayerIcon.qml
    qml/IconButton.qml
    qml/TitleBar.qml
    qml/VideoStage.qml
    qml/PlaybackBar.qml
    qml/PlaylistTab.qml
    qml/PlaylistPanel.qml
)
foreach(qml_file IN LISTS KPLAYER_QML_FILES)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(KPlayer
    URI KPlayer
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/KPlayer
    QML_FILES ${KPLAYER_QML_FILES}
    SOURCES
        src/playback/MpvVideo.h
        src/playback/MpvVideo.cpp
)

target_include_directories(KPlayer PRIVATE src/playback)
target_link_libraries(KPlayer PRIVATE Qt6::Quick Qt6::OpenGL PkgConfig::MPV)

target_compile_definitions(KPlayer PRIVATE
    $<$<CONFIG:Debug>:QT_QML_DEBUG>
)

set_target_properties(KPlayer PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
