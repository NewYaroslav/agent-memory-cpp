option(AGENT_MEMORY_BUILD_TESTS "Build agent-memory-cpp tests" ${AGENT_MEMORY_TOP_LEVEL})
option(AGENT_MEMORY_BUILD_EXAMPLES "Build agent-memory-cpp examples" OFF)
option(AGENT_MEMORY_ENABLE_WARNINGS "Enable compiler warnings for agent-memory-cpp" ON)
option(AGENT_MEMORY_ENABLE_MDBX "Enable MDBX-backed storage dependencies" OFF)

set(AGENT_MEMORY_MDBX_CONTAINERS_SOURCE_DIR "" CACHE PATH
    "Path to a local mdbx-containers source tree used when AGENT_MEMORY_ENABLE_MDBX is ON")

set(AGENT_MEMORY_MDBX_DEPS_MODE "AUTO" CACHE STRING
    "MDBX dependency mode forwarded to mdbx-containers when added from source: AUTO|SYSTEM|BUNDLED")
set_property(CACHE AGENT_MEMORY_MDBX_DEPS_MODE PROPERTY STRINGS AUTO SYSTEM BUNDLED)
