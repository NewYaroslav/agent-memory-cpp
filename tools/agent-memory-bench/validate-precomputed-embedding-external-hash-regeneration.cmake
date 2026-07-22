if(NOT DEFINED AGENT_MEMORY_PYTHON_EXECUTABLE)
    message(FATAL_ERROR "AGENT_MEMORY_PYTHON_EXECUTABLE is required")
endif()
if(NOT DEFINED AGENT_MEMORY_PRECOMPUTED_EXTERNAL_HASH_GENERATOR)
    message(FATAL_ERROR "AGENT_MEMORY_PRECOMPUTED_EXTERNAL_HASH_GENERATOR is required")
endif()
if(NOT DEFINED AGENT_MEMORY_PRECOMPUTED_EXTERNAL_HASH_FIXTURE)
    message(FATAL_ERROR "AGENT_MEMORY_PRECOMPUTED_EXTERNAL_HASH_FIXTURE is required")
endif()
if(NOT DEFINED AGENT_MEMORY_PRECOMPUTED_EXTERNAL_HASH_REGEN_WORKDIR)
    message(FATAL_ERROR "AGENT_MEMORY_PRECOMPUTED_EXTERNAL_HASH_REGEN_WORKDIR is required")
endif()

file(MAKE_DIRECTORY "${AGENT_MEMORY_PRECOMPUTED_EXTERNAL_HASH_REGEN_WORKDIR}")
set(generated_fixture
    "${AGENT_MEMORY_PRECOMPUTED_EXTERNAL_HASH_REGEN_WORKDIR}/precomputed-embedding-external-hash-regenerated.json")

execute_process(
    COMMAND
        "${AGENT_MEMORY_PYTHON_EXECUTABLE}"
        "${AGENT_MEMORY_PRECOMPUTED_EXTERNAL_HASH_GENERATOR}"
        --output
        "${generated_fixture}"
    RESULT_VARIABLE generator_result
    OUTPUT_VARIABLE generator_stdout
    ERROR_VARIABLE generator_stderr
)
if(NOT generator_result EQUAL 0)
    message(FATAL_ERROR
        "external-hash fixture generator failed with exit code ${generator_result}\n"
        "stdout:\n${generator_stdout}\n"
        "stderr:\n${generator_stderr}"
    )
endif()

execute_process(
    COMMAND
        "${CMAKE_COMMAND}"
        -E
        compare_files
        "${AGENT_MEMORY_PRECOMPUTED_EXTERNAL_HASH_FIXTURE}"
        "${generated_fixture}"
    RESULT_VARIABLE compare_result
)
if(NOT compare_result EQUAL 0)
    file(SHA256 "${AGENT_MEMORY_PRECOMPUTED_EXTERNAL_HASH_FIXTURE}" committed_hash)
    file(SHA256 "${generated_fixture}" generated_hash)
    message(FATAL_ERROR
        "external-hash fixture regeneration is not byte-for-byte reproducible\n"
        "committed: ${AGENT_MEMORY_PRECOMPUTED_EXTERNAL_HASH_FIXTURE}\n"
        "generated:  ${generated_fixture}\n"
        "committed sha256: ${committed_hash}\n"
        "generated sha256:  ${generated_hash}"
    )
endif()
