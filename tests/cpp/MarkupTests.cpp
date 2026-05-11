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

TEST_CASE("AppendCode_BacktickWrapping") {
    using namespace server::markup;

    // Test case from issue #310: SystemVerilog macros with backticks
    // should be wrapped with dynamic delimiter (one more than longest run)
    Paragraph para;
    para.appendCode("`define MACRO_A 10");

    std::string result{para.asMarkdown()};

    // Should use double backticks with spaces: `` code ``
    // (single backtick in content requires 2-backtick delimiter)
    CHECK(result == "`` `define MACRO_A 10 ``");
}

TEST_CASE("AppendCode_TripleBacktickTokenPaste") {
    using namespace server::markup;

    // Test case: macro token-paste operators with triple backticks (```)
    // Dynamic delimiter must use 4 backticks to wrap content with 3
    Paragraph para;
    para.appendCode("`define JOIN_MACRO(name) name```MACRO_A");

    std::string result{para.asMarkdown()};

    // Should use quad backticks (4) to wrap triple backticks (3) in content
    CHECK(result == "```` `define JOIN_MACRO(name) name```MACRO_A ````");
}

TEST_CASE("AppendCode_NoBackticks") {
    using namespace server::markup;

    // Content with no backticks should use single backtick delimiter
    Paragraph para;
    para.appendCode("int variable = 42");

    std::string result{para.asMarkdown()};

    // Should use single backticks (minimum)
    CHECK(result == "` int variable = 42 `");
}
