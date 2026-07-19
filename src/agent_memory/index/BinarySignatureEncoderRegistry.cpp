#include "BinarySignatureEncoderRegistry.hpp"

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

    } // namespace

    std::size_t BinarySignatureEncoderRegistry::size() const noexcept {
        return m_entries.size();
    }

    bool BinarySignatureEncoderRegistry::empty() const noexcept {
        return m_entries.empty();
    }

    void BinarySignatureEncoderRegistry::register_encoder(EncoderPtr encoder) {
        if(!encoder) {
            throw std::invalid_argument("binary signature encoder registry cannot store null encoder");
        }

        const auto info = encoder->info();
        validate_encoder_info(info);

        const auto existing = find_entry(info.config_fingerprint);
        if(existing != m_entries.end()) {
            require_consistent_entry(*existing);
            if(!same_encoder_info(existing->info, info)) {
                throw std::invalid_argument(
                    "binary signature encoder fingerprint collision with different metadata"
                );
            }
            return;
        }

        m_entries.push_back(Entry{info, std::move(encoder)});
        std::sort(
            m_entries.begin(),
            m_entries.end(),
            [](const auto& lhs, const auto& rhs) {
                return lhs.info.config_fingerprint < rhs.info.config_fingerprint;
            }
        );
    }

    bool BinarySignatureEncoderRegistry::contains(std::string_view config_fingerprint) const {
        return find_entry(config_fingerprint) != m_entries.end();
    }

    BinarySignatureEncoderRegistry::EncoderPtr BinarySignatureEncoderRegistry::find(
        std::string_view config_fingerprint
    ) const {
        const auto existing = find_entry(config_fingerprint);
        if(existing == m_entries.end()) {
            return nullptr;
        }
        require_consistent_entry(*existing);
        return existing->encoder;
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
        result.reserve(m_entries.size());
        for(const auto& entry : m_entries) {
            result.push_back(entry.info);
        }
        return result;
    }

    std::vector<BinarySignatureEncoderRegistry::Entry>::const_iterator
    BinarySignatureEncoderRegistry::find_entry(
        std::string_view config_fingerprint
    ) const {
        return std::find_if(
            m_entries.begin(),
            m_entries.end(),
            [&](const auto& entry) {
                return std::string_view(entry.info.config_fingerprint) == config_fingerprint;
            }
        );
    }

    void BinarySignatureEncoderRegistry::require_consistent_entry(const Entry& entry) const {
        if(!entry.encoder || !same_encoder_info(entry.info, entry.encoder->info())) {
            throw std::logic_error(
                "registered binary signature encoder metadata changed after registration"
            );
        }
    }

} // namespace agent_memory
