foreach(required_var
        AGENT_MEMORY_AGGREGATE_BENCH_EXE
        AGENT_MEMORY_AGGREGATE_BENCH_CONFIG
        AGENT_MEMORY_AGGREGATE_BENCH_OUTPUT
        AGENT_MEMORY_AGGREGATE_BENCH_WORKDIR)
    if(NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} is required")
    endif()
endforeach()

execute_process(
    COMMAND
        "${AGENT_MEMORY_AGGREGATE_BENCH_EXE}"
        "${AGENT_MEMORY_AGGREGATE_BENCH_CONFIG}"
        "${AGENT_MEMORY_AGGREGATE_BENCH_OUTPUT}"
    WORKING_DIRECTORY "${AGENT_MEMORY_AGGREGATE_BENCH_WORKDIR}"
    RESULT_VARIABLE bench_result
    OUTPUT_VARIABLE bench_stdout
    ERROR_VARIABLE bench_stderr
)

if(NOT bench_result EQUAL 0)
    message(FATAL_ERROR
        "aggregate signature benchmark failed with ${bench_result}\n"
        "stdout:\n${bench_stdout}\n"
        "stderr:\n${bench_stderr}"
    )
endif()

if(NOT EXISTS "${AGENT_MEMORY_AGGREGATE_BENCH_OUTPUT}")
    message(FATAL_ERROR
        "aggregate signature benchmark did not create output: "
        "${AGENT_MEMORY_AGGREGATE_BENCH_OUTPUT}"
    )
endif()

file(READ "${AGENT_MEMORY_AGGREGATE_BENCH_OUTPUT}" report_json)

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

function(require_number_range value label min_value max_value)
    if(${value} LESS ${min_value} OR ${value} GREATER ${max_value})
        message(FATAL_ERROR
            "${label} must be in [${min_value}, ${max_value}], got ${value}"
        )
    endif()
endfunction()

function(require_nonnegative value label)
    if(${value} LESS 0)
        message(FATAL_ERROR "${label} must be non-negative, got ${value}")
    endif()
endfunction()

require_json(schema_version GET schema_version)
if(NOT schema_version EQUAL 1)
    message(FATAL_ERROR "schema_version must be 1, got ${schema_version}")
endif()

require_json(mode GET mode)
if(NOT mode STREQUAL "synthetic_aggregate_binary_signature")
    message(FATAL_ERROR "unexpected mode: ${mode}")
endif()

require_json(document_count GET document_count)
require_json(chunks_per_document GET chunks_per_document)
require_json(query_count GET query_count)
require_json(embedding_dimensions GET embedding_dimensions)
require_json(bit_count GET bit_count)
require_json(result_limit GET result_limit)

if(NOT document_count EQUAL 24)
    message(FATAL_ERROR "document_count must stay 24 in the smoke config")
endif()
if(NOT chunks_per_document EQUAL 3)
    message(FATAL_ERROR "chunks_per_document must stay 3 in the smoke config")
endif()
if(NOT query_count EQUAL 24)
    message(FATAL_ERROR "query_count must stay 24 in the smoke config")
endif()
if(NOT embedding_dimensions EQUAL 64)
    message(FATAL_ERROR "embedding_dimensions must stay 64 in the smoke config")
endif()
if(NOT bit_count EQUAL 128)
    message(FATAL_ERROR "bit_count must stay 128 in the smoke config")
endif()
if(NOT result_limit EQUAL 5)
    message(FATAL_ERROR "result_limit must stay 5 in the smoke config")
endif()

set(expected_candidate_limits 3 5 10 24)
require_json(candidate_limit_count LENGTH candidate_limits)
list(LENGTH expected_candidate_limits expected_candidate_limit_count)
if(NOT candidate_limit_count EQUAL expected_candidate_limit_count)
    message(FATAL_ERROR
        "candidate_limits must contain ${expected_candidate_limit_count} entries"
    )
endif()
foreach(index RANGE 0 3)
    require_json(candidate_limit GET candidate_limits ${index})
    list(GET expected_candidate_limits ${index} expected_candidate_limit)
    if(NOT candidate_limit EQUAL expected_candidate_limit)
        message(FATAL_ERROR
            "candidate_limits[${index}] must be ${expected_candidate_limit}, "
            "got ${candidate_limit}"
        )
    endif()
