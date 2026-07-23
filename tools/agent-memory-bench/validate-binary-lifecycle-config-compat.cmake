foreach(required_var
        AGENT_MEMORY_BINARY_LIFECYCLE_BENCH_EXE
        AGENT_MEMORY_BINARY_LIFECYCLE_COMPAT_WORKDIR)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} is required")
    endif()
endforeach()

file(MAKE_DIRECTORY "${AGENT_MEMORY_BINARY_LIFECYCLE_COMPAT_WORKDIR}")

set(legacy_config "${AGENT_MEMORY_BINARY_LIFECYCLE_COMPAT_WORKDIR}/legacy-result-limit.json")
set(legacy_output "${AGENT_MEMORY_BINARY_LIFECYCLE_COMPAT_WORKDIR}/legacy-result-limit-output.json")
file(WRITE "${legacy_config}" [=[
{
  "output_path": "legacy-result-limit-output.json",
  "document_count": 64,
  "query_count": 8,
  "embedding_dimensions": 16,
  "bit_count": 64,
  "result_limit": 5,
  "mutation_count": 4,
  "seed": 42,
  "multiprobe_table_count": 8,
  "multiprobe_bits_per_table": 8,
  "multiprobe_max_probe_radius": 1,
  "multiprobe_candidate_multiplier": 8,
  "multiprobe_minimum_candidate_count": 16
}
]=])

execute_process(
    COMMAND
        "${AGENT_MEMORY_BINARY_LIFECYCLE_BENCH_EXE}"
        "${legacy_config}"
        "${legacy_output}"
    WORKING_DIRECTORY "${AGENT_MEMORY_BINARY_LIFECYCLE_COMPAT_WORKDIR}"
    RESULT_VARIABLE legacy_result
    OUTPUT_VARIABLE legacy_stdout
    ERROR_VARIABLE legacy_stderr
)

if(NOT legacy_result EQUAL 0)
    message(FATAL_ERROR
        "legacy result_limit-only config must remain accepted\n"
        "stdout:\n${legacy_stdout}\n"
        "stderr:\n${legacy_stderr}"
    )
endif()

file(READ "${legacy_output}" legacy_report_json)

function(require_legacy_json out_var)
    list(GET ARGN 0 json_operation)
    list(REMOVE_AT ARGN 0)
    string(
        JSON value
        ERROR_VARIABLE json_error
        ${json_operation}
        "${legacy_report_json}"
        ${ARGN}
    )
    if(json_error)
        message(FATAL_ERROR
            "missing or invalid legacy JSON field ${ARGN}: ${json_error}"
        )
    endif()
    set(${out_var} "${value}" PARENT_SCOPE)
endfunction()

require_legacy_json(legacy_oracle_k GET oracle_k)
require_legacy_json(legacy_returned_candidate_limit GET returned_candidate_limit)
require_legacy_json(legacy_result_limit GET result_limit)
if(NOT legacy_oracle_k EQUAL 5)
    message(FATAL_ERROR "legacy oracle_k must fall back to result_limit")
endif()
if(NOT legacy_returned_candidate_limit EQUAL 5)
    message(FATAL_ERROR
        "legacy returned_candidate_limit must fall back to result_limit"
    )
endif()
if(NOT legacy_result_limit EQUAL legacy_returned_candidate_limit)
    message(FATAL_ERROR "legacy report result_limit must match returned limit")
endif()

set(conflict_config "${AGENT_MEMORY_BINARY_LIFECYCLE_COMPAT_WORKDIR}/conflicting-result-limit.json")
set(conflict_output "${AGENT_MEMORY_BINARY_LIFECYCLE_COMPAT_WORKDIR}/conflicting-result-limit-output.json")
file(WRITE "${conflict_config}" [=[
{
  "output_path": "conflicting-result-limit-output.json",
  "document_count": 64,
  "query_count": 8,
  "embedding_dimensions": 16,
  "bit_count": 64,
  "oracle_k": 5,
  "returned_candidate_limit": 6,
  "result_limit": 5,
  "mutation_count": 4,
  "seed": 42,
  "multiprobe_table_count": 8,
  "multiprobe_bits_per_table": 8,
  "multiprobe_max_probe_radius": 1,
  "multiprobe_candidate_multiplier": 8,
  "multiprobe_minimum_candidate_count": 16
}
]=])

execute_process(
    COMMAND
        "${AGENT_MEMORY_BINARY_LIFECYCLE_BENCH_EXE}"
        "${conflict_config}"
        "${conflict_output}"
    WORKING_DIRECTORY "${AGENT_MEMORY_BINARY_LIFECYCLE_COMPAT_WORKDIR}"
    RESULT_VARIABLE conflict_result
    OUTPUT_VARIABLE conflict_stdout
    ERROR_VARIABLE conflict_stderr
)

if(conflict_result EQUAL 0)
    message(FATAL_ERROR
        "conflicting result_limit and returned_candidate_limit must be rejected"
    )
endif()

set(conflict_combined_output "${conflict_stdout}\n${conflict_stderr}")
if(NOT conflict_combined_output MATCHES "legacy alias")
    message(FATAL_ERROR
        "conflict error should explain the legacy alias mismatch\n"
        "stdout:\n${conflict_stdout}\n"
        "stderr:\n${conflict_stderr}"
    )
endif()
