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
    string(JSON aggregate_sample_count GET
        "${grid_json}" aggregate_summary ${aggregate_index} sample_count
    )
    if(NOT aggregate_sample_count EQUAL 4)
        message(FATAL_ERROR
            "expected aggregate sample_count 4, got ${aggregate_sample_count}"
        )
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

    string(JSON report_count LENGTH "${grid_json}" seed_runs ${seed_run_index} reports)
    if(NOT report_count EQUAL 2)
        message(FATAL_ERROR
            "expected 2 bit reports for seed_run ${seed_run_index}, got ${report_count}"
        )
    endif()

    math(EXPR last_report "${report_count} - 1")
    foreach(report_index RANGE 0 ${last_report})
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
