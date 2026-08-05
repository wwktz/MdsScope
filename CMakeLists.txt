# SPDX-FileCopyrightText: 2026 Weikang Wang
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

project(MdsScope VERSION 5.14.2 LANGUAGES CXX)

execute_process(
    COMMAND git rev-parse --short=9 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE MDS_SCOPE_GIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
execute_process(
    COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE MDS_SCOPE_GIT_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE MDS_SCOPE_GIT_TAG_RESULT
    ERROR_QUIET
)
if(MDS_SCOPE_GIT_TAG_RESULT EQUAL 0 AND MDS_SCOPE_GIT_TAG)
    execute_process(
        COMMAND git rev-list --count "${MDS_SCOPE_GIT_TAG}..HEAD"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE MDS_SCOPE_GIT_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
else()
    execute_process(
        COMMAND git rev-list --count HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE MDS_SCOPE_GIT_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
execute_process(
    COMMAND git status --porcelain
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE MDS_SCOPE_GIT_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(MDS_SCOPE_GIT_HASH)
    set(MDS_SCOPE_GIT_VERSION "${PROJECT_VERSION}.r${MDS_SCOPE_GIT_REVISION}.g${MDS_SCOPE_GIT_HASH}")
    if(MDS_SCOPE_GIT_STATUS)
        string(APPEND MDS_SCOPE_GIT_VERSION ".dirty")
    endif()
else()
    set(MDS_SCOPE_GIT_VERSION "unknown")
endif()

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT AND UNIX)
    if("$ENV{USER}" STREQUAL "root" OR DEFINED ENV{SUDO_USER})
        set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Install prefix" FORCE)
    elseif(DEFINED ENV{HOME})
        set(CMAKE_INSTALL_PREFIX "$ENV{HOME}/.local" CACHE PATH "Install prefix" FORCE)
    endif()
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Widgets Network Concurrent)
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.4 REQUIRED COMPONENTS DBus)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1)
endif()

option(MDS_SCOPE_DEPLOY_RUNTIME "Deploy Qt runtime dependencies during installation" OFF)
option(MDS_SCOPE_BUILD_TESTS "Build MdsScope internal tests" OFF)
option(MDS_SCOPE_NATIVE_CREDENTIAL_TESTS
    "Run isolated native credential-store integration tests"
    OFF
)
option(MDS_SCOPE_STRICT_COMPILE "Treat project compiler warnings as errors" OFF)
set(MDS_SCOPE_RPM_RELEASE "1" CACHE STRING "RPM release suffix")

function(mdsscope_configure_target target)
    if(MDS_SCOPE_STRICT_COMPILE)
        if(MSVC)
            target_compile_options(${target} PRIVATE
                /W4
                /WX
                /permissive-
                /Zc:__cplusplus
            )
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(${target} PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
                -Wshadow
                -Werror
            )
        endif()
    endif()
endfunction()

configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
    @ONLY
)

add_custom_target(uninstall
    COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
)

