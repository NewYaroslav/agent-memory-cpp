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
if(NOT seed_run_count EQUAL 2)
    message(FATAL_ERROR "expected 2 seed_runs, got ${seed_run_count}")
endif()

string(JSON aggregate_count LENGTH "${grid_json}" aggregate_summary)
if(NOT aggregate_count EQUAL 2)
    message(FATAL_ERROR "expected 2 aggregate bit summaries, got ${aggregate_count}")
endif()

math(EXPR last_aggregate "${aggregate_count} - 1")
foreach(aggregate_index RANGE 0 ${last_aggregate})
    string(JSON aggregate_bit_count GET
        "${grid_json}" aggregate_summary ${aggregate_index} bit_count
    )
    if(aggregate_index EQUAL 0 AND NOT aggregate_bit_count EQUAL 8)
        message(FATAL_ERROR
            "expected first aggregate bit_count 8, got ${aggregate_bit_count}"
        )
    endif()
    if(aggregate_index EQUAL 1 AND NOT aggregate_bit_count EQUAL 16)
        message(FATAL_ERROR
            "expected second aggregate bit_count 16, got ${aggregate_bit_count}"
        )
    endif()
    string(JSON aggregate_quality_count GET
        "${grid_json}" aggregate_summary ${aggregate_index} quality_sample_count
    )
    string(JSON aggregate_timing_count GET
        "${grid_json}" aggregate_summary ${aggregate_index} timing_sample_count
    )
    if(NOT aggregate_quality_count EQUAL 2)
        message(FATAL_ERROR
            "expected aggregate quality_sample_count 2, got ${aggregate_quality_count}"
        )
    endif()
    if(NOT aggregate_timing_count EQUAL 4)
        message(FATAL_ERROR
            "expected aggregate timing_sample_count 4, got ${aggregate_timing_count}"
        )
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
    if(NOT quality_stat_count EQUAL 2 OR NOT timing_stat_count EQUAL 4)
        message(FATAL_ERROR
            "aggregate nested quality/timing sample counts are inconsistent"
        )
    endif()
    if(NOT timing_p95 GREATER 0)
        message(FATAL_ERROR "aggregate p95_nearest_rank must be positive")
    endif()
endforeach()

math(EXPR last_seed_run "${seed_run_count} - 1")
foreach(seed_run_index RANGE 0 ${last_seed_run})
    string(JSON exact_total_ms GET
        "${grid_json}" seed_runs ${seed_run_index} common_exact exact_float_query_total_ms
    )
    if(NOT exact_total_ms GREATER 0)
        message(FATAL_ERROR "common exact timing must be positive")
    endif()
    string(JSON exact_repeat_count GET
        "${grid_json}" seed_runs ${seed_run_index} common_exact exact_timing_repeat_count
    )
    string(JSON exact_stat_count GET
        "${grid_json}" seed_runs ${seed_run_index} common_exact
        exact_float_query_total_ms_stats sample_count
    )
    string(JSON speedup_denominator GET
        "${grid_json}" seed_runs ${seed_run_index} common_exact speedup_denominator
    )
    string(JSON similarity_backend GET
        "${grid_json}" seed_runs ${seed_run_index} common_exact
        exact_vector_similarity_backend
    )
    if(NOT exact_repeat_count EQUAL 3 OR NOT exact_stat_count EQUAL 3)
        message(FATAL_ERROR "expected 3 exact timing samples")
    endif()
    string(JSON exact_stat_median GET
        "${grid_json}" seed_runs ${seed_run_index} common_exact
        exact_float_query_total_ms_stats median
    )
    if(NOT exact_total_ms EQUAL exact_stat_median)
        message(FATAL_ERROR
            "exact speedup denominator must equal median exact timing: "
            "${exact_total_ms} != ${exact_stat_median}"
        )
    endif()
    if(NOT speedup_denominator STREQUAL "median_exact_float_query_total_ms")
        message(FATAL_ERROR "unexpected speedup denominator: ${speedup_denominator}")
    endif()
    if(NOT similarity_backend MATCHES "^(scalar|sse2|avx2)$")
        message(FATAL_ERROR "unexpected exact vector backend: ${similarity_backend}")
    endif()

    string(JSON report_count LENGTH "${grid_json}" seed_runs ${seed_run_index} reports)
    if(NOT report_count EQUAL 2)
        message(FATAL_ERROR
            "expected 2 bit reports for seed_run ${seed_run_index}, got ${report_count}"
        )
    endif()

    math(EXPR last_report "${report_count} - 1")
    foreach(report_index RANGE 0 ${last_report})
        string(JSON report_bit_count GET
            "${grid_json}" seed_runs ${seed_run_index} reports ${report_index} bit_count
        )
        if(report_index EQUAL 0 AND NOT report_bit_count EQUAL 8)
            message(FATAL_ERROR "expected first report bit_count 8")
        endif()
        if(report_index EQUAL 1 AND NOT report_bit_count EQUAL 16)
            message(FATAL_ERROR "expected second report bit_count 16")
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
