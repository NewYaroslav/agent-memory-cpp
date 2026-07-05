#include <agent_memory/AgentMemory.hpp>

#include <iostream>
#include <string_view>

#ifndef AGENT_MEMORY_HAS_MDBX
#   error "AGENT_MEMORY_HAS_MDBX must be defined by the agent_memory target"
#endif

#ifndef AGENT_MEMORY_EXPECTED_HAS_MDBX
#   error "AGENT_MEMORY_EXPECTED_HAS_MDBX must be defined by the test target"
#endif

namespace {

    int fail(std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

} // namespace

int main() {
    constexpr int has_mdbx = AGENT_MEMORY_HAS_MDBX;
    if(has_mdbx != 0 && has_mdbx != 1) {
        return fail("AGENT_MEMORY_HAS_MDBX must be 0 or 1");
    }

    constexpr int expected_has_mdbx = AGENT_MEMORY_EXPECTED_HAS_MDBX;
    if(expected_has_mdbx != 0 && expected_has_mdbx != 1) {
        return fail("AGENT_MEMORY_EXPECTED_HAS_MDBX must be 0 or 1");
    }

    if(has_mdbx != expected_has_mdbx) {
        return fail("AGENT_MEMORY_HAS_MDBX does not match the configured MDBX option");
    }

    return 0;
}
