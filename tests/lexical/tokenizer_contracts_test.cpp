#include <agent_memory/AgentMemory.hpp>

#include <iostream>
#include <string_view>

namespace {

    class FakeTokenizer final : public agent_memory::ITokenizer {
    public:
        [[nodiscard]] agent_memory::TokenizationResult tokenize(
            const std::string_view text,
            const agent_memory::TokenizeOptions& options = {}
        ) const override {
            (void)options;

            agent_memory::TokenizationResult result;
            if(text.empty()) {
                return result;
            }

            result.tokens.push_back(agent_memory::Token{
                "hello",
                agent_memory::TextRange{0, 5},
                0,
                agent_memory::TokenKind::Word
            });
            result.tokens.push_back(agent_memory::Token{
                "agent_memory",
                agent_memory::TextRange{6, 12},
                1,
                agent_memory::TokenKind::Identifier
            });

            return result;
        }
    };

    int fail(const std::string_view message) {
        std::cerr << message << '\n';
        return 1;
    }

} // namespace

int main() {
    agent_memory::TokenKind kind = agent_memory::TokenKind::Custom;
    if(!agent_memory::parse_token_kind("Identifier", kind)) {
        return fail("token kind parser must accept mixed-case names");
    }

    if(kind != agent_memory::TokenKind::Identifier) {
        return fail("token kind parser returned unexpected kind");
    }

    if(agent_memory::to_string(agent_memory::TokenKind::Path) != "path") {
        return fail("token kind names must be stable lowercase strings");
    }

    if(agent_memory::parse_token_kind("not_a_kind", kind)) {
        return fail("unknown token kind must not parse");
    }

    const agent_memory::Token empty_token;
    if(!empty_token.empty()) {
        return fail("default token must be empty");
    }

    const agent_memory::TokenizeOptions options;
    if(!options.lowercase_ascii || !options.emit_identifier_parts) {
        return fail("tokenize options must keep practical defaults enabled");
    }

    if(options.emit_symbol_tokens) {
        return fail("symbol tokens must be opt-in by default");
    }

    const FakeTokenizer tokenizer;
    const auto result = tokenizer.tokenize("Hello AGENT_MEMORY");

    if(result.empty() || result.size() != 2) {
        return fail("tokenizer result must expose size and empty helpers");
    }

    if(result.tokens[0].text != "hello") {
        return fail("tokens must carry normalized lookup text");
    }

    if(result.tokens[0].source_range.offset != 0 || result.tokens[0].source_range.length != 5) {
        return fail("tokens must carry source byte ranges");
    }

    if(result.tokens[1].position != 1) {
        return fail("tokens must carry deterministic emitted positions");
    }

    if(result.tokens[1].kind != agent_memory::TokenKind::Identifier) {
        return fail("tokens must carry token kind");
    }

    return 0;
}
