foreach(required_var
    AGENT_MEMORY_BENCH_EXE
    AGENT_MEMORY_BENCH_CONFIG
    AGENT_MEMORY_BENCH_OUTPUT
    AGENT_MEMORY_BENCH_WORKDIR
)
    if(NOT DEFINED ${required_var})
        message(FATAL_ERROR "${required_var} must be defined")
    endif()
endforeach()

function(expect_benchmark_config_failure case_name config_json)
    get_filename_component(output_directory "${AGENT_MEMORY_BENCH_OUTPUT}" DIRECTORY)
    set(invalid_config "${output_directory}/${case_name}.json")
    set(invalid_output "${output_directory}/${case_name}-output.json")
    file(WRITE "${invalid_config}" "${config_json}\n")
    execute_process(
        COMMAND
            "${AGENT_MEMORY_BENCH_EXE}"
            "${invalid_config}"
            "${invalid_output}"
        WORKING_DIRECTORY "${AGENT_MEMORY_BENCH_WORKDIR}"
        RESULT_VARIABLE invalid_result
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(invalid_result EQUAL 0)
        message(FATAL_ERROR "${case_name} config must be rejected")
    endif()
endfunction()

file(READ "${AGENT_MEMORY_BENCH_CONFIG}" valid_config_json)
string(JSON invalid_repeat_json SET "${valid_config_json}" repeat_count 0)
expect_benchmark_config_failure("binary-grid-invalid-repeat" "${invalid_repeat_json}")

string(JSON invalid_exact_repeat_json SET
    "${valid_config_json}" exact_timing_repeat_count 0
)
expect_benchmark_config_failure(
    "binary-grid-invalid-exact-repeat"
    "${invalid_exact_repeat_json}"
)

string(JSON invalid_bool_seed_json SET "${valid_config_json}" data_seeds "[true]")
expect_benchmark_config_failure("binary-grid-invalid-bool-seed" "${invalid_bool_seed_json}")

string(JSON invalid_string_seed_json SET "${valid_config_json}" encoder_seeds "[\"bad\"]")
expect_benchmark_config_failure("binary-grid-invalid-string-seed" "${invalid_string_seed_json}")

string(JSON invalid_encoder_family_json SET
    "${valid_config_json}" encoder_families "[\"unknown_encoder\"]"
)
expect_benchmark_config_failure(
    "binary-grid-invalid-encoder-family"
    "${invalid_encoder_family_json}"
)

string(JSON invalid_coordinate_bits_json SET
    "${valid_config_json}" bit_counts "[16]"
)
expect_benchmark_config_failure(
    "binary-grid-invalid-coordinate-bit-counts"
    "${invalid_coordinate_bits_json}"
)

execute_process(
    COMMAND
        "${AGENT_MEMORY_BENCH_EXE}"
        "${AGENT_MEMORY_BENCH_CONFIG}"
        "${AGENT_MEMORY_BENCH_OUTPUT}"
    WORKING_DIRECTORY "${AGENT_MEMORY_BENCH_WORKDIR}"
    RESULT_VARIABLE bench_result
    OUTPUT_VARIABLE bench_stdout
    ERROR_VARIABLE bench_stderr
)
if(NOT bench_result EQUAL 0)
    message(STATUS "benchmark stdout:\n${bench_stdout}")
    message(STATUS "benchmark stderr:\n${bench_stderr}")
    message(FATAL_ERROR "synthetic_binary_rerank_grid smoke benchmark failed")
endif()

file(READ "${AGENT_MEMORY_BENCH_OUTPUT}" grid_json)
string(JSON mode GET "${grid_json}" mode)
if(NOT mode STREQUAL "synthetic_binary_rerank_grid")
    message(FATAL_ERROR "unexpected grid mode: ${mode}")
endif()

string(JSON seed_run_count LENGTH "${grid_json}" seed_runs)
if(NOT seed_run_count EQUAL 5)
    message(FATAL_ERROR "expected 5 seed_runs, got ${seed_run_count}")
endif()

string(JSON aggregate_count LENGTH "${grid_json}" aggregate_summary)
if(NOT aggregate_count EQUAL 5)
    message(FATAL_ERROR
        "expected 5 aggregate encoder/bit summaries, got ${aggregate_count}"
    )
endif()

set(saw_random_8 FALSE)
set(saw_random_16 FALSE)
set(saw_coordinate_8 FALSE)
set(saw_hadamard_8 FALSE)
set(saw_hadamard_16 FALSE)
math(EXPR last_aggregate "${aggregate_count} - 1")
foreach(aggregate_index RANGE 0 ${last_aggregate})
    string(JSON aggregate_encoder_family GET
        "${grid_json}" aggregate_summary ${aggregate_index} encoder_family
    )
    string(JSON aggregate_bit_count GET
        "${grid_json}" aggregate_summary ${aggregate_index} bit_count
    )
    if(NOT aggregate_encoder_family MATCHES
       "^(random_hyperplane_rademacher|coordinate_sign|randomized_hadamard_projection)$")
        message(FATAL_ERROR
            "unexpected aggregate encoder_family: ${aggregate_encoder_family}"
        )
    endif()
    string(JSON aggregate_quality_count GET
        "${grid_json}" aggregate_summary ${aggregate_index} quality_sample_count
    )
    string(JSON aggregate_timing_count GET
        "${grid_json}" aggregate_summary ${aggregate_index} timing_sample_count
    )
    set(expected_quality_count 2)
    set(expected_timing_count 4)
    if(aggregate_encoder_family STREQUAL "coordinate_sign")
        set(expected_quality_count 1)
        set(expected_timing_count 2)
        if(NOT aggregate_bit_count EQUAL 8)
            message(FATAL_ERROR "coordinate_sign must only report the 8-bit dim")
        endif()
        set(saw_coordinate_8 TRUE)
    elseif(aggregate_encoder_family STREQUAL "random_hyperplane_rademacher")
        if(aggregate_bit_count EQUAL 8)
            set(saw_random_8 TRUE)
        elseif(aggregate_bit_count EQUAL 16)
            set(saw_random_16 TRUE)
        else()
            message(FATAL_ERROR "unexpected random aggregate bit_count")
        endif()
    elseif(aggregate_encoder_family STREQUAL "randomized_hadamard_projection")
        if(aggregate_bit_count EQUAL 8)
            set(saw_hadamard_8 TRUE)
        elseif(aggregate_bit_count EQUAL 16)
            set(saw_hadamard_16 TRUE)
        else()
            message(FATAL_ERROR "unexpected randomized Hadamard aggregate bit_count")
        endif()
    endif()
    if(NOT aggregate_quality_count EQUAL expected_quality_count)
        message(FATAL_ERROR "aggregate quality_sample_count is inconsistent")
    endif()
    if(NOT aggregate_timing_count EQUAL expected_timing_count)
        message(FATAL_ERROR "aggregate timing_sample_count is inconsistent")
    endif()

    string(JSON quality_stat_count GET
        "${grid_json}" aggregate_summary ${aggregate_index} quality
        mean_recall_at_k_vs_exact sample_count
    )
    string(JSON timing_stat_count GET
        "${grid_json}" aggregate_summary ${aggregate_index} speed
        binary_query_total_ms sample_count
    )
    string(JSON timing_p95 GET
        "${grid_json}" aggregate_summary ${aggregate_index} speed
        binary_query_total_ms p95_nearest_rank
    )
    string(JSON current_speedup_count GET
        "${grid_json}" aggregate_summary ${aggregate_index} speed
        binary_total_speedup_vs_current_exact_index_including_encode sample_count
    )
    string(JSON contiguous_speedup_count GET
        "${grid_json}" aggregate_summary ${aggregate_index} speed
        binary_total_speedup_vs_contiguous_exact_including_encode sample_count
    )
    if(NOT quality_stat_count EQUAL expected_quality_count
       OR NOT timing_stat_count EQUAL expected_timing_count
       OR NOT current_speedup_count EQUAL expected_timing_count
       OR NOT contiguous_speedup_count EQUAL expected_timing_count)
        message(FATAL_ERROR
            "aggregate nested quality/timing sample counts are inconsistent"
        )
    endif()
    if(NOT timing_p95 GREATER 0)
        message(FATAL_ERROR "aggregate p95_nearest_rank must be positive")
    endif()
endforeach()

if(NOT saw_random_8 OR NOT saw_random_16 OR NOT saw_coordinate_8
   OR NOT saw_hadamard_8 OR NOT saw_hadamard_16)
    message(FATAL_ERROR "aggregate summary is missing an expected encoder/bit row")
endif()

math(EXPR last_seed_run "${seed_run_count} - 1")
foreach(seed_run_index RANGE 0 ${last_seed_run})
    string(JSON seed_encoder_family GET
        "${grid_json}" seed_runs ${seed_run_index} encoder_family
    )
    string(JSON seed_encoder_seed GET
        "${grid_json}" seed_runs ${seed_run_index} encoder_seed
    )
    if(NOT seed_encoder_family MATCHES
       "^(random_hyperplane_rademacher|coordinate_sign|randomized_hadamard_projection)$")
        message(FATAL_ERROR "unexpected seed_run encoder_family: ${seed_encoder_family}")
    endif()
    if(seed_encoder_family STREQUAL "coordinate_sign"
       AND NOT seed_encoder_seed EQUAL 0)
        message(FATAL_ERROR "coordinate_sign seed_run must use synthetic encoder_seed 0")
    endif()
    string(JSON current_exact_total_ms GET
        "${grid_json}" seed_runs ${seed_run_index} common_exact
        current_exact_index_query_total_ms
    )
    string(JSON contiguous_exact_total_ms GET
        "${grid_json}" seed_runs ${seed_run_index} common_exact
        contiguous_exact_query_total_ms
    )
    if(NOT current_exact_total_ms GREATER 0
       OR NOT contiguous_exact_total_ms GREATER 0)
        message(FATAL_ERROR "both common exact timings must be positive")
    endif()
    string(JSON exact_repeat_count GET
        "${grid_json}" seed_runs ${seed_run_index} common_exact exact_timing_repeat_count
    )
    string(JSON current_exact_stat_count GET
        "${grid_json}" seed_runs ${seed_run_index} common_exact
        current_exact_index_query_total_ms_stats sample_count
    )
    string(JSON contiguous_exact_stat_count GET
        "${grid_json}" seed_runs ${seed_run_index} common_exact
        contiguous_exact_query_total_ms_stats sample_count
    )
    string(JSON current_speedup_denominator GET
        "${grid_json}" seed_runs ${seed_run_index} common_exact
        speedup_denominators current_exact_index
    )
    string(JSON contiguous_speedup_denominator GET
        "${grid_json}" seed_runs ${seed_run_index} common_exact
        speedup_denominators contiguous_exact
    )
    string(JSON current_similarity_backend GET
        "${grid_json}" seed_runs ${seed_run_index} common_exact
        current_exact_index_similarity_backend
    )
    string(JSON contiguous_similarity_backend GET
        "${grid_json}" seed_runs ${seed_run_index} common_exact
        contiguous_exact_similarity_backend
    )
    if(NOT exact_repeat_count EQUAL 3
       OR NOT current_exact_stat_count EQUAL 3
       OR NOT contiguous_exact_stat_count EQUAL 3)
        message(FATAL_ERROR "expected 3 samples for both exact timing baselines")
    endif()
    string(JSON current_exact_stat_median GET
        "${grid_json}" seed_runs ${seed_run_index} common_exact
        current_exact_index_query_total_ms_stats median
    )
    string(JSON contiguous_exact_stat_median GET
        "${grid_json}" seed_runs ${seed_run_index} common_exact
        contiguous_exact_query_total_ms_stats median
    )
    if(NOT current_exact_total_ms EQUAL current_exact_stat_median)
        message(FATAL_ERROR
            "current-index denominator must equal its median timing: "
            "${current_exact_total_ms} != ${current_exact_stat_median}"
        )
    endif()
    if(NOT contiguous_exact_total_ms EQUAL contiguous_exact_stat_median)
        message(FATAL_ERROR
            "contiguous denominator must equal its median timing: "
            "${contiguous_exact_total_ms} != ${contiguous_exact_stat_median}"
        )
    endif()
    if(NOT current_speedup_denominator STREQUAL
           "median_current_exact_index_query_total_ms"
       OR NOT contiguous_speedup_denominator STREQUAL
           "median_contiguous_exact_query_total_ms")
        message(FATAL_ERROR "unexpected exact speedup denominator names")
    endif()
    if(NOT current_similarity_backend MATCHES "^(scalar|sse2|avx2)$"
       OR NOT contiguous_similarity_backend MATCHES "^(scalar|sse2|avx2)$")
        message(FATAL_ERROR "unexpected exact vector backend")
    endif()

    string(JSON report_count LENGTH "${grid_json}" seed_runs ${seed_run_index} reports)
    set(expected_report_count 2)
    if(seed_encoder_family STREQUAL "coordinate_sign")
        set(expected_report_count 1)
    endif()
    if(NOT report_count EQUAL expected_report_count)
        message(FATAL_ERROR
            "unexpected bit report count for seed_run ${seed_run_index}: "
            "${report_count}"
        )
    endif()

    math(EXPR last_report "${report_count} - 1")
    foreach(report_index RANGE 0 ${last_report})
        string(JSON report_encoder_family GET
            "${grid_json}" seed_runs ${seed_run_index} reports ${report_index}
            encoder_family
        )
        if(NOT report_encoder_family STREQUAL seed_encoder_family)
            message(FATAL_ERROR "bit report encoder_family must match seed_run")
        endif()
        string(JSON report_bit_count GET
            "${grid_json}" seed_runs ${seed_run_index} reports ${report_index} bit_count
        )
        if(seed_encoder_family STREQUAL "coordinate_sign")
            if(NOT report_bit_count EQUAL 8)
                message(FATAL_ERROR "coordinate_sign report must use bit_count 8")
            endif()
        else()
            if(report_index EQUAL 0 AND NOT report_bit_count EQUAL 8)
                message(FATAL_ERROR "expected first report bit_count 8")
            endif()
            if(report_index EQUAL 1 AND NOT report_bit_count EQUAL 16)
                message(FATAL_ERROR "expected second report bit_count 16")
            endif()
        endif()
        string(JSON summary_quality_count GET
            "${grid_json}" seed_runs ${seed_run_index} reports ${report_index}
            summary quality_sample_count
        )
        string(JSON summary_timing_count GET
            "${grid_json}" seed_runs ${seed_run_index} reports ${report_index}
            summary timing_sample_count
        )
        if(NOT summary_quality_count EQUAL 1 OR NOT summary_timing_count EQUAL 2)
            message(FATAL_ERROR "per-seed report sample counts are inconsistent")
        endif()
        string(JSON repeat_count LENGTH
            "${grid_json}" seed_runs ${seed_run_index} reports ${report_index} repeats
        )
        if(NOT repeat_count EQUAL 2)
            message(FATAL_ERROR
                "expected 2 repeats for seed_run ${seed_run_index}, "
                "report ${report_index}, got ${repeat_count}"
            )
        endif()

        math(EXPR last_repeat "${repeat_count} - 1")
        foreach(repeat_index RANGE 0 ${last_repeat})
            string(JSON stored_repeat_index GET
                "${grid_json}" seed_runs ${seed_run_index} reports ${report_index}
                repeats ${repeat_index} repeat_index
            )
            if(NOT stored_repeat_index EQUAL repeat_index)
                message(FATAL_ERROR
                    "repeat reports must be sorted and unique: expected ${repeat_index}, "
                    "got ${stored_repeat_index}"
                )
            endif()
            string(JSON hamming_backend GET
                "${grid_json}" seed_runs ${seed_run_index} reports ${report_index}
                repeats ${repeat_index} speed binary_hamming_backend
            )
            string(JSON encoder_backend GET
                "${grid_json}" seed_runs ${seed_run_index} reports ${report_index}
                repeats ${repeat_index} speed binary_encoder_similarity_backend
            )
            if(NOT hamming_backend MATCHES "^(lookup_table|hardware_popcount|avx2_simd)$")
                message(FATAL_ERROR "unexpected Hamming backend: ${hamming_backend}")
            endif()
            if(NOT encoder_backend MATCHES "^(scalar|sse2|avx2|coordinate_sign|fwht_scalar)$")
                message(FATAL_ERROR "unexpected encoder vector backend: ${encoder_backend}")
            endif()
            string(JSON current_direct_speedup GET
                "${grid_json}" seed_runs ${seed_run_index} reports ${report_index}
                repeats ${repeat_index} speed
                binary_total_speedup_vs_current_exact_index_including_encode
            )
            string(JSON contiguous_direct_speedup GET
                "${grid_json}" seed_runs ${seed_run_index} reports ${report_index}
                repeats ${repeat_index} speed
                binary_total_speedup_vs_contiguous_exact_including_encode
            )
            if(NOT current_direct_speedup GREATER 0
               OR NOT contiguous_direct_speedup GREATER 0)
                message(FATAL_ERROR "both direct exact speedups must be positive")
            endif()
            string(JSON rerank_count LENGTH
                "${grid_json}" seed_runs ${seed_run_index} reports ${report_index}
                repeats ${repeat_index} rerank
            )
            if(NOT rerank_count EQUAL 3)
                message(FATAL_ERROR
                    "expected 3 rerank rows for seed_run ${seed_run_index}, "
                    "report ${report_index}, repeat ${repeat_index}, got ${rerank_count}"
                )
            endif()

            set(previous_coverage -1)
            math(EXPR last_rerank "${rerank_count} - 1")
            foreach(rerank_index RANGE 0 ${last_rerank})
                string(JSON candidate_limit GET
                    "${grid_json}" seed_runs ${seed_run_index} reports ${report_index}
                    repeats ${repeat_index} rerank ${rerank_index} candidate_limit
                )
                string(JSON coverage GET
                    "${grid_json}" seed_runs ${seed_run_index} reports ${report_index}
                    repeats ${repeat_index} rerank ${rerank_index}
                    exact_top_k_candidate_coverage
                )
                string(JSON reranked_recall GET
                    "${grid_json}" seed_runs ${seed_run_index} reports ${report_index}
                    repeats ${repeat_index} rerank ${rerank_index}
                    reranked_recall_at_k_vs_exact
                )
                string(JSON current_rerank_speedup GET
                    "${grid_json}" seed_runs ${seed_run_index} reports ${report_index}
                    repeats ${repeat_index} rerank ${rerank_index}
                    total_speedup_vs_current_exact_index
                )
                string(JSON contiguous_rerank_speedup GET
                    "${grid_json}" seed_runs ${seed_run_index} reports ${report_index}
                    repeats ${repeat_index} rerank ${rerank_index}
                    total_speedup_vs_contiguous_exact
                )

                if(rerank_index EQUAL 0 AND NOT candidate_limit EQUAL 3)
                    message(FATAL_ERROR "expected first candidate_limit 3")
                endif()
                if(rerank_index EQUAL 1 AND NOT candidate_limit EQUAL 10)
                    message(FATAL_ERROR "expected second candidate_limit 10")
                endif()
                if(rerank_index EQUAL 2 AND NOT candidate_limit EQUAL 30)
                    message(FATAL_ERROR "expected third candidate_limit 30")
                endif()

                if(coverage LESS 0 OR coverage GREATER 1)
                    message(FATAL_ERROR "coverage out of range: ${coverage}")
                endif()
                if(NOT current_rerank_speedup GREATER 0
                   OR NOT contiguous_rerank_speedup GREATER 0)
                    message(FATAL_ERROR "both rerank exact speedups must be positive")
                endif()
                if(NOT coverage STREQUAL reranked_recall)
                    message(FATAL_ERROR
                        "coverage and reranked recall must match for exact rerank: "
                        "${coverage} != ${reranked_recall}"
                    )
                endif()
                if(coverage LESS previous_coverage)
                    message(FATAL_ERROR
                        "coverage must not decrease as candidate_limit increases"
                    )
                endif()
                if(candidate_limit EQUAL 30 AND NOT coverage EQUAL 1)
                    message(FATAL_ERROR
                        "full-corpus candidate_limit must recover exact top-k; "
                        "coverage=${coverage}"
                    )
                endif()
                set(previous_coverage "${coverage}")
            endforeach()
        endforeach()
    endforeach()
endforeach()
