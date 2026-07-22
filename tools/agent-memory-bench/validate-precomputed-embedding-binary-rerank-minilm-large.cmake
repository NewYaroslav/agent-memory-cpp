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
    message(FATAL_ERROR "precomputed large MiniLM embedding benchmark failed")
endif()

file(READ "${AGENT_MEMORY_BENCH_OUTPUT}" report_json)
string(JSON schema_version GET "${report_json}" schema_version)
if(NOT schema_version EQUAL 1)
    message(FATAL_ERROR "unexpected schema_version: ${schema_version}")
endif()

string(JSON mode GET "${report_json}" mode)
if(NOT mode STREQUAL "precomputed_embedding_binary_rerank_grid")
    message(FATAL_ERROR "unexpected mode: ${mode}")
endif()

string(JSON dataset_name GET "${report_json}" dataset_name)
string(JSON corpus_size GET "${report_json}" corpus_size)
string(JSON query_count GET "${report_json}" query_count)
if(NOT dataset_name STREQUAL "precomputed-embedding-minilm-l6-v2-large"
   OR NOT corpus_size EQUAL 36
   OR NOT query_count EQUAL 12)
    message(FATAL_ERROR "unexpected large MiniLM dataset identity or dimensions")
endif()

string(JSON model_id GET "${report_json}" embedding_model model_id)
string(JSON model_dimension GET "${report_json}" embedding_model dimension)
string(JSON model_normalized GET "${report_json}" embedding_model normalized)
if(NOT model_id STREQUAL "sentence-transformers/all-MiniLM-L6-v2"
   OR NOT model_dimension EQUAL 384
   OR NOT model_normalized)
    message(FATAL_ERROR "unexpected large MiniLM embedding model metadata")
endif()

