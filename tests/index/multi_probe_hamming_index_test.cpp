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

    agent_memory::BinarySignature make_signature(std::vector<std::size_t> bits) {
        agent_memory::BinarySignature signature(16);
        for(const auto bit : bits) {
            signature.set_bit(bit);
        }
        return signature;
    }

    agent_memory::BinarySignatureInfo make_info() {
        agent_memory::BinarySignatureInfo info;
        info.encoder_id = "test";
        info.encoder_version = "v1";
        info.encoder_config_fingerprint = "test:16";
        info.source_model_id = "model:test";
        info.projection_kind = "symmetric";
        info.source_dimension = 4;
        info.bit_count = 16;
        info.source_similarity_metric = agent_memory::SimilarityMetric::Cosine;
        info.source_normalized = true;
        return info;
    }

    agent_memory::BinarySignatureRecord make_record(
        std::string id,
        std::vector<std::size_t> bits,
        std::string scope
    ) {
        agent_memory::Metadata metadata;
        metadata.set("scope", std::move(scope));
        return {
            agent_memory::ChunkId{std::move(id)},
            make_signature(std::move(bits)),
            make_info(),
            std::move(metadata)
        };
    }

    agent_memory::BinarySignatureSearchQuery make_query(std::size_t limit) {
        return {make_signature({}), make_info(), limit, {}};
    }

    template <typename Function>
    bool throws_invalid_argument(Function&& function) {
        try {
            function();
        } catch(const std::invalid_argument&) {
            return true;
        }
        return false;
    }

} // namespace

int main() {
    agent_memory::MultiProbeHammingIndexOptions options;
    options.signature_info = make_info();
    options.table_count = 2;
    options.bits_per_table = 4;
    options.max_probe_radius = 0;
    options.candidate_multiplier = 1;
    options.minimum_candidate_count = 0;
    agent_memory::MultiProbeHammingIndex index(options);

    index.upsert(make_record("chunk:exact", {}, "keep"));
    index.upsert(make_record("chunk:near", {1}, "keep"));
    index.upsert(make_record("chunk:miss-a", {0, 8}, "keep"));
    index.upsert(make_record("chunk:miss-b", {2, 10}, "drop"));

    const auto diagnostic = index.search_with_diagnostics(make_query(2));
    if(diagnostic.candidate_count != 2 || diagnostic.candidate_count >= index.size()) {
        return fail("multi-probe search must rank a strict sub-linear candidate subset");
    }
    if(diagnostic.results.size() != 2
       || diagnostic.results[0].chunk_id.value() != "chunk:exact"
       || diagnostic.results[0].hamming_distance != 0
       || diagnostic.results[1].chunk_id.value() != "chunk:near"
       || diagnostic.results[1].hamming_distance != 1) {
        return fail("multi-probe search must exact-rank the candidates it discovers");
    }

    index.upsert(make_record("chunk:near", {3}, "updated"));
    const auto updated = index.find(agent_memory::ChunkId{"chunk:near"});
    if(!updated || !updated->metadata.get("scope")
       || *updated->metadata.get("scope") != "updated") {
        return fail("multi-probe upsert must replace signature and metadata in place");
    }

    if(!index.erase(agent_memory::ChunkId{"chunk:exact"})
       || index.find(agent_memory::ChunkId{"chunk:exact"})) {
        return fail("multi-probe erase must remove records and bucket postings");
    }
    if(index.size() != 3 || index.search(make_query(2)).empty()) {
        return fail("multi-probe erase must preserve swapped record positions");
    }

    if(!throws_invalid_argument([] {
           agent_memory::MultiProbeHammingIndexOptions invalid;
           invalid.signature_info = make_info();
           invalid.table_count = 5;
           invalid.bits_per_table = 4;
           (void)agent_memory::MultiProbeHammingIndex(invalid);
       })) {
        return fail("multi-probe index must reject projections wider than signatures");
    }

    index.clear();
    if(index.size() != 0 || index.bit_count() != 16) {
        return fail("multi-probe clear must preserve configured identity");
    }

    return 0;
}