endforeach()

require_json(data_generation_ms GET timing_ms data_generation)
require_json(exact_object_query_ms GET timing_ms exact_object_query)
require_json(binary_encoding_ms GET timing_ms binary_chunk_and_query_encoding)
require_nonnegative(${data_generation_ms} "timing_ms.data_generation")
require_nonnegative(${exact_object_query_ms} "timing_ms.exact_object_query")
require_nonnegative(
    ${binary_encoding_ms}
    "timing_ms.binary_chunk_and_query_encoding"
)

set(expected_modes
    any_set_bit
    majority_set_bit
    all_set_bits
    threshold_fraction
)
require_json(aggregation_report_count LENGTH aggregation_reports)
list(LENGTH expected_modes expected_mode_count)
if(NOT aggregation_report_count EQUAL expected_mode_count)
    message(FATAL_ERROR
        "aggregation_reports must contain ${expected_mode_count} entries"
    )
endif()

foreach(mode_index RANGE 0 3)
    list(GET expected_modes ${mode_index} expected_mode)
    require_json(actual_mode GET aggregation_reports ${mode_index} aggregation_mode)
    if(NOT actual_mode STREQUAL expected_mode)
        message(FATAL_ERROR
            "aggregation_reports[${mode_index}].aggregation_mode must be "
            "${expected_mode}, got ${actual_mode}"
        )
    endif()

    require_json(aggregation_build_ms
        GET aggregation_reports ${mode_index} aggregation_build_ms)
    require_nonnegative(
        ${aggregation_build_ms}
        "aggregation_reports[${mode_index}].aggregation_build_ms"
    )

    require_json(mode_candidate_count
        LENGTH aggregation_reports ${mode_index} candidate_limits)
    if(NOT mode_candidate_count EQUAL expected_candidate_limit_count)
        message(FATAL_ERROR
            "aggregation_reports[${mode_index}].candidate_limits must contain "
            "${expected_candidate_limit_count} entries"
        )
    endif()

    set(previous_coverage -1)
    foreach(candidate_index RANGE 0 3)
        list(GET expected_candidate_limits ${candidate_index} expected_candidate_limit)
        require_json(actual_candidate_limit
            GET aggregation_reports ${mode_index} candidate_limits
            ${candidate_index} candidate_limit)
        if(NOT actual_candidate_limit EQUAL expected_candidate_limit)
            message(FATAL_ERROR
                "aggregation_reports[${mode_index}].candidate_limits"
                "[${candidate_index}].candidate_limit must be "
                "${expected_candidate_limit}, got ${actual_candidate_limit}"
            )
        endif()

        require_json(coverage
            GET aggregation_reports ${mode_index} candidate_limits
            ${candidate_index} exact_top_k_candidate_coverage)
        require_json(top1
            GET aggregation_reports ${mode_index} candidate_limits
            ${candidate_index} top1_agreement_vs_exact)
        require_json(aggregate_query_ms
            GET aggregation_reports ${mode_index} candidate_limits
            ${candidate_index} aggregate_query_ms)

        require_number_range(
            ${coverage}
            "aggregation_reports[${mode_index}].candidate_limits"
            0
            1
        )
        require_number_range(
            ${top1}
            "aggregation_reports[${mode_index}].top1_agreement_vs_exact"
            0
            1
        )
        require_nonnegative(
            ${aggregate_query_ms}
            "aggregation_reports[${mode_index}].aggregate_query_ms"
        )

        if(${coverage} LESS ${previous_coverage})
            message(FATAL_ERROR
                "exact_top_k_candidate_coverage must be monotonic for "
                "${actual_mode}"
            )
        endif()
        set(previous_coverage ${coverage})

        if(actual_candidate_limit EQUAL document_count AND NOT coverage EQUAL 1)
            message(FATAL_ERROR
                "candidate_limit == document_count must provide full coverage "
                "for ${actual_mode}, got ${coverage}"
            )
        endif()
    endforeach()
endforeach()
