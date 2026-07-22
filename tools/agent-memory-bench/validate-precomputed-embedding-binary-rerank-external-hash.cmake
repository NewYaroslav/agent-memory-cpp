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
    message(FATAL_ERROR "precomputed external-hash embedding benchmark failed")
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
if(NOT dataset_name STREQUAL "precomputed-embedding-external-hash"
   OR NOT corpus_size EQUAL 12
   OR NOT query_count EQUAL 5)
    message(FATAL_ERROR "unexpected external-hash dataset identity or dimensions")
endif()

string(JSON model_id GET "${report_json}" embedding_model model_id)
string(JSON model_dimension GET "${report_json}" embedding_model dimension)
string(JSON model_normalized GET "${report_json}" embedding_model normalized)
if(NOT model_id STREQUAL "agent-memory.external-hash-embedding-32d-v1"
   OR NOT model_dimension EQUAL 32
   OR NOT model_normalized)
    message(FATAL_ERROR "unexpected external-hash embedding model metadata")
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
string(JSON artifact_model_revision GET
    "${report_json}" embedding_artifact model_revision
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
if(NOT artifact_generator STREQUAL "agent-memory.tools.external-hash-embedding"
   OR NOT artifact_version STREQUAL "v1"
   OR NOT artifact_dataset_revision STREQUAL "agent-memory-external-hash-fixture:2026-07-22"
   OR NOT artifact_generator_revision STREQUAL "agent-memory-cpp:external-hash-fixture-v1"
   OR NOT artifact_model_revision STREQUAL "external-hash-embedding-32d-v1"
   OR NOT artifact_qrels_revision STREQUAL "agent-memory-external-hash-qrels:2026-07-22"
   OR NOT artifact_document_prompt STREQUAL "title-plus-text-v1"
   OR NOT artifact_query_prompt STREQUAL "query-text-v1"
   OR NOT artifact_projection STREQUAL "external_hash_text_ngrams_32d"
   OR NOT artifact_normalization STREQUAL "l2"
   OR NOT artifact_dtype STREQUAL "float32"
   OR NOT artifact_hash_algorithm STREQUAL "sha256"
   OR NOT artifact_config_hash STREQUAL "51adac9183b83b5d90769b3063b51fb4e296460ffdba475de9e9743e54e84d36"
   OR NOT artifact_dataset_hash STREQUAL "86a39ff110bf1becf553d360fac1e579539ed132411756fd3413b76fab749c38"
   OR NOT artifact_qrels_hash STREQUAL "7900952369b02c1222c774d6b634d70cdb5bbc8e86a470e1744a48e958821a3a"
   OR NOT artifact_artifact_hash STREQUAL "d34be4248d6b6391ffb5404fc337329c605937178030432503911acb6fdb6784")
    message(FATAL_ERROR "external-hash embedding artifact provenance was not reported")
endif()

string(JSON exact_recall GET "${report_json}" exact_oracle quality recall_at_10)
string(JSON exact_ndcg GET "${report_json}" exact_oracle quality ndcg_at_10)
if(exact_recall LESS 0.8 OR exact_ndcg LESS 0.8)
    message(FATAL_ERROR "external-hash exact oracle quality unexpectedly regressed")
endif()

string(JSON report_count LENGTH "${report_json}" reports)
if(NOT report_count EQUAL 7)
    message(FATAL_ERROR "expected 7 encoder/bit reports, got ${report_count}")
endif()

math(EXPR last_report "${report_count} - 1")
foreach(report_index RANGE 0 ${last_report})
    string(JSON rerank_count LENGTH "${report_json}" reports ${report_index} rerank)
    if(NOT rerank_count EQUAL 3)
        message(FATAL_ERROR "each external-hash report must contain 3 candidate rows")
    endif()

    string(JSON full_candidate_limit GET
        "${report_json}" reports ${report_index} rerank 2 candidate_limit
    )
    string(JSON full_exact_coverage GET
        "${report_json}" reports ${report_index} rerank 2
        exact_top_k_candidate_coverage
    )
    string(JSON full_qrels_coverage GET
        "${report_json}" reports ${report_index} rerank 2
        qrels_candidate_relevant_coverage
    )
    if(NOT full_candidate_limit EQUAL 12
       OR NOT full_exact_coverage EQUAL 1
       OR NOT full_qrels_coverage EQUAL 1)
        message(FATAL_ERROR
            "full external-hash candidate row must cover exact top-k and qrels"
        )
    endif()
endforeach()
