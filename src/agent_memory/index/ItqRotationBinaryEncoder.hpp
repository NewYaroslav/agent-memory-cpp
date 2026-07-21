#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_ITQ_ROTATION_BINARY_ENCODER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_ITQ_ROTATION_BINARY_ENCODER_HPP_INCLUDED

/// \file ItqRotationBinaryEncoder.hpp
/// \brief Dependency-free PCA + ITQ-style rotation binary signature encoder.

#include "IBinarySignatureEncoder.hpp"
#include "VectorSimilarityComputer.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace agent_memory {

    /// \brief Immutable options carrying a trained PCA + ITQ rotation artifact.
    struct ItqRotationBinaryEncoderOptions final {
        /// \brief Expected dense-vector dimension.
        std::size_t input_dimension = 0;
        /// \brief Number of output signature bits.
        std::size_t bit_count = 0;
        /// \brief Seed used for deterministic PCA initialization and sampling.
        std::uint64_t seed = 0;
        /// \brief Number of training vectors used to build this artifact.
        std::size_t training_vector_count = 0;
        /// \brief Number of PCA power iterations used per principal axis.
        std::size_t pca_power_iterations = 0;
        /// \brief Number of ITQ rotation iterations.
        std::size_t rotation_iterations = 0;
        /// \brief Training mean vector subtracted before projection.
        std::vector<float> mean;
        /// \brief Row-major final projection matrix: `bit_count * input_dimension`.
        std::vector<float> projection_rows;
        /// \brief One projection threshold per output bit.
        ///
        /// ITQ uses zero-centered signs after PCA centering and rotation.
        std::vector<float> thresholds;
    };

    /// \brief Options for dependency-free PCA + ITQ-style training.
    struct ItqRotationTrainingOptions final {
        /// \brief Expected dense-vector dimension.
        std::size_t input_dimension = 0;
        /// \brief Number of output signature bits; must not exceed input dimension.
        std::size_t bit_count = 128;
        /// \brief Seed used for deterministic PCA initialization and sampling.
        std::uint64_t seed = 0;
        /// \brief Number of PCA power iterations per principal axis.
        std::size_t pca_power_iterations = 24;
        /// \brief Number of ITQ rotation iterations.
        std::size_t rotation_iterations = 16;
        /// \brief Maximum number of training vectors to sample.
        ///
        /// Zero means all vectors. Positive values must be at least two.
        std::size_t max_training_vectors = 2048;
    };

    /// \brief Trains a global PCA + ITQ-style projection artifact.
    ///
    /// The trainer first learns the same dependency-free PCA subspace as
    /// `PcaProjectionBinaryEncoder`, then learns an orthogonal rotation in the
    /// PCA space by alternating binary assignment and orthogonal Procrustes
    /// updates. This is an unsupervised ITQ-style baseline, not supervised
    /// hashing and not an autoencoder.
    [[nodiscard]] ItqRotationBinaryEncoderOptions train_itq_rotation_encoder(
        const std::vector<Embedding>& training_vectors,
        ItqRotationTrainingOptions options
    );

    /// \brief Global PCA + ITQ-style binary encoder using a trained artifact.
    ///
    /// Bits are set when `dot(row_i, vector - mean) > 0`. Query vectors and
    /// document vectors must be encoded by the same trained artifact; training
    /// must not use evaluation queries when measuring retrieval quality.
    class ItqRotationBinaryEncoder final : public IBinarySignatureEncoder {
    public:
        explicit ItqRotationBinaryEncoder(ItqRotationBinaryEncoderOptions options);

        [[nodiscard]] const BinarySignatureEncoderInfo& info() const noexcept override;

        [[nodiscard]] BinarySignature encode(const Embedding& vector) const override;

        [[nodiscard]] std::vector<BinarySignature> encode_batch(
            const std::vector<Embedding>& vectors
        ) const override;

        /// \brief SIMD backend used for dense projection dot products.
        [[nodiscard]] VectorSimilarityBackend similarity_backend() const noexcept;

        /// \brief Training mean vector subtracted before projection.
        [[nodiscard]] const std::vector<float>& mean() const noexcept;

        /// \brief Row-major final projection matrix.
        [[nodiscard]] const std::vector<float>& projection_rows() const noexcept;

        /// \brief Trained projection thresholds.
        [[nodiscard]] const std::vector<float>& thresholds() const noexcept;

    private:
        void validate_input(const Embedding& vector) const;
        [[nodiscard]] BinarySignature encode_validated(const Embedding& vector) const;

        ItqRotationBinaryEncoderOptions m_options;
        BinarySignatureEncoderInfo m_info;
        VectorSimilarityComputer m_similarity;
    };

} // namespace agent_memory

#endif
