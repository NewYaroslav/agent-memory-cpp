#include <nlohmann/json.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

    class Sha256 final {
    public:
        void update(const std::uint8_t* data, std::size_t size) {
            for(std::size_t index = 0; index < size; ++index) {
                m_buffer[m_buffer_size++] = data[index];
                m_bit_count += 8;
                if(m_buffer_size == m_buffer.size()) {
                    transform(m_buffer.data());
                    m_buffer_size = 0;
                }
            }
        }

        void update(std::string_view text) {
            update(
                reinterpret_cast<const std::uint8_t*>(text.data()),
                text.size()
            );
        }

        [[nodiscard]] std::array<std::uint8_t, 32> digest() {
            const auto total_bits = m_bit_count;
            m_buffer[m_buffer_size++] = 0x80U;
            if(m_buffer_size > 56) {
                while(m_buffer_size < m_buffer.size()) {
                    m_buffer[m_buffer_size++] = 0U;
                }
                transform(m_buffer.data());
                m_buffer_size = 0;
            }
            while(m_buffer_size < 56) {
                m_buffer[m_buffer_size++] = 0U;
            }
            for(int shift = 56; shift >= 0; shift -= 8) {
                m_buffer[m_buffer_size++] =
                    static_cast<std::uint8_t>((total_bits >> shift) & 0xFFU);
            }
            transform(m_buffer.data());

            std::array<std::uint8_t, 32> output{};
            for(std::size_t index = 0; index < m_state.size(); ++index) {
                output[index * 4] =
                    static_cast<std::uint8_t>((m_state[index] >> 24) & 0xFFU);
                output[index * 4 + 1] =
                    static_cast<std::uint8_t>((m_state[index] >> 16) & 0xFFU);
                output[index * 4 + 2] =
                    static_cast<std::uint8_t>((m_state[index] >> 8) & 0xFFU);
                output[index * 4 + 3] =
                    static_cast<std::uint8_t>(m_state[index] & 0xFFU);
            }
            return output;
        }

    private:
        static constexpr std::array<std::uint32_t, 64> kRoundConstants{{
            0x428A2F98U, 0x71374491U, 0xB5C0FBCFU, 0xE9B5DBA5U,
            0x3956C25BU, 0x59F111F1U, 0x923F82A4U, 0xAB1C5ED5U,
            0xD807AA98U, 0x12835B01U, 0x243185BEU, 0x550C7DC3U,
            0x72BE5D74U, 0x80DEB1FEU, 0x9BDC06A7U, 0xC19BF174U,
            0xE49B69C1U, 0xEFBE4786U, 0x0FC19DC6U, 0x240CA1CCU,
            0x2DE92C6FU, 0x4A7484AAU, 0x5CB0A9DCU, 0x76F988DAU,
            0x983E5152U, 0xA831C66DU, 0xB00327C8U, 0xBF597FC7U,
            0xC6E00BF3U, 0xD5A79147U, 0x06CA6351U, 0x14292967U,
            0x27B70A85U, 0x2E1B2138U, 0x4D2C6DFCU, 0x53380D13U,
            0x650A7354U, 0x766A0ABBU, 0x81C2C92EU, 0x92722C85U,
            0xA2BFE8A1U, 0xA81A664BU, 0xC24B8B70U, 0xC76C51A3U,
            0xD192E819U, 0xD6990624U, 0xF40E3585U, 0x106AA070U,
            0x19A4C116U, 0x1E376C08U, 0x2748774CU, 0x34B0BCB5U,
            0x391C0CB3U, 0x4ED8AA4AU, 0x5B9CCA4FU, 0x682E6FF3U,
            0x748F82EEU, 0x78A5636FU, 0x84C87814U, 0x8CC70208U,
            0x90BEFFFAU, 0xA4506CEBU, 0xBEF9A3F7U, 0xC67178F2U,
        }};

        static std::uint32_t rotate_right(std::uint32_t value, int bits) {
            return (value >> bits) | (value << (32 - bits));
        }

        static std::uint32_t choose(
            std::uint32_t x,
            std::uint32_t y,
            std::uint32_t z
        ) {
            return (x & y) ^ (~x & z);
        }

        static std::uint32_t majority(
            std::uint32_t x,
            std::uint32_t y,
            std::uint32_t z
        ) {
            return (x & y) ^ (x & z) ^ (y & z);
        }

        static std::uint32_t big_sigma0(std::uint32_t x) {
            return rotate_right(x, 2) ^ rotate_right(x, 13) ^ rotate_right(x, 22);
        }

        static std::uint32_t big_sigma1(std::uint32_t x) {
            return rotate_right(x, 6) ^ rotate_right(x, 11) ^ rotate_right(x, 25);
        }

        static std::uint32_t small_sigma0(std::uint32_t x) {
            return rotate_right(x, 7) ^ rotate_right(x, 18) ^ (x >> 3);
        }

        static std::uint32_t small_sigma1(std::uint32_t x) {
            return rotate_right(x, 17) ^ rotate_right(x, 19) ^ (x >> 10);
        }

        static std::uint32_t read_be32(const std::uint8_t* data) {
            return (static_cast<std::uint32_t>(data[0]) << 24)
                | (static_cast<std::uint32_t>(data[1]) << 16)
                | (static_cast<std::uint32_t>(data[2]) << 8)
                | static_cast<std::uint32_t>(data[3]);
        }

        void transform(const std::uint8_t* chunk) {
            std::array<std::uint32_t, 64> words{};
            for(std::size_t index = 0; index < 16; ++index) {
                words[index] = read_be32(chunk + index * 4);
            }
            for(std::size_t index = 16; index < words.size(); ++index) {
                words[index] = small_sigma1(words[index - 2]) + words[index - 7]
                    + small_sigma0(words[index - 15]) + words[index - 16];
            }

            auto a = m_state[0];
            auto b = m_state[1];
            auto c = m_state[2];
            auto d = m_state[3];
            auto e = m_state[4];
            auto f = m_state[5];
            auto g = m_state[6];
            auto h = m_state[7];

            for(std::size_t index = 0; index < words.size(); ++index) {
                const auto temp1 = h + big_sigma1(e) + choose(e, f, g)
                    + kRoundConstants[index] + words[index];
                const auto temp2 = big_sigma0(a) + majority(a, b, c);
                h = g;
                g = f;
                f = e;
                e = d + temp1;
                d = c;
                c = b;
                b = a;
                a = temp1 + temp2;
            }

            m_state[0] += a;
            m_state[1] += b;
            m_state[2] += c;
            m_state[3] += d;
            m_state[4] += e;
            m_state[5] += f;
            m_state[6] += g;
            m_state[7] += h;
        }

        std::array<std::uint32_t, 8> m_state{{
            0x6A09E667U,
            0xBB67AE85U,
            0x3C6EF372U,
            0xA54FF53AU,
            0x510E527FU,
            0x9B05688CU,
            0x1F83D9ABU,
            0x5BE0CD19U,
        }};
        std::array<std::uint8_t, 64> m_buffer{};
        std::size_t m_buffer_size = 0;
        std::uint64_t m_bit_count = 0;
    };

    [[nodiscard]] std::string to_hex(const std::array<std::uint8_t, 32>& digest) {
        constexpr std::array<char, 16> kHexDigits{{
            '0', '1', '2', '3', '4', '5', '6', '7',
            '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
        }};

        std::string output;
        output.reserve(digest.size() * 2);
        for(const auto byte : digest) {
            output.push_back(kHexDigits[(byte >> 4) & 0x0FU]);
            output.push_back(kHexDigits[byte & 0x0FU]);
        }
        return output;
    }

    [[nodiscard]] std::string sha256_hex(std::string_view text) {
        Sha256 sha;
        sha.update(text);
        return to_hex(sha.digest());
    }

    void update_u8(Sha256& sha, std::uint8_t value) {
        sha.update(&value, 1);
    }

    void update_u32_le(Sha256& sha, std::uint32_t value) {
        std::array<std::uint8_t, 4> bytes{{
            static_cast<std::uint8_t>(value & 0xFFU),
            static_cast<std::uint8_t>((value >> 8) & 0xFFU),
            static_cast<std::uint8_t>((value >> 16) & 0xFFU),
            static_cast<std::uint8_t>((value >> 24) & 0xFFU),
        }};
        sha.update(bytes.data(), bytes.size());
    }

    void update_string_bytes(Sha256& sha, const std::string& value) {
        if(value.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("string field is too large for canonical payload");
        }
        update_u32_le(sha, static_cast<std::uint32_t>(value.size()));
        sha.update(
            reinterpret_cast<const std::uint8_t*>(value.data()),
            value.size()
        );
    }

    void update_float32_le(Sha256& sha, float value) {
        std::uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(value), "float32 size mismatch");
        std::memcpy(&bits, &value, sizeof(bits));
        update_u32_le(sha, bits);
    }

    [[nodiscard]] const nlohmann::json& require_field(
        const nlohmann::json& value,
        std::string_view field
    ) {
        const auto iter = value.find(field);
        if(iter == value.end()) {
            throw std::runtime_error("missing required JSON field: " + std::string{field});
        }
        return *iter;
    }

    [[nodiscard]] std::string read_string(
        const nlohmann::json& object,
        std::string_view field
    ) {
        const auto& value = require_field(object, field);
        if(!value.is_string()) {
            throw std::runtime_error("JSON field must be a string: " + std::string{field});
        }
        return value.get<std::string>();
    }

    [[nodiscard]] std::uint32_t read_u32(
        const nlohmann::json& object,
        std::string_view field
    ) {
        const auto& value = require_field(object, field);
        if(!value.is_number_unsigned() && !value.is_number_integer()) {
            throw std::runtime_error("JSON field must be an integer: " + std::string{field});
        }
        if(value.is_number_integer() && !value.is_number_unsigned()) {
            const auto parsed_signed = value.get<std::int64_t>();
            if(parsed_signed < 0) {
                throw std::runtime_error(
                    "JSON integer must be non-negative: " + std::string{field}
                );
            }
            if(static_cast<std::uint64_t>(parsed_signed)
               > std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error(
                    "JSON integer is too large: " + std::string{field}
                );
            }
            return static_cast<std::uint32_t>(parsed_signed);
        }

        const auto parsed = value.get<std::uint64_t>();
        if(parsed > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("JSON integer is too large: " + std::string{field});
        }
        return static_cast<std::uint32_t>(parsed);
    }

    [[nodiscard]] bool read_bool(
        const nlohmann::json& object,
        std::string_view field
    ) {
        const auto& value = require_field(object, field);
        if(!value.is_boolean()) {
            throw std::runtime_error("JSON field must be a bool: " + std::string{field});
        }
        return value.get<bool>();
    }

    [[nodiscard]] float read_float32(
        const nlohmann::json& object,
        std::size_t index
    ) {
        if(index >= object.size()) {
            throw std::runtime_error("embedding vector index is out of range");
        }
        const auto& value = object[index];
        if(!value.is_number()) {
            throw std::runtime_error("embedding vector value must be numeric");
        }
        const auto parsed = value.get<double>();
        if(parsed < -static_cast<double>(std::numeric_limits<float>::max())
           || parsed > static_cast<double>(std::numeric_limits<float>::max())) {
            throw std::runtime_error("embedding vector value is outside float32 range");
        }
        const auto narrowed = static_cast<float>(parsed);
        if(!std::isfinite(narrowed)) {
            throw std::runtime_error("embedding vector value must be finite");
        }
        return narrowed == 0.0F ? 0.0F : narrowed;
    }

    void update_embedding_records(
        Sha256& sha,
        const nlohmann::json& root,
        std::string_view field,
        std::uint8_t record_type
    ) {
        const auto& records = require_field(root, field);
        if(!records.is_array()) {
            throw std::runtime_error("embedding record field must be an array");
        }
        for(const auto& record : records) {
            if(!record.is_object()) {
                throw std::runtime_error("embedding record must be an object");
            }
            const auto id = read_string(record, "id");
            const auto& vector = require_field(record, "vector");
            if(!vector.is_array() || vector.empty()) {
                throw std::runtime_error("embedding vector must be a non-empty array");
            }
            if(vector.size() > std::numeric_limits<std::uint32_t>::max()) {
                throw std::runtime_error("embedding vector dimension is too large");
            }

            update_u8(sha, record_type);
            update_string_bytes(sha, id);
            update_u32_le(sha, static_cast<std::uint32_t>(vector.size()));
            for(std::size_t index = 0; index < vector.size(); ++index) {
                update_float32_le(sha, read_float32(vector, index));
            }
        }
    }

    [[nodiscard]] std::string canonical_config_text(const nlohmann::json& root) {
        const auto& model = require_field(root, "embedding_model");
        const auto& artifact = require_field(root, "embedding_artifact");

        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << "dataset_revision=" << read_string(artifact, "dataset_revision") << '\n';
        output << "document_prompt_id=" << read_string(artifact, "document_prompt_id") << '\n';
        output << "dtype=" << read_string(artifact, "dtype") << '\n';
        output << "embedding_dimension=" << read_u32(model, "dimension") << '\n';
        output << "embedding_model_id=" << read_string(model, "model_id") << '\n';
        output << "embedding_normalized="
               << (read_bool(model, "normalized") ? "true" : "false") << '\n';
        output << "generator_id=" << read_string(artifact, "generator_id") << '\n';
        output << "generator_revision=" << read_string(artifact, "generator_revision")
               << '\n';
        output << "generator_version=" << read_string(artifact, "generator_version") << '\n';
        output << "model_revision=" << read_string(artifact, "model_revision") << '\n';
        output << "normalization=" << read_string(artifact, "normalization") << '\n';
        output << "pooling_mode=" << read_string(model, "pooling_mode") << '\n';
        output << "projection_kind=" << read_string(artifact, "projection_kind") << '\n';
        output << "qrels_revision=" << read_string(artifact, "qrels_revision") << '\n';
        output << "query_prompt_id=" << read_string(artifact, "query_prompt_id") << '\n';
        output << "similarity_metric=" << read_string(model, "similarity_metric") << '\n';
        return output.str();
    }

    [[nodiscard]] std::string canonical_artifact_hash(const nlohmann::json& root) {
        Sha256 sha;
        sha.update("agent-memory-precomputed-embedding-payload-v1");
        update_embedding_records(sha, root, "document_embeddings", 1U);
        update_embedding_records(sha, root, "query_embeddings", 2U);
        return to_hex(sha.digest());
    }

    [[nodiscard]] std::string read_file_text(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        if(!input) {
            throw std::runtime_error("cannot open fixture: " + path.string());
        }
        return {
            std::istreambuf_iterator<char>{input},
            std::istreambuf_iterator<char>{}
        };
    }

    void verify_fixture(const std::filesystem::path& path) {
        const auto root = nlohmann::json::parse(read_file_text(path));
        const auto& model = require_field(root, "embedding_model");
        const auto& artifact = require_field(root, "embedding_artifact");

        const auto hash_algorithm = read_string(artifact, "hash_algorithm");
        if(hash_algorithm != "sha256") {
            throw std::runtime_error("embedding_artifact.hash_algorithm must be sha256");
        }

        const auto normalization = read_string(artifact, "normalization");
        if(read_bool(model, "normalized")) {
            if(normalization != "l2") {
                throw std::runtime_error(
                    "embedding_model.normalized=true requires normalization=l2"
                );
            }
        } else if(normalization != "none") {
            throw std::runtime_error(
                "embedding_model.normalized=false requires normalization=none"
            );
        }

        const auto dtype = read_string(artifact, "dtype");
        if(dtype != "float32") {
            throw std::runtime_error(
                "canonical artifact verifier currently requires dtype=float32"
            );
        }

        const auto expected_config_hash = read_string(artifact, "config_hash");
        const auto expected_artifact_hash = read_string(artifact, "artifact_hash");
        const auto actual_config_hash = sha256_hex(canonical_config_text(root));
        const auto actual_artifact_hash = canonical_artifact_hash(root);

        if(actual_config_hash != expected_config_hash) {
            throw std::runtime_error(
                "config_hash mismatch: expected " + expected_config_hash
                + ", got " + actual_config_hash
            );
        }
        if(actual_artifact_hash != expected_artifact_hash) {
            throw std::runtime_error(
                "artifact_hash mismatch: expected " + expected_artifact_hash
                + ", got " + actual_artifact_hash
            );
        }
    }

    void self_test_sha256() {
        struct TestVector {
            std::string_view input;
            std::string_view expected_hex;
        };
        constexpr std::array<TestVector, 3> kVectors{{
            {
                "",
                "e3b0c44298fc1c149afbf4c8996fb924"
                "27ae41e4649b934ca495991b7852b855",
            },
            {
                "abc",
                "ba7816bf8f01cfea414140de5dae2223"
                "b00361a396177a9cb410ff61f20015ad",
            },
            {
                "The quick brown fox jumps over the lazy dog",
                "d7a8fbb307d7809469ca9abcb0082e4f"
                "8d5651e46d3cdb762d02d0bf37c9e592",
            },
        }};

        for(const auto& vector : kVectors) {
            if(sha256_hex(vector.input) != vector.expected_hex) {
                throw std::logic_error("internal SHA-256 self-test failed");
            }
        }
    }

} // namespace

int main(int argc, char** argv) {
    try {
        if(argc != 2) {
            std::cerr << "usage: agent-memory-precomputed-artifact-verify <fixture.json>\n";
            return 2;
        }
        self_test_sha256();
        verify_fixture(argv[1]);
        return 0;
    } catch(const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }
}
