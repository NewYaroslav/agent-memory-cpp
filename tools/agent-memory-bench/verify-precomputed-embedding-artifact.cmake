if(NOT DEFINED AGENT_MEMORY_PRECOMPUTED_EMBEDDING_DATASET)
    message(FATAL_ERROR "AGENT_MEMORY_PRECOMPUTED_EMBEDDING_DATASET must be defined")
endif()

file(READ "${AGENT_MEMORY_PRECOMPUTED_EMBEDDING_DATASET}" report_json)

function(agent_memory_json_get output_variable)
    string(JSON value ERROR_VARIABLE json_error GET "${report_json}" ${ARGN})
    if(json_error)
        message(FATAL_ERROR
            "failed to read JSON path '${ARGN}' from "
            "${AGENT_MEMORY_PRECOMPUTED_EMBEDDING_DATASET}: ${json_error}"
        )
    endif()
    set("${output_variable}" "${value}" PARENT_SCOPE)
endfunction()

function(agent_memory_json_length output_variable)
    string(JSON value ERROR_VARIABLE json_error LENGTH "${report_json}" ${ARGN})
    if(json_error)
        message(FATAL_ERROR
            "failed to read JSON length '${ARGN}' from "
            "${AGENT_MEMORY_PRECOMPUTED_EMBEDDING_DATASET}: ${json_error}"
        )
    endif()
    set("${output_variable}" "${value}" PARENT_SCOPE)
endfunction()

function(agent_memory_append_embedding_payload output_variable record_kind json_field)
    set(payload "${${output_variable}}")
    agent_memory_json_length(record_count ${json_field})
    if(record_count EQUAL 0)
        set("${output_variable}" "${payload}" PARENT_SCOPE)
        return()
    endif()
    math(EXPR last_record "${record_count} - 1")
    foreach(record_index RANGE 0 ${last_record})
        agent_memory_json_get(record_id ${json_field} ${record_index} id)
        agent_memory_json_length(value_count ${json_field} ${record_index} vector)
        if(value_count EQUAL 0)
            message(FATAL_ERROR
                "embedding vector '${json_field}[${record_index}]' must not be empty"
            )
        endif()
        math(EXPR last_value "${value_count} - 1")
        set(values "")
        foreach(value_index RANGE 0 ${last_value})
            agent_memory_json_get(value ${json_field} ${record_index} vector ${value_index})
            if(value_index EQUAL 0)
                set(values "${value}")
            else()
                string(APPEND values ",${value}")
            endif()
        endforeach()
        string(APPEND payload "${record_kind}|${record_id}|${values}\n")
    endforeach()
    set("${output_variable}" "${payload}" PARENT_SCOPE)
endfunction()

agent_memory_json_get(model_id embedding_model model_id)
agent_memory_json_get(model_dimension embedding_model dimension)
agent_memory_json_get(model_similarity_metric embedding_model similarity_metric)
agent_memory_json_get(model_pooling_mode embedding_model pooling_mode)
agent_memory_json_get(model_normalized embedding_model normalized)

agent_memory_json_get(generator_id embedding_artifact generator_id)
agent_memory_json_get(generator_version embedding_artifact generator_version)
agent_memory_json_get(dataset_revision embedding_artifact dataset_revision)
agent_memory_json_get(generator_revision embedding_artifact generator_revision)
agent_memory_json_get(model_revision embedding_artifact model_revision)
agent_memory_json_get(qrels_revision embedding_artifact qrels_revision)
agent_memory_json_get(document_prompt_id embedding_artifact document_prompt_id)
agent_memory_json_get(query_prompt_id embedding_artifact query_prompt_id)
agent_memory_json_get(projection_kind embedding_artifact projection_kind)
agent_memory_json_get(normalization embedding_artifact normalization)
agent_memory_json_get(dtype embedding_artifact dtype)
agent_memory_json_get(hash_algorithm embedding_artifact hash_algorithm)
agent_memory_json_get(expected_config_hash embedding_artifact config_hash)
agent_memory_json_get(expected_artifact_hash embedding_artifact artifact_hash)

if(NOT hash_algorithm STREQUAL "sha256")
    message(FATAL_ERROR "embedding_artifact.hash_algorithm must be sha256")
endif()

if(model_normalized AND NOT normalization STREQUAL "l2")
    message(FATAL_ERROR
        "embedding_model.normalized=true requires embedding_artifact.normalization=l2"
    )
endif()
if(NOT model_normalized AND NOT normalization STREQUAL "none")
    message(FATAL_ERROR
        "embedding_model.normalized=false requires embedding_artifact.normalization=none"
    )
endif()

if(NOT dtype MATCHES "^(float16|bfloat16|float32|float64)$")
    message(FATAL_ERROR "embedding_artifact.dtype uses an unsupported value")
endif()

set(canonical_config "")
string(APPEND canonical_config "dataset_revision=${dataset_revision}\n")
string(APPEND canonical_config "document_prompt_id=${document_prompt_id}\n")
string(APPEND canonical_config "dtype=${dtype}\n")
string(APPEND canonical_config "embedding_dimension=${model_dimension}\n")
string(APPEND canonical_config "embedding_model_id=${model_id}\n")
string(APPEND canonical_config "embedding_normalized=${model_normalized}\n")
string(APPEND canonical_config "generator_id=${generator_id}\n")
string(APPEND canonical_config "generator_revision=${generator_revision}\n")
string(APPEND canonical_config "generator_version=${generator_version}\n")
string(APPEND canonical_config "model_revision=${model_revision}\n")
string(APPEND canonical_config "normalization=${normalization}\n")
string(APPEND canonical_config "pooling_mode=${model_pooling_mode}\n")
string(APPEND canonical_config "projection_kind=${projection_kind}\n")
string(APPEND canonical_config "qrels_revision=${qrels_revision}\n")
string(APPEND canonical_config "query_prompt_id=${query_prompt_id}\n")
string(APPEND canonical_config "similarity_metric=${model_similarity_metric}\n")
string(SHA256 actual_config_hash "${canonical_config}")

set(canonical_payload "")
agent_memory_append_embedding_payload(canonical_payload "document" document_embeddings)
agent_memory_append_embedding_payload(canonical_payload "query" query_embeddings)
string(SHA256 actual_artifact_hash "${canonical_payload}")

if(NOT actual_config_hash STREQUAL expected_config_hash)
    message(FATAL_ERROR
        "config_hash mismatch for ${AGENT_MEMORY_PRECOMPUTED_EMBEDDING_DATASET}: "
        "expected ${expected_config_hash}, got ${actual_config_hash}"
    )
endif()

if(NOT actual_artifact_hash STREQUAL expected_artifact_hash)
    message(FATAL_ERROR
        "artifact_hash mismatch for ${AGENT_MEMORY_PRECOMPUTED_EMBEDDING_DATASET}: "
        "expected ${expected_artifact_hash}, got ${actual_artifact_hash}"
    )
endif()
