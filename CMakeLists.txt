cmake_minimum_required(VERSION 3.20)

project(agent-memory-cpp
    VERSION 0.1.0
    DESCRIPTION "Embedded C++17 memory and retrieval toolkit for AI agents"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(AGENT_MEMORY_TOP_LEVEL OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(AGENT_MEMORY_TOP_LEVEL ON)
endif()

include(cmake/AgentMemoryOptions.cmake)

add_library(agent_memory STATIC
    src/agent_memory/core/LibraryInfo.cpp
)
add_library(agent_memory::agent_memory ALIAS agent_memory)

target_compile_features(agent_memory PUBLIC cxx_std_17)

target_include_directories(agent_memory
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

set_target_properties(agent_memory PROPERTIES
    OUTPUT_NAME agent_memory
)

if(AGENT_MEMORY_ENABLE_WARNINGS)
    if(MSVC)
        target_compile_options(agent_memory PRIVATE /W4 /permissive-)
    else()
        target_compile_options(agent_memory PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endif()

if(AGENT_MEMORY_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(AGENT_MEMORY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
