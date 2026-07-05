# Dependency helpers for agent-memory-cpp.

set(_AGENT_MEMORY_DEPENDENCIES_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(agent_memory_provide_mdbx_containers out_target)
    if(NOT AGENT_MEMORY_MDBX_DEPS_MODE MATCHES "^(AUTO|SYSTEM|BUNDLED)$")
        message(FATAL_ERROR
            "AGENT_MEMORY_MDBX_DEPS_MODE must be AUTO, SYSTEM, or BUNDLED; "
            "got '${AGENT_MEMORY_MDBX_DEPS_MODE}'"
        )
    endif()

    if(TARGET mdbx_containers::mdbx_containers)
        set(${out_target} mdbx_containers::mdbx_containers PARENT_SCOPE)
        return()
    endif()

    get_filename_component(_agent_memory_root "${_AGENT_MEMORY_DEPENDENCIES_DIR}/.." ABSOLUTE)
    set(_mdbx_containers_source_dir "${AGENT_MEMORY_MDBX_CONTAINERS_SOURCE_DIR}")

    if("${_mdbx_containers_source_dir}" STREQUAL "")
        set(_default_source_dir "${_agent_memory_root}/external/mdbx-containers")
        if(EXISTS "${_default_source_dir}/CMakeLists.txt")
            set(_mdbx_containers_source_dir "${_default_source_dir}")
        endif()
    endif()

    if(NOT "${_mdbx_containers_source_dir}" STREQUAL "")
        if(NOT EXISTS "${_mdbx_containers_source_dir}/CMakeLists.txt")
            message(FATAL_ERROR
                "AGENT_MEMORY_MDBX_CONTAINERS_SOURCE_DIR does not contain CMakeLists.txt: "
                "${_mdbx_containers_source_dir}"
            )
        endif()

        if(NOT DEFINED MDBXC_BUILD_TESTS)
            set(MDBXC_BUILD_TESTS OFF CACHE BOOL "Build mdbx-containers tests")
        endif()
        if(NOT DEFINED MDBXC_BUILD_EXAMPLES)
            set(MDBXC_BUILD_EXAMPLES OFF CACHE BOOL "Build mdbx-containers examples")
        endif()
        if(NOT DEFINED MDBXC_USE_ASAN)
            set(MDBXC_USE_ASAN OFF CACHE BOOL "Enable mdbx-containers AddressSanitizer")
        endif()
        if(NOT DEFINED MDBXC_DEPS_MODE)
            set(MDBXC_DEPS_MODE "${AGENT_MEMORY_MDBX_DEPS_MODE}" CACHE STRING
                "Dependency mode for MDBX: AUTO|SYSTEM|BUNDLED")
            set_property(CACHE MDBXC_DEPS_MODE PROPERTY STRINGS AUTO SYSTEM BUNDLED)
        endif()

        add_subdirectory(
            "${_mdbx_containers_source_dir}"
            "${CMAKE_BINARY_DIR}/_deps/mdbx-containers"
            EXCLUDE_FROM_ALL
        )
    else()
        find_package(mdbx_containers CONFIG REQUIRED)
    endif()

    if(NOT TARGET mdbx_containers::mdbx_containers)
        message(FATAL_ERROR
            "mdbx-containers was found, but target mdbx_containers::mdbx_containers is missing"
        )
    endif()

    set(${out_target} mdbx_containers::mdbx_containers PARENT_SCOPE)
endfunction()
