#include <agent_memory.hpp>

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

    int fail(std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

    agent_memory::BinarySignature make_signature(
        std::size_t bit_count,
        std::vector<std::size_t> bits
    ) {
        agent_memory::BinarySignature signature(bit_count);
        for(const auto bit : bits) {
            signature.set_bit(bit);
        }
        return signature;
    }

    agent_memory::Metadata make_metadata(std::string scope) {
        agent_memory::Metadata metadata;
        metadata.set("scope", std::move(scope));
        return metadata;
    }

    agent_memory::BinarySignatureInfo make_signature_info(
        std::size_t bit_count = 8,
        std::string fingerprint = "encoder:fingerprint:a",
        std::string source_model_id = "embedding:model:a",
        std::string projection_kind = "document"
    ) {
        agent_memory::BinarySignatureInfo info;
        info.encoder_id = "random_hyperplane_rademacher";
        info.encoder_version = "v1";
        info.encoder_config_fingerprint = std::move(fingerprint);
        info.source_model_id = std::move(source_model_id);
        info.projection_kind = std::move(projection_kind);
        info.source_dimension = 3;
        info.bit_count = bit_count;
        info.source_similarity_metric = agent_memory::SimilarityMetric::Cosine;
        info.source_normalized = true;
        info.seed = 42;
        return info;
    }

    agent_memory::BinarySignatureRecord make_record(
        std::string chunk_id,
        agent_memory::BinarySignature signature,
        std::string scope,
        agent_memory::BinarySignatureInfo signature_info = make_signature_info()
    ) {
        return agent_memory::BinarySignatureRecord{
            agent_memory::ChunkId{std::move(chunk_id)},
            std::move(signature),
            std::move(signature_info),
            make_metadata(std::move(scope))
        };
    }

    agent_memory::BinarySignatureSearchQuery make_query(
        agent_memory::BinarySignature signature,
        std::size_t limit,
        std::vector<agent_memory::MetadataFilter> filters = {},
        agent_memory::BinarySignatureInfo signature_info = make_signature_info()
    ) {
        return agent_memory::BinarySignatureSearchQuery{
            std::move(signature),
            std::move(signature_info),
            limit,
            std::move(filters)
        };
    }

    template <typename Fn>
    bool throws_invalid_argument(Fn&& fn) {
        try {
            fn();
        } catch(const std::invalid_argument&) {
            return true;
        }
        return false;
    }

} // namespace

