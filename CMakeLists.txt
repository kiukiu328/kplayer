cmake_minimum_required(VERSION 3.24)

project(KPlayer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.11.2 REQUIRED COMPONENTS Quick OpenGL Svg)
if(WIN32)
    # Native Windows CMake cannot use MSYS paths from dependency .pc files.
    find_path(MPV_INCLUDE_DIR NAMES mpv/client.h REQUIRED)
    find_library(MPV_LIBRARY NAMES mpv REQUIRED)
    add_library(MPV::MPV UNKNOWN IMPORTED)
    set_target_properties(MPV::MPV PROPERTIES
        IMPORTED_LOCATION "${MPV_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${MPV_INCLUDE_DIR}"
    )
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
    add_library(MPV::MPV ALIAS PkgConfig::MPV)
endif()
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
    qml/PlayerSlider.qml
    qml/PlayerMenu.qml
    qml/PlayerMenuItem.qml
    qml/PlayerMenuSeparator.qml
    qml/AppMenu.qml
    qml/TitleBar.qml
    qml/VideoStage.qml
    qml/WindowResizeFrame.qml
    qml/PlaybackBar.qml
    qml/PlaylistTab.qml
    qml/PlaylistPanel.qml
)
foreach(qml_file IN LISTS KPLAYER_QML_FILES)
    get_filename_component(qml_name "${qml_file}" NAME)
    set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
endforeach()
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

set(KPLAYER_ICONS
    chevronDown chevronRight close down eject exitFullscreen first fullscreen
    last list maximize menu minimize next pause pin play previous restore search
    searchList settings stop up volume volumeMuted
)
set(KPLAYER_ICON_FILES)
foreach(icon IN LISTS KPLAYER_ICONS)
    set(icon_file "assets/icons/${icon}.svg")
    set_source_files_properties("${icon_file}" PROPERTIES QT_RESOURCE_ALIAS "icons/${icon}.svg")
    list(APPEND KPLAYER_ICON_FILES "${icon_file}")
endforeach()

qt_add_qml_module(KPlayer
    URI KPlayer
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/KPlayer
    QML_FILES ${KPLAYER_QML_FILES}
    RESOURCES ${KPLAYER_ICON_FILES}
    SOURCES
        src/playback/MpvVideo.h
        src/playback/MpvVideo.cpp
        src/playback/MpvPreview.h
        src/playback/MpvPreview.cpp
        src/playback/PlaylistModel.h
        src/playback/PlaylistModel.cpp
)

target_include_directories(KPlayer PRIVATE src/playback)
target_link_libraries(KPlayer PRIVATE Qt6::Quick Qt6::OpenGL Qt6::Svg MPV::MPV)

target_compile_definitions(KPlayer PRIVATE
    $<$<CONFIG:Debug>:QT_QML_DEBUG>
)

set_target_properties(KPlayer PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

option(KPLAYER_BUILD_TESTS "Build playlist and playback interaction tests" OFF)
if(KPLAYER_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.11.2 REQUIRED COMPONENTS Test)
    qt_add_executable(KPlayerTests tests/tst_player.cpp)
    qt_add_qml_module(KPlayerTests
        URI KPlayer
        OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests/qml/KPlayer
        QML_FILES ${KPLAYER_QML_FILES}
        RESOURCES ${KPLAYER_ICON_FILES}
        SOURCES
            src/playback/MpvVideo.h src/playback/MpvVideo.cpp
            src/playback/MpvPreview.h src/playback/MpvPreview.cpp
            src/playback/PlaylistModel.h src/playback/PlaylistModel.cpp
    )
    target_include_directories(KPlayerTests PRIVATE src/playback)
    target_link_libraries(KPlayerTests PRIVATE
        Qt6::Quick Qt6::OpenGL Qt6::Svg Qt6::Test MPV::MPV)
    add_test(NAME player COMMAND KPlayerTests
        -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/player-tests.xml,junitxml")
    set_tests_properties(player PROPERTIES TIMEOUT 120)
endif()
