cmake_minimum_required(VERSION 3.24)
if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
endif()

get_filename_component(deploy_dir "${PLAYER}" DIRECTORY)
set(ENV{PATH} "${QT_BIN};${MPV_BIN};$ENV{PATH}")
execute_process(
    COMMAND "${WINDEPLOYQT}" --no-translations --verbose 0 --qmldir "${QML_DIR}"
            --dir "${deploy_dir}" "${PLAYER}"
    RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Qt runtime deployment failed: ${result}")
endif()
# MSYS2's Qt uses share/qt6/qml in the SDK. Use the deployed layout explicitly.
file(WRITE "${deploy_dir}/qt.conf" "[Paths]\nPrefix=.\nPlugins=.\nQmlImports=qml\n")

# windeployqt handles Qt plugins/QML imports; also deploy mpv and transitive DLLs.
if(OBJDUMP)
    set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
    set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
    set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${OBJDUMP}")
endif()
file(GLOB_RECURSE deployed_plugins "${deploy_dir}/*.dll")
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${PLAYER}"
    LIBRARIES ${deployed_plugins}
    DIRECTORIES "${QT_BIN}" "${MPV_BIN}"
    RESOLVED_DEPENDENCIES_VAR resolved
    UNRESOLVED_DEPENDENCIES_VAR unresolved
    CONFLICTING_DEPENDENCIES_PREFIX conflicts
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
    POST_EXCLUDE_REGEXES ".*[/\\][Ww][Ii][Nn][Dd][Oo][Ww][Ss][/\\].*"
)
if(unresolved)
    message(FATAL_ERROR "Unresolved runtime libraries: ${unresolved}")
endif()
# QtCore deployed by windeployqt has a patched install prefix. Keep that copy
# when a plugin resolves the original DLL from the SDK as well.
foreach(name IN LISTS conflicts_FILENAMES)
    if(NOT EXISTS "${deploy_dir}/${name}")
        list(GET conflicts_${name} 0 library)
        list(APPEND resolved "${library}")
    endif()
endforeach()
foreach(library IN LISTS resolved)
    get_filename_component(source_dir "${library}" DIRECTORY)
    if(NOT source_dir STREQUAL deploy_dir)
        file(COPY "${library}" DESTINATION "${deploy_dir}")
    endif()
endforeach()