int main() {
    const auto signature_info = make_signature_info();
    agent_memory::FlatBinarySignatureIndex index(
        agent_memory::FlatBinarySignatureIndexOptions{signature_info}
    );

    if(index.bit_count() != 8 || index.size() != 0) {
        return fail("flat binary index must expose configured bit count and initial size");
    }

    index.upsert(make_record("chunk:exact", make_signature(8, {0, 1, 2}), "documents"));
    index.upsert(make_record("chunk:near", make_signature(8, {0, 1}), "documents"));
    index.upsert(make_record("chunk:chat", make_signature(8, {0, 3}), "chat"));
    index.upsert(make_record("chunk:far", make_signature(8, {4, 5}), "documents"));

    if(index.size() != 4) {
        return fail("flat binary index must count inserted records");
    }

    const auto stored = index.find(agent_memory::ChunkId{"chunk:exact"});
    if(!stored || stored->signature.bit_count() != 8) {
        return fail("flat binary index must find inserted records");
    }

    const auto filtered_results = index.search(make_query(
        make_signature(8, {0, 1, 2}),
        3,
        {agent_memory::MetadataFilter{"scope", "documents"}}
    ));

    if(filtered_results.size() != 3) {
        return fail("flat binary search must apply limit and metadata filters");
    }
    if(
        filtered_results[0].chunk_id.value() != "chunk:exact" ||
        filtered_results[0].hamming_distance != 0 ||
        filtered_results[1].chunk_id.value() != "chunk:near" ||
        filtered_results[1].hamming_distance != 1 ||
        filtered_results[2].chunk_id.value() != "chunk:far" ||
        filtered_results[2].hamming_distance != 5
    ) {
        return fail("flat binary search must rank by exact Hamming distance");
    }

    index.upsert(make_record("chunk:exact", make_signature(8, {7}), "documents"));
    if(index.size() != 4) {
        return fail("upsert with existing chunk id must replace without growing");
    }
    const auto replaced = index.find(agent_memory::ChunkId{"chunk:exact"});
    if(!replaced || agent_memory::hamming_distance(
        replaced->signature,
        make_signature(8, {7})
    ) != 0) {
        return fail("upsert must replace stored binary signature records");
    }

    agent_memory::FlatBinarySignatureIndex tie_index(
        agent_memory::FlatBinarySignatureIndexOptions{signature_info}
    );
    tie_index.upsert(make_record("chunk:b", make_signature(8, {1}), "tie"));
    tie_index.upsert(make_record("chunk:a", make_signature(8, {2}), "tie"));
    const auto tie_results = tie_index.search(make_query(
        make_signature(8, {0}),
        2
    ));
    if(
        tie_results.size() != 2 ||
        tie_results[0].chunk_id.value() != "chunk:a" ||
        tie_results[1].chunk_id.value() != "chunk:b"
    ) {
        return fail("flat binary index must break distance ties by chunk id");
    }

    const auto zero_limit_results = index.search(agent_memory::BinarySignatureSearchQuery{
        {},
        {},
        0,
        {}
    });
    if(!zero_limit_results.empty()) {
        return fail("zero-limit binary search must return no results");
    }

    if(!index.erase(agent_memory::ChunkId{"chunk:chat"})) {
        return fail("erase must report removed binary signature records");
    }
    if(index.find(agent_memory::ChunkId{"chunk:chat"})) {
        return fail("erase must remove binary signature records");
    }

    agent_memory::FlatBinarySignatureIndex dynamic_index;
    const auto empty_dynamic_results = dynamic_index.search(
        make_query(
            make_signature(4, {0}),
            10,
            {},
            make_signature_info(4)
        )
    );
    if(dynamic_index.bit_count() != 0 || !empty_dynamic_results.empty()) {
        return fail("empty dynamic flat binary index must accept queries and return no results");
    }

    const auto dynamic_info = make_signature_info(4);
    dynamic_index.upsert(make_record(
        "chunk:dynamic",
        make_signature(4, {0, 3}),
        "dynamic",
        dynamic_info
    ));
    if(dynamic_index.bit_count() != 4) {
        return fail("dynamic flat binary index must adopt first record bit count");
    }

    if(!throws_invalid_argument([&dynamic_index] {
        dynamic_index.upsert(make_record(
            "chunk:bad",
            make_signature(5, {0}),
            "dynamic",
            make_signature_info(5)
        ));
    })) {
        return fail("flat binary index must reject record bit-count mismatches");
    }

    if(!throws_invalid_argument([&dynamic_index] {
        (void)dynamic_index.search(make_query(
            make_signature(5, {0}),
            1,
            {},
            make_signature_info(5)
        ));
    })) {
        return fail("flat binary index must reject query bit-count mismatches");
    }

    if(!throws_invalid_argument([&dynamic_index, &dynamic_info] {
        dynamic_index.upsert(make_record("chunk:empty", {}, "dynamic", dynamic_info));
    })) {
        return fail("flat binary index must reject empty record signatures");
    }

    if(!throws_invalid_argument([&dynamic_index, &dynamic_info] {
        (void)dynamic_index.search(make_query(
            {},
            1,
            {},
            dynamic_info
        ));
    })) {
        return fail("flat binary index must reject empty query signatures");
    }

    const auto different_fingerprint_info = make_signature_info(
        4,
        "encoder:fingerprint:b"
    );
    if(!throws_invalid_argument([&dynamic_index, &different_fingerprint_info] {
        dynamic_index.upsert(make_record(
            "chunk:different-fingerprint",
            make_signature(4, {0, 3}),
            "dynamic",
            different_fingerprint_info
        ));
    })) {
        return fail("flat binary index must reject records from a different encoder fingerprint");
    }

    if(!throws_invalid_argument([&dynamic_index, &different_fingerprint_info] {
        (void)dynamic_index.search(make_query(
            make_signature(4, {0}),
            1,
            {},
            different_fingerprint_info
        ));
    })) {
        return fail("flat binary index must reject queries from a different encoder fingerprint");
    }

    const auto different_model_info = make_signature_info(
        4,
        dynamic_info.encoder_config_fingerprint,
        "embedding:model:b"
    );
    if(!throws_invalid_argument([&dynamic_index, &different_model_info] {
        dynamic_index.upsert(make_record(
            "chunk:different-model",
            make_signature(4, {0, 3}),
            "dynamic",
            different_model_info
        ));
    })) {
        return fail("flat binary index must reject records from a different source model");
    }

    const auto different_projection_info = make_signature_info(
        4,
        dynamic_info.encoder_config_fingerprint,
        dynamic_info.source_model_id,
        "query"
    );
    if(!throws_invalid_argument([&dynamic_index, &different_projection_info] {
        (void)dynamic_index.search(make_query(
            make_signature(4, {0}),
            1,
            {},
            different_projection_info
        ));
    })) {
        return fail("flat binary index must reject queries from a different projection kind");
    }

    if(!throws_invalid_argument([] {
        (void)agent_memory::FlatBinarySignatureIndex(
            agent_memory::FlatBinarySignatureIndexOptions{
                agent_memory::BinarySignatureInfo{}
            }
        );
    })) {
        return fail("flat binary index must reject invalid configured signature identity");
    }

    index.clear();
    if(index.size() != 0 || index.bit_count() != 8) {
        return fail("clear must remove all binary records without changing bit count");
    }

    return 0;
}