string(JSON artifact_generator GET
    "${report_json}" embedding_artifact generator_id
)
string(JSON artifact_version GET
    "${report_json}" embedding_artifact generator_version
)
string(JSON artifact_dataset_revision GET
    "${report_json}" embedding_artifact dataset_revision
)
string(JSON artifact_generator_revision GET
    "${report_json}" embedding_artifact generator_revision
)
string(JSON artifact_source_hash GET
    "${report_json}" embedding_artifact generator_source_hash
)
string(JSON artifact_contract_source_hash GET
    "${report_json}" embedding_artifact generator_contract_source_hash
)
string(JSON artifact_command GET
    "${report_json}" embedding_artifact generator_command
)
string(JSON artifact_requirements GET
    "${report_json}" embedding_artifact generator_requirements_lock
)
string(JSON artifact_model_revision GET
    "${report_json}" embedding_artifact model_revision
)
string(JSON artifact_tokenizer_revision GET
    "${report_json}" embedding_artifact tokenizer_revision
)
string(JSON artifact_qrels_revision GET
    "${report_json}" embedding_artifact qrels_revision
)
string(JSON artifact_document_prompt GET
    "${report_json}" embedding_artifact document_prompt_id
)
string(JSON artifact_query_prompt GET
    "${report_json}" embedding_artifact query_prompt_id
)
string(JSON artifact_projection GET
    "${report_json}" embedding_artifact projection_kind
)
string(JSON artifact_normalization GET
    "${report_json}" embedding_artifact normalization
)
string(JSON artifact_dtype GET
    "${report_json}" embedding_artifact dtype
)
string(JSON artifact_hash_algorithm GET
    "${report_json}" embedding_artifact hash_algorithm
)
string(JSON artifact_config_hash GET
    "${report_json}" embedding_artifact config_hash
)
string(JSON artifact_dataset_hash GET
    "${report_json}" embedding_artifact dataset_hash
)
string(JSON artifact_qrels_hash GET
    "${report_json}" embedding_artifact qrels_hash
)
string(JSON artifact_artifact_hash GET
    "${report_json}" embedding_artifact artifact_hash
)
if(NOT artifact_generator STREQUAL "agent-memory.tools.minilm-precomputed-embedding"
   OR NOT artifact_version STREQUAL "v1"
   OR NOT artifact_dataset_revision STREQUAL "agent-memory-minilm-large-fixture:2026-07-22"
   OR NOT artifact_generator_revision STREQUAL "agent-memory-cpp:minilm-l6-v2-large-fixture-v1"
   OR NOT artifact_source_hash STREQUAL "4d151b05461ed7761ad18c2deedb913d10048882475e7d45b91bac6f7540dd2a"
   OR NOT artifact_contract_source_hash STREQUAL "c3ab60c1bfbeef809b27df48d12e39a6ffdd2b7b52c4903f2b41aa05b4ed98c8"
   OR NOT artifact_command STREQUAL "python tools/agent-memory-bench/generate-precomputed-minilm-large-fixture.py --output tests/eval/fixtures/precomputed-embedding-minilm-l6-v2-large.json"
   OR NOT artifact_requirements STREQUAL "tools/agent-memory-bench/requirements-minilm-fixture.txt;sha256=8f3ff40e6a27b0a723b9c37a397cd80a14e0b515a86383bd82360de49da250b5"
   OR NOT artifact_model_revision STREQUAL "1110a243fdf4706b3f48f1d95db1a4f5529b4d41"
   OR NOT artifact_tokenizer_revision STREQUAL "1110a243fdf4706b3f48f1d95db1a4f5529b4d41"
   OR NOT artifact_qrels_revision STREQUAL "agent-memory-minilm-large-qrels:2026-07-22"
   OR NOT artifact_document_prompt STREQUAL "title-plus-text-v1"
   OR NOT artifact_query_prompt STREQUAL "query-text-v1"
   OR NOT artifact_projection STREQUAL "minilm_l6_v2_mean_pool_normalized"
   OR NOT artifact_normalization STREQUAL "l2"
   OR NOT artifact_dtype STREQUAL "float32"
   OR NOT artifact_hash_algorithm STREQUAL "sha256"
   OR NOT artifact_config_hash STREQUAL "3aaffc9f01047d76f4d5f1f22ed194cacf1756e94a5f0bde9070dfda822df1d5"
   OR NOT artifact_dataset_hash STREQUAL "61fe0cadc963f59ecf804b179f98985b022d3af3bc81a809f692eb50b1e0f23d"
   OR NOT artifact_qrels_hash STREQUAL "d144b10c45e36c2b0404245f0e914f2bbab201810b95845f976c649cfff8a553"
   OR NOT artifact_artifact_hash STREQUAL "b802d414358da76ad2388be64b458345e5e40e6c59d03564d8d5ff6cd635850f")
    message(FATAL_ERROR "large MiniLM embedding artifact provenance was not reported")
endif()

file(SHA256
    "${AGENT_MEMORY_BENCH_WORKDIR}/tools/agent-memory-bench/generate-precomputed-minilm-large-fixture.py"
    actual_generator_driver_hash
)
file(SHA256
    "${AGENT_MEMORY_BENCH_WORKDIR}/tools/agent-memory-bench/minilm_fixture_generator_common.py"
    actual_generator_common_hash
)
file(SHA256
    "${AGENT_MEMORY_BENCH_WORKDIR}/tools/agent-memory-bench/precomputed_fixture_contract.py"
    actual_canonical_contract_source_hash
)
file(SHA256
    "${AGENT_MEMORY_BENCH_WORKDIR}/tools/agent-memory-bench/precomputed_fixture_large_contract.py"
    actual_content_contract_source_hash
)
set(contract_hash_payload
    "${actual_canonical_contract_source_hash}\n${actual_content_contract_source_hash}\n"
)
string(SHA256 actual_contract_source_hash "${contract_hash_payload}")
file(SHA256
    "${AGENT_MEMORY_BENCH_WORKDIR}/tools/agent-memory-bench/requirements-minilm-fixture.txt"
    actual_requirements_lock_hash
)
set(expected_requirements_lock
    "tools/agent-memory-bench/requirements-minilm-fixture.txt;sha256=${actual_requirements_lock_hash}"
)
set(generator_hash_payload
    "${actual_generator_driver_hash}\n${actual_generator_common_hash}\n"
)
string(SHA256 actual_generator_source_hash "${generator_hash_payload}")
if(NOT actual_generator_source_hash STREQUAL artifact_source_hash
   OR NOT actual_contract_source_hash STREQUAL artifact_contract_source_hash
   OR NOT expected_requirements_lock STREQUAL artifact_requirements)
    message(FATAL_ERROR
        "large MiniLM generator provenance does not match checked-in files"
    )
