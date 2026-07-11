#include "DatasetLoader.hpp"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace agent_memory {

    namespace {

        // Source location label used in error messages.
        struct SourceLocation final {
            std::string label;
        };

        [[noreturn]] void throw_field_error(
            const SourceLocation& loc,
            const std::string& field,
            const std::string& detail
        ) {
            std::ostringstream out;
            out << "DatasetLoader(" << loc.label << "): field '" << field
                << "' — " << detail;
            throw std::runtime_error(out.str());
        }

        [[noreturn]] void throw_parse_error(
            const SourceLocation& loc,
            const std::string& detail
        ) {
            std::ostringstream out;
            out << "DatasetLoader(" << loc.label << "): " << detail;
            throw std::runtime_error(out.str());
        }

        const nlohmann::json& require_array(
            const nlohmann::json& parent,
            std::string_view field,
            const SourceLocation& loc
        ) {
            const auto it = parent.find(std::string{field});
            if(it == parent.end()) {
                throw_field_error(loc, std::string{field}, "missing required array");
            }
            if(!it->is_array()) {
                throw_field_error(loc, std::string{field}, "must be a JSON array");
            }
            return *it;
        }

        const nlohmann::json& find_field(
            const nlohmann::json& parent,
            std::string_view field,
            const SourceLocation& loc
        ) {
            const auto it = parent.find(std::string{field});
            if(it == parent.end()) {
                throw_field_error(loc, std::string{field}, "missing required field");
            }
            return *it;
        }

        const nlohmann::json* find_optional(
            const nlohmann::json& parent,
            std::string_view field
        ) {
            const auto it = parent.find(std::string{field});
            return it == parent.end() ? nullptr : &*it;
        }

        std::string read_string(
            const nlohmann::json& parent,
            std::string_view field,
            const SourceLocation& loc
        ) {
            const auto& node = find_field(parent, field, loc);
            if(!node.is_string()) {
                throw_field_error(loc, std::string{field}, "must be a string");
            }
            return node.get<std::string>();
        }

        std::int32_t read_int32(
            const nlohmann::json& parent,
            std::string_view field,
            const SourceLocation& loc
        ) {
            const auto& node = find_field(parent, field, loc);
            if(!node.is_number_integer()) {
                throw_field_error(loc, std::string{field}, "must be an integer");
            }
            return node.get<std::int32_t>();
        }

        std::size_t read_size(
            const nlohmann::json& parent,
            std::string_view field,
            const SourceLocation& loc
        ) {
            const auto& node = find_field(parent, field, loc);
            if(!node.is_number_unsigned()) {
                throw_field_error(loc, std::string{field}, "must be a non-negative integer");
            }
            return node.get<std::size_t>();
        }

        EvalQueryAnswerMode read_answer_mode(
            const nlohmann::json& node,
            const SourceLocation& loc
        ) {
            if(node.is_null()) {
                return EvalQueryAnswerMode::JudgedRetrieval;
            }
            if(!node.is_string()) {
                throw_field_error(loc, "answer_mode", "must be a string");
            }
            const auto& text = node.get_ref<const std::string&>();
            if(text == "JudgedRetrieval") {
                return EvalQueryAnswerMode::JudgedRetrieval;
            }
            if(text == "NoAnswer") {
                return EvalQueryAnswerMode::NoAnswer;
            }
            if(text == "Ignore") {
                return EvalQueryAnswerMode::Ignore;
            }
            throw_field_error(
                loc,
                "answer_mode",
                "must be one of JudgedRetrieval|NoAnswer|Ignore"
            );
        }

        std::vector<MetadataFilter> read_metadata_filters(
            const nlohmann::json& node,
            const SourceLocation& loc
        ) {
            std::vector<MetadataFilter> result;
            if(node.is_null()) {
                return result;
            }
            if(!node.is_array()) {
                throw_field_error(loc, "metadata_filters", "must be an array");
            }
            result.reserve(node.size());
            for(std::size_t i = 0; i < node.size(); ++i) {
                const auto& entry = node[i];
                SourceLocation child_loc = loc;
                child_loc.label += ".metadata_filters[" + std::to_string(i) + "]";
                if(!entry.is_object()) {
                    throw_field_error(child_loc, "<entry>", "must be an object");
                }
                MetadataFilter filter;
                filter.key = read_string(entry, "key", child_loc);
                filter.value = read_string(entry, "value", child_loc);
                result.push_back(std::move(filter));
            }
            return result;
        }

        EvalCorpusItem read_corpus_item(
            const nlohmann::json& node,
            const SourceLocation& loc
        ) {
            if(!node.is_object()) {
                throw_field_error(loc, "<corpus entry>", "must be an object");
            }
            EvalCorpusItem item;
            item.id = read_string(node, "id", loc);
            item.title = read_string(node, "title", loc);
            item.text = read_string(node, "text", loc);
            if(const auto* meta_node = find_optional(node, "metadata")) {
                if(!meta_node->is_object()) {
                    throw_field_error(loc, "metadata", "must be an object");
                }
                for(auto it = meta_node->begin(); it != meta_node->end(); ++it) {
                    if(!it.value().is_string()) {
                        throw_field_error(
                            loc,
                            "metadata." + it.key(),
                            "must be a string"
                        );
                    }
                    item.metadata.set(
                        it.key(),
                        it.value().get<std::string>()
                    );
                }
            }
            return item;
        }

        EvalQuery read_query(
            const nlohmann::json& node,
            const SourceLocation& loc
        ) {
            if(!node.is_object()) {
                throw_field_error(loc, "<query entry>", "must be an object");
            }
            EvalQuery query;
            query.id = read_string(node, "id", loc);
            query.text = read_string(node, "text", loc);
            query.query_type = read_string(node, "query_type", loc);
            query.limit = read_size(node, "limit", loc);
            if(const auto* mf_node = find_optional(node, "metadata_filters")) {
                query.metadata_filters = read_metadata_filters(*mf_node, loc);
            }
            if(const auto* am_node = find_optional(node, "answer_mode")) {
                query.answer_mode = read_answer_mode(*am_node, loc);
            } else {
                query.answer_mode = EvalQueryAnswerMode::JudgedRetrieval;
            }
            return query;
        }

        RelevanceJudgment read_judgment(
            const nlohmann::json& node,
            const SourceLocation& loc
        ) {
            if(!node.is_object()) {
                throw_field_error(loc, "<judgment entry>", "must be an object");
            }
            RelevanceJudgment judgment;
            judgment.query_id = read_string(node, "query_id", loc);
            judgment.item_id = read_string(node, "item_id", loc);
            judgment.relevance_grade = read_int32(node, "relevance_grade", loc);
            return judgment;
        }

        RetrievalEvalDataset parse_dataset(
            const nlohmann::json& root,
            const SourceLocation& loc
        ) {
            if(!root.is_object()) {
                throw_parse_error(loc, "top-level value must be a JSON object");
            }
            RetrievalEvalDataset dataset;
            dataset.name = read_string(root, "name", loc);

            {
                const auto& arr = require_array(root, "corpus", loc);
                dataset.corpus.reserve(arr.size());
                for(std::size_t i = 0; i < arr.size(); ++i) {
                    SourceLocation child_loc = loc;
                    child_loc.label += ".corpus[" + std::to_string(i) + "]";
                    dataset.corpus.push_back(read_corpus_item(arr[i], child_loc));
                }
            }

            {
                const auto& arr = require_array(root, "queries", loc);
                dataset.queries.reserve(arr.size());
                for(std::size_t i = 0; i < arr.size(); ++i) {
                    SourceLocation child_loc = loc;
                    child_loc.label += ".queries[" + std::to_string(i) + "]";
                    dataset.queries.push_back(read_query(arr[i], child_loc));
                }
            }

            {
                const auto& arr = require_array(root, "judgments", loc);
                dataset.judgments.reserve(arr.size());
                for(std::size_t i = 0; i < arr.size(); ++i) {
                    SourceLocation child_loc = loc;
                    child_loc.label += ".judgments[" + std::to_string(i) + "]";
                    dataset.judgments.push_back(
                        read_judgment(arr[i], child_loc)
                    );
                }
            }

            return dataset;
        }

        std::string read_file_text(const std::filesystem::path& path) {
            std::ifstream stream(path, std::ios::binary);
            if(!stream) {
                std::ostringstream out;
                out << "DatasetLoader: cannot open file '" << path.string()
                    << "' for reading";
                throw std::runtime_error(out.str());
            }
            std::ostringstream buffer;
            buffer << stream.rdbuf();
            return buffer.str();
        }

    } // namespace

    RetrievalEvalDataset load_dataset_from_json_file(
        const std::filesystem::path& path
    ) {
        SourceLocation loc{path.string()};
        const std::string text = read_file_text(path);
        nlohmann::json root;
        try {
            root = nlohmann::json::parse(text);
        } catch(const nlohmann::json::parse_error& err) {
            std::ostringstream out;
            out << "DatasetLoader(" << path.string()
                << "): malformed JSON: " << err.what();
            throw std::runtime_error(out.str());
        }
        auto dataset = parse_dataset(root, loc);
        validate_retrieval_eval_dataset(dataset);
        return dataset;
    }

    RetrievalEvalDataset load_dataset_from_json_string(
        std::string_view json_text
    ) {
        SourceLocation loc{"<string>"};
        nlohmann::json root;
        try {
            root = nlohmann::json::parse(json_text);
        } catch(const nlohmann::json::parse_error& err) {
            std::ostringstream out;
            out << "DatasetLoader(<string>): malformed JSON: " << err.what();
            throw std::runtime_error(out.str());
        }
        auto dataset = parse_dataset(root, loc);
        validate_retrieval_eval_dataset(dataset);
        return dataset;
    }

} // namespace agent_memory