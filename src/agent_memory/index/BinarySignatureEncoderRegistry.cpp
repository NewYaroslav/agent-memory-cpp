#include "BinarySignatureEncoderRegistry.hpp"

#include <agent_memory/embedding/embedding_types.hpp>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace agent_memory {
    namespace {

        [[nodiscard]] bool same_encoder_info(
            const BinarySignatureEncoderInfo& lhs,
            const BinarySignatureEncoderInfo& rhs
        ) noexcept {
            return lhs.encoder_id == rhs.encoder_id &&
                   lhs.encoder_version == rhs.encoder_version &&
                   lhs.input_dimension == rhs.input_dimension &&
                   lhs.bit_count == rhs.bit_count &&
                   lhs.seed == rhs.seed &&
                   lhs.config_fingerprint == rhs.config_fingerprint;
        }

        void validate_encoder_info(const BinarySignatureEncoderInfo& info) {
            if(info.encoder_id.empty()) {
                throw std::invalid_argument("binary signature encoder id must not be empty");
            }
            if(info.encoder_version.empty()) {
                throw std::invalid_argument("binary signature encoder version must not be empty");
            }
            if(info.input_dimension == 0) {
                throw std::invalid_argument("binary signature encoder input dimension must be positive");
            }
            if(info.bit_count == 0) {
                throw std::invalid_argument("binary signature encoder bit count must be positive");
            }
            if(info.config_fingerprint.empty()) {
                throw std::invalid_argument("binary signature encoder fingerprint must not be empty");
            }
        }

        [[nodiscard]] auto find_by_fingerprint(
            const std::vector<BinarySignatureEncoderRegistry::EncoderPtr>& encoders,
            std::string_view config_fingerprint
        ) {
            return std::find_if(
                encoders.begin(),
                encoders.end(),
                [&](const auto& encoder) {
                    return std::string_view(encoder->info().config_fingerprint) ==
                           config_fingerprint;
                }
            );
        }

    } // namespace

    bool operator==(
        const BinarySignatureInfo& lhs,
        const BinarySignatureInfo& rhs
    ) noexcept {
        return lhs.encoder_id == rhs.encoder_id &&
               lhs.encoder_version == rhs.encoder_version &&
               lhs.encoder_config_fingerprint == rhs.encoder_config_fingerprint &&
               lhs.source_model_id == rhs.source_model_id &&
               lhs.projection_kind == rhs.projection_kind &&
               lhs.source_dimension == rhs.source_dimension &&
               lhs.bit_count == rhs.bit_count &&
               lhs.source_similarity_metric == rhs.source_similarity_metric &&
               lhs.source_normalized == rhs.source_normalized &&
               lhs.seed == rhs.seed;
    }

    bool operator!=(
        const BinarySignatureInfo& lhs,
        const BinarySignatureInfo& rhs
    ) noexcept {
        return !(lhs == rhs);
    }

    BinarySignatureInfo make_binary_signature_info(
        const BinarySignatureEncoderInfo& encoder,
        const EmbeddingModelInfo& source_model,
        std::string projection_kind
    ) {
        validate_encoder_info(encoder);
        if(source_model.model_id.empty()) {
            throw std::invalid_argument("binary signature source model id must not be empty");
        }
        if(source_model.dimension == 0) {
            throw std::invalid_argument("binary signature source model dimension must be positive");
        }
        if(source_model.dimension != encoder.input_dimension) {
            throw std::invalid_argument("binary signature encoder dimension must match source model");
        }
        if(projection_kind.empty()) {
            throw std::invalid_argument("binary signature projection kind must not be empty");
        }

        BinarySignatureInfo info;
        info.encoder_id = encoder.encoder_id;
        info.encoder_version = encoder.encoder_version;
        info.encoder_config_fingerprint = encoder.config_fingerprint;
        info.source_model_id = source_model.model_id;
        info.projection_kind = std::move(projection_kind);
        info.source_dimension = source_model.dimension;
        info.bit_count = encoder.bit_count;
        info.source_similarity_metric = source_model.similarity_metric;
        info.source_normalized = source_model.normalized;
        info.seed = encoder.seed;
        return info;
    }

    bool is_compatible(
        const BinarySignatureInfo& signature_info,
        const BinarySignatureEncoderInfo& encoder
    ) noexcept {
        return signature_info.encoder_id == encoder.encoder_id &&
               signature_info.encoder_version == encoder.encoder_version &&
               signature_info.encoder_config_fingerprint == encoder.config_fingerprint &&
               signature_info.source_dimension == encoder.input_dimension &&
               signature_info.bit_count == encoder.bit_count &&
               signature_info.seed == encoder.seed;
    }

    std::size_t BinarySignatureEncoderRegistry::size() const noexcept {
        return m_encoders.size();
    }

    bool BinarySignatureEncoderRegistry::empty() const noexcept {
        return m_encoders.empty();
    }

    void BinarySignatureEncoderRegistry::register_encoder(EncoderPtr encoder) {
        if(!encoder) {
            throw std::invalid_argument("binary signature encoder registry cannot store null encoder");
        }

        const auto& info = encoder->info();
        validate_encoder_info(info);

        const auto existing = find_by_fingerprint(m_encoders, info.config_fingerprint);
        if(existing != m_encoders.end()) {
            if(!same_encoder_info((*existing)->info(), info)) {
                throw std::invalid_argument(
                    "binary signature encoder fingerprint collision with different metadata"
                );
            }
            return;
        }

        m_encoders.push_back(std::move(encoder));
        std::sort(
            m_encoders.begin(),
            m_encoders.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs->info().config_fingerprint < rhs->info().config_fingerprint;
            }
        );
    }

    bool BinarySignatureEncoderRegistry::contains(std::string_view config_fingerprint) const {
        return find_by_fingerprint(m_encoders, config_fingerprint) != m_encoders.end();
    }

    BinarySignatureEncoderRegistry::EncoderPtr BinarySignatureEncoderRegistry::find(
        std::string_view config_fingerprint
    ) const {
        const auto existing = find_by_fingerprint(m_encoders, config_fingerprint);
        if(existing == m_encoders.end()) {
            return nullptr;
        }
        return *existing;
    }

    BinarySignatureEncoderRegistry::EncoderPtr BinarySignatureEncoderRegistry::require(
        std::string_view config_fingerprint
    ) const {
        auto encoder = find(config_fingerprint);
        if(!encoder) {
            throw std::out_of_range("binary signature encoder fingerprint is not registered");
        }
        return encoder;
    }

    std::vector<BinarySignatureEncoderInfo> BinarySignatureEncoderRegistry::entries() const {
        std::vector<BinarySignatureEncoderInfo> result;
        result.reserve(m_encoders.size());
        for(const auto& encoder : m_encoders) {
            result.push_back(encoder->info());
        }
        return result;
    }

} // namespace agent_memory
