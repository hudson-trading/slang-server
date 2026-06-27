#include "utils/ServerHarness.h"
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace {
struct DecodedSemanticToken {
    uint32_t line;
    uint32_t startChar;
    uint32_t length;
    uint32_t tokenType;
    uint32_t tokenModifiers;
    std::string text;
};

static size_t offsetForLineChar(std::string_view text, uint32_t targetLine, uint32_t targetChar) {
    size_t offset = 0;

    for (uint32_t line = 0; line < targetLine; line++) {
        offset = text.find('\n', offset);
        if (offset == std::string_view::npos)
            return text.size();

        offset++; // move past '\n'
    }

    return std::min(offset + static_cast<size_t>(targetChar), text.size());
}

static std::string_view sliceTokenText(std::string_view sourceText, uint32_t line,
                                       uint32_t startChar, uint32_t length) {

    const size_t startOffset = offsetForLineChar(sourceText, line, startChar);
    const size_t endOffset = std::min(startOffset + static_cast<size_t>(length), sourceText.size());

    if (startOffset >= sourceText.size() || endOffset < startOffset)
        return {};

    return sourceText.substr(startOffset, endOffset - startOffset);
}

static std::vector<DecodedSemanticToken> decodeSemanticTokens(const std::vector<uint32_t>& data,
                                                              std::string_view sourceText) {

    std::vector<DecodedSemanticToken> decoded;

    uint32_t line = 0;
    uint32_t startChar = 0;

    for (size_t i = 0; i + 4 < data.size(); i += 5) {
        const uint32_t deltaLine = data[i + 0];
        const uint32_t deltaStart = data[i + 1];
        const uint32_t length = data[i + 2];
        const uint32_t tokenType = data[i + 3];
        const uint32_t tokenModifiers = data[i + 4];

        line += deltaLine;

        if (deltaLine == 0)
            startChar += deltaStart;
        else
            startChar = deltaStart;

        decoded.push_back({
            .line = line,
            .startChar = startChar,
            .length = length,
            .tokenType = tokenType,
            .tokenModifiers = tokenModifiers,
            .text = std::string(sliceTokenText(sourceText, line, startChar, length)),
        });
    }

    return decoded;
}
} // namespace

TEST_CASE("SemanticTokens_InactiveRegions") {
    ServerHarness server;
    JsonGoldenTest golden;

    auto header = server.openFile("foo.svh", "`define FOO\n");

    const std::string source = R"(
module top;
`ifdef FOO
    logic[2:0] a;
`else
    logic b;
`endif

`define BAR
`define BAZ

`ifndef BAR
    bit[2:0] c[13];
`elsif BAZ
    int d;
`else
    int e;
`endif
endmodule

`ifdef A logic foo; `else logic bar; `endif

`include "foo.svh"

`ifdef FOO
    logic[7:0] foot;
`else
    logic[7:0] bart;
`endif
)";

    auto doc = server.openFile("test.sv", source);

    Config::SemanticTokensConfig cfg{.enabled = true};

    const auto result = doc.doc->getAnalysis()->getSemanticTokens(false, cfg, false);

    const auto decoded = decodeSemanticTokens(result.data, source);

    golden.record(decoded);
}

TEST_CASE("SemanticTokens_InactiveRegions_Multiline") {
    ServerHarness server;
    JsonGoldenTest golden;

    const std::string source = R"(
module top;
`ifdef MISSING
    logic[2:0] inactive_a;
    logic inactive_b;
    int inactive_c;
`else
    logic active;
`endif
endmodule
)";

    auto doc = server.openFile("test.sv", source);

    Config::SemanticTokensConfig cfg{.enabled = true};

    const auto result = doc.doc->getAnalysis()->getSemanticTokens(false, cfg, true);

    const auto decoded = decodeSemanticTokens(result.data, source);

    golden.record(decoded);
}
