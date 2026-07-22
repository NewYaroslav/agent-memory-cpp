if(NOT DEFINED AGENT_MEMORY_PRECOMPUTED_ARTIFACT_VERIFY_EXE)
    message(FATAL_ERROR "AGENT_MEMORY_PRECOMPUTED_ARTIFACT_VERIFY_EXE is required")
endif()
if(NOT DEFINED AGENT_MEMORY_PRECOMPUTED_EMBEDDING_DATASET)
    message(FATAL_ERROR "AGENT_MEMORY_PRECOMPUTED_EMBEDDING_DATASET is required")
endif()
if(NOT DEFINED AGENT_MEMORY_PRECOMPUTED_EMBEDDING_CANONICAL_WORKDIR)
    message(FATAL_ERROR "AGENT_MEMORY_PRECOMPUTED_EMBEDDING_CANONICAL_WORKDIR is required")
endif()

file(READ "${AGENT_MEMORY_PRECOMPUTED_EMBEDDING_DATASET}" fixture_json)
file(MAKE_DIRECTORY "${AGENT_MEMORY_PRECOMPUTED_EMBEDDING_CANONICAL_WORKDIR}")

function(write_mutated_fixture file_name needle replacement)
    string(REPLACE "${needle}" "${replacement}" mutated_json "${fixture_json}")
    if(mutated_json STREQUAL fixture_json)
        message(FATAL_ERROR "canonical fixture mutation did not change ${file_name}")
    endif()
    set(mutated_path
        "${AGENT_MEMORY_PRECOMPUTED_EMBEDDING_CANONICAL_WORKDIR}/${file_name}"
    )
    file(WRITE "${mutated_path}" "${mutated_json}")
    set(mutated_path "${mutated_path}" PARENT_SCOPE)
endfunction()

function(expect_verifier_success case_name fixture_path)
    execute_process(
        COMMAND
            "${AGENT_MEMORY_PRECOMPUTED_ARTIFACT_VERIFY_EXE}"
            "${fixture_path}"
        RESULT_VARIABLE verifier_result
        OUTPUT_VARIABLE verifier_stdout
        ERROR_VARIABLE verifier_stderr
    )
    if(NOT verifier_result EQUAL 0)
        message(FATAL_ERROR
            "artifact verifier rejected ${case_name}: ${fixture_path}\n"
            "stdout: ${verifier_stdout}\n"
            "stderr: ${verifier_stderr}"
        )
    endif()
endfunction()

write_mutated_fixture(
    "numeric-spelling.json"
    "{\"id\": \"doc:alpha\", \"vector\": [1.0, 0.0, 0.0, 0.0]}"
    "{\"id\": \"doc:alpha\", \"vector\": [1e0, 0.00, 0e0, 0.000]}"
)
expect_verifier_success("equivalent JSON numeric spelling" "${mutated_path}")

write_mutated_fixture(
    "negative-zero.json"
    "{\"id\": \"doc:alpha\", \"vector\": [1.0, 0.0, 0.0, 0.0]}"
    "{\"id\": \"doc:alpha\", \"vector\": [1.0, -0.0, -0.00, -0e0]}"
)
expect_verifier_success("negative zero canonicalization" "${mutated_path}")