set(MDSSCOPE_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/src")

set(MDSSCOPE_CREDENTIAL_STORE_SOURCE
    src/core/credential_store_unavailable.cpp
)
if(WIN32)
    set(MDSSCOPE_CREDENTIAL_STORE_SOURCE
        src/core/credential_store_windows.cpp
    )
elseif(APPLE)
    set(MDSSCOPE_CREDENTIAL_STORE_SOURCE
        src/core/credential_store_macos.cpp
    )
elseif(UNIX)
    set(MDSSCOPE_CREDENTIAL_STORE_SOURCE
        src/core/credential_store_linux.cpp
    )
endif()

add_library(mdsscope_core STATIC
    src/core/api_auth.cpp
    src/core/internal/auth_payload.cpp
    src/core/internal/legacy_auth_store.cpp
    ${MDSSCOPE_CREDENTIAL_STORE_SOURCE}
    src/core/environment_io.cpp
    src/core/mds_helpers.cpp
    src/core/app_paths.cpp
    src/core/series_style.cpp
    src/core/text_utils.cpp
    src/core/shot_metadata_client.cpp
)
mdsscope_configure_target(mdsscope_core)
target_include_directories(mdsscope_core PUBLIC
    "${MDSSCOPE_INCLUDE_DIR}"
)
target_link_libraries(mdsscope_core PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Network
)
if(WIN32)
    target_link_libraries(mdsscope_core PRIVATE Advapi32)
elseif(APPLE)
    find_library(MDS_SCOPE_SECURITY_FRAMEWORK Security)
    if(NOT MDS_SCOPE_SECURITY_FRAMEWORK)
        message(FATAL_ERROR
            "The macOS Security framework was not found"
        )
    endif()
    target_link_libraries(mdsscope_core PRIVATE
        "${MDS_SCOPE_SECURITY_FRAMEWORK}"
    )
elseif(UNIX)
    target_link_libraries(mdsscope_core PRIVATE
        PkgConfig::LIBSECRET
    )
endif()

add_library(mdsscope_mds STATIC
    src/mds/mds_client.cpp
    src/mds/mds_ip_client.cpp
    src/mds/mds_ip_east.cpp
    src/mds/mds_ip_fetch.cpp
    src/mds/mds_ip_protocol.cpp
    src/mds/mds_ip_signal_fetch.cpp
    src/mds/mds_ip_series.cpp
)
mdsscope_configure_target(mdsscope_mds)
target_include_directories(mdsscope_mds PUBLIC
    "${MDSSCOPE_INCLUDE_DIR}"
)
target_link_libraries(mdsscope_mds PUBLIC
    mdsscope_core
)
target_link_libraries(mdsscope_mds PRIVATE
    Qt6::Concurrent
    Qt6::Network
)

add_library(mdsscope_ssh STATIC
    src/ssh/ssh_tunnel_manager.hpp
    src/ssh/ssh_tunnel_manager.cpp
    src/ssh/ssh_diagnostic.cpp
)
mdsscope_configure_target(mdsscope_ssh)
target_include_directories(mdsscope_ssh PUBLIC
    "${MDSSCOPE_INCLUDE_DIR}"
)
target_link_libraries(mdsscope_ssh PUBLIC
    mdsscope_core
)
target_link_libraries(mdsscope_ssh PRIVATE
    mdsscope_mds
    Qt6::Network
)

add_library(mdsscope_plot STATIC
    src/ui/plot/plot_widget.hpp
    src/ui/plot/helpers.cpp
    src/ui/plot/events.cpp
    src/ui/plot/readout.cpp
    src/ui/plot/render.cpp
    src/ui/plot/state.cpp
    src/ui/plot/view.cpp
)
mdsscope_configure_target(mdsscope_plot)
target_include_directories(mdsscope_plot PUBLIC
    "${MDSSCOPE_INCLUDE_DIR}"
)
target_link_libraries(mdsscope_plot PUBLIC
    mdsscope_core
    Qt6::Widgets
)

add_library(mdsscope_services STATIC
    src/services/data_export_service.cpp
)
mdsscope_configure_target(mdsscope_services)
target_include_directories(mdsscope_services PUBLIC
    "${MDSSCOPE_INCLUDE_DIR}"
)
target_link_libraries(mdsscope_services PUBLIC
    mdsscope_core
)
target_link_libraries(mdsscope_services PRIVATE
    mdsscope_mds
)

add_library(mdsscope_ui_support STATIC
    src/ui/visuals.cpp
    src/ui/login_dialog.cpp
    src/ui/ssh_dialog.cpp
)
mdsscope_configure_target(mdsscope_ui_support)
target_include_directories(mdsscope_ui_support PUBLIC
    "${MDSSCOPE_INCLUDE_DIR}"
)
target_link_libraries(mdsscope_ui_support PUBLIC
    mdsscope_core
    mdsscope_ssh
    Qt6::Widgets
)
target_link_libraries(mdsscope_ui_support PRIVATE
    Qt6::Concurrent
    Qt6::Network
)

add_library(mdsscope_ui STATIC
    src/ui/main_window/main_window.hpp
    src/ui/main_window/about_dialog.cpp
    src/ui/main_window/window.cpp
    src/ui/main_window/data.cpp
    src/ui/main_window/environment.cpp
    src/ui/main_window/export.cpp
    src/ui/main_window/export_dialog.cpp
    src/ui/main_window/layout.cpp
    src/ui/main_window/layout_dialog.cpp
    src/ui/main_window/refresh_coordinator.cpp
    src/ui/main_window/scale.cpp
    src/ui/main_window/shortcut_dialog.cpp
    src/ui/main_window/shortcut_settings.cpp
    src/ui/main_window/signal_dialogs.cpp
    src/ui/main_window/user_preferences.cpp
    src/ui/main_window/shortcuts.cpp
    src/ui/main_window/shared.cpp
    src/ui/main_window/shot.cpp
    src/ui/main_window/shot_workflow.cpp
    src/ui/main_window/ssh.cpp
    src/ui/main_window/theme.cpp
    src/ui/main_window/web_access.cpp
    src/ui/main_window/web_bookmarks.cpp
    src/ui/main_window/ui.cpp
)
mdsscope_configure_target(mdsscope_ui)
target_include_directories(mdsscope_ui PUBLIC
    "${MDSSCOPE_INCLUDE_DIR}"
)
target_compile_definitions(mdsscope_ui PRIVATE
    MDSSCOPE_VERSION="${PROJECT_VERSION}"
    MDSSCOPE_GIT_VERSION="${MDS_SCOPE_GIT_VERSION}"
)
target_link_libraries(mdsscope_ui PUBLIC
    mdsscope_core
    mdsscope_ui_support
    Qt6::Widgets
)
target_link_libraries(mdsscope_ui PRIVATE
    mdsscope_mds
    mdsscope_ssh
    mdsscope_plot
    mdsscope_services
    Qt6::Network
    Qt6::Concurrent
)

add_executable(MdsScope
    src/main.cpp
    src/app/app_runtime.cpp
    src/app/benchmark.cpp
    src/app/login_bootstrap.cpp
    src/app/platform_integration.cpp
    src/app/runtime_paths.cpp
    src/app/theme_runtime.cpp
)
mdsscope_configure_target(MdsScope)
target_include_directories(MdsScope PRIVATE
    "${MDSSCOPE_INCLUDE_DIR}"
)
target_compile_definitions(MdsScope PRIVATE
    MDSSCOPE_VERSION="${PROJECT_VERSION}"
    MDSSCOPE_GIT_VERSION="${MDS_SCOPE_GIT_VERSION}"
)
target_link_libraries(MdsScope PRIVATE
    mdsscope_ui
    mdsscope_core
    mdsscope_mds
    mdsscope_ssh
    mdsscope_ui_support
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
)

add_executable(transfer
    src/transfer.cpp
)
mdsscope_configure_target(transfer)
target_include_directories(transfer PRIVATE
    "${MDSSCOPE_INCLUDE_DIR}"
)
target_link_libraries(transfer PRIVATE
    mdsscope_core
)

set_target_properties(transfer PROPERTIES AUTOMOC OFF)
set_target_properties(MdsScope PROPERTIES WIN32_EXECUTABLE TRUE)

if(WIN32)
    target_compile_definitions(MdsScope PRIVATE NOMINMAX)
    set(MDS_SCOPE_WINDOWS_ICON "${PROJECT_SOURCE_DIR}/packaging/windows/mdsscope.ico")
    configure_file(
        "${PROJECT_SOURCE_DIR}/packaging/windows/mdsscope.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/mdsscope.rc"
        @ONLY
    )
    target_sources(MdsScope PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/mdsscope.rc"
    )
    target_link_libraries(MdsScope PRIVATE Shell32 Propsys Ole32 Uuid)
endif()

if(APPLE)
    file(GLOB MDS_SCOPE_SOURCE_INDEX_SIGNALS CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/resources/source_index/signals/*.txt"
    )
    set(MDS_SCOPE_MACOS_ICON "${PROJECT_SOURCE_DIR}/packaging/macos/mdsscope.icns")
    set(MDS_SCOPE_MACOS_ENVIRONMENT_FILES
        "${PROJECT_SOURCE_DIR}/resources/environment/init.toml"
        "${PROJECT_SOURCE_DIR}/resources/environment/init.webscp"
    )
    set(MDS_SCOPE_MACOS_RESOURCE_FILES
        "${MDS_SCOPE_MACOS_ICON}"
        "${PROJECT_SOURCE_DIR}/resources/APIurl"
        "${PROJECT_SOURCE_DIR}/resources/source_index/trees.txt"
        ${MDS_SCOPE_MACOS_ENVIRONMENT_FILES}
        ${MDS_SCOPE_SOURCE_INDEX_SIGNALS}
    )
    target_sources(MdsScope PRIVATE ${MDS_SCOPE_MACOS_RESOURCE_FILES})
    set_source_files_properties("${MDS_SCOPE_MACOS_ICON}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    set_source_files_properties("${PROJECT_SOURCE_DIR}/resources/APIurl" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    set_source_files_properties("${PROJECT_SOURCE_DIR}/resources/source_index/trees.txt" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/source_index"
    )
    set_source_files_properties(${MDS_SCOPE_MACOS_ENVIRONMENT_FILES} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/environment"
    )
    set_source_files_properties(${MDS_SCOPE_SOURCE_INDEX_SIGNALS} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources/source_index/signals"
    )
    set_target_properties(MdsScope PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "MdsScope"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.mdsscope.MdsScope"
        MACOSX_BUNDLE_ICON_FILE "mdsscope.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${MDS_SCOPE_GIT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright 2026 Weikang Wang"
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
endif()

if(MDS_SCOPE_BUILD_TESTS)
    enable_testing()
    add_test(
        NAME module_boundaries
        COMMAND "${CMAKE_COMMAND}"
            "-DMDSSCOPE_SOURCE_DIR=${PROJECT_SOURCE_DIR}"
            -P "${PROJECT_SOURCE_DIR}/cmake/check_module_boundaries.cmake"
    )

    add_executable(refresh_coordinator_test
        tests/refresh_coordinator_test.cpp
        src/ui/main_window/refresh_coordinator.cpp
    )
    mdsscope_configure_target(refresh_coordinator_test)
    set_target_properties(refresh_coordinator_test PROPERTIES AUTOMOC OFF)
    target_include_directories(refresh_coordinator_test PRIVATE src)
    target_link_libraries(refresh_coordinator_test PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Concurrent
    )
    add_test(NAME refresh_coordinator COMMAND refresh_coordinator_test)

    add_executable(plot_helpers_test
        tests/plot_helpers_test.cpp
    )
    mdsscope_configure_target(plot_helpers_test)
    set_target_properties(plot_helpers_test PROPERTIES AUTOMOC OFF)
    target_include_directories(plot_helpers_test PRIVATE src)
    target_link_libraries(plot_helpers_test PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::Network
        mdsscope_plot
    )
    add_test(NAME plot_helpers COMMAND plot_helpers_test)
    set_tests_properties(plot_helpers PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(environment_io_test
        tests/environment_io_test.cpp
    )
    mdsscope_configure_target(environment_io_test)
    set_target_properties(environment_io_test PROPERTIES AUTOMOC OFF)
    target_include_directories(environment_io_test PRIVATE src)
    target_link_libraries(environment_io_test PRIVATE
        mdsscope_core
        Qt6::Core
    )
    add_test(NAME environment_io COMMAND environment_io_test)

    add_executable(shortcut_settings_test
        tests/shortcut_settings_test.cpp
        src/ui/main_window/shortcut_settings.cpp
    )
    mdsscope_configure_target(shortcut_settings_test)
    set_target_properties(shortcut_settings_test PROPERTIES AUTOMOC OFF)
    target_include_directories(shortcut_settings_test PRIVATE src)
    target_link_libraries(shortcut_settings_test PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::Network
        mdsscope_core
    )
    add_test(NAME shortcut_settings COMMAND shortcut_settings_test)

    add_executable(shortcut_dispatch_test
        tests/shortcut_dispatch_test.cpp
        src/app/theme_runtime.cpp
    )
    mdsscope_configure_target(shortcut_dispatch_test)
    target_include_directories(shortcut_dispatch_test PRIVATE src)
    target_link_libraries(shortcut_dispatch_test PRIVATE
        mdsscope_ui
        mdsscope_plot
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::Network
    )
    if(UNIX AND NOT APPLE)
        target_link_libraries(shortcut_dispatch_test PRIVATE Qt6::DBus)
    endif()
    add_test(NAME shortcut_dispatch COMMAND shortcut_dispatch_test)
    set_tests_properties(shortcut_dispatch PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(shot_workflow_test
        tests/shot_workflow_test.cpp
        src/ui/main_window/shot_workflow.cpp
    )
    mdsscope_configure_target(shot_workflow_test)
    set_target_properties(shot_workflow_test PROPERTIES AUTOMOC OFF)
    target_include_directories(shot_workflow_test PRIVATE src)
    target_link_libraries(shot_workflow_test PRIVATE
        mdsscope_core
        Qt6::Core
    )
    add_test(NAME shot_workflow COMMAND shot_workflow_test)

    add_executable(auth_payload_test
        tests/auth_payload_test.cpp
    )
    mdsscope_configure_target(auth_payload_test)
    set_target_properties(auth_payload_test PROPERTIES
        AUTOMOC OFF
    )
    target_include_directories(auth_payload_test PRIVATE src)
    target_link_libraries(auth_payload_test PRIVATE
        mdsscope_core
        Qt6::Core
    )
    add_test(NAME auth_payload COMMAND auth_payload_test)

    if(MDS_SCOPE_NATIVE_CREDENTIAL_TESTS)
        add_executable(credential_store_integration_test
            tests/credential_store_integration_test.cpp
        )
        mdsscope_configure_target(
            credential_store_integration_test
        )
        set_target_properties(
            credential_store_integration_test
            PROPERTIES AUTOMOC OFF
        )
        target_include_directories(
            credential_store_integration_test
            PRIVATE src
        )
        target_link_libraries(
            credential_store_integration_test
            PRIVATE mdsscope_core Qt6::Core
        )
        add_test(
            NAME credential_store_integration
            COMMAND credential_store_integration_test
        )
        set_tests_properties(
            credential_store_integration
            PROPERTIES ENVIRONMENT
                "MDSSCOPE_CREDENTIAL_PROFILE=mdsscope-test-native-credential-store"
        )
    endif()
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(MdsScope PRIVATE Qt6::DBus)
endif()

if(APPLE)
    install(TARGETS MdsScope
        BUNDLE DESTINATION .
        COMPONENT app
    )
    install(PROGRAMS "$<TARGET_FILE:MdsScope>"
        DESTINATION ${CMAKE_INSTALL_BINDIR}
        RENAME MdsScope
        COMPONENT tools
    )
    install(TARGETS transfer
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT tools
    )

    install(FILES
        "${PROJECT_SOURCE_DIR}/resources/environment/init.toml"
        "${PROJECT_SOURCE_DIR}/resources/environment/init.webscp"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/environment
        COMPONENT tools
    )

    install(FILES
        "${PROJECT_SOURCE_DIR}/resources/APIurl"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope
        COMPONENT tools
    )

    install(FILES
        "${PROJECT_SOURCE_DIR}/resources/source_index/trees.txt"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/source_index
        COMPONENT tools
    )

    install(DIRECTORY
        "${PROJECT_SOURCE_DIR}/resources/source_index/signals"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/source_index
        COMPONENT tools
        FILES_MATCHING PATTERN "*.txt"
    )
else()
    install(TARGETS MdsScope transfer
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        BUNDLE DESTINATION .
    )

    install(FILES
        "${PROJECT_SOURCE_DIR}/resources/environment/init.toml"
        "${PROJECT_SOURCE_DIR}/resources/environment/init.webscp"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/environment
    )

    install(FILES
        "${PROJECT_SOURCE_DIR}/resources/APIurl"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope
    )

    if(UNIX)
        install(FILES
            "${PROJECT_SOURCE_DIR}/packaging/linux/mdsscope.svg"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        )

        install(FILES
            "${PROJECT_SOURCE_DIR}/packaging/linux/mdsscope.desktop"
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
        )
    endif()

    install(FILES
        "${PROJECT_SOURCE_DIR}/resources/source_index/trees.txt"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/source_index
    )

    install(DIRECTORY
        "${PROJECT_SOURCE_DIR}/resources/source_index/signals"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mdsscope/source_index
        FILES_MATCHING PATTERN "*.txt"
    )
endif()

if(MDS_SCOPE_DEPLOY_RUNTIME AND APPLE)
    if(Qt6_VERSION VERSION_LESS 6.5)
        qt_generate_deploy_app_script(
            TARGET MdsScope
            FILENAME_VARIABLE MDS_SCOPE_DEPLOY_SCRIPT
            NO_TRANSLATIONS
            NO_UNSUPPORTED_PLATFORM_ERROR
        )
    else()
        qt_generate_deploy_app_script(
            TARGET MdsScope
            OUTPUT_SCRIPT MDS_SCOPE_DEPLOY_SCRIPT
            NO_TRANSLATIONS
            NO_UNSUPPORTED_PLATFORM_ERROR
        )
    endif()
    install(SCRIPT "${MDS_SCOPE_DEPLOY_SCRIPT}" COMPONENT app)
endif()

set(CPACK_PACKAGE_NAME "mdsscope")
set(CPACK_PACKAGE_VENDOR "MdsScope")
set(CPACK_PACKAGE_CONTACT "MdsScope Maintainers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt desktop application for plotting MDSplus signal data")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_BINARY_DIR}")

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "MdsScope Maintainers")
set(CPACK_DEBIAN_PACKAGE_SECTION "science")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)

set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Engineering")
set(CPACK_RPM_PACKAGE_RELEASE "${MDS_SCOPE_RPM_RELEASE}")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)

include(CPack)
