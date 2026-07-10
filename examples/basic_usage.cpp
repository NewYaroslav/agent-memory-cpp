#include <agent_memory.hpp>

#include <iostream>

int main() {
    const auto& version = agent_memory::library_version();

    std::cout
        << agent_memory::library_name()
        << " "
        << version.major
        << "."
        << version.minor
        << "."
        << version.patch
        << '\n';

    return 0;
}
