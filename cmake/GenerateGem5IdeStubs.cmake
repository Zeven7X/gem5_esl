function(gem5_generate_ide_stubs)
    cmake_parse_arguments(GEM5_STUB "" "ROOT_DIR;OUTPUT_DIR" "" ${ARGN})

    if(NOT GEM5_STUB_ROOT_DIR)
        message(FATAL_ERROR "gem5_generate_ide_stubs requires ROOT_DIR")
    endif()

    if(NOT GEM5_STUB_OUTPUT_DIR)
        message(FATAL_ERROR "gem5_generate_ide_stubs requires OUTPUT_DIR")
    endif()

    file(MAKE_DIRECTORY "${GEM5_STUB_OUTPUT_DIR}")

    file(GLOB_RECURSE gem5_scan_files CONFIGURE_DEPENDS
        "${GEM5_STUB_ROOT_DIR}/src/*.hh"
        "${GEM5_STUB_ROOT_DIR}/src/*.h"
        "${GEM5_STUB_ROOT_DIR}/src/*.cc"
        "${GEM5_STUB_ROOT_DIR}/src/*.cpp"
    )

    set(gem5_missing_headers "")
    foreach(gem5_scan_file IN LISTS gem5_scan_files)
        file(STRINGS "${gem5_scan_file}" gem5_include_lines REGEX "^#include \"[^\"]+\"")
        foreach(gem5_include_line IN LISTS gem5_include_lines)
            string(REGEX REPLACE "^#include \"([^\"]+)\".*" "\\1" gem5_header "${gem5_include_line}")

            if(gem5_header MATCHES "^params/"
               OR gem5_header MATCHES "^debug/"
               OR gem5_header MATCHES "^config/"
               OR gem5_header MATCHES "^enums/"
               OR gem5_header MATCHES "^arch/.*/generated/"
               OR gem5_header MATCHES "^arch/.*/gdb-xml/"
               OR gem5_header MATCHES "^arch/gpu_.*")
                if(NOT EXISTS "${GEM5_STUB_ROOT_DIR}/src/${gem5_header}"
                   AND NOT EXISTS "${GEM5_STUB_ROOT_DIR}/include/${gem5_header}"
                   AND NOT EXISTS "${GEM5_STUB_ROOT_DIR}/${gem5_header}"
                   AND NOT EXISTS "${GEM5_STUB_ROOT_DIR}/ext/${gem5_header}")
                    list(APPEND gem5_missing_headers "${gem5_header}")
                endif()
            endif()
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES gem5_missing_headers)

    foreach(gem5_header IN LISTS gem5_missing_headers)
        set(gem5_stub_path "${GEM5_STUB_OUTPUT_DIR}/${gem5_header}")
        get_filename_component(gem5_stub_dir "${gem5_stub_path}" DIRECTORY)
        file(MAKE_DIRECTORY "${gem5_stub_dir}")

        get_filename_component(gem5_stub_name "${gem5_header}" NAME_WE)
        string(MAKE_C_IDENTIFIER "${gem5_stub_name}" gem5_identifier)

        if(gem5_header MATCHES "^params/")
            set(gem5_stub_body
"#pragma once

namespace gem5
{

struct ${gem5_identifier}
{
};

} // namespace gem5
")
        elseif(gem5_header MATCHES "^debug/")
            set(gem5_stub_body
"#pragma once

namespace gem5::debug
{

inline constexpr bool ${gem5_identifier} = false;

} // namespace gem5::debug
")
        elseif(gem5_header MATCHES "^config/")
            set(gem5_stub_body
"#pragma once
")
        elseif(gem5_header MATCHES "^enums/")
            set(gem5_stub_body
"#pragma once

namespace gem5::enums
{

enum class ${gem5_identifier} : int
{
};

} // namespace gem5::enums
")
        else()
            set(gem5_stub_body
"#pragma once
")
        endif()

        file(WRITE "${gem5_stub_path}" "${gem5_stub_body}")
    endforeach()
endfunction()

