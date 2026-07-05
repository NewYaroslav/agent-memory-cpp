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
include(cmake/AgentMemoryDependencies.cmake)

add_library(agent_memory STATIC
    src/agent_memory/core/LibraryInfo.cpp
    src/agent_memory/domain/Document.cpp
    src/agent_memory/domain/Identifiers.cpp
    src/agent_memory/domain/Metadata.cpp
    src/agent_memory/domain/MetadataFilter.cpp
    src/agent_memory/domain/Resource.cpp
    src/agent_memory/domain/SourceKind.cpp
    src/agent_memory/embedding/Embedding.cpp
    src/agent_memory/embedding/IEmbedder.cpp
    src/agent_memory/ingestion/ResourceIndexer.cpp
    src/agent_memory/index/ExactVectorIndex.cpp
    src/agent_memory/index/IVectorIndex.cpp
    src/agent_memory/index/VectorIndex.cpp
    src/agent_memory/lexical/ITokenDictionary.cpp
    src/agent_memory/lexical/ITokenizer.cpp
    src/agent_memory/lexical/Lexical.cpp
    src/agent_memory/lexical/StandardTokenizer.cpp
    src/agent_memory/lexical/TokenDictionary.cpp
    src/agent_memory/lexical/Tokenizer.cpp
    src/agent_memory/retrieval/IRetriever.cpp
    src/agent_memory/retrieval/Retrieval.cpp
    src/agent_memory/storage/IDocumentStorage.cpp
    src/agent_memory/storage/IResourceManifestStorage.cpp
)
add_library(agent_memory::agent_memory ALIAS agent_memory)

target_compile_features(agent_memory PUBLIC cxx_std_17)

target_include_directories(agent_memory
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:include>
)

target_compile_definitions(agent_memory
    PUBLIC
        AGENT_MEMORY_HAS_MDBX=$<BOOL:${AGENT_MEMORY_ENABLE_MDBX}>
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

if(AGENT_MEMORY_ENABLE_MDBX)
    target_sources(agent_memory
        PRIVATE
            src/agent_memory/infrastructure/mdbx/MdbxDocumentStorage.cpp
            src/agent_memory/infrastructure/mdbx/MdbxResourceManifestStorage.cpp
    )

    agent_memory_provide_mdbx_containers(AGENT_MEMORY_MDBX_CONTAINERS_TARGET)
    target_link_libraries(agent_memory
        PRIVATE
            ${AGENT_MEMORY_MDBX_CONTAINERS_TARGET}
    )
endif()

if(AGENT_MEMORY_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(AGENT_MEMORY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
