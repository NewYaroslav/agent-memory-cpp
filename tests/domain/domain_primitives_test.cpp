#include <agent_memory.hpp>

#include <iostream>
#include <string_view>

namespace {

    int fail(std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

} // namespace

int main() {
    const agent_memory::DocumentId document_id{"doc:alpha"};
    const agent_memory::DocumentId same_document_id{"doc:alpha"};
    const agent_memory::DocumentId other_document_id{"doc:beta"};

    if(document_id.empty()) {
        return fail("document id must not be empty");
    }

    if(document_id != same_document_id) {
        return fail("matching document ids must compare equal");
    }

    if(!(document_id < other_document_id)) {
        return fail("document ids must provide deterministic ordering");
    }

    agent_memory::Metadata metadata;
    metadata.set("scope", "user:42");
    metadata.set("language", "en");

    if(metadata.size() != 2) {
        return fail("metadata size must reflect inserted values");
    }

    const auto scope = metadata.get("scope");
    if(!scope || *scope != "user:42") {
        return fail("metadata lookup must return stored value");
    }

    if(!metadata.erase("language") || metadata.contains("language")) {
        return fail("metadata erase must remove existing value");
    }

    agent_memory::SourceKind parsed_kind = agent_memory::SourceKind::Unknown;
    if(!agent_memory::parse_source_kind("Markdown", parsed_kind)) {
        return fail("source kind parser must accept mixed-case input");
    }

    if(parsed_kind != agent_memory::SourceKind::Markdown) {
        return fail("source kind parser returned unexpected value");
    }

    if(agent_memory::to_string(parsed_kind) != "markdown") {
        return fail("source kind names must be stable lowercase strings");
    }

    const agent_memory::Document document{
        document_id,
        agent_memory::SourceKind::Markdown,
        "notes/project.md",
        "# Project\nMemory notes.",
        metadata
    };

    if(!agent_memory::has_content(document)) {
        return fail("document with text must have content");
    }

    const agent_memory::DocumentChunk chunk{
        agent_memory::ChunkId{"chunk:alpha:0"},
        document.id,
        agent_memory::TextRange{0, 9},
        "# Project",
        {}
    };

    if(!agent_memory::has_content(chunk)) {
        return fail("chunk with text must have content");
    }

    if(agent_memory::is_empty(chunk.source_range)) {
        return fail("non-zero source range must not be empty");
    }

    return 0;
}