endif()

string(JSON exact_recall GET "${report_json}" exact_oracle quality recall_at_10)
string(JSON exact_ndcg GET "${report_json}" exact_oracle quality ndcg_at_10)
if(exact_recall LESS 0.95 OR exact_ndcg LESS 0.90)
    message(FATAL_ERROR "large MiniLM exact oracle quality unexpectedly regressed")
endif()

string(JSON report_count LENGTH "${report_json}" reports)
if(NOT report_count EQUAL 4)
    message(FATAL_ERROR "expected 4 large MiniLM encoder reports, got ${report_count}")
endif()

math(EXPR last_report "${report_count} - 1")
foreach(report_index RANGE 0 ${last_report})
    if(report_index LESS 2)
        set(expected_encoder_family "random_hyperplane_rademacher")
    else()
        set(expected_encoder_family "randomized_hadamard_projection")
    endif()
    if(report_index EQUAL 0 OR report_index EQUAL 2)
        set(expected_bit_count 128)
    else()
        set(expected_bit_count 256)
    endif()

    string(JSON actual_encoder_family GET
        "${report_json}" reports ${report_index} encoder_family
    )
    string(JSON actual_bit_count GET
        "${report_json}" reports ${report_index} bit_count
    )
    string(JSON actual_encoder_seed GET
        "${report_json}" reports ${report_index} encoder_seed
    )
    if(NOT actual_encoder_family STREQUAL expected_encoder_family
       OR NOT actual_bit_count EQUAL expected_bit_count
       OR NOT actual_encoder_seed EQUAL 42)
        message(FATAL_ERROR
            "unexpected large MiniLM report identity at index ${report_index}"
        )
    endif()

    string(JSON rerank_count LENGTH "${report_json}" reports ${report_index} rerank)
    if(NOT rerank_count EQUAL 4)
        message(FATAL_ERROR
            "each large MiniLM report must contain 4 candidate rows"
        )
    endif()

    foreach(rerank_index RANGE 0 3)
        if(rerank_index EQUAL 0)
            set(expected_candidate_limit 10)
        elseif(rerank_index EQUAL 1)
            set(expected_candidate_limit 18)
        elseif(rerank_index EQUAL 2)
            set(expected_candidate_limit 27)
        else()
            set(expected_candidate_limit 36)
        endif()

        string(JSON actual_candidate_limit GET
            "${report_json}" reports ${report_index} rerank ${rerank_index}
            candidate_limit
        )
        if(NOT actual_candidate_limit EQUAL expected_candidate_limit)
            message(FATAL_ERROR
                "unexpected large MiniLM candidate limit at report "
                "${report_index}, row ${rerank_index}"
            )
        endif()
    endforeach()

    string(JSON first_qrels_coverage GET
        "${report_json}" reports ${report_index} rerank 0
        qrels_candidate_relevant_coverage
    )
    if(first_qrels_coverage LESS 0.90)
        message(FATAL_ERROR
            "large MiniLM first candidate row lost too many qrels-relevant items"
        )
    endif()

    string(JSON full_candidate_limit GET
        "${report_json}" reports ${report_index} rerank 3 candidate_limit
    )
    string(JSON full_exact_coverage GET
        "${report_json}" reports ${report_index} rerank 3
        exact_top_k_candidate_coverage
    )
    string(JSON full_qrels_coverage GET
        "${report_json}" reports ${report_index} rerank 3
        qrels_candidate_relevant_coverage
    )
    if(NOT full_candidate_limit EQUAL 36
       OR NOT full_exact_coverage EQUAL 1
       OR NOT full_qrels_coverage EQUAL 1)
        message(FATAL_ERROR
            "full large MiniLM candidate row must cover exact top-k and qrels"
        )
    endif()
endforeach()
