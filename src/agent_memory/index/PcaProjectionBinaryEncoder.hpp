#pragma once
#ifndef AGENT_MEMORY_HEADER_INDEX_PCA_PROJECTION_BINARY_ENCODER_HPP_INCLUDED
#define AGENT_MEMORY_HEADER_INDEX_PCA_PROJECTION_BINARY_ENCODER_HPP_INCLUDED

/// \file PcaProjectionBinaryEncoder.hpp
/// \brief Dependency-free PCA-style global binary signature encoder.

#include "IBinarySignatureEncoder.hpp"
#include "VectorSimilarityComputer.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace agent_memory {

    /// \brief Immutable options carrying a trained PCA projection artifact.
    struct PcaProjectionBinaryEncoderOptions final {
        /// \brief Expected dense-vector dimension.
        std::size_t input_dimension = 0;
        /// \brief Number of output signature bits.
        std::size_t bit_count = 0;
        /// \brief Seed used for deterministic power-iteration initialization.
        std::uint64_t seed = 0;
        /// \brief Number of training vectors used to build this artifact.
        std::size_t training_vector_count = 0;
        /// \brief Number of power iterations used per principal axis.
        std::size_t power_iterations = 0;
        /// \brief Training mean vector subtracted before projection.
        std::vector<float> mean;
        /// \brief Row-major projection matrix: `bit_count * input_dimension` floats.
        std::vector<float> projection_rows;
        /// \brief One centered projection threshold per output bit.
        std::vector<float> thresholds;
    };

    /// \brief Options for dependency-free PCA-style global training.
    struct PcaProjectionTrainingOptions final {
        /// \brief Expected dense-vector dimension.
        std::size_t input_dimension = 0;
        /// \brief Number of output signature bits; must not exceed input dimension.
        std::size_t bit_count = 128;
        /// \brief Seed used for deterministic power-iteration initialization.
        std::uint64_t seed = 0;
        /// \brief Number of power iterations per principal axis.
        std::size_t power_iterations = 24;
        /// \brief Maximum number of training vectors to sample.
        ///
        /// Zero means all vectors. Positive values must be at least two.
        std::size_t max_training_vectors = 2048;
    };

    /// \brief Trains a global PCA-style projection artifact.
    ///
    /// The trainer uses only the supplied training vectors. It centers the
    /// sampled corpus, computes a covariance matrix, extracts principal axes by
    /// deterministic power iteration with deflation, and stores median centered
    /// projections as bit thresholds. This is a dependency-free PCA baseline,
    /// not ITQ or an autoencoder.
    [[nodiscard]] PcaProjectionBinaryEncoderOptions train_pca_projection_encoder(
        const std::vector<Embedding>& training_vectors,
        PcaProjectionTrainingOptions options
    );

    /// \brief Global PCA-style binary encoder using a trained immutable artifact.
    ///
    /// Bits are set when
    /// `dot(row_i, vector - mean) > threshold_i`. Query vectors and document
    /// vectors must be encoded by the same trained artifact; training must not
    /// use evaluation queries when measuring retrieval quality.
    class PcaProjectionBinaryEncoder final : public IBinarySignatureEncoder {
    public:
        explicit PcaProjectionBinaryEncoder(PcaProjectionBinaryEncoderOptions options);

        [[nodiscard]] const BinarySignatureEncoderInfo& info() const noexcept override;

        [[nodiscard]] BinarySignature encode(const Embedding& vector) const override;

        [[nodiscard]] std::vector<BinarySignature> encode_batch(
            const std::vector<Embedding>& vectors
        ) const override;

        /// \brief SIMD backend used for dense projection dot products.
        [[nodiscard]] VectorSimilarityBackend similarity_backend() const noexcept;

        /// \brief Training mean vector subtracted before projection.
        [[nodiscard]] const std::vector<float>& mean() const noexcept;

        /// \brief Row-major trained projection matrix.
        [[nodiscard]] const std::vector<float>& projection_rows() const noexcept;

        /// \brief Trained bit thresholds.
        [[nodiscard]] const std::vector<float>& thresholds() const noexcept;

    private:
        void validate_input(const Embedding& vector) const;
        [[nodiscard]] BinarySignature encode_validated(const Embedding& vector) const;

        PcaProjectionBinaryEncoderOptions m_options;
        BinarySignatureEncoderInfo m_info;
        VectorSimilarityComputer m_similarity;
    };

} // namespace agent_memory

#endif
