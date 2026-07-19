#include "BinarySignatureInfo.hpp"

#include <agent_memory/embedding/embedding_types.hpp>
#include <agent_memory/index/IBinarySignatureEncoder.hpp>

#include <stdexcept>
#include <utility>

namespace agent_memory {
    namespace {

        void validate_encoder_info(const BinarySignatureEncoderInfo& info) {
            if(info.encoder_id.empty()) {
                throw std::invalid_argument("binary signature encoder id must not be empty");
            }
            if(info.encoder_version.empty()) {
                throw std::invalid_argument("binary signature encoder version must not be empty");
            }
            if(info.input_dimension == 0) {
                throw std::invalid_argument(
                    "binary signature encoder input dimension must be positive"
                );
            }
            if(info.bit_count == 0) {
                throw std::invalid_argument(
                    "binary signature encoder bit count must be positive"
                );
            }
            if(info.config_fingerprint.empty()) {
                throw std::invalid_argument("binary signature encoder fingerprint must not be empty");
            }
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

    bool is_valid(const BinarySignatureInfo& info) noexcept {
        return !info.encoder_id.empty() &&
               !info.encoder_version.empty() &&
               !info.encoder_config_fingerprint.empty() &&
               !info.source_model_id.empty() &&
               !info.projection_kind.empty() &&
               info.source_dimension != 0 &&
               info.bit_count != 0;
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

} // namespace agent_memory
