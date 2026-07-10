# Optional JSON dependency helpers for agent-memory-cpp (PR #27).
#
# When AGENT_MEMORY_ENABLE_JSON is ON, this module makes the nlohmann/json
# library available. It first tries `find_package(nlohmann_json CONFIG)`.
# When the library is not installed system-wide and the project has network
# access, it falls back to CMake FetchContent pulling a pinned release tag.
# The "vendored" path (in-tree external/nlohmann via submodule) is left to a
# later PR per docs/eval/json-library-choice.md.
#
# The function `agent_memory_provide_json` exports either:
#   * the discovered imported target `nlohmann_json::nlohmann_json`, or
#   * the FetchContent-built imported target `nlohmann_json::nlohmann_json`.
#
# A no-op stub is used when AGENT_MEMORY_ENABLE_JSON is OFF so other code can
# link `agent_memory::json` unconditionally; the loader header still `#error`s.

set(_AGENT_MEMORY_JSON_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(agent_memory_provide_json out_target)
    if(NOT AGENT_MEMORY_ENABLE_JSON)
        # Provide an empty INTERFACE target so consumers can link unconditionally
        # even when the JSON loader is disabled. The loader header still fails
        # to compile in this configuration.
        if(NOT TARGET agent_memory_json)
            add_library(agent_memory_json INTERFACE)
            target_compile_definitions(agent_memory_json INTERFACE
                AGENT_MEMORY_ENABLE_JSON=0
            )
        endif()
        if(NOT TARGET agent_memory::json)
            add_library(agent_memory::json ALIAS agent_memory_json)
        endif()
        set(${out_target} agent_memory_json PARENT_SCOPE)
        message(STATUS
            "[agent-memory] JSON dataset loader disabled (AGENT_MEMORY_ENABLE_JSON=OFF)")
        return()
    endif()

    find_package(nlohmann_json CONFIG QUIET)
    if(NOT nlohmann_json_FOUND)
        include(FetchContent)
        # Pinned single-line tag documented in docs/eval/json-library-choice.md.
        set(_nlohmann_json_tag "v3.11.3")
        set(_nlohmann_json_repo "https://github.com/nlohmann/json.git")
        message(STATUS
            "[agent-memory] nlohmann_json not on system; FetchContent ${_nlohmann_json_tag}")
        FetchContent_Declare(
            nlohmann_json
            GIT_REPOSITORY "${_nlohmann_json_repo}"
            GIT_TAG "${_nlohmann_json_tag}"
            GIT_SHALLOW TRUE
        )
        # The upstream target builds tests/examples by default; silence them.
        set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
        set(JSON_BuildExamples OFF CACHE BOOL "" FORCE)
        set(JSON_Install OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(nlohmann_json)
    endif()

    if(NOT TARGET nlohmann_json::nlohmann_json)
        message(FATAL_ERROR
            "[agent-memory] nlohmann_json was found but the imported target "
            "nlohmann_json::nlohmann_json is missing. Update nlohmann_json "
            "to >= 3.9 or set AGENT_MEMORY_ENABLE_JSON=OFF.")
    endif()

    if(NOT TARGET agent_memory_json)
        add_library(agent_memory_json INTERFACE)
        target_link_libraries(agent_memory_json
            INTERFACE
                nlohmann_json::nlohmann_json
        )
        target_compile_definitions(agent_memory_json INTERFACE
            AGENT_MEMORY_ENABLE_JSON=1
        )
    endif()
    if(NOT TARGET agent_memory::json)
        add_library(agent_memory::json ALIAS agent_memory_json)
    endif()

    set(${out_target} nlohmann_json::nlohmann_json PARENT_SCOPE)
endfunction()
