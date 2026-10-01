cmake_minimum_required(VERSION 3.24)

project(KPlayer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.12 REQUIRED COMPONENTS Quick)
qt_standard_project_setup(REQUIRES 6.12)

qt_add_executable(KPlayer
    main.cpp
)

qt_add_qml_module(KPlayer
    URI KPlayer
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/KPlayer
    QML_FILES Main.qml
)

target_link_libraries(KPlayer PRIVATE Qt6::Quick)

target_compile_definitions(KPlayer PRIVATE
    $<$<CONFIG:Debug>:QT_QML_DEBUG>
)

set_target_properties(KPlayer PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
)
