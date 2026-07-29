# SPDX-FileCopyrightText: 2026 Weikang Wang
# SPDX-License-Identifier: GPL-3.0-or-later

if(NOT DEFINED MDSSCOPE_SOURCE_DIR)
    message(FATAL_ERROR "MDSSCOPE_SOURCE_DIR is required")
endif()

function(check_forbidden_includes module_dir forbidden_pattern)
    file(GLOB_RECURSE module_files
        "${MDSSCOPE_SOURCE_DIR}/${module_dir}/*.cpp"
        "${MDSSCOPE_SOURCE_DIR}/${module_dir}/*.hpp"
    )
    foreach(module_file IN LISTS module_files)
        file(STRINGS "${module_file}" include_lines
            REGEX "^[ \t]*#[ \t]*include[ \t]*\"")
        foreach(include_line IN LISTS include_lines)
            if(include_line MATCHES "${forbidden_pattern}")
                file(RELATIVE_PATH relative_file
                    "${MDSSCOPE_SOURCE_DIR}"
                    "${module_file}")
                message(FATAL_ERROR
                    "${relative_file} violates its module boundary: ${include_line}")
            endif()
        endforeach()
    endforeach()
endfunction()

check_forbidden_includes(
    "src/core"
    "#[ \t]*include[ \t]*\"(mds|services|ssh|ui)/"
)
check_forbidden_includes(
    "src/mds"
    "#[ \t]*include[ \t]*\"(services|ssh|ui)/"
)
check_forbidden_includes(
    "src/ssh"
    "#[ \t]*include[ \t]*\"(services|ui)/"
)
check_forbidden_includes(
    "src/services"
    "#[ \t]*include[ \t]*\"(services|ssh|ui)/"
)
check_forbidden_includes(
    "src/ui/plot"
    "#[ \t]*include[ \t]*\"(mds|services|ssh|ui/main_window)/"
)

file(GLOB_RECURSE production_files
    "${MDSSCOPE_SOURCE_DIR}/src/*.cpp"
    "${MDSSCOPE_SOURCE_DIR}/src/*.hpp"
)
foreach(production_file IN LISTS production_files)
    file(READ "${production_file}" production_contents)
    file(RELATIVE_PATH relative_file
        "${MDSSCOPE_SOURCE_DIR}"
        "${production_file}")
    if(production_contents MATCHES "mdsscope_internal\\.hpp")
        message(FATAL_ERROR
            "${relative_file} reintroduces the removed omnibus header")
    endif()
    if(NOT relative_file MATCHES "^src/mds/"
       AND production_contents MATCHES "mds/internal/")
        message(FATAL_ERROR
            "${relative_file} depends on private MDS implementation details")
    endif()
endforeach()
