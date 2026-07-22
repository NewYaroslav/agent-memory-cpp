foreach(required_var
        AGENT_MEMORY_BINARY_LIFECYCLE_BENCH_EXE
        AGENT_MEMORY_BINARY_LIFECYCLE_BENCH_CONFIG
        AGENT_MEMORY_BINARY_LIFECYCLE_BENCH_OUTPUT
        AGENT_MEMORY_BINARY_LIFECYCLE_BENCH_WORKDIR)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} is required")
    endif()
endforeach()

execute_process(
    COMMAND
        "${AGENT_MEMORY_BINARY_LIFECYCLE_BENCH_EXE}"
        "${AGENT_MEMORY_BINARY_LIFECYCLE_BENCH_CONFIG}"
        "${AGENT_MEMORY_BINARY_LIFECYCLE_BENCH_OUTPUT}"
    WORKING_DIRECTORY "${AGENT_MEMORY_BINARY_LIFECYCLE_BENCH_WORKDIR}"
    RESULT_VARIABLE bench_result
    OUTPUT_VARIABLE bench_stdout
    ERROR_VARIABLE bench_stderr
)

if(NOT bench_result EQUAL 0)
    message(FATAL_ERROR
        "binary lifecycle benchmark failed with ${bench_result}\n"
        "stdout:\n${bench_stdout}\n"
        "stderr:\n${bench_stderr}"
    )
endif()

if(NOT EXISTS "${AGENT_MEMORY_BINARY_LIFECYCLE_BENCH_OUTPUT}")
    message(FATAL_ERROR
        "binary lifecycle benchmark did not create output: "
        "${AGENT_MEMORY_BINARY_LIFECYCLE_BENCH_OUTPUT}"
    )
endif()

file(READ "${AGENT_MEMORY_BINARY_LIFECYCLE_BENCH_OUTPUT}" report_json)

function(require_json out_var)
    list(GET ARGN 0 json_operation)
    list(REMOVE_AT ARGN 0)
    string(
        JSON value
        ERROR_VARIABLE json_error
        ${json_operation}
        "${report_json}"
        ${ARGN}
    )
    if(json_error)
        message(FATAL_ERROR "missing or invalid JSON field ${ARGN}: ${json_error}")
    endif()
    set(${out_var} "${value}" PARENT_SCOPE)
endfunction()

function(require_nonnegative value label)
    if(${value} LESS 0)
        message(FATAL_ERROR "${label} must be non-negative, got ${value}")
    endif()
endfunction()

function(require_range value label min_value max_value)
    if(${value} LESS ${min_value} OR ${value} GREATER ${max_value})
        message(FATAL_ERROR
            "${label} must be in [${min_value}, ${max_value}], got ${value}"
        )
    endif()
endfunction()

require_json(schema_version GET schema_version)
if(NOT schema_version EQUAL 1)
    message(FATAL_ERROR "schema_version must be 1")
endif()

require_json(mode GET mode)
if(NOT mode STREQUAL "synthetic_binary_lifecycle")
    message(FATAL_ERROR "unexpected mode: ${mode}")
endif()

require_json(document_count GET document_count)
require_json(query_count GET query_count)
require_json(embedding_dimensions GET embedding_dimensions)
require_json(bit_count GET bit_count)
require_json(result_limit GET result_limit)
require_json(mutation_count GET mutation_count)
require_json(seed GET seed)

if(NOT document_count EQUAL 256)
    message(FATAL_ERROR "document_count must stay 256")
endif()
if(NOT query_count EQUAL 64)
    message(FATAL_ERROR "query_count must stay 64")
endif()
if(NOT embedding_dimensions EQUAL 64)
    message(FATAL_ERROR "embedding_dimensions must stay 64")
endif()
if(NOT bit_count EQUAL 128)
    message(FATAL_ERROR "bit_count must stay 128")
endif()
if(NOT result_limit EQUAL 10)
    message(FATAL_ERROR "result_limit must stay 10")
endif()
if(NOT mutation_count EQUAL 16)
    message(FATAL_ERROR "mutation_count must stay 16")
endif()
if(NOT seed EQUAL 42)
    message(FATAL_ERROR "seed must stay 42")
endif()

function(require_nonnegative_path label)
    require_json(value GET ${ARGN})
    require_nonnegative(${value} "${label}")
