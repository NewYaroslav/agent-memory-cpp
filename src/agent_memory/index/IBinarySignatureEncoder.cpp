#include "IBinarySignatureEncoder.hpp"

namespace agent_memory {

    IBinarySignatureEncoder::~IBinarySignatureEncoder() = default;

    std::vector<BinarySignature> IBinarySignatureEncoder::encode_batch(
        const std::vector<Embedding>& vectors
    ) const {
        std::vector<BinarySignature> signatures;
        signatures.reserve(vectors.size());
        for(const auto& vector : vectors) {
            signatures.push_back(encode(vector));
        }
        return signatures;
    }

} // namespace agent_memory
