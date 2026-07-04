#include <agent_memory/AgentMemory.hpp>

#include <iostream>
#include <string_view>

namespace {

    int fail(std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

} // namespace

int main() {
    if(agent_memory::library_name().empty()) {
        return fail("library name must not be empty");
    }

    if(agent_memory::library_description().empty()) {
        return fail("library description must not be empty");
    }

    const auto& version = agent_memory::library_version();
    if(version.major != 0 || version.minor != 1 || version.patch != 0) {
        return fail("unexpected library version");
    }

    return 0;
}
