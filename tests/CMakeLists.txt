add_executable(agent_memory_smoke
    smoke/agent_memory_smoke.cpp
)

target_link_libraries(agent_memory_smoke
    PRIVATE
        agent_memory::agent_memory
)

add_test(NAME agent_memory_smoke COMMAND agent_memory_smoke)
