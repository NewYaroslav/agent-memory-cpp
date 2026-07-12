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

add_executable(agent_memory_resource_manifest_test
    domain/resource_manifest_test.cpp
)

target_link_libraries(agent_memory_resource_manifest_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(NAME agent_memory_resource_manifest_test COMMAND agent_memory_resource_manifest_test)

add_executable(agent_memory_embedding_contracts_test
    embedding/embedding_contracts_test.cpp
)

target_link_libraries(agent_memory_embedding_contracts_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(NAME agent_memory_embedding_contracts_test COMMAND agent_memory_embedding_contracts_test)

add_executable(agent_memory_evaluation_contracts_test
    eval/evaluation_contracts_test.cpp
)

target_link_libraries(agent_memory_evaluation_contracts_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(NAME agent_memory_evaluation_contracts_test COMMAND agent_memory_evaluation_contracts_test)

add_executable(agent_memory_eval_runner_test
    eval/runner_test.cpp
)

target_link_libraries(agent_memory_eval_runner_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(NAME agent_memory_eval_runner_test COMMAND agent_memory_eval_runner_test)

if(AGENT_MEMORY_ENABLE_JSON)
    add_executable(agent_memory_dataset_loader_test
        eval/loader_test.cpp
    )

    target_link_libraries(agent_memory_dataset_loader_test
        PRIVATE
            agent_memory::agent_memory
            agent_memory::json
    )

    target_include_directories(agent_memory_dataset_loader_test
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/eval/fixtures
    )

    add_test(
        NAME agent_memory_dataset_loader_test
        COMMAND agent_memory_dataset_loader_test
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/eval
    )
endif()

add_executable(agent_memory_vector_index_contracts_test
    index/vector_index_contracts_test.cpp
)

target_link_libraries(agent_memory_vector_index_contracts_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(NAME agent_memory_vector_index_contracts_test COMMAND agent_memory_vector_index_contracts_test)

add_executable(agent_memory_exact_vector_index_test
    index/exact_vector_index_test.cpp
)

target_link_libraries(agent_memory_exact_vector_index_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(NAME agent_memory_exact_vector_index_test COMMAND agent_memory_exact_vector_index_test)

add_executable(agent_memory_retrieval_contracts_test
    retrieval/retrieval_contracts_test.cpp
)

target_link_libraries(agent_memory_retrieval_contracts_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(NAME agent_memory_retrieval_contracts_test COMMAND agent_memory_retrieval_contracts_test)

add_executable(agent_memory_exact_vector_retriever_test
    retrieval/exact_vector_retriever_test.cpp
)

target_link_libraries(agent_memory_exact_vector_retriever_test
    PRIVATE
        agent_memory::agent_memory
        agent_memory::json
)

target_include_directories(agent_memory_exact_vector_retriever_test
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/eval/fixtures
)

add_test(
    NAME agent_memory_exact_vector_retriever_test
    COMMAND agent_memory_exact_vector_retriever_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/retrieval
)

add_executable(agent_memory_exact_lexical_retriever_test
    retrieval/exact_lexical_retriever_test.cpp
)

target_link_libraries(agent_memory_exact_lexical_retriever_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(
    NAME agent_memory_exact_lexical_retriever_test
    COMMAND agent_memory_exact_lexical_retriever_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/retrieval
)

add_executable(agent_memory_resource_indexer_test
    ingestion/resource_indexer_test.cpp
)

target_link_libraries(agent_memory_resource_indexer_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(NAME agent_memory_resource_indexer_test COMMAND agent_memory_resource_indexer_test)

add_executable(agent_memory_tokenizer_contracts_test
    lexical/tokenizer_contracts_test.cpp
)

target_link_libraries(agent_memory_tokenizer_contracts_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(NAME agent_memory_tokenizer_contracts_test COMMAND agent_memory_tokenizer_contracts_test)

add_executable(agent_memory_standard_tokenizer_test
    lexical/standard_tokenizer_test.cpp
)

target_link_libraries(agent_memory_standard_tokenizer_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(NAME agent_memory_standard_tokenizer_test COMMAND agent_memory_standard_tokenizer_test)

add_executable(agent_memory_lexical_value_types_test
    lexical/lexical_value_types_test.cpp
)

target_link_libraries(agent_memory_lexical_value_types_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(NAME agent_memory_lexical_value_types_test COMMAND agent_memory_lexical_value_types_test)

add_executable(agent_memory_token_dictionary_contract_test
    lexical/token_dictionary_contract_test.cpp
)

target_link_libraries(agent_memory_token_dictionary_contract_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(
    NAME agent_memory_token_dictionary_contract_test
    COMMAND agent_memory_token_dictionary_contract_test
)

add_executable(agent_memory_lexical_index_contract_test
    lexical/lexical_index_contract_test.cpp
)

target_link_libraries(agent_memory_lexical_index_contract_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(
    NAME agent_memory_lexical_index_contract_test
    COMMAND agent_memory_lexical_index_contract_test
)

add_executable(agent_memory_exact_lexical_index_test
    lexical/exact_lexical_index_test.cpp
)

target_link_libraries(agent_memory_exact_lexical_index_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(
    NAME agent_memory_exact_lexical_index_test
    COMMAND agent_memory_exact_lexical_index_test
)

add_executable(agent_memory_scaffolding_test
    lexical/scaffolding_test.cpp
)

target_link_libraries(agent_memory_scaffolding_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(
    NAME agent_memory_scaffolding_test
    COMMAND agent_memory_scaffolding_test
)

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

add_executable(agent_memory_resource_manifest_storage_contract_test
    storage/resource_manifest_storage_contract_test.cpp
)

target_link_libraries(agent_memory_resource_manifest_storage_contract_test
    PRIVATE
        agent_memory::agent_memory
)

add_test(
    NAME agent_memory_resource_manifest_storage_contract_test
    COMMAND agent_memory_resource_manifest_storage_contract_test
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

if(AGENT_MEMORY_ENABLE_MDBX)
    add_executable(agent_memory_mdbx_document_storage_test
        infrastructure/mdbx/mdbx_document_storage_test.cpp
    )

    target_link_libraries(agent_memory_mdbx_document_storage_test
        PRIVATE
            agent_memory::agent_memory
    )

    add_test(
        NAME agent_memory_mdbx_document_storage_test
        COMMAND agent_memory_mdbx_document_storage_test
    )

    add_executable(agent_memory_mdbx_resource_manifest_storage_test
        infrastructure/mdbx/mdbx_resource_manifest_storage_test.cpp
    )

    target_link_libraries(agent_memory_mdbx_resource_manifest_storage_test
        PRIVATE
            agent_memory::agent_memory
    )

    add_test(
        NAME agent_memory_mdbx_resource_manifest_storage_test
        COMMAND agent_memory_mdbx_resource_manifest_storage_test
    )
endif()
