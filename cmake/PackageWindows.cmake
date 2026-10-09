if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "MSI packaging requires a 64-bit Windows build.")
endif()
if(CMAKE_VERSION VERSION_LESS 3.30)
    message(FATAL_ERROR "MSI packaging with WiX 4 requires CMake 3.30 or newer.")
endif()
if(NOT KPLAYER_DEPLOY_WINDOWS_RUNTIME)
    message(FATAL_ERROR "MSI packaging requires KPLAYER_DEPLOY_WINDOWS_RUNTIME=ON.")
endif()

install(TARGETS KPlayer RUNTIME DESTINATION .)
# Deploy into the install staging directory, rather than packaging the build
# tree, which can contain tests, screenshots, and stale runtime libraries.
configure_file("${CMAKE_CURRENT_LIST_DIR}/InstallWindows.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/InstallWindows.cmake" @ONLY)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/InstallWindows.cmake")

set(CPACK_GENERATOR WIX)
set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_PACKAGE_NAME KPlayer)
set(CPACK_PACKAGE_VENDOR KPlayer)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt Quick media player powered by libmpv")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY KPlayer)
set(CPACK_PACKAGE_FILE_NAME "KPlayer-${PROJECT_VERSION}-windows-x64")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/msi")
set(CPACK_PACKAGE_EXECUTABLES KPlayer KPlayer)
set(CPACK_WIX_VERSION 4)
set(CPACK_WIX_ARCHITECTURE x64)
set(CPACK_WIX_INSTALL_SCOPE perMachine)
set(CPACK_WIX_PROGRAM_MENU_FOLDER KPlayer)
# Keep this identity stable across versions so MSI can upgrade existing installs.
set(CPACK_WIX_UPGRADE_GUID "4B93EA75-801F-41DB-BD5D-E658F36D75F5")
set(CPACK_WIX_PRODUCT_ICON "${PROJECT_SOURCE_DIR}/assets/icons/kplayer.ico")
# Use Windows Installer's basic progress UI. The repository has no license
# agreement, so do not display CPack's placeholder license in a setup wizard.
set(CPACK_WIX_TEMPLATE "${PROJECT_SOURCE_DIR}/packaging/windows/installer.wxs.in")
set(CPACK_WIX_SKIP_WIX_UI_EXTENSION ON)
include(CPack)
