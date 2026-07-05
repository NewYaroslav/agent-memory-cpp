add_executable(agent_memory_smoke
    smoke/agent_memory_smoke.cpp
)

target_link_libraries(agent_memory_smoke
    PRIVATE
        agent_memory::agent_memory
)

add_test(NAME agent_memory_smoke COMMAND agent_memory_smoke)

add_executable(agent_memory_domain_primitives_test
    domain/domain_primitives_test.cpp
)

target_link_libraries(agent_memory_domain_primitives_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(NAME agent_memory_domain_primitives_test COMMAND agent_memory_domain_primitives_test)

add_executable(agent_memory_embedding_contracts_test
    embedding/embedding_contracts_test.cpp
)

target_link_libraries(agent_memory_embedding_contracts_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(NAME agent_memory_embedding_contracts_test COMMAND agent_memory_embedding_contracts_test)

add_executable(agent_memory_vector_index_contracts_test
    index/vector_index_contracts_test.cpp
)

target_link_libraries(agent_memory_vector_index_contracts_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(NAME agent_memory_vector_index_contracts_test COMMAND agent_memory_vector_index_contracts_test)

add_executable(agent_memory_document_storage_contract_test
    storage/document_storage_contract_test.cpp
)

target_link_libraries(agent_memory_document_storage_contract_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(
    NAME agent_memory_document_storage_contract_test
    COMMAND agent_memory_document_storage_contract_test
)

add_executable(agent_memory_feature_flags_test
    storage/feature_flags_test.cpp
)

target_link_libraries(agent_memory_feature_flags_test
    PRIVATE
        agent_memory::agent_memory
)

target_compile_definitions(agent_memory_feature_flags_test
    PRIVATE
        AGENT_MEMORY_EXPECTED_HAS_MDBX=$<BOOL:${AGENT_MEMORY_ENABLE_MDBX}>
)

add_test(NAME agent_memory_feature_flags_test COMMAND agent_memory_feature_flags_test)
