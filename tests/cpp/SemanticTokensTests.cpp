// SPDX-FileCopyrightText: Hudson River Trading
// SPDX-License-Identifier: MIT

#include "document/ShallowAnalysis.h"
#include "util/Converters.h"
#include "utils/GoldenTest.h"
#include "utils/ServerHarness.h"
#include <algorithm>
#include <string>
#include <vector>

struct SemanticTokenInfo {
    lsp::Range range;
    std::string text;
    std::string type;
    std::vector<std::string> modifiers;
};

static std::vector<SemanticTokenInfo> decodeSemanticTokens(DocumentHandle& hdl,
                                                           const lsp::SemanticTokens& tokens) {
    std::vector<SemanticTokenInfo> decoded;
    decoded.reserve(tokens.data.size() / 5);

    const auto& typeLegend = server::ShallowAnalysis::semanticTokenLegendTypes();
    const auto& modifierLegend = server::ShallowAnalysis::semanticTokenLegendModifiers();
    auto& sm = hdl.doc->getSourceManager();

    lsp::uint line = 0;
    lsp::uint character = 0;
    for (size_t i = 0; i + 4 < tokens.data.size(); i += 5) {
        auto deltaLine = tokens.data[i];
        auto deltaCharacter = tokens.data[i + 1];
        auto length = tokens.data[i + 2];
        auto typeIndex = tokens.data[i + 3];
        auto modifierMask = tokens.data[i + 4];

        line += deltaLine;
        if (deltaLine == 0) {
            character += deltaCharacter;
        }
        else {
            character = deltaCharacter;
        }

        lsp::Position start{.line = line, .character = character};
        lsp::Position end{.line = line, .character = character + length};
        lsp::Range range{.start = start, .end = end};

        std::vector<std::string> modifiers;
        for (lsp::uint bit = 0; bit < modifierLegend.size(); bit++) {
            if ((modifierMask & (1u << bit)) != 0) {
                modifiers.push_back(modifierLegend[bit]);
            }
        }

        std::string text;
        auto startLoc = server::toSourceLocation(hdl.doc->getBuffer(), start, sm);
        if (startLoc) {
            text = std::string(sm.getSourceText(
                slang::SourceRange(*startLoc, *startLoc + static_cast<size_t>(length))));
        }

        std::string tokenType = typeIndex < typeLegend.size() ? typeLegend[typeIndex] : "unknown";
        decoded.push_back(SemanticTokenInfo{
            .range = range,
            .text = std::move(text),
            .type = std::move(tokenType),
            .modifiers = std::move(modifiers),
        });
    }

    return decoded;
}

static bool hasModifier(const SemanticTokenInfo& token, std::string_view modifier) {
    return std::find(token.modifiers.begin(), token.modifiers.end(), modifier) !=
           token.modifiers.end();
}

TEST_CASE("SemanticTokens_Phase1") {
    ServerHarness server;
    auto hdl = server.openFile("semantic_tokens_phase1.sv", R"(
module m #(
    parameter int WIDTH = 4,
    parameter type ENTRY_T = logic
);
    typedef logic [31:0] word_t;
    word_t addr;

    function automatic word_t f(input word_t x);
        return x + WIDTH;
    endfunction

    initial begin
        word_t y = f(addr);
        $display("%0d", y);
    end
endmodule
)");

    auto maybeTokens = server.getDocSemanticTokensFull(lsp::SemanticTokensParams{
        .textDocument = {.uri = hdl.m_uri},
    });

    REQUIRE(maybeTokens.has_value());
    auto decoded = decodeSemanticTokens(hdl, *maybeTokens);
    REQUIRE_FALSE(decoded.empty());

    auto findByText = [&](std::string_view text, std::string_view type) {
        return std::find_if(decoded.begin(), decoded.end(), [&](const SemanticTokenInfo& token) {
            return token.text == text && token.type == type;
        });
    };

    auto width = findByText("WIDTH", "parameter");
    REQUIRE(width != decoded.end());
    CHECK(hasModifier(*width, "declaration"));
    CHECK(hasModifier(*width, "readonly"));

    auto typedefName = findByText("word_t", "type");
    REQUIRE(typedefName != decoded.end());
    CHECK(hasModifier(*typedefName, "declaration"));

    auto systemTask = findByText("$display", "function");
    REQUIRE(systemTask != decoded.end());
    CHECK(hasModifier(*systemTask, "defaultLibrary"));

    JsonGoldenTest golden;
    golden.record(decoded);
}
