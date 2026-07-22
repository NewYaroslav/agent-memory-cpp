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
    message(FATAL_ERROR "precomputed embedding medium benchmark failed")
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

string(JSON corpus_size GET "${report_json}" corpus_size)
string(JSON query_count GET "${report_json}" query_count)
if(NOT corpus_size EQUAL 12 OR NOT query_count EQUAL 4)
    message(FATAL_ERROR "unexpected precomputed medium dataset dimensions")
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
string(JSON artifact_artifact_hash GET
    "${report_json}" embedding_artifact artifact_hash
)
if(NOT artifact_generator STREQUAL "agent-memory.fixture.semantic-axis"
   OR NOT artifact_version STREQUAL "v1"
   OR NOT artifact_dataset_revision STREQUAL "hand-authored:2026-07-22"
   OR NOT artifact_generator_revision STREQUAL "agent-memory-cpp:820e967"
   OR NOT artifact_model_revision STREQUAL "fixture-semantic-axis-8d-v1"
   OR NOT artifact_qrels_revision STREQUAL "hand-authored:2026-07-22"
   OR NOT artifact_document_prompt STREQUAL "identity-document-v1"
   OR NOT artifact_query_prompt STREQUAL "identity-query-v1"
   OR NOT artifact_projection STREQUAL "semantic_axes_8d"
   OR NOT artifact_normalization STREQUAL "l2"
   OR NOT artifact_dtype STREQUAL "float32"
   OR NOT artifact_hash_algorithm STREQUAL "sha256"
   OR NOT artifact_config_hash STREQUAL "23ba385225fea17324e2f5d53fe94340ce9a2ed5deb3f256901198b0ea155c08"
   OR NOT artifact_artifact_hash STREQUAL "542bec06c248884525e783dc30cf110a3441c1267753f9c2661cfec7588e04f0")
    message(FATAL_ERROR "embedding artifact provenance was not reported")
endif()

string(JSON exact_recall GET "${report_json}" exact_oracle quality recall_at_10)
string(JSON exact_ndcg GET "${report_json}" exact_oracle quality ndcg_at_10)
if(NOT exact_recall EQUAL 1 OR NOT exact_ndcg EQUAL 1)
    message(FATAL_ERROR "exact oracle must recover all qrels in the medium fixture")
endif()

string(JSON report_count LENGTH "${report_json}" reports)
if(NOT report_count EQUAL 7)
    message(FATAL_ERROR "expected 7 encoder/bit reports, got ${report_count}")
endif()

set(saw_random_4 FALSE)
set(saw_random_8 FALSE)
set(saw_coordinate_8 FALSE)
set(saw_pca_4 FALSE)
set(saw_pca_8 FALSE)
set(saw_itq_4 FALSE)
set(saw_itq_8 FALSE)
math(EXPR last_report "${report_count} - 1")
foreach(report_index RANGE 0 ${last_report})
    string(JSON encoder_family GET
        "${report_json}" reports ${report_index} encoder_family
    )
    string(JSON bit_count GET "${report_json}" reports ${report_index} bit_count)
    if(encoder_family STREQUAL "random_hyperplane_rademacher")
        if(bit_count EQUAL 4)
            set(saw_random_4 TRUE)
        elseif(bit_count EQUAL 8)
            set(saw_random_8 TRUE)
        else()
            message(FATAL_ERROR "unexpected random-hyperplane bit_count")
        endif()
    elseif(encoder_family STREQUAL "coordinate_sign")
        if(NOT bit_count EQUAL 8)
            message(FATAL_ERROR "coordinate_sign must emit 8 bits for this fixture")
        endif()
        set(saw_coordinate_8 TRUE)
    elseif(encoder_family STREQUAL "pca_projection")
        if(bit_count EQUAL 4)
            set(saw_pca_4 TRUE)
        elseif(bit_count EQUAL 8)
            set(saw_pca_8 TRUE)
        else()
            message(FATAL_ERROR "unexpected PCA bit_count")
        endif()
    elseif(encoder_family STREQUAL "itq_rotation_projection")
        if(bit_count EQUAL 4)
            set(saw_itq_4 TRUE)
        elseif(bit_count EQUAL 8)
            set(saw_itq_8 TRUE)
        else()
            message(FATAL_ERROR "unexpected ITQ bit_count")
        endif()
    else()
        message(FATAL_ERROR "unexpected encoder_family: ${encoder_family}")
    endif()

    string(JSON rerank_count LENGTH "${report_json}" reports ${report_index} rerank)
    if(NOT rerank_count EQUAL 3)
        message(FATAL_ERROR "each report must contain 3 candidate-limit rows")
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
    string(JSON full_recall GET
        "${report_json}" reports ${report_index} rerank 2 reranked_recall_at_10
    )
    string(JSON full_ndcg GET
        "${report_json}" reports ${report_index} rerank 2 reranked_ndcg_at_10
    )
    if(NOT full_candidate_limit EQUAL 12
       OR NOT full_exact_coverage EQUAL 1
       OR NOT full_qrels_coverage EQUAL 1
       OR NOT full_recall EQUAL 1
       OR NOT full_ndcg EQUAL 1)
        message(FATAL_ERROR
            "full-corpus candidate row must recover exact top-k and qrels quality"
        )
    endif()
endforeach()

if(NOT saw_random_4 OR NOT saw_random_8 OR NOT saw_coordinate_8
   OR NOT saw_pca_4 OR NOT saw_pca_8 OR NOT saw_itq_4 OR NOT saw_itq_8)
    message(FATAL_ERROR "precomputed medium report is missing an expected encoder row")
endif()
