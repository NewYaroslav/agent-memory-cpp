if(NOT DEFINED AGENT_MEMORY_PYTHON_EXECUTABLE)
    message(FATAL_ERROR "AGENT_MEMORY_PYTHON_EXECUTABLE is required")
endif()
if(NOT DEFINED AGENT_MEMORY_PRECOMPUTED_CONTENT_CONTRACT_VERIFY)
    message(FATAL_ERROR "AGENT_MEMORY_PRECOMPUTED_CONTENT_CONTRACT_VERIFY is required")
endif()
if(NOT DEFINED AGENT_MEMORY_PRECOMPUTED_CONTENT_CONTRACT_FIXTURE)
    message(FATAL_ERROR "AGENT_MEMORY_PRECOMPUTED_CONTENT_CONTRACT_FIXTURE is required")
endif()
if(NOT DEFINED AGENT_MEMORY_PRECOMPUTED_CONTENT_CONTRACT_MODULE)
    message(FATAL_ERROR "AGENT_MEMORY_PRECOMPUTED_CONTENT_CONTRACT_MODULE is required")
endif()
if(NOT DEFINED AGENT_MEMORY_PRECOMPUTED_CONTENT_CONTRACT_NEGATIVE_WORKDIR)
    message(FATAL_ERROR
        "AGENT_MEMORY_PRECOMPUTED_CONTENT_CONTRACT_NEGATIVE_WORKDIR is required")
endif()

file(READ "${AGENT_MEMORY_PRECOMPUTED_CONTENT_CONTRACT_FIXTURE}" fixture_json)
file(MAKE_DIRECTORY "${AGENT_MEMORY_PRECOMPUTED_CONTENT_CONTRACT_NEGATIVE_WORKDIR}")

function(write_mutated_fixture file_name needle replacement)
    string(REPLACE "${needle}" "${replacement}" mutated_json "${fixture_json}")
    if(mutated_json STREQUAL fixture_json)
        message(FATAL_ERROR "content-contract mutation did not change ${file_name}")
    endif()
    set(mutated_path
        "${AGENT_MEMORY_PRECOMPUTED_CONTENT_CONTRACT_NEGATIVE_WORKDIR}/${file_name}"
    )
    file(WRITE "${mutated_path}" "${mutated_json}")
    set(mutated_path "${mutated_path}" PARENT_SCOPE)
endfunction()

function(expect_content_contract_failure case_name fixture_path expected_stderr)
    execute_process(
        COMMAND
            "${AGENT_MEMORY_PYTHON_EXECUTABLE}"
            "${AGENT_MEMORY_PRECOMPUTED_CONTENT_CONTRACT_VERIFY}"
            --fixture
            "${fixture_path}"
            --contract
            "${AGENT_MEMORY_PRECOMPUTED_CONTENT_CONTRACT_MODULE}"
        RESULT_VARIABLE verifier_result
        OUTPUT_VARIABLE verifier_stdout
        ERROR_VARIABLE verifier_stderr
    )
    if(verifier_result EQUAL 0)
        message(FATAL_ERROR
            "content-contract verifier unexpectedly accepted ${case_name}: ${fixture_path}"
        )
    endif()
    string(FIND "${verifier_stderr}" "${expected_stderr}" expected_offset)
    if(expected_offset EQUAL -1)
        message(FATAL_ERROR
            "content-contract verifier rejected ${case_name} for the wrong reason.\n"
            "Expected stderr to contain: ${expected_stderr}\n"
            "Actual stdout: ${verifier_stdout}\n"
            "Actual stderr: ${verifier_stderr}"
        )
    endif()
endfunction()

write_mutated_fixture(
    "mutated-corpus.json"
    "\"text\": \"Binary signatures scan a compact Hamming space first and then over-fetch candidates for exact vector reranking.\""
    "\"text\": \"Binary signatures scan a mutated Hamming space first and then over-fetch candidates for exact vector reranking.\""
)
expect_content_contract_failure(
    "mutated corpus"
    "${mutated_path}"
    "fixture corpus does not match content contract"
)

write_mutated_fixture(
    "mutated-query.json"
    "\"text\": \"binary hamming candidates with exact rerank\""
    "\"text\": \"binary hamming candidates with mutated exact rerank\""
)
expect_content_contract_failure(
    "mutated query"
    "${mutated_path}"
    "fixture queries does not match content contract"
)

write_mutated_fixture(
    "mutated-judgment.json"
    "\"query_id\": \"q:binary-rerank\",\n      \"item_id\": \"doc:binary-candidate-filter\",\n      \"relevance_grade\": 3"
    "\"query_id\": \"q:binary-rerank\",\n      \"item_id\": \"doc:binary-candidate-filter\",\n      \"relevance_grade\": 2"
)
expect_content_contract_failure(
    "mutated judgment"
    "${mutated_path}"
    "fixture judgments does not match content contract"
)
