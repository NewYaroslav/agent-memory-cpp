#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_LEARNED_PROJECTION_BINARY_ENCODER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_LEARNED_PROJECTION_BINARY_ENCODER_HPP_INCLUDED

/// \file LearnedProjectionBinaryEncoder.hpp
/// \brief Dependency-free global learned projection binary signature encoder.

#include "IBinarySignatureEncoder.hpp"
#include "VectorSimilarityComputer.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace agent_memory {

    /// \brief Immutable options carrying a trained learned-projection artifact.
    struct LearnedProjectionBinaryEncoderOptions final {
        /// \brief Expected dense-vector dimension.
        std::size_t input_dimension = 0;
        /// \brief Number of output signature bits.
        std::size_t bit_count = 0;
        /// \brief Training seed used to derive deterministic pair candidates.
        std::uint64_t seed = 0;
        /// \brief Number of training vectors used to build this artifact.
        std::size_t training_vector_count = 0;
        /// \brief Row-major projection matrix: `bit_count * input_dimension` floats.
        std::vector<float> projection_rows;
        /// \brief One projection threshold per output bit.
        std::vector<float> thresholds;
    };

    /// \brief Options for dependency-free global pair-difference training.
    struct LearnedProjectionTrainingOptions final {
        /// \brief Expected dense-vector dimension.
        std::size_t input_dimension = 0;
        /// \brief Number of output signature bits.
        std::size_t bit_count = 128;
        /// \brief Seed used for deterministic training pair selection.
        std::uint64_t seed = 0;
        /// \brief Maximum number of training vectors to sample.
        ///
        /// Zero means all vectors. Positive values must be at least two because
        /// the trainer learns pair-difference directions.
        std::size_t max_training_vectors = 2048;
    };

    /// \brief Trains a global pair-difference projection artifact.
    ///
    /// The trainer uses only the supplied training vectors. For each bit it
    /// picks one deterministic anchor, chooses the farthest candidate among a
    /// small deterministic candidate set, normalizes the pair difference, and
    /// stores the median training projection as the bit threshold. The resulting
    /// encoder is globally comparable across all records encoded with the same
    /// artifact. It is a simple learned baseline, not ITQ or an autoencoder.
    [[nodiscard]] LearnedProjectionBinaryEncoderOptions train_learned_projection_encoder(
        const std::vector<Embedding>& training_vectors,
        LearnedProjectionTrainingOptions options
    );

    /// \brief Global learned projection encoder using a trained immutable artifact.
    ///
    /// Bits are set when `dot(row_i, vector) > threshold_i`. Query vectors and
    /// document vectors must be encoded by the same trained artifact; training
    /// must not use evaluation queries when measuring retrieval quality.
    class LearnedProjectionBinaryEncoder final : public IBinarySignatureEncoder {
    public:
        explicit LearnedProjectionBinaryEncoder(
            LearnedProjectionBinaryEncoderOptions options
        );

        [[nodiscard]] const BinarySignatureEncoderInfo& info() const noexcept override;

        [[nodiscard]] BinarySignature encode(const Embedding& vector) const override;

        [[nodiscard]] std::vector<BinarySignature> encode_batch(
            const std::vector<Embedding>& vectors
        ) const override;

        /// \brief SIMD backend used for dense projection dot products.
        [[nodiscard]] VectorSimilarityBackend similarity_backend() const noexcept;

        /// \brief Row-major trained projection matrix.
        [[nodiscard]] const std::vector<float>& projection_rows() const noexcept;

        /// \brief Trained bit thresholds.
        [[nodiscard]] const std::vector<float>& thresholds() const noexcept;

    private:
        void validate_input(const Embedding& vector) const;
        [[nodiscard]] BinarySignature encode_validated(const Embedding& vector) const;

        LearnedProjectionBinaryEncoderOptions m_options;
        BinarySignatureEncoderInfo m_info;
        VectorSimilarityComputer m_similarity;
    };

} // namespace agent_memory

#endif
