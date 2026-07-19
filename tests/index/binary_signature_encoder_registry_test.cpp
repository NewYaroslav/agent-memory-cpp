#include <agent_memory.hpp>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

    int fail(std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

    template <typename Exception, typename Fn>
    bool throws(Fn&& fn) {
        try {
            fn();
        } catch(const Exception&) {
            return true;
        }
        return false;
    }

    agent_memory::RandomHyperplaneBinaryEncoderOptions make_options(
        std::size_t input_dimension,
        std::size_t bit_count,
        std::uint64_t seed
    ) {
        agent_memory::RandomHyperplaneBinaryEncoderOptions options;
        options.input_dimension = input_dimension;
        options.bit_count = bit_count;
        options.seed = seed;
        return options;
    }

    agent_memory::EmbeddingModelInfo make_model(std::size_t dimension = 3) {
        agent_memory::EmbeddingModelInfo model;
        model.model_id = "test-embedding-model";
        model.dimension = dimension;
        model.similarity_metric = agent_memory::SimilarityMetric::Cosine;
        model.normalized = true;
        return model;
    }

    class FakeBinarySignatureEncoder final : public agent_memory::IBinarySignatureEncoder {
    public:
        explicit FakeBinarySignatureEncoder(agent_memory::BinarySignatureEncoderInfo info)
            : m_info(std::move(info)) {}

        [[nodiscard]] const agent_memory::BinarySignatureEncoderInfo& info() const noexcept override {
            return m_info;
        }

        [[nodiscard]] agent_memory::BinarySignature encode(
            const agent_memory::Embedding& vector
        ) const override {
            if(vector.dimension() != m_info.input_dimension) {
                throw std::invalid_argument("fake encoder dimension mismatch");
            }
            return agent_memory::BinarySignature(m_info.bit_count);
        }

    private:
        agent_memory::BinarySignatureEncoderInfo m_info;
    };

    agent_memory::BinarySignatureEncoderInfo make_fake_info(std::string fingerprint) {
        agent_memory::BinarySignatureEncoderInfo info;
        info.encoder_id = "fake";
        info.encoder_version = "v1";
        info.input_dimension = 3;
        info.bit_count = 16;
        info.seed = 7;
        info.config_fingerprint = std::move(fingerprint);
        return info;
    }

} // namespace

int main() {
    using agent_memory::BinarySignatureEncoderRegistry;
    using agent_memory::RandomHyperplaneBinaryEncoder;

    BinarySignatureEncoderRegistry registry;
    if(!registry.empty() || registry.size() != 0) {
        return fail("new binary encoder registry must be empty");
    }
    if(registry.find("missing") != nullptr || registry.contains("missing")) {
        return fail("missing encoder fingerprint must not be found");
    }
    if(!throws<std::out_of_range>([&] {
           (void)registry.require("missing");
       })) {
        return fail("requiring a missing encoder fingerprint must throw");
    }
    if(!throws<std::invalid_argument>([&] {
           registry.register_encoder(nullptr);
       })) {
        return fail("registry must reject null encoders");
    }

    auto encoder = std::make_shared<RandomHyperplaneBinaryEncoder>(make_options(3, 16, 42));
    registry.register_encoder(encoder);
    if(registry.empty() || registry.size() != 1) {
        return fail("registry must store one encoder");
    }
    if(!registry.contains(encoder->info().config_fingerprint)) {
        return fail("registry must find registered fingerprint");
    }
    if(registry.find(encoder->info().config_fingerprint) != encoder) {
        return fail("registry find must return the registered encoder");
    }
    if(registry.require(encoder->info().config_fingerprint) != encoder) {
        return fail("registry require must return the registered encoder");
    }

    auto duplicate = std::make_shared<RandomHyperplaneBinaryEncoder>(make_options(3, 16, 42));
    registry.register_encoder(duplicate);
    if(registry.size() != 1 || registry.find(encoder->info().config_fingerprint) != encoder) {
        return fail("registry registration must be idempotent for identical encoder metadata");
    }

    auto second = std::make_shared<RandomHyperplaneBinaryEncoder>(make_options(3, 8, 42));
    registry.register_encoder(second);
    if(registry.size() != 2) {
        return fail("registry must store distinct fingerprints");
    }
    const auto entries = registry.entries();
    if(entries.size() != 2 ||
       entries.front().config_fingerprint > entries.back().config_fingerprint) {
        return fail("registry entries must be sorted by config fingerprint");
    }

    auto collision_info = make_fake_info(encoder->info().config_fingerprint);
    collision_info.bit_count = 32;
    if(!throws<std::invalid_argument>([&] {
           registry.register_encoder(std::make_shared<FakeBinarySignatureEncoder>(collision_info));
       })) {
        return fail("registry must reject fingerprint collisions with different metadata");
    }

    auto empty_fingerprint = make_fake_info("");
    if(!throws<std::invalid_argument>([&] {
           registry.register_encoder(std::make_shared<FakeBinarySignatureEncoder>(empty_fingerprint));
       })) {
        return fail("registry must reject encoders without config fingerprint");
    }

    const auto signature_info =
        agent_memory::make_binary_signature_info(encoder->info(), make_model(), "document");
    if(signature_info.encoder_id != encoder->info().encoder_id ||
       signature_info.encoder_version != encoder->info().encoder_version ||
       signature_info.encoder_config_fingerprint != encoder->info().config_fingerprint ||
       signature_info.source_model_id != "test-embedding-model" ||
       signature_info.projection_kind != "document" ||
       signature_info.source_dimension != 3 ||
       signature_info.bit_count != 16 ||
       signature_info.source_similarity_metric != agent_memory::SimilarityMetric::Cosine ||
       !signature_info.source_normalized ||
       signature_info.seed != 42) {
        return fail("binary signature info must carry encoder and source model identity");
    }
    if(!agent_memory::is_compatible(signature_info, encoder->info())) {
        return fail("signature metadata must be compatible with the encoder that created it");
    }
    if(agent_memory::is_compatible(signature_info, second->info())) {
        return fail("signature metadata must reject incompatible encoder config");
    }
    const auto same_signature_info =
        agent_memory::make_binary_signature_info(encoder->info(), make_model(), "document");
    if(signature_info != same_signature_info) {
        return fail("binary signature info equality must compare identity fields");
    }
    const auto query_signature_info =
        agent_memory::make_binary_signature_info(encoder->info(), make_model(), "query");
    if(signature_info == query_signature_info) {
        return fail("binary signature info equality must include projection kind");
    }

    if(!throws<std::invalid_argument>([&] {
           (void)agent_memory::make_binary_signature_info(
               encoder->info(),
               make_model(4),
               "document"
           );
       })) {
        return fail("binary signature info must reject source dimension mismatch");
    }
    if(!throws<std::invalid_argument>([&] {
           (void)agent_memory::make_binary_signature_info(
               encoder->info(),
               make_model(),
               ""
           );
       })) {
        return fail("binary signature info must reject empty projection kind");
    }

    auto no_model = make_model();
    no_model.model_id.clear();
    if(!throws<std::invalid_argument>([&] {
           (void)agent_memory::make_binary_signature_info(
               encoder->info(),
               no_model,
               "document"
           );
       })) {
        return fail("binary signature info must reject empty source model id");
    }

    return 0;
}
