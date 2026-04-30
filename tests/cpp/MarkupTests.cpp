#include "catch2/catch_test_macros.hpp"
#include "util/Markdown.h"

static std::string escapeMultilineMarkdown(std::string_view text) {
    std::string escaped;
    size_t pos = 0;

    while (pos <= text.size()) {
        size_t next = text.find('\n', pos);
        if (next == std::string_view::npos)
            next = text.size();

        escaped += server::markup::escapeMarkdownLine(text.substr(pos, next - pos));
        if (next < text.size())
            escaped += '\n';

        pos = next + 1;
    }

    return escaped;
}

TEST_CASE("EscapeMarkdown") {
    struct TestCase {
        std::string_view input;
        std::string_view expected;
    };

    const std::vector<TestCase> cases = {
        {"*asterisk*", "\\*asterisk\\*"},
        {"mix_of_words_and_underscores", "mix_of_words_and_underscores"},
        {"1. numbered list", "1\\. numbered list"},
        {"2) numbered alt", "2\\) numbered alt"},
        {"- bullet", "\\- bullet"},
        {"<tag/>", "\\<tag/>"},
        {"<br />", "\\<br />"},
        {"<tag attr=value>", "\\<tag attr=value>"},
        {"&amp;", "\\&amp;"},
    };

    for (const auto& testCase : cases)
        CHECK(server::markup::escapeMarkdownLine(testCase.input) == testCase.expected);

    CHECK(escapeMultilineMarkdown("1. item\n2) item\n<tag/>") == "1\\. item\n2\\) item\n\\<tag/>");
}
