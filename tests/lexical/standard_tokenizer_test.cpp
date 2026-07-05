#include <agent_memory/AgentMemory.hpp>

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

    int fail(const std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

    [[nodiscard]] std::vector<std::string> token_texts(
        const agent_memory::TokenizationResult& result
    ) {
        std::vector<std::string> texts;
        for(const auto& token : result.tokens) {
            texts.push_back(token.text);
        }
        return texts;
    }

} // namespace

int main() {
    const agent_memory::StandardTokenizer tokenizer;

    const auto result = tokenizer.tokenize("Hello AGENT_MEMORY_HAS_MDBX error 11001");
    const auto texts = token_texts(result);

    const std::vector<std::string> expected{
        "hello",
        "agent_memory_has_mdbx",
        "agent",
        "memory",
        "has",
        "mdbx",
        "error",
        "11001"
    };

    if(texts != expected) {
        return fail("standard tokenizer must normalize words and split identifiers");
    }

    if(result.tokens[1].kind != agent_memory::TokenKind::Identifier) {
        return fail("identifier token must be classified as identifier");
    }

    if(result.tokens[7].kind != agent_memory::TokenKind::Number) {
        return fail("numeric token must be classified as number");
    }

    if(result.tokens[0].source_range.offset != 0 || result.tokens[0].source_range.length != 5) {
        return fail("word token must preserve source byte range");
    }

    for(std::size_t index = 0; index < result.tokens.size(); ++index) {
        if(result.tokens[index].position != index) {
            return fail("token positions must match emitted order");
        }
    }

    const auto path_result = tokenizer.tokenize("Open docs/readme.md");
    if(path_result.tokens.size() != 2 || path_result.tokens[1].kind != agent_memory::TokenKind::Path) {
        return fail("path-like token must be classified as path");
    }

    const auto utf8_result = tokenizer.tokenize("Привет Мир");
    if(utf8_result.tokens.size() != 2 || utf8_result.tokens[0].text != "Привет") {
        return fail("std tokenizer must keep non-ASCII UTF-8 bytes intact");
    }

    agent_memory::TokenizeOptions preserve_case;
    preserve_case.lowercase_ascii = false;

    const auto case_result = tokenizer.tokenize("CamelCase", preserve_case);
    if(case_result.tokens.empty() || case_result.tokens[0].text != "CamelCase") {
        return fail("lowercase_ascii=false must preserve ASCII case");
    }

    agent_memory::TokenizeOptions symbols;
    symbols.emit_symbol_tokens = true;
    symbols.emit_identifier_parts = false;

    const auto symbol_result = tokenizer.tokenize("a+b", symbols);
    if(
        symbol_result.tokens.size() != 3 ||
        symbol_result.tokens[1].kind != agent_memory::TokenKind::Symbol ||
        symbol_result.tokens[1].text != "+"
    ) {
        return fail("symbol tokens must be emitted when requested");
    }

    return 0;
}
