cmake_minimum_required(VERSION 3.24)

project(KPlayer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.11.2 REQUIRED COMPONENTS Quick OpenGL Svg LinguistTools)
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
if(WIN32)
    enable_language(RC)
    configure_file(packaging/windows/kplayer.rc.in kplayer.rc @ONLY)
    target_sources(KPlayer PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/kplayer.rc")
    set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/kplayer.rc" PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/kplayer.ico")
endif()

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
    qml/SubtitleMenu.qml
    qml/AdvancedSettings.qml
    qml/VideoAdjustments.qml
    qml/AppMenu.qml
    qml/TitleBar.qml
    qml/VideoStage.qml
    qml/WindowResizeFrame.qml
    qml/WindowDragArea.qml
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
    searchList settings stop up volume volumeMuted kplayer
)
set(KPLAYER_ICON_FILES)
foreach(icon IN LISTS KPLAYER_ICONS)
    set(icon_file "assets/icons/${icon}.svg")
    set_source_files_properties("${icon_file}" PROPERTIES QT_RESOURCE_ALIAS "icons/${icon}.svg")
    list(APPEND KPLAYER_ICON_FILES "${icon_file}")
endforeach()

set(KPLAYER_SOURCES
    src/integration/AppLanguage.h
    src/integration/AppLanguage.cpp
    src/playback/MpvVideo.h
    src/playback/MpvVideo.cpp
    src/playback/MpvPreview.h
    src/playback/MpvPreview.cpp
    src/playback/PlaylistModel.h
    src/playback/PlaylistModel.cpp
    src/integration/VideoFormats.h
    src/integration/VideoLibrary.h
    src/integration/VideoLibrary.cpp
    src/integration/FileAssociations.h
    src/integration/FileAssociations.cpp
    src/playback/Favorites.h
    src/playback/Favorites.cpp
)

# Qt can create empty TS files when inputs are missing. Fail instead of shipping
# a package whose language selector silently leaves the UI untranslated.
foreach(language en zh_TW)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/translations/kplayer_${language}.ts")
        message(FATAL_ERROR "Missing translation source: translations/kplayer_${language}.ts")
    endif()
endforeach()
qt_add_lrelease(TS_FILES translations/kplayer_en.ts
    QM_FILES_OUTPUT_VARIABLE KPLAYER_TRANSLATIONS)
qt_add_lrelease(TS_FILES translations/kplayer_zh_TW.ts
    MERGE_QT_TRANSLATIONS QT_TRANSLATION_CATALOGS qtbase qtdeclarative
    QM_FILES_OUTPUT_VARIABLE KPLAYER_CHINESE_TRANSLATIONS)
list(APPEND KPLAYER_TRANSLATIONS ${KPLAYER_CHINESE_TRANSLATIONS})

function(kplayer_add_translations target)
    qt_add_resources(${target} translations
        PREFIX "/i18n" BASE "${CMAKE_CURRENT_BINARY_DIR}"
        FILES ${KPLAYER_TRANSLATIONS})
endfunction()
kplayer_add_translations(KPlayer)

qt_add_qml_module(KPlayer
    URI KPlayer
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/KPlayer
    QML_FILES ${KPLAYER_QML_FILES}
    RESOURCES ${KPLAYER_ICON_FILES}
    SOURCES ${KPLAYER_SOURCES}
)

target_include_directories(KPlayer PRIVATE src/playback src/integration)
target_link_libraries(KPlayer PRIVATE Qt6::Quick Qt6::OpenGL Qt6::Svg MPV::MPV)
if(WIN32)
    target_link_libraries(KPlayer PRIVATE advapi32 shell32)
endif()

target_compile_definitions(KPlayer PRIVATE
    $<$<CONFIG:Debug>:QT_QML_DEBUG>
)

set_target_properties(KPlayer PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS KPlayer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/linux/kplayer.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES assets/icons/kplayer.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(FILES assets/icons/kplayer.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
endif()

if(WIN32)
    option(KPLAYER_DEPLOY_WINDOWS_RUNTIME "Copy runtime libraries for Explorer launching" ON)
    if(KPLAYER_DEPLOY_WINDOWS_RUNTIME)
        get_target_property(_qt_qmake Qt6::qmake IMPORTED_LOCATION)
        get_filename_component(_qt_bin "${_qt_qmake}" DIRECTORY)
        get_filename_component(_mpv_lib_dir "${MPV_LIBRARY}" DIRECTORY)
        find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt HINTS "${_qt_bin}" REQUIRED)
        if(MINGW)
            get_filename_component(_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
            find_program(KPLAYER_OBJDUMP NAMES objdump HINTS "${_compiler_bin}" REQUIRED)
        endif()
        add_custom_command(TARGET KPlayer POST_BUILD
            COMMAND "${CMAKE_COMMAND}"
                "-DPLAYER=$<TARGET_FILE:KPlayer>"
                "-DQT_BIN=${_qt_bin}"
                "-DMPV_BIN=${_mpv_lib_dir}/../bin"
                "-DWINDEPLOYQT=${WINDEPLOYQT_EXECUTABLE}"
                "-DOBJDUMP=${KPLAYER_OBJDUMP}"
                "-DQML_DIR=${CMAKE_CURRENT_SOURCE_DIR}/qml"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployWindows.cmake"
            COMMENT "Deploying Qt and libmpv runtime libraries for Explorer"
            VERBATIM
        )
        set_property(TARGET KPlayer APPEND PROPERTY LINK_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployWindows.cmake")
    endif()
endif()

option(KPLAYER_BUILD_TESTS "Build playlist and playback interaction tests" OFF)
if(KPLAYER_BUILD_TESTS)
    enable_testing()
    find_package(Qt6 6.11.2 REQUIRED COMPONENTS Test)
    qt_add_executable(KPlayerTests tests/tst_player.cpp)
    kplayer_add_translations(KPlayerTests)
    qt_add_qml_module(KPlayerTests
        URI KPlayer
        OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests/qml/KPlayer
        QML_FILES ${KPLAYER_QML_FILES}
        RESOURCES ${KPLAYER_ICON_FILES}
        SOURCES ${KPLAYER_SOURCES}
    )
    target_include_directories(KPlayerTests PRIVATE src/playback src/integration)
    target_link_libraries(KPlayerTests PRIVATE
        Qt6::Quick Qt6::OpenGL Qt6::Svg Qt6::Test MPV::MPV)
    if(WIN32)
        target_link_libraries(KPlayerTests PRIVATE advapi32 shell32)
    endif()
    add_test(NAME player COMMAND KPlayerTests
        -o -,txt -o "${CMAKE_CURRENT_BINARY_DIR}/player-tests.xml,junitxml")
    set_tests_properties(player PROPERTIES TIMEOUT 120)
endif()