endfunction()

require_nonnegative_path("timing_ms.data_generation" timing_ms data_generation)
require_nonnegative_path(
    "timing_ms.binary_chunk_and_query_encoding"
    timing_ms
    binary_chunk_and_query_encoding
)
require_nonnegative_path("exact_vector.build_ms" exact_vector build_ms)
require_nonnegative_path("exact_vector.query_total_ms" exact_vector query_total_ms)
require_nonnegative_path("exact_vector.query_mean_ms" exact_vector query_mean_ms)
require_nonnegative_path("flat_binary.build_ms" flat_binary build_ms)
require_nonnegative_path("flat_binary.rebuild_ms" flat_binary rebuild_ms)
require_nonnegative_path("flat_binary.query.total_ms" flat_binary query total_ms)
require_nonnegative_path("flat_binary.query.mean_ms" flat_binary query mean_ms)
require_nonnegative_path(
    "flat_binary.mutations.erase_ms"
    flat_binary
    mutations
    erase_ms
)
require_nonnegative_path(
    "flat_binary.mutations.upsert_ms"
    flat_binary
    mutations
    upsert_ms
)
require_nonnegative_path("multiprobe_binary.build_ms" multiprobe_binary build_ms)
require_nonnegative_path(
    "multiprobe_binary.rebuild_ms"
    multiprobe_binary
    rebuild_ms
)
require_nonnegative_path(
    "multiprobe_binary.query.total_ms"
    multiprobe_binary
    query
    total_ms
)
require_nonnegative_path(
    "multiprobe_binary.query.mean_ms"
    multiprobe_binary
    query
    mean_ms
)
require_nonnegative_path(
    "multiprobe_binary.mutations.erase_ms"
    multiprobe_binary
    mutations
    erase_ms
)
require_nonnegative_path(
    "multiprobe_binary.mutations.upsert_ms"
    multiprobe_binary
    mutations
    upsert_ms
)

require_json(flat_coverage
    GET flat_binary query exact_top_k_candidate_coverage)
require_json(multiprobe_coverage
    GET multiprobe_binary query exact_top_k_candidate_coverage)
require_range(${flat_coverage} "flat coverage" 0 1)
require_range(${multiprobe_coverage} "multiprobe coverage" 0 1)

require_json(flat_erased GET flat_binary mutations erased_count)
require_json(multiprobe_erased GET multiprobe_binary mutations erased_count)
if(NOT flat_erased EQUAL mutation_count)
    message(FATAL_ERROR "flat erase count must match mutation_count")
endif()
if(NOT multiprobe_erased EQUAL mutation_count)
    message(FATAL_ERROR "multiprobe erase count must match mutation_count")
endif()

require_json(exact_payload GET exact_vector payload_bytes)
require_json(flat_payload GET flat_binary payload_bytes)
require_json(multiprobe_payload GET multiprobe_binary payload_bytes)
if(NOT exact_payload GREATER flat_payload)
    message(FATAL_ERROR "exact payload should be larger than flat binary payload")
endif()
if(NOT flat_payload EQUAL multiprobe_payload)
    message(FATAL_ERROR "binary payload bytes should match between binary indexes")
endif()

require_json(table_count GET multiprobe_binary options table_count)
require_json(bits_per_table GET multiprobe_binary options bits_per_table)
require_json(max_probe_radius GET multiprobe_binary options max_probe_radius)
require_json(candidate_multiplier
    GET multiprobe_binary options candidate_multiplier)
require_json(minimum_candidate_count
    GET multiprobe_binary options minimum_candidate_count)
if(NOT table_count EQUAL 8)
    message(FATAL_ERROR "multiprobe table_count must stay 8")
endif()
if(NOT bits_per_table EQUAL 8)
    message(FATAL_ERROR "multiprobe bits_per_table must stay 8")
endif()
if(NOT max_probe_radius EQUAL 1)
    message(FATAL_ERROR "multiprobe max_probe_radius must stay 1")
endif()
if(NOT candidate_multiplier EQUAL 64)
    message(FATAL_ERROR "multiprobe candidate_multiplier must stay 64")
endif()
if(NOT minimum_candidate_count EQUAL 64)
    message(FATAL_ERROR "multiprobe minimum_candidate_count must stay 64")
endif()
