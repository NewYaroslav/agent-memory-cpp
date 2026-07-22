if(NOT DEFINED AGENT_MEMORY_PRECOMPUTED_ARTIFACT_VERIFY_EXE)
    message(FATAL_ERROR "AGENT_MEMORY_PRECOMPUTED_ARTIFACT_VERIFY_EXE is required")
endif()
if(NOT DEFINED AGENT_MEMORY_PRECOMPUTED_EMBEDDING_DATASET)
    message(FATAL_ERROR "AGENT_MEMORY_PRECOMPUTED_EMBEDDING_DATASET is required")
endif()
if(NOT DEFINED AGENT_MEMORY_PRECOMPUTED_EMBEDDING_NEGATIVE_WORKDIR)
    message(FATAL_ERROR "AGENT_MEMORY_PRECOMPUTED_EMBEDDING_NEGATIVE_WORKDIR is required")
endif()

file(READ "${AGENT_MEMORY_PRECOMPUTED_EMBEDDING_DATASET}" fixture_json)
file(MAKE_DIRECTORY "${AGENT_MEMORY_PRECOMPUTED_EMBEDDING_NEGATIVE_WORKDIR}")

function(write_mutated_fixture file_name needle replacement)
    string(REPLACE "${needle}" "${replacement}" mutated_json "${fixture_json}")
    if(mutated_json STREQUAL fixture_json)
        message(FATAL_ERROR "negative fixture mutation did not change ${file_name}")
    endif()
    set(mutated_path
        "${AGENT_MEMORY_PRECOMPUTED_EMBEDDING_NEGATIVE_WORKDIR}/${file_name}"
    )
    file(WRITE "${mutated_path}" "${mutated_json}")
    set(mutated_path "${mutated_path}" PARENT_SCOPE)
endfunction()

function(expect_verifier_failure case_name fixture_path)
    execute_process(
        COMMAND
            "${AGENT_MEMORY_PRECOMPUTED_ARTIFACT_VERIFY_EXE}"
            "${fixture_path}"
        RESULT_VARIABLE verifier_result
        OUTPUT_VARIABLE verifier_stdout
        ERROR_VARIABLE verifier_stderr
    )
    if(verifier_result EQUAL 0)
        message(FATAL_ERROR
            "artifact verifier unexpectedly accepted ${case_name}: ${fixture_path}"
        )
    endif()
endfunction()

write_mutated_fixture(
    "mutated-config.json"
    "\"model_revision\": \"fixture-semantic-axis-4d-v1\""
    "\"model_revision\": \"fixture-semantic-axis-4d-v2\""
)
expect_verifier_failure("mutated config" "${mutated_path}")

write_mutated_fixture(
    "mutated-vector.json"
    "{\"id\": \"doc:alpha\", \"vector\": [1.0, 0.0, 0.0, 0.0]}"
    "{\"id\": \"doc:alpha\", \"vector\": [0.5, 0.0, 0.0, 0.0]}"
)
expect_verifier_failure("mutated vector" "${mutated_path}")

write_mutated_fixture(
    "mutated-record-order.json"
    "    {\"id\": \"doc:alpha\", \"vector\": [1.0, 0.0, 0.0, 0.0]},\n    {\"id\": \"doc:alpha-near\", \"vector\": [0.98, 0.2, 0.0, 0.0]}"
    "    {\"id\": \"doc:alpha-near\", \"vector\": [0.98, 0.2, 0.0, 0.0]},\n    {\"id\": \"doc:alpha\", \"vector\": [1.0, 0.0, 0.0, 0.0]}"
)
expect_verifier_failure("mutated record order" "${mutated_path}")
